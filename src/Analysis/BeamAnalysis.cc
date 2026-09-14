//==============================================================================
//
// G4CARE
//
// @file    BeamAnalysis.cc
// @brief   Implementation of BeamAnalysis — central analysis manager (thread-local singleton).
//
// @details
//   Initialises the G4AnalysisManager (ROOT file), creates all NTuple
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
//      loads NTUPLE expressions, writes dictionary trees.
//   3. CreateTrees() — builds ROOT NTuples from NTUPLE.* YAML.
//
//   Configuration keys read:
//     ENABLE_DICTIONARY, NTUPLES.*, G4RUNMANAGER_PRINT_PROGRESS, OUTPUT_FILE
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
#include "DictionaryWriter.hh"
#include "G4RunManager.hh"
#include "G4Run.hh"
#include "G4Event.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4SystemOfUnits.hh"
#include "GeometryPropertyRegistry.hh"
#include "ExprsManager.hh"

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
/// - G4AnalysisManager reference.
/// - VolumeMaterialRegistry, CrossSectionCalculator, SurfaceRegistry.
/// - DataExtractor and TreeManager (bound to the created registries).
/// - DictionaryWriter.
void BeamAnalysis::Initialize() {
    if (fInitialized) return;
    fParticleNameReg = std::make_unique<TypedRegistry<std::string>>();
    fVolumeNameReg = std::make_unique<TypedRegistry<std::string>>();
    fMaterialNameReg = std::make_unique<TypedRegistry<std::string>>();
    fRegionNameReg = std::make_unique<TypedRegistry<std::string>>();
    fSourceNameReg = std::make_unique<TypedRegistry<std::string>>();
    fDetectorNameReg = std::make_unique<TypedRegistry<std::string>>();
    fTouchablePathReg = std::make_unique<TypedRegistry<std::string>>();
    fChemicalFormulaReg = std::make_unique<TypedRegistry<std::string>>();
    fChemSpeciesReg = std::make_unique<ChemSpeciesRegistry>();
    fProcessSubTypeReg = std::make_unique<TypedRegistry<int>>();
    fPDGReg = std::make_unique<TypedRegistry<int>>();
    fMassReg = std::make_unique<TypedRegistry<double>>();
    fChargeReg = std::make_unique<TypedRegistry<double>>();
    fEvaluator = std::make_unique<ExpressionEvaluator>();
    fExpressionManager = std::make_unique<ExpressionManager>();
    fAnalysisManager = G4AnalysisManager::Instance();
    fRegistry = std::make_unique<VolumeMaterialRegistry>();
    fXSCalculator = std::make_unique<CrossSectionCalculator>();
    fSurfaceRegistry = std::make_unique<SurfaceRegistry>();
    fDataExtractor = std::make_unique<DataExtractor>(fThreadCache, fXSCalculator.get(), fRegistry.get(),
        fParticleNameReg.get(), fVolumeNameReg.get(), fMaterialNameReg.get(), fRegionNameReg.get(),
        fSourceNameReg.get(), fDetectorNameReg.get(), fTouchablePathReg.get(), fChemicalFormulaReg.get(),
        fPDGReg.get(), fMassReg.get(), fChargeReg.get(), fChemSpeciesReg.get());
    fTreeManager = std::make_unique<TreeManager>(*fRegistry, *fEvaluator, fXSCalculator.get(),
        fAnalysisManager, fParticleNameReg.get(), fVolumeNameReg.get(), fMaterialNameReg.get(),
        fRegionNameReg.get(), fSourceNameReg.get(), fDetectorNameReg.get(), fTouchablePathReg.get(),
        fChemicalFormulaReg.get(), fPDGReg.get(), fProcessSubTypeReg.get(), fMassReg.get(), fChargeReg.get(),
        fDetectorRegistry, fSurfaceRegistry.get(), fChemSpeciesReg.get());
    fDictionaryWriter = std::make_unique<DictionaryWriter>(*fRegistry, fXSCalculator.get(), fAnalysisManager,
        fParticleNameReg.get(), fVolumeNameReg.get(), fMaterialNameReg.get(), fRegionNameReg.get(),
        fSourceNameReg.get(), fDetectorNameReg.get(), fTouchablePathReg.get(), fChemicalFormulaReg.get(),
        fPDGReg.get(), fProcessSubTypeReg.get(), fMassReg.get(), fChargeReg.get(),
        fChemSpeciesReg.get(), fDetectorRegistry, fSurfaceRegistry.get());
    fInitialized = true;
}

/// @brief Per-run initialisation: populate registries, load NTUPLE config, write dictionaries.
///
/// **Master thread only** (first BeginRun):
/// - Iterates G4ParticleTable, G4MaterialTable, G4LogicalVolumeStore,
///   G4PhysicalVolumeStore to populate name and ID registries.
/// - Builds parent logical volume relationships via VolumeMaterialRegistry.
/// - Builds geometry properties via GeometryPropertyRegistry::Build().
/// - Loads SCORING (ExprsManager) configuration.
///
/// **All threads:**
/// - Builds volume/material ID caches for the current thread.
/// - Writes dictionary NTuple trees if ENABLE_DICTIONARY = true (once).
/// - Applies geometry properties to ExpressionManager.
/// - Loads NTUPLE expressions from config.
/// - Sets the current run ID in the thread cache.
void BeamAnalysis::BeginRun() {
    auto* cfg = ConfigManager::Instance();

    if (!fMasterInitDone) {
        fMasterInitDone = true;
        auto* pTable = G4ParticleTable::GetParticleTable();
        if (pTable) { G4ParticleTable::G4PTblDicIterator* iter = pTable->GetIterator(); if (iter) { iter->reset();
            while ((*iter)()) { G4ParticleDefinition* def = iter->value(); if (!def) continue;
                if (fParticleNameReg) fParticleNameReg->Register(def->GetParticleName());
                if (fPDGReg) fPDGReg->Register(def->GetPDGEncoding());
                if (fMassReg) fMassReg->Register(def->GetPDGMass());
                if (fChargeReg) fChargeReg->Register(def->GetPDGCharge()); } } }
        const G4MaterialTable* matTable = G4Material::GetMaterialTable();
        if (matTable) { for (auto* mat : *matTable) { if (!mat) continue;
            if (fMaterialNameReg) fMaterialNameReg->Register(mat->GetName());
            if (fChemicalFormulaReg) fChemicalFormulaReg->Register(mat->GetChemicalFormula()); } }
        auto* lvStore = G4LogicalVolumeStore::GetInstance();
        if (lvStore) { for (auto* lv : *lvStore) { if (!lv) continue;
            if (fVolumeNameReg) fVolumeNameReg->Register(lv->GetName());
            auto* r = lv->GetRegion(); if (r && fRegionNameReg) fRegionNameReg->Register(r->GetName()); } }
        if (lvStore) { for (auto* lv : *lvStore) { fRegistry->GetVolumeID(lv);
            fRegistry->GetMaterialID(lv->GetMaterial()); } }
        auto* pvStore = G4PhysicalVolumeStore::GetInstance();
        if (pvStore) { for (auto* pv : *pvStore) { G4LogicalVolume* c = pv->GetLogicalVolume();
            G4LogicalVolume* p = pv->GetMotherLogical(); if (c && p) fRegistry->SetParentLogicalVolume(c, p); } }

        // Build geometry properties in MASTER thread (G4LogicalVolumeStore exists here)
        auto* geoProp = GeometryPropertyRegistry::Instance();
        geoProp->Build(cfg);

        // Load SCORING configuration (requires G4LogicalVolumeStore)
        auto* scoring = ExprsManager::Instance();
        scoring->LoadFromConfig(cfg);
    }
    fRegistry->BuildCaches(fThreadCache.volIdCache, fThreadCache.matIdCache);

    if (!fBeginRunCalled) {
        fBeginRunCalled = true;
        if (cfg->GetBool("ENABLE_DICTIONARY", true)) fDictionaryWriter->FillDictionaryTrees();
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

/// @brief NTuple fill from a name-value map (used for "activation" and "exprs_user" trees).
///
/// For each active column in the named tree:
/// - For non-expression columns, looks up the value in @p values by column name.
/// - For dictionary columns, the value is stored as an integer index.
/// - For string columns, the dictionary registry is consulted to retrieve the
///   human-readable string.
/// - Adds one ROOT NTuple row at the end.
///
/// @param treeName NTuple tree name (e.g. "activation", "exprs_user").
/// @param values   Column name → numeric value map.
void BeamAnalysis::Fill(const std::string& treeName, const std::map<std::string, double>& values) {
    auto* tree = fTreeManager->GetTree(treeName); if (!tree) return;
    for (size_t idx : tree->activeColumns) { const auto& col = tree->allColumns[idx]; double val = 0.0;
        if (col.type != ColType::Expression || !col.expr) {
            auto it = values.find(col.name); if (it != values.end()) val = it->second;
        }
        if (col.isDictionary) fAnalysisManager->FillNtupleIColumn(tree->ntupleId, col.ntupleColumnId, (int)val);
        else if (col.isSColumn) { std::string s = fTreeManager->GetDictString(col.dictionaryName, (int)val);
            fAnalysisManager->FillNtupleSColumn(tree->ntupleId, col.ntupleColumnId, s); }
        else fAnalysisManager->FillNtupleDColumn(tree->ntupleId, col.ntupleColumnId, val);
    }
    fAnalysisManager->AddNtupleRow(tree->ntupleId);
}

/// @brief Create ROOT NTuple trees from the NTUPLES YAML list.
///
/// For each tree name listed in NTUPLES, calls TreeManager::CreateTreeFromConfig()
/// which parses the NTUPLE.<name>.columns and NTUPLE.<name>.filter keys
/// to build column descriptors and compile filter/expression ExprTK trees.
///
/// Optionally creates dictionary NTuple trees first if ENABLE_DICTIONARY is set.
/// This function is called once per thread (thread_local guard).
void BeamAnalysis::CreateTrees() {
    static G4ThreadLocal bool done = false; if (done) return; done = true;
    auto* cfg = ConfigManager::Instance();
    if (cfg->GetBool("ENABLE_DICTIONARY", true)) fDictionaryWriter->CreateDictionaryTrees(cfg);
    for (const auto& tname : cfg->GetStringVector("NTUPLES")) fTreeManager->CreateTreeFromConfig(tname, cfg);
}

/// @brief End-of-run: reserved for writing/closing NTuple files.
///        Actual Write/CloseFile is performed in RunAction::EndOfRunAction().
void BeamAnalysis::EndRun() {}

/// @brief Evaluate all filter criteria for an NTuple tree.
///
/// @param tree TreeInfo with optional volume filter list, filterCondition,
///             and filter expression.
/// @param vars FilterVars populated for the current step/track.
/// @return @c true if the data passes all active filters.
///
/// Filter evaluation order:
/// 1. Volume name check: if tree.filterVolumes is non-empty, the current
///    volume name (resolved from VolumeID via the registry) must match
///    one of the listed names.
/// 2. Filter condition: if non-null, the compiled condition expression
///    must evaluate to a non-zero value.
/// 3. Filter expression: if non-null, the compiled filter expression
///    must evaluate to a non-zero value.
///
/// If no filters are defined, returns @c true unconditionally.
bool BeamAnalysis::PassFilter(const TreeManager::TreeInfo& tree, const FilterVars& vars) const {
    if (!tree.filterVolumes.empty()) {
        int vid = (int)vars[(size_t)FilterVar::VolumeID];
        std::string vn = fVolumeNameReg ? fVolumeNameReg->GetValue(vid) : "";
        bool found = false;
        for (const auto& fv : tree.filterVolumes) {
            // Exact match first, then substring match (handles GDML _log suffixes)
            if (fv == vn || vn.find(fv) != std::string::npos) { found = true; break; }
        }
        if (!found) return false;
    }
    if (tree.filterCondition) { if (fEvaluator->Execute(tree.filterCondition.get(), vars) == 0.0) return false; }
    if (tree.filter) return fEvaluator->Execute(tree.filter.get(), vars) != 0.0;
    return true;
}