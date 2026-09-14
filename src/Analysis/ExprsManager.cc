//==============================================================================
//
// G4CARE
//
// @file    ExprsManager.cc
// @brief   Expression block manager for ExprTk scripting.
//
// @details
//   Reads EXPRS section directly from YAML (array of blocks). Each block
//   contains a SCRIPT executed at configurable intervals (N_STEP, N_EVENT,
//   N_TRACK, N_RUN) on specific volumes (VOLUME with wildcard support).
//   Supports PCOMP precompiled expressions, WHEN timing (Start/End),
//   and EXCLUDE.  Compiles scripts via ExpressionEvaluator, resolves
//   target logical volumes, and uses lock-free thread-local accumulator
//   pools for per-volume data collection.
//
//   Configuration keys read (via direct YAML access):
//     EXPRS[].NAME, VOLUME, EXCLUDE
//     EXPRS[].N_STEP, N_EVENT, N_TRACK, N_RUN, WHEN
//     EXPRS[].PCOMP, SCRIPT
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-08-05
// @version 0.10.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "ExprsManager.hh"
#include "ConfigManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4LogicalVolume.hh"
#include "G4ios.hh"
#include "G4SystemOfUnits.hh"
#include "G4Material.hh"
#include "G4VSolid.hh"
#include <yaml-cpp/yaml.h>
#include <fnmatch.h>
#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
#include "FilterVarRegistry.hh"

ExprsManager* ExprsManager::fInstance = nullptr;

std::vector<ExprsManager::ThreadSlot>* ExprsManager::sGlobalSlots = nullptr;
size_t ExprsManager::sGlobalNumSlots = 0;
std::map<const G4LogicalVolume*, FilterVars> ExprsManager::sMerged;
std::map<const G4LogicalVolume*, double> ExprsManager::sMergedEvents;
std::map<const G4LogicalVolume*, double> ExprsManager::sMergedInteracting;

ExprsManager* ExprsManager::Instance() {
    if (!fInstance) fInstance = new ExprsManager();
    return fInstance;
}

void ExprsManager::DeleteInstance() {
    delete fInstance; fInstance = nullptr;
    delete sGlobalSlots; sGlobalSlots = nullptr;
}

static std::pair<std::string, std::vector<std::string>>
PreprocessScriptAssignments(const std::string& source,
                            const std::vector<std::string>& baseVars) {
    std::set<std::string> seen(baseVars.begin(), baseVars.end());
    std::vector<std::string> newVars;
    {
        std::regex varDeclRe(R"(\bvar\s+([a-zA-Z_][a-zA-Z0-9_]*))");
        std::sregex_iterator it(source.begin(), source.end(), varDeclRe), end;
        for (; it != end; ++it) seen.insert((*it)[1].str());
    }
    std::regex assignRe(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*:=)");
    std::istringstream iss(source); std::string line, result;
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, assignRe)) {
            std::string vn = m[1].str();
            size_t fs = line.find_first_not_of(" \t\r\n");
            bool av = (fs != std::string::npos && line.substr(fs, 4) == "var ");
            if (!av && seen.find(vn) == seen.end()) {
                line.insert(fs, "var "); newVars.push_back(vn); seen.insert(vn);
            }
        }
        result += line + "\n";
    }
    return {result, newVars};
}

bool ExprsManager::LoadFromConfig(ConfigManager* cfg) {
    if (!cfg) return false;
    fBlocks.clear();
    const std::string& fn = cfg->GetConfigFileName();
    if (fn.empty()) { G4cout << "ExprsManager: No config filename." << G4endl; return true; }
    YAML::Node root;
    try { root = YAML::LoadFile(fn); }
    catch (const YAML::Exception& e) { G4cerr << "ExprsManager: YAML error: " << e.what() << G4endl; return false; }
    if (!root["EXPRS"] || !root["EXPRS"].IsSequence()) {
        G4cout << "ExprsManager: No EXPRS section." << G4endl; return true;
    }
    auto exprsSeq = root["EXPRS"];
    static const std::vector<std::string> sBaseVarNames = {
        "edep","track_length","step_length","n_secondaries",
        "radical_oh","radical_h","radical_eaq","h2o2","h2",
        "volume_mass","n_events","n_interacting_events",
        "chem_n_species","chem_edep_eV","chem_end_ns"
    };
    ExpressionEvaluator eval;
    fGlobalFuncs.clear(); fGlobalFuncMap.clear();
    std::regex funcDefRe(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*:=\(([^)]*)\)\s*(.+)$)");
    for (size_t i = 0; i < exprsSeq.size(); ++i) {
        auto nd = exprsSeq[i]; if (!nd.IsMap() || !nd["SCRIPT"]) continue;
        std::string rs = nd["SCRIPT"].as<std::string>();
        std::istringstream iss(rs); std::string line;
        while (std::getline(iss, line)) {
            std::smatch m;
            if (std::regex_match(line, m, funcDefRe)) {
                std::string fname = m[1], fargs = m[2], fbody = m[3];
                std::vector<std::string> args;
                size_t p = 0;
                while (p < fargs.size()) {
                    size_t c = fargs.find(',', p);
                    std::string a = fargs.substr(p, c == std::string::npos ? c : c - p);
                    size_t s2 = a.find_first_not_of(" \t"), e2 = a.find_last_not_of(" \t");
                    if (s2 != std::string::npos) args.push_back(a.substr(s2, e2 - s2 + 1));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
                auto bc = eval.Precompile(fbody, args);
                if (bc && fGlobalFuncMap.find(fname) == fGlobalFuncMap.end()) {
                    auto uf = std::make_unique<UserInlineFunc>();
                    uf->compiled = bc; uf->argNames = args;
                    fGlobalFuncMap[fname] = uf.get();
                    fGlobalFuncs.push_back(std::move(uf));
                    G4cout << "ExprsManager: global function '" << fname << "'" << G4endl;
                }
            }
        }
    }
    for (size_t idx = 0; idx < exprsSeq.size(); ++idx) {
        auto bn = exprsSeq[idx]; if (!bn.IsMap()) continue;
        ExprsBlock blk;
        if (bn["NAME"]) blk.name = bn["NAME"].as<std::string>();
        if (bn["VOLUME"]) {
            auto vn = bn["VOLUME"];
            if (vn.IsScalar()) {
                blk.volumeSelector = vn.as<std::string>();
                blk.isPattern = (blk.volumeSelector.find('*') != std::string::npos);
            } else if (vn.IsSequence()) {
                for (const auto& v : vn) if (v.IsScalar()) {
                    if (!blk.volumeSelector.empty()) blk.volumeSelector += ",";
                    std::string vs = v.as<std::string>(); blk.volumeSelector += vs;
                    if (vs.find('*') != std::string::npos) blk.isPattern = true;
                }
            }
        } else { G4cerr << "ExprsManager: Block #" << idx << " no VOLUME, skip." << G4endl; continue; }
        if (bn["EXCLUDE"] && bn["EXCLUDE"].IsSequence())
            for (const auto& ex : bn["EXCLUDE"]) if (ex.IsScalar()) blk.excludeVolumes.push_back(ex.as<std::string>());
        if (bn["N_STEP"])  blk.nStep  = bn["N_STEP"].as<int>();
        if (bn["N_EVENT"]) blk.nEvent = bn["N_EVENT"].as<int>();
        if (bn["N_TRACK"]) blk.nTrack = bn["N_TRACK"].as<int>();
        if (bn["N_RUN"])   blk.nRun   = bn["N_RUN"].as<int>();
        if (bn["WHEN"]) {
            std::string w = bn["WHEN"].as<std::string>();
            blk.when = (w == "start") ? When::Start : When::End;
        }
        if (bn["PCOMP"] && bn["PCOMP"].IsSequence())
            for (const auto& pn : bn["PCOMP"]) if (pn.IsScalar()) blk.pcomp.push_back(pn.as<std::string>());
        if (!bn["SCRIPT"]) { G4cerr << "ExprsManager: Block #" << idx << " no SCRIPT, skip." << G4endl; continue; }
        blk.scriptSource = bn["SCRIPT"].as<std::string>();
        ResolveVolumes(blk);
        if (blk.volumes.empty()) { G4cerr << "ExprsManager: Block #" << idx << " matched no volumes, skip." << G4endl; continue; }
        if (blk.name.empty()) blk.name = "[" + blk.volumes[0]->GetName() + "]";
        {   std::string cs; std::istringstream is2(blk.scriptSource); std::string l2;
            while (std::getline(is2, l2)) { std::smatch m2; if (!std::regex_match(l2, m2, funcDefRe)) cs += l2 + "\n"; }
            blk.scriptSource = cs;
        }
        auto [ps, nv] = PreprocessScriptAssignments(blk.scriptSource, sBaseVarNames);
        std::vector<std::string> avn = sBaseVarNames;
        for (const auto& v : nv) avn.push_back(v);
        std::set<std::string> skip(sBaseVarNames.begin(), sBaseVarNames.end());
        skip.insert({"Nc","Nt","Gc","Gt","charge"});
        for (const auto& [sn2, fv2] : GetNameToFilterVar())
            if (skip.count(std::string(sn2)) == 0) avn.push_back(std::string(sn2));
        {
            auto comp = eval.Precompile(ps, avn, fGlobalFuncMap);
            if (comp) { blk.compiledScripts.clear(); blk.compiledScripts.push_back(comp); }
            else { G4cerr << "ExprsManager: COMPILATION FAILED for '" << blk.name << "'" << G4endl; continue; }
        }
        blk.varNames = avn;
        for (const auto& pn : blk.pcomp) { auto pc = eval.Precompile(pn, {}); if (pc) blk.precompiledValues[pn] = pc->expr.value(); }
        G4cout << "ExprsManager: Loaded block '" << blk.name << "'" << G4endl;
        fBlocks.push_back(std::move(blk));
    }
    G4cout << "ExprsManager: " << fBlocks.size() << " blocks loaded." << G4endl;
    if (!fBlocks.empty()) {
        InitAccumulators();
        sBlockStepCounts.assign(fBlocks.size(), 0);
        sBlockEventCounts.assign(fBlocks.size(), 0);
        sBlockTrackCounts.assign(fBlocks.size(), 0);
    }
    return true;
}

ExprsManager::ExprsManager() = default;
ExprsManager::~ExprsManager() = default;

void ExprsManager::InitAccumulators() {
    size_t n = G4Threading::GetNumberOfRunningWorkerThreads(); if (n == 0) n = 1;
    if (sGlobalSlots && sGlobalNumSlots >= n + 1) return;
    delete sGlobalSlots; sGlobalNumSlots = n;
    sGlobalSlots = new std::vector<ThreadSlot>(sGlobalNumSlots + 1);
}

void ExprsManager::Reset() {
    if (!sGlobalSlots) return;
    for (auto& sl : *sGlobalSlots) { sl.data.clear(); sl.nEvents.clear(); sl.nInteractingEvents.clear(); }
    sMerged.clear(); sMergedEvents.clear(); sMergedInteracting.clear();
}

void ExprsManager::Accumulate(const G4LogicalVolume* lv, const FilterVars& vars) {
    if (!lv || !sGlobalSlots) return;
    G4int t = G4Threading::G4GetThreadId(); size_t tid = (t < 0) ? 0 : static_cast<size_t>(t);
    if (tid >= sGlobalSlots->size()) return;
    auto& a = (*sGlobalSlots)[tid].data[lv];
    for (size_t i = 0; i < static_cast<size_t>(FilterVar::Count); ++i) a[i] += vars[i];
}

void ExprsManager::EventEnd(const G4LogicalVolume* lv, bool hi) {
    if (!lv || !sGlobalSlots) return;
    G4int t = G4Threading::G4GetThreadId(); size_t tid = (t < 0) ? 0 : static_cast<size_t>(t);
    if (tid >= sGlobalSlots->size()) return;
    (*sGlobalSlots)[tid].nEvents[lv] += 1.0;
    if (hi) (*sGlobalSlots)[tid].nInteractingEvents[lv] += 1.0;
}

void ExprsManager::Merge() {
    sMerged.clear(); sMergedEvents.clear(); sMergedInteracting.clear();
    if (!sGlobalSlots) return;
    for (size_t tid = 0; tid < sGlobalSlots->size(); ++tid) {
        const auto& sl = (*sGlobalSlots)[tid];
        for (const auto& [lv, v] : sl.data) { auto& a = sMerged[lv]; for (size_t i = 0; i < static_cast<size_t>(FilterVar::Count); ++i) a[i] += v[i]; }
        for (const auto& [lv, n] : sl.nEvents) sMergedEvents[lv] += n;
        for (const auto& [lv, n] : sl.nInteractingEvents) sMergedInteracting[lv] += n;
    }
}

std::map<std::string, double> ExprsManager::GetBlockValues(const ExprsBlock& blk) const {
    std::map<std::string, double> r; r["volume_mass"] = blk.volumeMass;
    const auto& n2fv = GetNameToFilterVar();
    static const std::set<std::string> cn = {"Nc","Nt","Gc","Gt","charge"};
    for (const auto* lv : blk.volumes) {
        std::string tn = lv ? lv->GetName() : "";
        for (const auto& [mlv, v] : sMerged) { if (!mlv || mlv->GetName() != tn) continue;
            for (const auto& [sn, fv] : n2fv) { std::string s(sn); if (fv == FilterVar::VolumeMass || cn.count(s)) continue; r[s] += v[static_cast<size_t>(fv)]; }
        }
        for (const auto& [mlv, ne] : sMergedEvents) if (mlv && mlv->GetName() == tn) r["n_events"] += ne;
        for (const auto& [mlv, ni] : sMergedInteracting) if (mlv && mlv->GetName() == tn) r["n_interacting_events"] += ni;
    }
    return r;
}

void ExprsManager::ResolveVolumes(ExprsBlock& blk) {
    auto* st = G4LogicalVolumeStore::GetInstance(); if (!st) return;
    auto add = [&](G4LogicalVolume* lv) { if (!lv) return; for (const auto& ex : blk.excludeVolumes) if (MatchWildcard(ex, lv->GetName())) return; blk.volumes.push_back(lv); };
    if (!blk.isPattern && !blk.volumeSelector.empty() && blk.volumeSelector != "*") {
        std::vector<std::string> ns; std::string cur;
        for (char c : blk.volumeSelector) { if (c == ',') { if (!cur.empty()) { ns.push_back(cur); cur.clear(); } } else cur += c; }
        if (!cur.empty()) ns.push_back(cur);
        for (const auto& n : ns) {
            bool ex = false; for (const auto& e : blk.excludeVolumes) if (n == e) { ex = true; break; } if (ex) continue;
            for (auto* lv : *st) { std::string ln = lv->GetName(); if (ln == n || ln == n + "_LV" || ln == n + "_lv") { add(lv); break; } }
        }
        if (!blk.volumes.empty()) { blk.volumeMass = 0;
            for (const auto* lv : blk.volumes) { if (!lv->GetSolid()) continue;
                auto* m = lv->GetMaterial(); double d = m ? m->GetDensity()/(CLHEP::g/CLHEP::cm3) : 1.0;
                double v = lv->GetSolid()->GetCubicVolume()/CLHEP::cm3; blk.volumeMass += d * v / 1000.0; }
        }
        return;
    }
    std::string pat = blk.isPattern ? blk.volumeSelector : "*";
    for (auto* lv : *st) { if (!lv) continue;
        bool ex = false; for (const auto& e : blk.excludeVolumes) if (MatchWildcard(e, lv->GetName()) || e == lv->GetName()) { ex = true; break; }
        if (ex) continue; if (MatchWildcard(pat, lv->GetName())) add(lv);
    }
}

bool ExprsManager::MatchWildcard(const std::string& p, const std::string& n) {
    return fnmatch(p.c_str(), n.c_str(), 0) == 0;
}

// ── Thread-local counters ──
thread_local std::vector<long> ExprsManager::sBlockStepCounts;
thread_local std::vector<long> ExprsManager::sBlockEventCounts;
thread_local std::vector<long> ExprsManager::sBlockTrackCounts;

/// @brief Check if a logical volume is in the block's target volume list.
/// @param blk Expression block with resolved volumes.
/// @param lv  Logical volume to check.
/// @return true if lv is in blk.volumes.
static bool VolMatch(const ExprsManager::ExprsBlock& blk, const G4LogicalVolume* lv) {
    for (const auto* bv : blk.volumes) if (bv == lv) return true;
    return false;
}

/// @brief Execute compiled ExprTk scripts for a block at end-of-step.
/// @param lv   Logical volume the current step ended in.
/// @param vars FilterVars for the current step.
/// @details Called by SteppingAction::UserSteppingAction.  Only blocks with
///   nStep > 0 and WHEN == End are triggered.  The per-block step counter is
///   incremented and scripts execute every N steps.
void ExprsManager::TryExecuteStepScripts(const G4LogicalVolume* lv, const FilterVars& vars) {
    if (!lv || sBlockStepCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nStep <= 0 || blk.compiledScripts.empty()) continue;
        if (blk.when != When::End) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockStepCounts[bi]++;
        if (sBlockStepCounts[bi] % blk.nStep == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            const auto& n2fv = GetNameToFilterVar();
            for (const auto& [sn, fv] : n2fv) vals[std::string(sn)] = vars[static_cast<size_t>(fv)];
            vals["volume_mass"] = blk.volumeMass;
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute compiled ExprTk scripts for a block at start-of-step.
/// @param lv   Logical volume the step is starting in.
/// @param vars FilterVars at the beginning of the step.
/// @details Called by SteppingAction for WHEN == Start blocks.
///   Only blocks with nStep > 0 and WHEN == Start are triggered.
void ExprsManager::TryExecuteStepScriptsStart(const G4LogicalVolume* lv, const FilterVars& vars) {
    if (!lv || sBlockStepCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nStep <= 0 || blk.compiledScripts.empty()) continue;
        if (blk.when != When::Start) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockStepCounts[bi]++;
        if (sBlockStepCounts[bi] % blk.nStep == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            const auto& n2fv = GetNameToFilterVar();
            for (const auto& [sn, fv] : n2fv) vals[std::string(sn)] = vars[static_cast<size_t>(fv)];
            vals["volume_mass"] = blk.volumeMass;
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute compiled ExprTk scripts for a block at end-of-event.
/// @param lv Logical volume where the event ended.
/// @details Called by EventAction::EndOfEventAction.  Only blocks with
///   nEvent > 0 are triggered; the per-block event counter is incremented
///   and scripts execute every N events.
void ExprsManager::TryExecuteEventScripts(const G4LogicalVolume* lv) {
    if (!lv || sBlockEventCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nEvent <= 0 || blk.compiledScripts.empty()) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockEventCounts[bi]++;
        if (sBlockEventCounts[bi] % blk.nEvent == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            vals["volume_mass"] = blk.volumeMass;
            vals["n_events"] = static_cast<double>(sBlockEventCounts[bi]);
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute compiled ExprTk scripts for a block at start-of-event.
/// @param lv Logical volume where the event starts.
/// @details Called by EventAction for WHEN == Start blocks.
///   Only blocks with nEvent > 0 and WHEN == Start are triggered.
void ExprsManager::TryExecuteEventScriptsStart(const G4LogicalVolume* lv) {
    if (!lv || sBlockEventCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nEvent <= 0 || blk.compiledScripts.empty()) continue;
        if (blk.when != When::Start) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockEventCounts[bi]++;
        if (sBlockEventCounts[bi] % blk.nEvent == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            vals["volume_mass"] = blk.volumeMass;
            vals["n_events"] = static_cast<double>(sBlockEventCounts[bi]);
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute compiled ExprTk scripts for a block at end-of-track.
/// @param lv   Logical volume where the track ended.
/// @param vars FilterVars for the track.
/// @details Called by TrackingAction::PostUserTrackingAction.
///   Only blocks with nTrack > 0 are triggered every N tracks.
void ExprsManager::TryExecuteTrackScripts(const G4LogicalVolume* lv, const FilterVars& vars) {
    if (!lv || sBlockTrackCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nTrack <= 0 || blk.compiledScripts.empty()) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockTrackCounts[bi]++;
        if (sBlockTrackCounts[bi] % blk.nTrack == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            const auto& n2fv = GetNameToFilterVar();
            for (const auto& [sn, fv] : n2fv) vals[std::string(sn)] = vars[static_cast<size_t>(fv)];
            vals["volume_mass"] = blk.volumeMass;
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute compiled ExprTk scripts for a block at start-of-track.
/// @param lv   Logical volume where the track starts.
/// @param vars FilterVars for the track.
/// @details Called by TrackingAction for WHEN == Start blocks.
///   Only blocks with nTrack > 0 and WHEN == Start are triggered.
void ExprsManager::TryExecuteTrackScriptsStart(const G4LogicalVolume* lv, const FilterVars& vars) {
    if (!lv || sBlockTrackCounts.size() != fBlocks.size()) return;
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nTrack <= 0 || blk.compiledScripts.empty()) continue;
        if (blk.when != When::Start) continue;
        if (!VolMatch(blk, lv)) continue;
        sBlockTrackCounts[bi]++;
        if (sBlockTrackCounts[bi] % blk.nTrack == 0) {
            ExpressionEvaluator eval; std::map<std::string, double> vals;
            const auto& n2fv = GetNameToFilterVar();
            for (const auto& [sn, fv] : n2fv) vals[std::string(sn)] = vars[static_cast<size_t>(fv)];
            vals["volume_mass"] = blk.volumeMass;
            for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
        }
    }
}

/// @brief Execute N_RUN scripts for all blocks at end of run.
/// @details Called by RunAction::EndOfRunAction after EXPRS block execution.
///   Iterates all blocks with nRun > 0 and executes their compiled scripts
///   with volume_mass available in the variable context.
void ExprsManager::ExecuteRunScripts() {
    for (size_t bi = 0; bi < fBlocks.size(); ++bi) {
        auto& blk = fBlocks[bi];
        if (blk.nRun <= 0 || blk.compiledScripts.empty()) continue;
        G4cout << "[ExprsManager] N_RUN script for block '" << blk.name << "'" << G4endl;
        ExpressionEvaluator eval; std::map<std::string, double> vals;
        vals["volume_mass"] = blk.volumeMass;
        for (auto& cl : blk.compiledScripts) if (cl) eval.Execute(cl.get(), vals);
    }
}

double ExprsManager::GetPrecompiled(const std::string& name) const {
    for (const auto& blk : fBlocks) { auto it = blk.precompiledValues.find(name); if (it != blk.precompiledValues.end()) return it->second; }
    return 0.0;
}