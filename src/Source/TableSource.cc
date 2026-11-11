//==============================================================================
//
// G4CARE
//
// @file    TableSource.cc
// @brief   Implementation of TableSource.
//
//==============================================================================

#include "TableSource.hh"

#include "ConfigManager.hh"
#include "IAEAReader.hh"
#include "RDFReader.hh"
#include "PositionSampler.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4Event.hh"
#include "G4ThreeVector.hh"
#include "G4SystemOfUnits.hh"
#include "G4UnitsTable.hh"
#include "G4RandomDirection.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4ios.hh"
#include "Randomize.hh"

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

/// @brief Create the right DataReader for a file based on its extension.
std::unique_ptr<DataReader> CreateReader(const std::string& file, const std::string& baseKey) {
    auto* cfg = ConfigManager::Instance();
    std::string lower = file;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const auto dot = lower.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : lower.substr(dot + 1);

    if (ext == "csv" || ext == "txt") {
        auto r = std::make_unique<RDFReader>();
        const bool header = cfg->GetBool(baseKey + "header", true);
        const std::string d = cfg->GetString(baseKey + "delimiter", ",");
        const char delim = d.empty() ? ',' : d[0];
        const int skip = cfg->GetInt(baseKey + "skip_first_n", 0);
        if (r->LoadCSVFile(file, header, delim, skip)) return r;
        return nullptr;
    }
    if (ext == "root") {
        auto r = std::make_unique<RDFReader>();
        const std::string tree = cfg->GetString(baseKey + "tree", "tree");
        if (r->LoadROOTFile({file}, tree)) return r;
        return nullptr;
    }
    if (ext == "iaeaheader" || lower.find(".iaeaheader") != std::string::npos) {
        auto r = std::make_unique<IAEAReader>();
        if (r->Load(file)) return r;
        return nullptr;
    }
    return nullptr;
}

/// @brief Case-insensitive column lookup over a list of aliases.
int FindColumn(const std::vector<std::string>& names,
               const std::vector<std::string>& aliases) {
    for (const auto& a : aliases) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::string n = names[i];
            std::transform(n.begin(), n.end(), n.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (n == a) return static_cast<int>(i);
        }
    }
    return -1;
}

/// @brief Trim leading/trailing whitespace.
std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

TableSource::TableSource(const std::string& sourceName) {
    fName = sourceName;
    auto* cfg = ConfigManager::Instance();
    const std::string file = cfg->GetString(sourceName + ".file", "");
    const std::string mode = cfg->GetString(sourceName + ".mode", "random");
    fSequential = (mode == "sequential");
    fLoop = cfg->GetBool(sourceName + ".loop", true);
    fParticle = cfg->GetString(sourceName + ".particle", "");
    fGun = std::make_unique<G4ParticleGun>(1);

    // ExprTK support (mirrors ParticleGunSource).  File columns are exposed
    // to expressions as: event_id, x, y, z, energy, u, v, w, wt.
    fEval = std::make_unique<ExpressionEvaluator>();

    const std::vector<std::string> varNames = {
        "event_id", "x", "y", "z", "energy", "u", "v", "w", "wt"};

    // Placement: surface > object > pos > file (x/y/z + offset).  pos/offset
    // are expressed in cm, consistent with the file's position columns.
    fPosX = CompileField(sourceName + ".pos.x", 0.0, cm, varNames);
    fPosY = CompileField(sourceName + ".pos.y", 0.0, cm, varNames);
    fPosZ = CompileField(sourceName + ".pos.z", 0.0, cm, varNames);
    fOffX = CompileField(sourceName + ".offset.x", 0.0, cm, varNames);
    fOffY = CompileField(sourceName + ".offset.y", 0.0, cm, varNames);
    fOffZ = CompileField(sourceName + ".offset.z", 0.0, cm, varNames);
    fEnergyExpr = CompileField(sourceName + ".energy", 0.0, MeV, varNames);
    fWeightExpr = CompileField(sourceName + ".weight", 1.0, 0.0, varNames);
    fRejectIf   = CompileField(sourceName + ".reject_if", 0.0, 0.0, varNames, /*bareExpr=*/true);
    fHasPos = fPosX.active || fPosY.active || fPosZ.active;
    fHasOffset = fOffX.active || fOffY.active || fOffZ.active;


    fObjectName  = cfg->GetString(sourceName + ".object", "");
    fSurfaceName = cfg->GetString(sourceName + ".surface", "");

    if (file.empty()) {
        G4cerr << "[TableSource] Missing 'file' for source '" << sourceName << "'" << G4endl;
        return;
    }
    fReader = CreateReader(file, sourceName + ".");
    if (!fReader) {
        G4cerr << "[TableSource] Failed to load table file: " << file << G4endl;
        return;
    }

    const auto names = fReader->GetColumnNames();
    fColType = FindColumn(names, {"type", "particle_type", "particle", "pdg", "pdgcode"});
    fColE    = FindColumn(names, {"energy", "e", "ke", "kinetic_energy"});
    fColX    = FindColumn(names, {"x", "pos_x", "position_x"});
    fColY    = FindColumn(names, {"y", "pos_y", "position_y"});
    fColZ    = FindColumn(names, {"z", "pos_z", "position_z"});
    fColU    = FindColumn(names, {"u", "dx", "dircosx", "px", "du"});
    fColV    = FindColumn(names, {"v", "dy", "dircosy", "py", "dv"});
    fColW    = FindColumn(names, {"w", "dz", "dircosz", "pz", "dw"});
    fColWt   = FindColumn(names, {"weight", "wt"});

    G4cout << "[TableSource] Loaded table: " << file
           << " (" << fReader->GetNumberOfRows() << " rows, "
           << names.size() << " columns)" << G4endl;

    // --- Initialise placement samplers (surface takes priority over object) ---
    if (!fSurfaceName.empty() && !fObjectName.empty()) {
        G4cerr << "[TableSource] both 'surface' and 'object' specified, using surface only." << G4endl;
        fObjectName.clear();
    }
    if (!fSurfaceName.empty()) {
        fSurfaceSampler = std::make_unique<SurfaceSampler>(fSurfaceName);
        if (!fSurfaceSampler->IsValid()) {
            G4cerr << "[TableSource] failed to create surface sampler for '" << fSurfaceName << "'" << G4endl;
            fSurfaceSampler.reset();
            fSurfaceName.clear();
        }
    }
    if (!fObjectName.empty()) {
        G4PhysicalVolumeStore* store = G4PhysicalVolumeStore::GetInstance();
        if (store) {
            fVolumePhys = store->GetVolume(fObjectName, false);
            if (!fVolumePhys) {
                G4cerr << "[TableSource] volume '" << fObjectName << "' not found in store." << G4endl;
            }
        } else {
            G4cerr << "[TableSource] PhysicalVolumeStore not available." << G4endl;
        }
    }
}

TableSource::~TableSource() = default;

TableSource::Expr TableSource::CompileField(const std::string& key,
                                            double defVal,
                                            double targetUnit,
                                            const std::vector<std::string>& varNames,
                                            bool bareExpr) {
    Expr e;
    auto* cfg = ConfigManager::Instance();
    const std::string raw = cfg->GetString(key, "");
    const std::string t = Trim(raw);
    if (t.empty()) { e.active = false; e.staticVal = defVal; return e; }
    e.active = true;

    // ExprTK expression wrapped in $$ ... $$ (optional trailing unit suffix).
    if (t.size() >= 4 && t[0] == '$' && t[1] == '$') {
        const std::size_t end = t.find("$$", 2);
        if (end != std::string::npos) {
            const std::string math = Trim(t.substr(2, end - 2));
            const std::string unit = Trim(t.substr(end + 2));
            e.compiled = fEval->Precompile(math, varNames);
            if (!e.compiled) {
                G4cerr << "[TableSource] failed to compile expression '" << raw
                       << "' for " << key << G4endl;
                e.active = false;
                e.staticVal = defVal;
                return e;
            }
            e.CachePointers();
            if (!unit.empty() && targetUnit > 0.0 &&
                G4UnitDefinition::IsUnitDefined(unit.c_str())) {
                e.unitScale = G4UnitDefinition::GetValueOf(unit.c_str()) / targetUnit;
            }
            return e;
        }
    }

    // Bare ExprTK expression (reject_if, variables): compile the whole string.
    if (bareExpr) {
        e.compiled = fEval->Precompile(t, varNames);
        if (!e.compiled) {
            G4cerr << "[TableSource] failed to compile expression '" << raw
                   << "' for " << key << G4endl;
            e.active = false;
            e.staticVal = defVal;
            return e;
        }
        e.CachePointers();
        e.unitScale = 1.0;
        return e;
    }

    // Static value (cm/MeV, same convention as before).
    e.staticVal = cfg->GetDouble(key, defVal);
    return e;
}

std::string TableSource::ParticleFromTypeCode(int type) {
    switch (type) {
        case 1: return "gamma";
        case 2: return "e-";
        case 3: return "e+";
        case 4: return "neutron";
        case 5: return "proton";
        default: return "";
    }
}

void TableSource::GeneratePrimaries(G4Event* event) {
    if (!fReader) return;
    const std::size_t n = fReader->GetNumberOfRows();
    if (n == 0) return;

    std::size_t row = 0;
    if (fSequential) {
        row = fNextRow;
        ++fNextRow;
        if (fNextRow >= n) fNextRow = fLoop ? 0 : (n - 1);
    } else {
        row = static_cast<std::size_t>(G4UniformRand() * static_cast<double>(n));
        if (row >= n) row = n - 1;
    }

    const auto vals = fReader->GetRow(row);
    if (!vals) return;

    const double eventId = static_cast<double>(event->GetEventID());

    // File-row values exposed to ExprTK expressions (cm / MeV / dimensionless).
    const double fx = (fColX >= 0 && static_cast<std::size_t>(fColX) < vals->size()) ? (*vals)[fColX] : 0.0;
    const double fy = (fColY >= 0 && static_cast<std::size_t>(fColY) < vals->size()) ? (*vals)[fColY] : 0.0;
    const double fz = (fColZ >= 0 && static_cast<std::size_t>(fColZ) < vals->size()) ? (*vals)[fColZ] : 0.0;
    const double fe = (fColE >= 0 && static_cast<std::size_t>(fColE) < vals->size()) ? (*vals)[fColE] : 0.0;
    const double fu = (fColU >= 0 && static_cast<std::size_t>(fColU) < vals->size()) ? (*vals)[fColU] : 0.0;
    const double fv = (fColV >= 0 && static_cast<std::size_t>(fColV) < vals->size()) ? (*vals)[fColV] : 0.0;
    const double fw = (fColW >= 0 && static_cast<std::size_t>(fColW) < vals->size()) ? (*vals)[fColW] : 0.0;
    const double fwt = (fColWt >= 0 && static_cast<std::size_t>(fColWt) < vals->size()) ? (*vals)[fColWt] : 1.0;

    // Rejection (before any heavy work).
    if (fRejectIf.active &&
        fRejectIf.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt) != 0.0)
        return;

    // Particle: type column wins, else fixed particle, else gamma.
    std::string pname;
    if (fColType >= 0 && static_cast<std::size_t>(fColType) < vals->size())
        pname = ParticleFromTypeCode(static_cast<int>((*vals)[fColType]));
    if (pname.empty()) pname = fParticle;
    if (pname.empty()) pname = "gamma";
    G4ParticleDefinition* pdef = G4ParticleTable::GetParticleTable()->FindParticle(pname);
    if (!pdef) return;

    // Energy: energy column wins, else fixed energy expression.
    double energy = 0.0;
    bool haveEnergy = false;
    if (fColE >= 0 && static_cast<std::size_t>(fColE) < vals->size()) {
        energy = fe;
        haveEnergy = true;
    } else if (fEnergyExpr.active) {
        energy = fEnergyExpr.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt);
        haveEnergy = true;
    }

    // Position resolution: surface > object > pos > file (x/y/z + offset).
    G4ThreeVector pos;  // internal units (mm)
    if (fSurfaceSampler) {
        pos = fSurfaceSampler->SamplePoint();
    } else if (fVolumePhys) {
        pos = PositionSampler::SampleInPhysicalVolume(fVolumePhys);
    } else if (fHasPos) {
        pos = G4ThreeVector(fPosX.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt) * cm,
                            fPosY.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt) * cm,
                            fPosZ.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt) * cm);
    } else {
        double x = fx, y = fy, z = fz;
        x += fOffX.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt);
        y += fOffY.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt);
        z += fOffZ.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt);
        pos = G4ThreeVector(x * cm, y * cm, z * cm);
    }

    // Direction (unit vector) or isotropic fallback.
    G4ThreeVector dir;
    if (fColU >= 0 && fColV >= 0 && fColW >= 0 &&
        static_cast<std::size_t>(fColU) < vals->size() &&
        static_cast<std::size_t>(fColV) < vals->size() &&
        static_cast<std::size_t>(fColW) < vals->size()) {
        dir = G4ThreeVector((*vals)[fColU], (*vals)[fColV], (*vals)[fColW]);
        if (dir.mag2() < 1e-20) dir = G4RandomDirection();
        else dir = dir.unit();
    } else {
        dir = G4RandomDirection();
    }

    // Weight: config override (expr) wins, else file weight column, else 1.0.
    double weight = 1.0;
    if (fWeightExpr.active) {
        weight = fWeightExpr.Evaluate(eventId, fx, fy, fz, fe, fu, fv, fw, fwt);
    } else if (fColWt >= 0 && static_cast<std::size_t>(fColWt) < vals->size()) {
        weight = fwt;
    }

    G4cout << "[TableSource] row=" << row << " particle=" << pname
           << " E=" << energy << " MeV pos=(" << pos.x() / cm << ","
           << pos.y() / cm << "," << pos.z() / cm << ") cm"
           << " dir=(" << dir.x() << "," << dir.y() << "," << dir.z() << ")"
           << " weight=" << weight << G4endl;
    fGun->SetParticleDefinition(pdef);
    if (haveEnergy) fGun->SetParticleEnergy(energy * MeV);
    fGun->SetParticlePosition(pos);
    fGun->SetParticleMomentumDirection(dir);
    fGun->SetParticleWeight(weight);
    fGun->GeneratePrimaryVertex(event);
}
