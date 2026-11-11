//==============================================================================
//
// G4CARE
//
// @file    BeamAnalysis.hh
// @brief   Central analysis manager: NTuple initialisation, step/track/vertex
//          data filling, material/volume/detector registries, data extraction.
//
// @details
//   Thread-local singleton that owns all analysis infrastructure:
//
//   - OutputManager: RNTuple stream booking and writing.
//   - Template Fill(): dispatches step, track, GPS vertex, and map-based
//     data to BeamDataExtractor, evaluates filter expressions, and writes
//     NTuple rows.
//   - Registries (thread-safe, master-initialised once): materials,
//     volumes, surfaces, detectors, chemical species, particle names,
//     process sub-types.
//   - TreeManager: builds per-NTUPLE column lists (ColType) and filter
//     expressions from NTUPLE.* YAML blocks.
//   - CrossSectionCalculator, ExpressionEvaluator, ExpressionManager —
//     all owned and accessible via this singleton.
//
//   Configuration keys read: OUTPUT.*.
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

#ifndef BEAM_ANALYSIS_HH
#define BEAM_ANALYSIS_HH

#include "G4Threading.hh"
#include "VolumeMaterialRegistry.hh"
#include "CrossSectionCalculator.hh"
#include "DataExtractor.hh"
#include "ExpressionEvaluator.hh"
#include "ExpressionManager.hh"
#include "UnifiedSource.hh"
#include "ColumnTypes.hh" 
#include "DetectorRegistry.hh"
#include "SurfaceRegistry.hh"
#include "ChemSpeciesRegistry.hh"
#include "ScriptsManager.hh"
#include "OutputManager.hh"
#include <memory>
#include <string>

/// @brief Per-thread cache for volume/material IDs and run/event counters.
///
/// Avoids repeated registry lookups by caching recently requested
/// volume and material IDs.  currentRunId and currentEventId are
/// updated at run/event boundaries.
struct ThreadCache {
    std::vector<int> volIdCache;
    std::vector<int> matIdCache;
    G4int currentRunId = 0;
    G4int currentEventId = -1;
};

/// @brief Central analysis manager — thread-local singleton.
///
/// Owns registries, data extractor, expression evaluator,
/// expression evaluator, and cross-section calculator.  Provides the
/// primary Fill() template used by SteppingAction, TrackingAction,
/// and EventAction to record simulation data.
class BeamAnalysis {
public:
    /// @brief Get or create the thread-local instance.
    static BeamAnalysis* Instance();
    /// @brief Delete the thread-local instance.
    static void DeleteInstance();

    /// @brief One-time initialisation: create registries, eval, extractor, trees.
    void Initialize();
    /// @brief Begin of run: create per-thread NTuples and notify extractor.
    void BeginRun();
    /// @brief End of run: write ROOT file and close.
    void EndRun();
    /// @brief Create NTuple trees from NTUPLE.* config.
    void CreateTrees();

    /// @brief Generic NTuple fill from step, track, or GPS vertex data.
    ///
    /// Builds FilterVars via DataExtractor, evaluates optional filter
    /// expression and SCRIPT expressions, then fills NTuple columns
    /// (double or string) and adds a row.
    /// @tparam SourceType Deduced from G4Step*, G4Track*, or UnifiedSource.
    /// @param treeName  NTuple tree name (e.g. "g4care", "secondary").
    /// @param source    Step, track, or vertex data wrapper.
    /// @param eventId   Override event ID (-1 = auto).
    /// @param volId     Override volume ID (-1 = auto).
    /// @param matId     Override material ID (-1 = auto).
    template<typename SourceType>
    void Fill(const std::string& treeName, const SourceType& source, int eventId = -1, int volId = -1, int matId = -1);

    /// @brief NTuple fill from a pre-built name-value map (used for
    ///        "activation" and "scripts_user" trees).

    /// @name Registry accessors
    /// @{
    int GetVolumeID(const G4LogicalVolume* lv) const { return fRegistry ? fRegistry->GetVolumeID(lv) : -1; }
    int GetMaterialID(const G4Material* mat) const { return fRegistry ? fRegistry->GetMaterialID(mat) : -1; }
    int GetCurrentRunId() const { return fThreadCache.currentRunId; }
    DetectorRegistry* GetDetectorRegistry() const { return fDetectorRegistry; }
    SurfaceRegistry* GetSurfaceRegistry() const { return fSurfaceRegistry.get(); }
    ChemSpeciesRegistry* GetChemSpeciesRegistry() const { return fChemSpeciesReg.get(); }
    DataExtractor* GetDataExtractor() const { return fDataExtractor.get(); }
    ExpressionEvaluator* GetEvaluator() const { return fEvaluator.get(); }
    ExpressionManager* GetExpressionManager() const { return fExpressionManager.get(); }
    OutputManager* GetOutputManager() const { return fOutputManager.get(); }
    /// @}

    /// @brief Set the detector registry (owned by GeometryManager).
    void SetDetectorRegistry(DetectorRegistry* reg) { fDetectorRegistry = reg; }

private:
    BeamAnalysis();
    ~BeamAnalysis();

    static G4ThreadLocal BeamAnalysis* fgInstance;
    static G4ThreadLocal ThreadCache fThreadCache;

    bool fInitialized = false;
    /// @brief Thread-local: each thread creates its own NTuples.
    static G4ThreadLocal bool fBeginRunCalled;
    /// @brief Global: registry registration is performed once by master.
    static bool fMasterInitDone;

    std::unique_ptr<ExpressionEvaluator> fEvaluator;
    std::unique_ptr<VolumeMaterialRegistry> fRegistry;
    std::unique_ptr<CrossSectionCalculator> fXSCalculator;
    std::unique_ptr<DataExtractor> fDataExtractor;
    
    DetectorRegistry* fDetectorRegistry = nullptr;  // Owned by GeometryManager
    std::unique_ptr<SurfaceRegistry> fSurfaceRegistry;
    std::unique_ptr<ChemSpeciesRegistry> fChemSpeciesReg;
    std::unique_ptr<OutputManager> fOutputManager;
    std::unique_ptr<ExpressionManager> fExpressionManager;

    /// @brief Evaluate a filter expression for a given NTuple tree.
    /// @param tree TreeInfo with compiled filter expression.
    /// @param vars FilterVars populated for the current step/track.
    /// @return @c true if the data should be recorded.
};

/// @brief Templated NTuple fill implementation.
///
/// 1. Creates UnifiedSource from step/track/vertex.
/// 2. Builds FilterVars via DataExtractor (only variables needed by
///    filter or expression columns are computed).
/// 3. Evaluates the optional filter expression; returns early if false.
/// 4. Evaluates all SCRIPT expressions at Step level (ExpressionManager).
/// 5. Iterates active columns: for Expression columns the evaluator is
///    called; for built-in columns DataExtractor::GetDouble/GetString is
///    used.  Name/string columns are stored as strings.
/// 6. Fills the RNTuple stream via OutputManager.
template<typename SourceType>
void BeamAnalysis::Fill(const std::string& treeName, const SourceType& source, int eventId, int volId, int matId) {
    // RNTuple output path (OUTPUT config).
    if (fOutputManager && fOutputManager->IsEnabled()) {
        StreamLevel level = LevelFromTreeName(treeName);
        if (level != StreamLevel::Unknown) {
            UnifiedSource src(source);
            fOutputManager->Fill(level, src, fDataExtractor.get(), fEvaluator.get(), eventId, volId, matId);
        }
    }
}
#endif