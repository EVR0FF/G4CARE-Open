//==============================================================================
//
// G4CARE
//
// @file    SteppingAction.cc
// @brief   Per-step action: energy deposition accumulation, dose control,
//          and ExprTK expression execution.
//
// @details
//   On each Geant4 step, SteppingAction performs three tasks:
//   1. Accumulates energy deposition in chemistry volumes
//      (CHEMISTRY.VOLUME) into a global atomic energy accumulator
//      for dose monitoring via sAccumulatedEnergyJ / sWaterMassKg.
//   2. Fills the "g4care" NTuple tree via BeamAnalysis::Fill().
//   3. If EXPRS blocks are defined, builds FilterVars from the step
//      data, accumulates per-volume statistics in ExprsManager,
//      and optionally executes step-level ExprTK scripts
//      (ExprsManager::TryExecuteStepScripts).
//
//   At event end, AccumulateEventEnergy() atomically transfers the
//   per-event (thread-local) energy deposit to the global atomic
//   counter and checks the dose threshold, aborting the run if
//   exceeded.
//
//   Configuration keys read:
//     CHEMISTRY.VOLUME
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "SteppingAction.hh"
#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "GeometryManager.hh"
#include "ChemistryExtractor.hh"
#include "G4RunManager.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4SystemOfUnits.hh"
#include "G4VPhysicalVolume.hh"
#include "G4LogicalVolume.hh"
#include "G4LogicalVolumeStore.hh"
#include "ExprsManager.hh"
#include <set>

std::atomic<double> SteppingAction::sAccumulatedEnergyJ{0.0};
double              SteppingAction::sDoseThresholdGy = 0.0;
double              SteppingAction::sTargetMassKg    = 0.0;
std::string         SteppingAction::sSensitiveVolumeName;
std::atomic<bool>   SteppingAction::sEnabled{false};

std::unordered_set<std::string> SteppingAction::sChemVolumeNames;
bool                SteppingAction::sChemEnabled     = false;
std::atomic<double> SteppingAction::sWaterMassKg{0.0};

thread_local double g_tlEventEnergyJ = 0.0;
thread_local std::set<const G4LogicalVolume*> g_tlEventVolumes;
thread_local bool g_tlHadInteraction = false;

SteppingAction::SteppingAction() {}

/// @brief Called by Geant4 on every simulation step.
///
/// @param step Current Geant4 step.
///
/// Three phases:
/// 1. **Chem EDEP accumulation** — if the current volume matches
///    sChemVolumeNames (populated from CHEMISTRY.VOLUME config),
///    the step's energy deposit (MeV) is added to the thread-local
///    g_tlEventEnergyJ and accumulated in ChemistryExtractor for
///    G-value scoring.
/// 2. **NTuple fill** — the step data is sent to BeamAnalysis::Fill("g4care").
/// 3. **EXPRS accumulation** — if ExprsManager has active blocks,
///    FilterVars are built from the step, accumulated per logical
///    volume, and step-level ExprTK scripts are executed when the
///    step frequency condition is met.
void SteppingAction::UserSteppingAction(const G4Step* step) {
    if (!step) return;

    double edep = step->GetTotalEnergyDeposit();

    if ((sEnabled.load(std::memory_order_relaxed) || sChemEnabled) && edep > 0.0) {
        G4VPhysicalVolume* pv = step->GetPreStepPoint()->GetPhysicalVolume();
        if (pv) {
            std::string rawName = pv->GetName();
            // Strip "_LV" suffix to obtain the clean logical-volume name
            // matching the keys in sChemVolumeNames (populated from CHEMISTRY.VOLUME).
            size_t lvPos = rawName.rfind("_LV");
            std::string cleanName = (lvPos != std::string::npos && lvPos + 3 == rawName.size())
                                    ? rawName.substr(0, lvPos) : rawName;
            // Check via sChemVolumeNames (populated from CHEMISTRY.VOLUME)
            bool inChemVol = sChemVolumeNames.count(cleanName) > 0;

            if (sEnabled.load(std::memory_order_relaxed) && inChemVol) {
                g_tlEventEnergyJ += edep;
            }
            if (sChemEnabled && inChemVol) {
                ChemistryExtractor::GlobalAccumulateEdep(edep);
            }
        }
    }

    auto* beam = BeamAnalysis::Instance();
    beam->Fill("g4care", step);

    // ── EXPRS accumulation: per-volume, independent of NTUPLES ──
    auto* sm = ExprsManager::Instance();
    if (!sm->GetBlocks().empty()) {
        auto* pv = step->GetPreStepPoint()->GetPhysicalVolume();
        auto* lv = pv ? pv->GetLogicalVolume() : nullptr;
        if (lv) {
            UnifiedSource src(step);
            FilterVars filterVars{};
            auto* de = beam->GetDataExtractor();
            if (de) {
                de->BuildFilterVars(filterVars, src, -1, -1, -1, nullptr);
                sm->Accumulate(lv, filterVars);
                g_tlEventVolumes.insert(lv);
                sm->TryExecuteStepScripts(lv, filterVars);
                if (edep > 0.0) g_tlHadInteraction = true;
            }
        }
    }
}

/// @brief Atomically transfer thread-local event energy to the global
///        accumulator and check the dose threshold.
///
/// Called from EventAction::EndOfEventAction().  Adds the per-event
/// energy deposit (converted from MeV to Joules) to the global atomic
/// accumulator sAccumulatedEnergyJ.  If the cumulative dose exceeds
/// sDoseThresholdGy for mass sWaterMassKg, the run is aborted via
/// G4RunManager::AbortRun(true).
void SteppingAction::AccumulateEventEnergy() {
    double massKg = sWaterMassKg.load(std::memory_order_relaxed);
    if (!sEnabled.load(std::memory_order_relaxed) || g_tlEventEnergyJ <= 0.0 || massKg <= 0.0) return;

    double totalJ = sAccumulatedEnergyJ.fetch_add(g_tlEventEnergyJ * CLHEP::MeV, std::memory_order_relaxed)
                    + g_tlEventEnergyJ * CLHEP::MeV;
    g_tlEventEnergyJ = 0.0;

    double doseGy = totalJ / massKg;
    if (doseGy >= sDoseThresholdGy) {
        G4cout << "[SteppingAction] Dose limit: " << doseGy/1e6 << " MGy >= "
               << sDoseThresholdGy/1e6 << " MGy. Aborting run." << G4endl;
        G4RunManager::GetRunManager()->AbortRun(true);
    }
}

/// @brief Return the cumulative absorbed dose in the target volume (Gy).
/// @return Dose in Gy, computed as total accumulated energy / target mass.
double SteppingAction::GetCumulativeDoseGy() {
    return sAccumulatedEnergyJ.load(std::memory_order_relaxed) / sTargetMassKg;
}

/// @brief Initialise static dose control parameters and chemistry
///        volume names from configuration.
///
/// Reads CHEMISTRY.VOLUME (string or vector) to populate
/// sChemVolumeNames, which is used by UserSteppingAction to decide
/// which volumes contribute to chemistry energy deposition.
/// Dose threshold is disabled (sDoseThresholdGy = 0.0) — cumulative
/// dose checks are performed in RunAction::EndOfRunAction instead.
///
/// @param geomMgr Pointer to GeometryManager (unused in current implementation).
void SteppingAction::InitializeDoseControl(GeometryManager* geomMgr) {
    if (!geomMgr) { sEnabled.store(false, std::memory_order_relaxed); return; }
    auto* cfg = ConfigManager::Instance();

    // ── Chemistry volumes ──
    {
        auto volNames = cfg->GetStringVector("CHEMISTRY.VOLUME");
        if (!volNames.empty()) {
            for (const auto& v : volNames) sChemVolumeNames.insert(v);
            sChemEnabled = true;
            G4cout << "[SteppingAction] Chem EDEP: volumes=[";
            bool first = true;
            for (const auto& v : sChemVolumeNames) {
                if (!first) G4cout << ", ";
                G4cout << v; first = false;
            }
            G4cout << "]" << G4endl;
        } else {
            std::string sv = cfg->GetString("CHEMISTRY.VOLUME", "");
            if (!sv.empty()) { sChemVolumeNames.insert(sv); sChemEnabled = true; }
        }
    }

    // ── MAX_DOSE disabled in SteppingAction — check in RunAction::EndOfRunAction ──
    sDoseThresholdGy = 0.0;
    sEnabled.store(false, std::memory_order_relaxed);
}