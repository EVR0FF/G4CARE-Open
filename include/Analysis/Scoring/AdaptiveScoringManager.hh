//==============================================================================
// G4CARE
// @file    AdaptiveScoringManager.hh
// @brief   Singleton manager that configures and controls the adaptive octree
//          scoring mesh for energy deposition.
// @details Reads SCORING.* parameters from ConfigManager, constructs AdaptiveScorer
//   sensitive detectors for both master and worker threads, provides run-level
//   operations (reset, reinitialize, merge worker data, export), and tracks
//   target-volume movement between stages.
//
//   Configuration keys read:
//     SCORING.TARGET_VOLUME_NAME, SCORING.HALF_SIZE_X/Y/Z,
//     SCORING.MAX_DEPTH, SCORING.MIN_EVENTS_PER_CELL,
//     SCORING.GRADIENT_THRESHOLD, SCORING.MIN_EVENTS_MERGE,
//     SCORING.SPLIT_COOLDOWN, SCORING.MERGE_COOLDOWN,
//     SCORING.MIN_EDEP_MEV, SCORING.MAX_REL_UNCERTAINTY,
//     SCORING.MAX_TOTAL_NODES, SCORING.REFINE_INTERVAL.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef ADAPTIVE_SCORING_MANAGER_HH
#define ADAPTIVE_SCORING_MANAGER_HH

#include "G4Threading.hh"
#include "G4String.hh"
#include "G4ThreeVector.hh"
#include <algorithm>
#include <vector>

class AdaptiveScorer;
class BeamAnalysis;
class G4LogicalVolume;
class GeometryManager;

/// @brief Singleton controlling the adaptive scoring mesh lifecycle.
class AdaptiveScoringManager {
public:
    /// Thread-local instance (each worker has its own copy).
    static AdaptiveScoringManager* Instance();
    /// Delete thread-local instance.
    static void DeleteInstance();
    /// Master instance for configuration loading (used only in BeginRun).
    static AdaptiveScoringManager* MasterInstance();
    static void DeleteMasterInstance();

    /// Registry of all thread-local instances for MT merging (no mutex —
    /// registration occurs in the TLS object constructor during initialization,
    /// merging on master in EndOfRun when workers have already finished).
    static void RegisterInstance(AdaptiveScoringManager* mgr);
    static void UnregisterInstance(AdaptiveScoringManager* mgr);
    /// Merge data from all worker instances into this instance.
    /// Called on the master thread: AdaptiveScoringManager::Instance()->MergeAllWorkerData()
    void MergeAllWorkerData();

    /// Pre-allocate the fAllInstances vector for N threads (master + workers).
    /// Called on the master thread before workers launch.
    static void PreAllocate(int nThreads);

    /// Load parameters from configuration.
    /// Keys:
    ///   <section>.TARGET_VOLUME_NAME  (string, "Target")
    ///   <section>.HALF_SIZE_X          (string, "10*cm")
    ///   <section>.HALF_SIZE_Y          (string, "10*cm")
    ///   <section>.HALF_SIZE_Z          (string, "10*cm")
    ///   <section>.MAX_DEPTH           (int, 8)
    ///   <section>.MIN_EVENTS_PER_CELL (int, 50)
    ///   <section>.GRADIENT_THRESHOLD  (double, 0.2)
    ///   <section>.MIN_EVENTS_MERGE    (int, 20)
    ///   <section>.SPLIT_COOLDOWN      (int, 10)
    ///   <section>.MERGE_COOLDOWN      (int, 50)
    ///   <section>.MIN_EDEP_MEV        (double, 0.0)
    ///   <section>.MAX_REL_UNCERTAINTY (double, 1.0)
    ///   <section>.MAX_TOTAL_NODES     (int, 0 = unlimited)
    ///   <section>.REFINE_INTERVAL     (int, 0 = every event)
    void LoadConfig(const std::string& section = "ADAPTIVE_SCORING");

    // Lifecycle
    void ConstructSD();
    /// For worker threads: obtain the cloned SD from the LV (called in BeginOfRunAction,
    /// after SDs have already been cloned).
    void TryAttachOnWorker();
    void Reinitialize();
    void Reset();

    // Export
    void ExportResults(const G4String& filename);
    void ExportToBeamAnalysis(BeamAnalysis* beamAnalysis) const;
    void SetGeometryManager(GeometryManager* geomMgr) { fGeometryManager = geomMgr; }
    bool IsInitialized() const { return fIsInitialized; }
    bool HasTargetMoved() const;

    // Scorer access
    const AdaptiveScorer* GetScorer() const { return fScorer; }
    double GetEdepAt(const G4ThreeVector& pos) const;

    // Manual parameter setup (for GeometryManager with arbitrary keys)
    void SetHalfSizes(const G4ThreeVector& hs) { fHalfSizes = hs; }
    void SetTargetVolumeName(const G4String& name) { fTargetVolumeName = name; }
    void SetParameters(size_t maxDepth, size_t minEvents, double gradThreshold, size_t minMerge,
                       size_t splitCool = 10, size_t mergeCool = 50,
                       double minEdep = 0.0, double maxUnc = 1.0,
                       size_t maxNodes = 0, size_t refineInterval = 0) {
        fMaxDepth = maxDepth;
        fMinEvents = minEvents;
        fGradientThreshold = gradThreshold;
        fMinEventsToMerge = minMerge;
        fSplitCooldown = splitCool;
        fMergeCooldown = mergeCool;
        fMinEdepForSplit = minEdep;
        fMaxRelUncertainty = maxUnc;
        fMaxTotalNodes = maxNodes;
        fRefineInterval = refineInterval;
    }

private:
    AdaptiveScoringManager();
    ~AdaptiveScoringManager();

    static G4ThreadLocal AdaptiveScoringManager* fInstance;
    static AdaptiveScoringManager* fMasterInstance;
    static std::vector<AdaptiveScoringManager*> fAllInstances;

    AdaptiveScorer* fScorer;
    GeometryManager* fGeometryManager = nullptr;
    G4String fTargetVolumeName;
    G4ThreeVector fHalfSizes;
    G4ThreeVector fLastCenter;
    size_t fMaxDepth;
    size_t fMinEvents;
    size_t fMinEventsToMerge;
    double fGradientThreshold;
    size_t fSplitCooldown;
    size_t fMergeCooldown;
    double fMinEdepForSplit;
    double fMaxRelUncertainty;
    size_t fMaxTotalNodes;
    size_t fRefineInterval;
    bool fIsInitialized;
};

#endif