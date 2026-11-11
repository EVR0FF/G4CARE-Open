//==============================================================================
//
// G4CARE
//
// @file    BeamAnalysis.cc
// @brief   Implementation of BeamAnalysis — central analysis manager (thread-local singleton).
//
// @details
//   Initialises the OutputManager (RNTuple streams), creates all
//   trees from NTUPLE.* config, manages typed registries for materials,
//   volumes, surfaces, detectors, chemical species, particle names,
//   and process sub-types.  Provides the primary Fill() template used
//   by SteppingAction, TrackingAction, and EventAction.
//
//   Initialisation flow:
//   1. Initialize() — creates all registries, extractor, tree manager,
//      expression evaluator, and cross-section calculator (once).
//   2. BeginRun() — master thread: populates particle/material/volume
//      registries, builds GeometryPropertyRegistry, loads SCORING.
//      All threads: applies geometry properties to ExpressionManager,
//      loads NTUPLE expressions.
//   3. CreateTrees() — builds ROOT NTuples from NTUPLE.* YAML.
//
//   Configuration keys read:
//     NTUPLES.*, G4RUNMANAGER_PRINT_PROGRESS, OUTPUT_FILE
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

#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "G4RunManager.hh"
#include "G4Run.hh"
#include "G4Event.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4SystemOfUnits.hh"
#include "GeometryPropertyRegistry.hh"
#include "ScriptsManager.hh"

G4ThreadLocal BeamAnalysis* BeamAnalysis::fgInstance = nullptr;
G4ThreadLocal ThreadCache BeamAnalysis::fThreadCache;
G4ThreadLocal bool BeamAnalysis::fBeginRunCalled = false;
bool BeamAnalysis::fMasterInitDone = false;

BeamAnalysis* BeamAnalysis::Instance() {
    if (!fgInstance) fgInstance = new BeamAnalysis();
    return fgInstance;
}

void BeamAnalysis::DeleteInstance() { delete fgInstance; fgInstance = nullptr; }

BeamAnalysis::BeamAnalysis() = default;
BeamAnalysis::~BeamAnalysis() = default;

/// @brief One-time initialisation of all registries, evaluator, extractor, tree manager.
///
/// Creates the following components (each thread-local instance owns its own):
/// - Typed registries (particle names, volume names, material names, region names,
///   source names, detector names, touchable paths, chemical formulas, PDG codes,
///   masses, charges, process sub-types).
/// - ChemSpeciesRegistry for Geant4-DNA species.
/// - ExpressionEvaluator and ExpressionManager.
/// - VolumeMaterialRegistry, CrossSectionCalculator, SurfaceRegistry.
/// - DataExtractor and TreeManager (bound to the created registries).
void BeamAnalysis::Initialize() {
    if (fInitialized) return;
    fChemSpeciesReg = std::make_unique<ChemSpeciesRegistry>();
    fEvaluator = std::make_unique<ExpressionEvaluator>();
    fExpressionManager = std::make_unique<ExpressionManager>();
    fRegistry = std::make_unique<VolumeMaterialRegistry>();
    fXSCalculator = std::make_unique<CrossSectionCalculator>();
    fSurfaceRegistry = std::make_unique<SurfaceRegistry>();
    fDataExtractor = std::make_unique<DataExtractor>(fThreadCache, fXSCalculator.get(), fRegistry.get(),
        fChemSpeciesReg.get());
    fOutputManager = std::make_unique<OutputManager>();
    fInitialized = true;
}

/// @brief Per-run initialisation: populate volume/material IDs, load NTUPLE config.
///
/// **Master thread only** (first BeginRun):
/// - Builds geometry properties via GeometryPropertyRegistry::Build().
/// - Loads SCORING (ScriptsManager) configuration.
///
/// **All threads:**
/// - Populate the volume/material ID registry from the global Geant4 stores.
/// - Build volume/material ID caches for the current thread.
/// - Applies geometry properties to ExpressionManager.
/// - Loads NTUPLE expressions from config.
/// - Sets the current run ID in the thread cache.
void BeamAnalysis::BeginRun() {
    auto* cfg = ConfigManager::Instance();

    // Populate the volume/material ID registry on EVERY thread from the global
    // Geant4 stores (safe to run on workers).
    auto* lvStore = G4LogicalVolumeStore::GetInstance();
    if (lvStore) { for (auto* lv : *lvStore) { fRegistry->GetVolumeID(lv);
        fRegistry->GetMaterialID(lv->GetMaterial()); } }
    auto* pvStore = G4PhysicalVolumeStore::GetInstance();
    if (pvStore) { for (auto* pv : *pvStore) { G4LogicalVolume* c = pv->GetLogicalVolume();
        G4LogicalVolume* p = pv->GetMotherLogical(); if (c && p) fRegistry->SetParentLogicalVolume(c, p); } }

    if (!fMasterInitDone) {
        fMasterInitDone = true;

        // Build geometry properties in MASTER thread (G4LogicalVolumeStore exists here)
        auto* geoProp = GeometryPropertyRegistry::Instance();
        geoProp->Build(cfg);

        // Load SCORING configuration (requires G4LogicalVolumeStore)
        auto* scoring = ScriptsManager::Instance();
        scoring->LoadFromConfig(cfg);
    }
    fRegistry->BuildCaches(fThreadCache.volIdCache, fThreadCache.matIdCache);

    if (!fBeginRunCalled) {
        fBeginRunCalled = true;
        G4RunManager::GetRunManager()->SetPrintProgress(cfg->GetInt("G4RUNMANAGER_PRINT_PROGRESS", 100000));
    }

    // Add geometry properties BEFORE loading expressions (every thread)
    auto* geoProp = GeometryPropertyRegistry::Instance();
    for (const auto& [k, v] : geoProp->GetProperties())
        fExpressionManager->AddVariable(k, v);

    if (fExpressionManager) fExpressionManager->LoadFromConfig(cfg);

    auto* run = G4RunManager::GetRunManager()->GetCurrentRun();
    fThreadCache.currentRunId = run ? run->GetRunID() : 0;
    fThreadCache.currentEventId = -1;
}


/// @brief Load the OUTPUT (RNTuple) stream configuration.
/// Called once per thread (thread_local guard).
void BeamAnalysis::CreateTrees() {
    static G4ThreadLocal bool done = false; if (done) return; done = true;
    auto* cfg = ConfigManager::Instance();
    fOutputManager->LoadFromConfig(cfg, fEvaluator.get());
}

/// @brief End-of-run: reserved for writing/closing NTuple files.
///        Actual Write/CloseFile is performed in RunAction::EndOfRunAction().
void BeamAnalysis::EndRun() {}

