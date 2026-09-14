//==============================================================================
// G4CARE
// @file    WeightWindowManager.cc
// @brief   Implementation of the weight-window biasing manager: configuration
//          parsing, process registration, weight loading/computation, scoring
//          setup, and parallel world configuration.
// @details Implements the singleton WeightWindowManager.  Supports manual,
//   GDML, and auto-computed mesh modes.  Reads WEIGHT_WINDOW configuration,
//   registers G4WeightWindowBiasing for requested particles with per-particle
//   overrides, loads/computes weights, and configures the scoring mesh and
//   G4WeightWindowStore.
//
//   Configuration keys read: WEIGHT_WINDOW.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "WeightWindowManager.hh"
#include "ConfigManager.hh"
#include "WeightWindowWorld.hh"
#include "G4WeightWindowStore.hh"
#include "G4WeightWindowBiasing.hh"
#include "G4WeightWindowAlgorithm.hh"
#include "G4ParallelWorldScoringProcess.hh"
#include "G4ParticleTable.hh"
#include "G4ProcessManager.hh"
#include "G4ios.hh"
#include "G4UImanager.hh"
#include "G4SystemOfUnits.hh"
#include "G4ScoringManager.hh"
#include "G4GeometrySampler.hh"
#include "G4TransportationManager.hh"
#include "G4VModularPhysicsList.hh"
#include "ExpressionEvaluator.hh"
#include "G4PSCellFlux3D.hh"
#include "G4THitsMap.hh"
#include "G4VScoringMesh.hh"
#include "G4ScoringBox.hh"
#include <fstream>
#include <map>

G4ThreadLocal WeightWindowManager* WeightWindowManager::fgInstance = nullptr;
WeightWindowManager* WeightWindowManager::fgMasterInstance = nullptr;

/// @brief Returns the thread-local singleton instance, copying from master
///        if needed.
WeightWindowManager* WeightWindowManager::Instance() {
    if (!fgInstance) {
        if (fgMasterInstance) {
            fgInstance = new WeightWindowManager(*fgMasterInstance);
        } else {
            fgInstance = new WeightWindowManager();
        }
    }
    return fgInstance;
}

/// @brief Initializes the master instance and reads configuration.
/// @param configPath YAML key prefix (e.g. WEIGHT_WINDOW).
void WeightWindowManager::Initialize(const std::string& configPath) {
    if (!fgMasterInstance) {
        fgMasterInstance = new WeightWindowManager();
        fgMasterInstance->ReadConfiguration(configPath);
        fgInstance = fgMasterInstance;
    } else {
        G4cerr << "WeightWindowManager::Initialize called more than once." << G4endl;
    }
}

WeightWindowManager::WeightWindowManager()
: fEnabled(false),
  fMeshType(MeshType::kNone),
  fUpperLimitFactor(5.0),
  fSurvivalFactor(3.0),
  fMaxNumberOfSplits(5),
  fGdmlFile(""),
  fWeightsApplied(false)
{
    fManual = {};
    fAuto   = {};
}

WeightWindowManager::WeightWindowManager(const WeightWindowManager& other)
: fEnabled(other.fEnabled),
  fMeshType(other.fMeshType),
  fManual(other.fManual),
  fGdmlFile(other.fGdmlFile),
  fAuto(other.fAuto),
  fParticles(other.fParticles),
  fUpperLimitFactor(other.fUpperLimitFactor),
  fSurvivalFactor(other.fSurvivalFactor),
  fMaxNumberOfSplits(other.fMaxNumberOfSplits),
  fWeightFormula(other.fWeightFormula),
  fParticleOverrides(other.fParticleOverrides),
  fWeightsApplied(false)
{
    fGrid = other.fGrid;
}

/// @brief Parses the WEIGHT_WINDOW configuration section (mesh type, bins,
///        weights, particles, algorithm parameters, and per-particle overrides).
/// @param configPath YAML key prefix.
void WeightWindowManager::ReadConfiguration(const std::string& configPath) {
    auto* cfg = ConfigManager::Instance();
    fEnabled = cfg->GetBool(configPath + ".ENABLE", false);
    
    if (!fEnabled) {
        G4cout << "WeightWindowManager: Weight Window is DISABLED" << G4endl;
        return;
    }
    
    G4cout << "==================================================" << G4endl;
    G4cout << "WeightWindowManager: Reading configuration from " << configPath << G4endl;
    G4cout << "==================================================" << G4endl;
    
    std::string meshTypeStr = cfg->GetString(configPath + ".mesh.type", "");
    
    if (meshTypeStr == "manual") {
        fMeshType = MeshType::kManual;
        fAuto.mode = "";
        std::vector<int> bins = cfg->GetIntVector(configPath + ".mesh.manual.bins");
        if (bins.size() >= 3) {
            fManual.nx = bins[0];
            fManual.ny = bins[1];
            fManual.nz = bins[2];
        } else {
            G4cerr << "WeightWindowManager: manual mesh requires bins array (size 3)" << G4endl;
            fEnabled = false; return;
        }
        
        fManual.weights = cfg->GetDoubleVectorWithUnits(configPath + ".mesh.manual.weights", {});
        fManual.weightsFile = cfg->GetString(configPath + ".mesh.manual.weights_file", "");
        
        if (!fManual.weights.empty()) {
            G4cout << "WeightWindowManager: loaded " << fManual.weights.size() 
                   << " weights from config" << G4endl;
        } else if (!fManual.weightsFile.empty()) {
            G4cout << "WeightWindowManager: weights will be loaded from file " 
                   << fManual.weightsFile << G4endl;
        } else {
            G4cerr << "WeightWindowManager: manual mesh requires either weights or weights_file" << G4endl;
            fEnabled = false; return;
        }
        
        std::vector<double> minVals = cfg->GetDoubleVectorWithUnits(configPath + ".mesh.manual.min");
        if (minVals.size() >= 3) {
            fManual.minX = minVals[0];
            fManual.minY = minVals[1];
            fManual.minZ = minVals[2];
        } else {
            G4cerr << "WeightWindowManager: manual mesh requires min array (size 3)" << G4endl;
            fEnabled = false; return;
        }
        
        std::vector<double> maxVals = cfg->GetDoubleVectorWithUnits(configPath + ".mesh.manual.max");
        if (maxVals.size() >= 3) {
            fManual.maxX = maxVals[0];
            fManual.maxY = maxVals[1];
            fManual.maxZ = maxVals[2];
        } else {
            G4cerr << "WeightWindowManager: manual mesh requires max array (size 3)" << G4endl;
            fEnabled = false; return;
        }
    }
    else if (meshTypeStr == "gdml") {
        fMeshType = MeshType::kGdml;
        fGdmlFile = cfg->GetString(configPath + ".mesh.gdml.file", "");
        if (fGdmlFile.empty()) {
            G4cerr << "WeightWindowManager: GDML mesh requires file name" << G4endl;
            fEnabled = false; return;
        }
    }
    else if (meshTypeStr == "auto") {
        fMeshType = MeshType::kAuto;
        std::vector<int> bins = cfg->GetIntVector(configPath + ".mesh.auto.bins");
        if (bins.size() >= 3) {
            fAuto.nx = bins[0];
            fAuto.ny = bins[1];
            fAuto.nz = bins[2];
        } else {
            G4cerr << "WeightWindowManager: auto mesh requires bins array (size 3)" << G4endl;
            fEnabled = false; return;
        }
        
        std::vector<double> minVals = cfg->GetDoubleVectorWithUnits(configPath + ".mesh.auto.min");
        if (minVals.size() >= 3) {
            fAuto.minX = minVals[0];
            fAuto.minY = minVals[1];
            fAuto.minZ = minVals[2];
        } else {
            G4cerr << "WeightWindowManager: auto mesh requires min array (size 3)" << G4endl;
            fEnabled = false; return;
        }
        
        std::vector<double> maxVals = cfg->GetDoubleVectorWithUnits(configPath + ".mesh.auto.max");
        if (maxVals.size() >= 3) {
            fAuto.maxX = maxVals[0];
            fAuto.maxY = maxVals[1];
            fAuto.maxZ = maxVals[2];
        } else {
            G4cerr << "WeightWindowManager: auto mesh requires max array (size 3)" << G4endl;
            fEnabled = false; return;
        }
        
        fAuto.mode = cfg->GetString(configPath + ".mesh.auto.mode", "collect");
        if (fAuto.mode != "collect" && fAuto.mode != "apply") {
            G4cerr << "WeightWindowManager: auto mesh mode must be 'collect' or 'apply'" << G4endl;
            fEnabled = false; return;
        }
        fAuto.statFile = cfg->GetString(configPath + ".mesh.auto.stat_file", "");
        if (fAuto.statFile.empty()) {
            G4cerr << "WeightWindowManager: auto mesh requires stat_file for both collect and apply modes." << G4endl;
            fEnabled = false; return;
        }
        
        if (fAuto.mode == "apply") {
            fWeightFormula = cfg->GetString(configPath + ".mesh.auto.formula", "");
            if (fWeightFormula.empty()) {
                G4cerr << "WeightWindowManager: auto mode 'apply' requires formula" << G4endl;
                fEnabled = false; return;
            }
            fAuto.statFile = cfg->GetString(configPath + ".mesh.auto.stat_file", "");
            if (fAuto.statFile.empty()) {
                G4cerr << "WeightWindowManager: auto mode 'apply' requires stat_file" << G4endl;
                fEnabled = false; return;
            }
        }
    }
    else {
        G4cerr << "WeightWindowManager: unknown mesh type '" << meshTypeStr << "'" << G4endl;
        fEnabled = false; return;
    }
    
    std::vector<std::string> partStrs = cfg->GetStringVector(configPath + ".particles");
    for (const auto& p : partStrs) {
        fParticles.push_back(G4String(p));
    }
    if (fParticles.empty()) {
        G4cerr << "WeightWindowManager: no particles specified for weight window" << G4endl;
        fEnabled = false; return;
    }
    
    fUpperLimitFactor = cfg->GetDouble(configPath + ".algorithm.upperLimitFactor", 5.0);
    fSurvivalFactor = cfg->GetDouble(configPath + ".algorithm.survivalFactor", 3.0);
    fMaxNumberOfSplits = cfg->GetInt(configPath + ".algorithm.maxNumberOfSplits", 5);
    
    G4cout << "WeightWindowManager: Default algorithm parameters:" << G4endl;
    G4cout << "  - upperLimitFactor = " << fUpperLimitFactor << G4endl;
    G4cout << "  - survivalFactor = " << fSurvivalFactor << G4endl;
    G4cout << "  - maxNumberOfSplits = " << fMaxNumberOfSplits << G4endl;
    
    std::vector<std::string> overrideParticles = cfg->GetSubsections(configPath + ".particle_overrides");
    
    for (const auto& particleName : overrideParticles) {
        std::string overridePath = configPath + ".particle_overrides." + particleName;
        
        ParticleAlgorithmParams params;
        params.upperLimitFactor = cfg->GetDouble(overridePath + ".upperLimitFactor", fUpperLimitFactor);
        params.survivalFactor = cfg->GetDouble(overridePath + ".survivalFactor", fSurvivalFactor);
        params.maxNumberOfSplits = cfg->GetInt(overridePath + ".maxNumberOfSplits", fMaxNumberOfSplits);
        
        fParticleOverrides[particleName] = params;
        
        G4cout << "WeightWindowManager: Particle override for '" << particleName << "':" << G4endl;
        G4cout << "  - upperLimitFactor = " << params.upperLimitFactor << G4endl;
        G4cout << "  - survivalFactor = " << params.survivalFactor << G4endl;
        G4cout << "  - maxNumberOfSplits = " << params.maxNumberOfSplits << G4endl;
    }
    
    G4cout << "==================================================" << G4endl;
    G4cout << "WeightWindowManager: Configuration loaded successfully" << G4endl;
    G4cout << "==================================================" << G4endl;
}

/// @brief Configures a WeightWindowWorld with the current grid or GDML file.
/// @param world Pointer to the parallel world to configure.
void WeightWindowManager::ConfigureParallelWorld(WeightWindowWorld* world) const {
    if (!world) return;
    
    switch (fMeshType) {
        case MeshType::kManual:
        case MeshType::kAuto:
            world->SetGrid(&fGrid);
            break;
        case MeshType::kGdml:
            world->SetGdmlFile(fGdmlFile);
            break;
        default:
            break;
    }
}

/// @brief Loads manual weights from file or config into the internal grid.
void WeightWindowManager::LoadWeights() {
    if (!fEnabled || fMeshType != MeshType::kManual) return;

    std::vector<double> weights;
    if (!fManual.weightsFile.empty()) {
        std::ifstream file(fManual.weightsFile.c_str());
        if (!file) {
            G4cerr << "WeightWindowManager: cannot open weights file" << G4endl;
            return;
        }
        double val;
        while (file >> val) weights.push_back(val);
    } else {
        weights = fManual.weights;
    }

    G4int expected = fManual.nx * fManual.ny * fManual.nz;
    if ((G4int)weights.size() != expected) {
        G4cerr << "WeightWindowManager: weight count mismatch" << G4endl;
        return;
    }

    // Create grid and fill it
    fGrid = RegularGrid<double>(fManual.nx, fManual.ny, fManual.nz,
                                fManual.minX, fManual.minY, fManual.minZ,
                                fManual.maxX, fManual.maxY, fManual.maxZ);
    for (G4int i = 0; i < expected; ++i) fGrid[i] = weights[i];

    G4cout << "WeightWindowManager: loaded " << expected << " weights into grid." << G4endl;
}

/// @brief Applies the loaded/computed weights to the G4WeightWindowStore.
void WeightWindowManager::ApplyWeights() {
    if (fWeightsApplied) return;
    if (!fEnabled) return;
    if (fMeshType == MeshType::kAuto && fAuto.mode == "collect") {
        G4cout << "WeightWindowManager: COLLECT mode – skipping biasing registration" << G4endl;
        return;
    }
    if (fMeshType != MeshType::kManual && 
        !(fMeshType == MeshType::kAuto && fAuto.mode == "apply")) {
        return;
    }

    auto* store = G4WeightWindowStore::GetInstance("WeightWindowWorld");
    if (!store) {
        G4cerr << "ERROR: Cannot get WeightWindowStore!" << G4endl;
        return;
    }

    // Set energy bounds (single interval for now)
    std::set<G4double> enBounds;
    enBounds.insert(100 * CLHEP::MeV);
    store->SetGeneralUpperEnergyBounds(enBounds);

    auto* parallelWorld = G4TransportationManager::GetTransportationManager()
                              ->GetParallelWorld("WeightWindowWorld");
    if (!parallelWorld) {
        G4cerr << "ERROR: Parallel world not found!" << G4endl;
        return;
    }

    G4GeometryCell motherCell(*parallelWorld, 0);
    // For mother volume weight = 1.0
    std::vector<G4double> motherWeight = { 1.0 };
    store->AddLowerWeights(motherCell, motherWeight);
    G4cout << "DEBUG: Added weight 1.0 for mother volume." << G4endl;

    G4LogicalVolume* logicalWorld = parallelWorld->GetLogicalVolume();
    G4int nDaughters = logicalWorld->GetNoDaughters();
    G4cout << "DEBUG: ApplyWeights: found " << nDaughters << " daughters." << G4endl;

    const G4int nVoxels = fGrid.GetTotalVoxels();

    if (nDaughters == 1 && logicalWorld->GetDaughter(0)->IsParameterised()) {
        // Geometry built via G4PVParameterised (usually for auto or manual modes)
        G4VPhysicalVolume* physVol = logicalWorld->GetDaughter(0);
        for (G4int copyNo = 0; copyNo < nVoxels; ++copyNo) {
            G4GeometryCell cell(*physVol, copyNo);
            std::vector<G4double> w = { fGrid[copyNo] };
            store->AddLowerWeights(cell, w);
        }
    } else {
        // Legacy method when voxels are placed individually (fallback)
        for (G4int i = 0; i < nDaughters; ++i) {
            G4VPhysicalVolume* physVol = logicalWorld->GetDaughter(i);
            G4int copyNo = physVol->GetCopyNo();
            if (copyNo >= 0 && copyNo < nVoxels) {
                G4GeometryCell cell(*physVol, copyNo);
                std::vector<G4double> w = { fGrid[copyNo] };
                store->AddLowerWeights(cell, w);
            }
        }
    }

    G4cout << "WeightWindowManager::ApplyWeights: applied weights for " 
           << nVoxels << " cells." << G4endl;
    fWeightsApplied = true;
}

/// @brief Registers G4WeightWindowBiasing for each requested particle type
///        with per-particle algorithm parameters.
/// @param physList Modular physics list to register the biasing with.
void WeightWindowManager::RegisterProcesses(G4VModularPhysicsList* physList) {
    if (!fEnabled) return;
    
    if (fMeshType == MeshType::kAuto && fAuto.mode == "collect") {
        G4cout << "WeightWindowManager: COLLECT mode – skipping biasing registration" << G4endl;
        return;
    }
    
    G4cout << "==================================================" << G4endl;
    G4cout << "WeightWindowManager::RegisterProcesses started" << G4endl;
    G4cout << "==================================================" << G4endl;
    
    G4String parallelWorldName = "WeightWindowWorld";
    
    for (const auto& particleName : fParticles) {
        ParticleAlgorithmParams params = GetParticleParams(particleName);
        
        G4cout << "\nDEBUG: Registering G4WeightWindowBiasing for " << particleName << G4endl;
        G4cout << "  - upperLimitFactor = " << params.upperLimitFactor << G4endl;
        G4cout << "  - survivalFactor = " << params.survivalFactor << G4endl;
        G4cout << "  - maxNumberOfSplits = " << params.maxNumberOfSplits << G4endl;
        
        G4GeometrySampler* sampler = new G4GeometrySampler(parallelWorldName, particleName);
        sampler->SetParallel(true);
        
        G4WeightWindowAlgorithm* algorithm = new G4WeightWindowAlgorithm(
            params.upperLimitFactor,
            params.survivalFactor,
            params.maxNumberOfSplits
        );
        
        physList->RegisterPhysics(
            new G4WeightWindowBiasing(sampler, algorithm, G4PlaceOfAction::onBoundary, parallelWorldName)
        );
        
        G4cout << "DEBUG: G4WeightWindowBiasing registered for " << particleName << G4endl;
    }
    
    G4cout << "\n==================================================" << G4endl;
    G4cout << "WeightWindowManager::RegisterProcesses completed" << G4endl;
    G4cout << "==================================================" << G4endl;
}

/// @brief Creates a G4Scoring mesh for auto-mode flux collection.
void WeightWindowManager::SetupScoring() {
    if (!fEnabled || fMeshType != MeshType::kAuto || fAuto.mode != "collect") return;

    G4ScoringManager::GetScoringManager();
    
    G4UImanager* ui = G4UImanager::GetUIpointer();

    ui->ApplyCommand("/score/create/boxMesh WWScoringMesh");
    
    G4double hx = (fAuto.maxX - fAuto.minX) / 2.0 / CLHEP::mm;
    G4double hy = (fAuto.maxY - fAuto.minY) / 2.0 / CLHEP::mm;
    G4double hz = (fAuto.maxZ - fAuto.minZ) / 2.0 / CLHEP::mm;
    ui->ApplyCommand("/score/mesh/boxSize " + std::to_string(hx) + " " + std::to_string(hy) + " " + std::to_string(hz) + " mm");
    
    G4double cx = (fAuto.maxX + fAuto.minX) / 2.0 / CLHEP::mm;
    G4double cy = (fAuto.maxY + fAuto.minY) / 2.0 / CLHEP::mm;
    G4double cz = (fAuto.maxZ + fAuto.minZ) / 2.0 / CLHEP::mm;
    ui->ApplyCommand("/score/mesh/translate/xyz " + std::to_string(cx) + " " + std::to_string(cy) + " " + std::to_string(cz) + " mm");
    
    ui->ApplyCommand("/score/mesh/nBin " + std::to_string(fAuto.nx) + " " + std::to_string(fAuto.ny) + " " + std::to_string(fAuto.nz));
  
    ui->ApplyCommand("/score/quantity/cellFlux flux");
    ui->ApplyCommand("/score/close");
    
    G4cout << "WeightWindowManager: Scoring mesh created safely via UI commands." << G4endl;
}

/// @brief Saves accumulated flux statistics from the scoring mesh to a file.
void WeightWindowManager::SaveStatistics() {
    if (!fEnabled || fMeshType != MeshType::kAuto || fAuto.mode != "collect") return;
    if (!G4Threading::IsMasterThread()) return;
    if (fAuto.statFile.empty()) {
        G4cerr << "WeightWindowManager: stat_file not specified, cannot save statistics." << G4endl;
        return;
    }

    auto* scoringMan = G4ScoringManager::GetScoringManager();
    if (!scoringMan) return;

    G4VScoringMesh* mesh = nullptr;
    for (size_t idx = 0; idx < scoringMan->GetNumberOfMesh(); ++idx) {
        if (scoringMan->GetMesh(idx)->GetWorldName() == "WWScoringMesh") {
            mesh = scoringMan->GetMesh(idx);
            break;
        }
    }

    if (!mesh) {
        G4cerr << "WeightWindowManager: Scoring mesh 'WWScoringMesh' not found!" << G4endl;
        return;
    }

    std::ofstream out(fAuto.statFile.c_str());
    if (!out.is_open()) {
        G4cerr << "WeightWindowManager: cannot open stat file " << fAuto.statFile << G4endl;
        return;
    }

    auto scoreMap = mesh->GetScoreMap();
    auto it = scoreMap.find("flux");
    if (it == scoreMap.end()) {
        G4cerr << "WeightWindowManager: flux scorer not found in mesh" << G4endl;
        return;
    }

    auto* hitsMap = it->second;
    auto* mapPtr = hitsMap->GetMap();

    const G4int nx = fAuto.nx;
    const G4int ny = fAuto.ny;
    const G4int nz = fAuto.nz;

    // Assume scorer uses the same indexing: i slowest, k fastest.
    for (G4int i = 0; i < nx; ++i) {
        for (G4int j = 0; j < ny; ++j) {
            for (G4int k = 0; k < nz; ++k) {
                G4int idx = i * ny * nz + j * nz + k; // matches old indexing
                auto itVal = mapPtr->find(idx);
                G4double flux = 0.0;
                if (itVal != mapPtr->end() && itVal->second) {
                    flux = itVal->second->sum_wx(); // sum of weights (weighted fluence)
                }
                out << i << " " << j << " " << k << " " << flux << "\n";
            }
        }
    }

    out.close();
    G4cout << "WeightWindowManager: statistics saved to " << fAuto.statFile << G4endl;
}

/// @brief Loads flux statistics from file and computes weight-window weights
///        using the configured formula or inverse-flux method.
void WeightWindowManager::LoadStatisticsAndComputeWeights() {
    if (!fEnabled || fMeshType != MeshType::kAuto || fAuto.mode != "apply") return;

    std::ifstream in(fAuto.statFile.c_str());
    if (!in.is_open()) {
        G4cerr << "WeightWindowManager: cannot open stat file " << fAuto.statFile 
               << " for reading." << G4endl;
        return;
    }

    const G4int nx = fAuto.nx;
    const G4int ny = fAuto.ny;
    const G4int nz = fAuto.nz;
    const G4int nCells = nx * ny * nz;
    std::vector<G4double> fluxes(nCells, 0.0);

    G4int i, j, k;
    G4double flux;
    while (in >> i >> j >> k >> flux) {
        G4int idx = i * ny * nz + j * nz + k; // Keeping old indexing: i outer, j middle, k inner
        // Bounds check
        if (idx >= 0 && idx < nCells) fluxes[idx] = flux;
    }
    in.close();

    // Initialize the grid with the correct dimensions
    fGrid = RegularGrid<double>(nx, ny, nz,
                                fAuto.minX, fAuto.minY, fAuto.minZ,
                                fAuto.maxX, fAuto.maxY, fAuto.maxZ);

    // Compute weights
    if (!fWeightFormula.empty()) {
        // Use ExpressionEvaluator to compute weight from formula
        ExpressionEvaluator eval;
        std::vector<std::string> varNames = {"flux", "i", "j", "k"};
        auto precompiled = ExpressionEvaluator::PrecompiledExpr(fWeightFormula, varNames, &eval);

        if (!precompiled.compiled) {
            G4cerr << "WeightWindowManager: failed to compile formula: " 
                   << fWeightFormula << G4endl;
            fEnabled = false;
            return;
        }

        for (G4int idx = 0; idx < nCells; ++idx) {
            G4int ii = idx % nx;
            G4int jj = (idx / nx) % ny;
            G4int kk = idx / (nx * ny);
            std::map<std::string, double> vars;
            vars["flux"] = fluxes[idx];
            vars["i"] = ii;
            vars["j"] = jj;
            vars["k"] = kk;
            fGrid[idx] = eval.Execute(precompiled.compiled.get(), vars);
        }
    } else {
        // Standard inverse-flux weighting
        for (G4int idx = 0; idx < nCells; ++idx) {
            if (fluxes[idx] > 0.0)
                fGrid[idx] = 1.0 / fluxes[idx];
            else
                fGrid[idx] = 1.0; // or a large value?
        }
    }

    G4cout << "WeightWindowManager: computed weights for " << nCells << " cells." << G4endl;
}
/// @brief Returns the algorithm parameters for a particle, applying defaults
///        and per-particle overrides.
/// @param particleName Particle name (e.g. "neutron", "gamma").
/// @return Filled ParticleAlgorithmParams struct.
WeightWindowManager::ParticleAlgorithmParams
WeightWindowManager::GetParticleParams(const G4String& particleName) const {
    auto it = fParticleOverrides.find(particleName);
    if (it != fParticleOverrides.end()) {
        return it->second;
    }
    
    ParticleAlgorithmParams params;
    params.upperLimitFactor = fUpperLimitFactor;
    params.survivalFactor = fSurvivalFactor;
    params.maxNumberOfSplits = fMaxNumberOfSplits;
    return params;
}

/// @brief Checks whether a particle has specific override parameters.
/// @param particleName Particle name.
/// @return true if override parameters exist for this particle.
bool WeightWindowManager::HasParticleOverride(const G4String& particleName) const {
    return fParticleOverrides.find(particleName) != fParticleOverrides.end();
}