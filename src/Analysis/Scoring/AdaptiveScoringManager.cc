//==============================================================================
// G4CARE
// @file    AdaptiveScoringManager.cc
// @brief   Singleton manager that configures and controls the adaptive octree
//          scoring mesh for energy deposition.
// @details AdaptiveScoringManager reads SCORING.* parameters from ConfigManager,
//   constructs AdaptiveScorer sensitive detectors (both master and worker
//   instances for Geant4 MT), provides run-level operations (reset, reinitialize,
//   merge worker data, export to VTK/BeamAnalysis), and tracks whether the
//   target volume has moved between stages.
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

#include "AdaptiveScoringManager.hh"
#include "AdaptiveScorer.hh"
#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "GeometryManager.hh"
#include "G4SDManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4TransportationManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"
#include "G4RunManager.hh"
#include <sstream>
#include <vector>

G4ThreadLocal AdaptiveScoringManager* AdaptiveScoringManager::fInstance = nullptr;
AdaptiveScoringManager* AdaptiveScoringManager::fMasterInstance = nullptr;
std::vector<AdaptiveScoringManager*> AdaptiveScoringManager::fAllInstances;


/// @brief Returns the thread-local singleton instance.
/// @return Pointer to the thread-local AdaptiveScoringManager.
AdaptiveScoringManager* AdaptiveScoringManager::Instance() {
    if (!fInstance) fInstance = new AdaptiveScoringManager();
    return fInstance;
}

/// @brief Deletes the thread-local instance.
void AdaptiveScoringManager::DeleteInstance() {
    delete fInstance;
    fInstance = nullptr;
}

/// @brief Returns the master-thread singleton instance.
/// @return Pointer to the master AdaptiveScoringManager.
AdaptiveScoringManager* AdaptiveScoringManager::MasterInstance() {
    if (!fMasterInstance) fMasterInstance = new AdaptiveScoringManager();
    return fMasterInstance;
}

/// @brief Deletes the master-thread instance.
void AdaptiveScoringManager::DeleteMasterInstance() {
    delete fMasterInstance;
    fMasterInstance = nullptr;
}

/// @brief Pre-allocates the instance vector for master + nThreads workers.
/// @param nThreads Number of worker threads.
void AdaptiveScoringManager::PreAllocate(int nThreads) {
    // nThreads = number of worker threads (master + workers = nThreads+1)
    size_t totalSlots = static_cast<size_t>(nThreads + 1);
    fAllInstances.clear();
    fAllInstances.resize(totalSlots, nullptr);
    G4cout << "AdaptiveScoringManager::PreAllocate: reserved " << totalSlots
           << " slots (master + " << nThreads << " workers)" << G4endl;
}

/// @brief Default constructor. Registers this instance in the static vector.
AdaptiveScoringManager::AdaptiveScoringManager()
    : fScorer(nullptr),
      fTargetVolumeName("Target"),
      fHalfSizes(10*cm, 10*cm, 10*cm),
      fMaxDepth(8),
      fMinEvents(50),
      fMinEventsToMerge(20),
      fGradientThreshold(0.2),
      fSplitCooldown(10),
      fMergeCooldown(50),
      fMinEdepForSplit(0.0),
      fMaxRelUncertainty(1.0),
      fMaxTotalNodes(0),
      fRefineInterval(0),
      fIsInitialized(false)
{
    RegisterInstance(this);
}


AdaptiveScoringManager::~AdaptiveScoringManager() {
    UnregisterInstance(this);
    delete fScorer;
    fScorer = nullptr;
}


// ---- Load parameters from ConfigManager ----

/// @brief Reads scoring configuration from the given ConfigManager section.
/// @param section Configuration section name (e.g. "SCORING").
void AdaptiveScoringManager::LoadConfig(const std::string& section) {
    auto* cfg = ConfigManager::Instance();

    // 1. Target volume name
    fTargetVolumeName = cfg->GetString(section + ".TARGET_VOLUME_NAME", "Target");

    // 2. Half-sizes (three separate keys with units)
    double sx = cfg->GetValueWithUnits(section + ".HALF_SIZE_X", 10.0 * cm);
    double sy = cfg->GetValueWithUnits(section + ".HALF_SIZE_Y", 10.0 * cm);
    double sz = cfg->GetValueWithUnits(section + ".HALF_SIZE_Z", 10.0 * cm);
    fHalfSizes = G4ThreeVector(sx, sy, sz);

    // 3. Grid parameters
    fMaxDepth           = static_cast<size_t>(cfg->GetInt(section + ".MAX_DEPTH",           8));
    fMinEvents          = static_cast<size_t>(cfg->GetInt(section + ".MIN_EVENTS_PER_CELL", 50));
    fGradientThreshold   = cfg->GetDouble(section + ".GRADIENT_THRESHOLD", 0.2);
    fMinEventsToMerge    = static_cast<size_t>(cfg->GetInt(section + ".MIN_EVENTS_MERGE",    20));

    // 4. New parameters
    fSplitCooldown      = static_cast<size_t>(cfg->GetInt(section + ".SPLIT_COOLDOWN",      10));
    fMergeCooldown      = static_cast<size_t>(cfg->GetInt(section + ".MERGE_COOLDOWN",      50));
    fMinEdepForSplit    = cfg->GetDouble(section + ".MIN_EDEP_MEV",     0.0);
    fMaxRelUncertainty  = cfg->GetDouble(section + ".MAX_REL_UNCERTAINTY", 1.0);
    fMaxTotalNodes      = static_cast<size_t>(cfg->GetInt(section + ".MAX_TOTAL_NODES",     0));
    fRefineInterval     = static_cast<size_t>(cfg->GetInt(section + ".REFINE_INTERVAL",     0));

    G4cout << "AdaptiveScoringManager: loaded config from section '" << section << "'" << G4endl;
    G4cout << "  Target: " << fTargetVolumeName << G4endl;
    G4cout << "  HalfSizes: (" << fHalfSizes/cm << " cm)^3" << G4endl;
    G4cout << "  MaxDepth=" << fMaxDepth
           << " MinEvents=" << fMinEvents
           << " GradThreshold=" << fGradientThreshold
           << " MinMerge=" << fMinEventsToMerge << G4endl;
    G4cout << "  SplitCooldown=" << fSplitCooldown
           << " MergeCooldown=" << fMergeCooldown
           << " MinEdep=" << fMinEdepForSplit
           << " MaxUnc=" << fMaxRelUncertainty
           << " MaxNodes=" << fMaxTotalNodes
           << " RefineInterval=" << fRefineInterval << G4endl;
}

/// @brief Creates and registers the AdaptiveScorer sensitive detector,
///        attaching it to the target logical volume.
void AdaptiveScoringManager::ConstructSD() {
    if (fIsInitialized) {
        G4cerr << "AdaptiveScoringManager: Already initialized." << G4endl;
        return;
    }

    auto* geomMgr = fGeometryManager;
    if (!geomMgr) {
        G4cerr << "AdaptiveScoringManager: GeometryManager not available." << G4endl;
        return;
    }

    auto* sdManager = G4SDManager::GetSDMpointer();
    auto* lvStore = G4LogicalVolumeStore::GetInstance();
    // ObjectManager names logical volumes as "<name>_LV"
    G4String lvName = fTargetVolumeName + "_LV";
    G4LogicalVolume* targetLV = lvStore->GetVolume(lvName, false);

    if (!targetLV) {
        G4cerr << "AdaptiveScoringManager: Logical volume '" << lvName << "' not found." << G4endl;
        return;
    }

    bool isMaster = G4Threading::IsMasterThread();

    if (isMaster) {
        // Master thread: create a new scorer, register in SDManager
        G4ThreeVector center = geomMgr->GetVolumeCenter(fTargetVolumeName);
        G4ThreeVector minC = center - fHalfSizes;
        G4ThreeVector maxC = center + fHalfSizes;
        fLastCenter = center;

        G4cout << "AdaptiveScoringManager: [MASTER] Initializing grid around '" << fTargetVolumeName
               << "' at " << center/mm << " mm." << G4endl;

        fScorer = new AdaptiveScorer("AdaptiveScorer", "AdaptiveHits",
                                     fMaxDepth, fMinEvents, fGradientThreshold, fMinEventsToMerge,
                                     fSplitCooldown, fMergeCooldown,
                                     fMinEdepForSplit, fMaxRelUncertainty, fMaxTotalNodes,
                                     fRefineInterval);

        fScorer->SetGridBounds(minC, maxC);
        fScorer->InitializeGrid();

        sdManager->AddNewDetector(fScorer);
        targetLV->SetSensitiveDetector(fScorer);

    } else {
        // Worker thread: Geant4 MT does NOT clone SD automatically via SDManager.
        // Each worker must create its OWN AdaptiveScorer instance
        // and register it in the LOCAL G4SDManager.
        G4cout << "AdaptiveScoringManager::ConstructSD: [WORKER] Creating worker-scoped AdaptiveScorer,"
               << " threadID=" << G4Threading::G4GetThreadId() << G4endl;

        fScorer = new AdaptiveScorer("AdaptiveScorer", "AdaptiveHits",
                                     fMaxDepth, fMinEvents, fGradientThreshold, fMinEventsToMerge,
                                     fSplitCooldown, fMergeCooldown,
                                     fMinEdepForSplit, fMaxRelUncertainty, fMaxTotalNodes,
                                     fRefineInterval);

        G4ThreeVector center = geomMgr->GetVolumeCenter(fTargetVolumeName);
        G4ThreeVector minC = center - fHalfSizes;
        G4ThreeVector maxC = center + fHalfSizes;
        fScorer->SetGridBounds(minC, maxC);
        fScorer->InitializeGrid();

        // Register in the worker's local SDManager AND attach to the cloned volume
        sdManager->AddNewDetector(fScorer);
        targetLV->SetSensitiveDetector(fScorer);

        G4cout << "AdaptiveScoringManager::ConstructSD: [WORKER] AdaptiveScorer created and attached,"
               << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
    }

    fIsInitialized = true;
}

/// @brief Attempts to locate and attach the cloned AdaptiveScorer from the
///        worker-thread G4SDManager.
void AdaptiveScoringManager::TryAttachOnWorker() {
    if (fScorer) return;  // Already have a scorer (master or repeat call)

    if (G4Threading::IsMasterThread()) return;  // Only for worker threads

    // Look for the cloned AdaptiveScorer via G4SDManager
    // (Geant4 MT clones SDs registered in the master's SDManager
    //  and places them in the worker's SDManager under the original name)
    auto* sdManager = G4SDManager::GetSDMpointer();
    static const G4String kScorerName = "AdaptiveScorer";
    G4VSensitiveDetector* clonedSD = sdManager->FindSensitiveDetector(kScorerName, false);

    G4cout << "AdaptiveScoringManager::TryAttachOnWorker: searching 'AdaptiveScorer' in SDManager"
           << " SDptr=" << clonedSD
           << " threadID=" << G4Threading::G4GetThreadId() << G4endl;

    if (clonedSD) {
        fScorer = dynamic_cast<AdaptiveScorer*>(clonedSD);
        if (fScorer) {
            G4cout << "AdaptiveScoringManager::TryAttachOnWorker: attached cloned AdaptiveScorer via SDManager,"
                   << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
        } else {
            G4cerr << "AdaptiveScoringManager::TryAttachOnWorker: dynamic_cast failed, SD is not AdaptiveScorer."
                   << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
        }
    } else {
        G4cerr << "AdaptiveScoringManager::TryAttachOnWorker: 'AdaptiveScorer' not found in SDManager."
               << " Worker will NOT score! threadID=" << G4Threading::G4GetThreadId() << G4endl;
    }
}

/// @brief Resets the scorer, re-centers the grid on the current target volume
///        position, and reinitializes the mesh.
void AdaptiveScoringManager::Reinitialize() {
    if (!fScorer) {
        G4cerr << "AdaptiveScoringManager: Cannot reinitialize, scorer is null." << G4endl;
        return;
    }

    auto* geomMgr = fGeometryManager;
    if (!geomMgr) return;

    G4ThreeVector newCenter = geomMgr->GetVolumeCenter(fTargetVolumeName);
    G4ThreeVector newMin = newCenter - fHalfSizes;
    G4ThreeVector newMax = newCenter + fHalfSizes;
    fLastCenter = newCenter;

    G4cout << "AdaptiveScoringManager: Reinitializing grid. New center: "
           << newCenter/mm << " mm." << G4endl;

    fScorer->Reset();
    fScorer->SetGridBounds(newMin, newMax);
    fScorer->InitializeGrid();
}

/// @brief Resets scoring data, preserving the mesh structure.
void AdaptiveScoringManager::Reset() {
    if (fScorer) fScorer->Reset();
}

/// @brief Exports the current adaptive grid to a VTK file.
/// @param filename Output VTK filename.
void AdaptiveScoringManager::ExportResults(const G4String& filename) {
    if (!fScorer) return;

    auto* transMan = G4TransportationManager::GetTransportationManager();
    G4VPhysicalVolume* world = transMan->GetNavigatorForTracking()->GetWorldVolume();

    if (world) {
        fScorer->ExportToVTK(filename, world, fTargetVolumeName);
    } else {
        G4cerr << "AdaptiveScoringManager: Cannot export, World volume not found." << G4endl;
    }
}

/// @brief Exports all grid cell data to the BeamAnalysis ntuple system.
/// @param beamAnalysis BeamAnalysis singleton.
void AdaptiveScoringManager::ExportToBeamAnalysis(BeamAnalysis* beamAnalysis) const {
    if (!fScorer) return;

    auto* transMan = G4TransportationManager::GetTransportationManager();
    G4VPhysicalVolume* world = transMan->GetNavigatorForTracking()->GetWorldVolume();

    fScorer->ExportToBeamAnalysis(beamAnalysis, world);
}

/// @brief Returns the accumulated energy deposit at a given position.
/// @param pos Position in global coordinates.
/// @return Energy deposit, or 0.0 if not found.
double AdaptiveScoringManager::GetEdepAt(const G4ThreeVector& pos) const {
    if (!fScorer) return 0.0;
    return fScorer->GetEdepAt(pos);
}

/// @brief Registers an instance in the static vector at its thread-ID slot.
/// @param mgr Pointer to the AdaptiveScoringManager instance.
void AdaptiveScoringManager::RegisterInstance(AdaptiveScoringManager* mgr) {
    // Thread-safe: each thread writes to its own cell at index threadId+1
    // (master=-1→0, worker0=0→1, worker1=1→2, ...).
    // The vector is pre-allocated in BuildForMaster() before workers launch.
    G4int tid = G4Threading::G4GetThreadId();
    size_t idx = static_cast<size_t>(tid + 1);  // master:-1→0, workers:0→1,1→2,...
    if (idx >= fAllInstances.size()) {
        G4cerr << "AdaptiveScoringManager::RegisterInstance: tid=" << tid
               << " idx=" << idx << " >= size=" << fAllInstances.size()
               << " — vector not pre-allocated?" << G4endl;
        return;
    }
    fAllInstances[idx] = mgr;
    G4cout << "AdaptiveScoringManager: registered instance tid=" << tid
           << " at idx=" << idx << G4endl;
}

/// @brief Removes an instance from the static vector.
/// @param mgr Pointer to the instance to remove.
void AdaptiveScoringManager::UnregisterInstance(AdaptiveScoringManager* mgr) {
    G4int tid = G4Threading::G4GetThreadId();
    size_t idx = static_cast<size_t>(tid + 1);
    if (idx < fAllInstances.size() && fAllInstances[idx] == mgr) {
        fAllInstances[idx] = nullptr;
    }
}

/// @brief Merges grid data from all worker instances into the master scorer.
void AdaptiveScoringManager::MergeAllWorkerData() {
    if (!fScorer) return;
    auto* dstGrid = fScorer->GetGrid();
    if (!dstGrid) return;

    G4cout << "AdaptiveScoringManager::MergeAllWorkerData: merging "
           << fAllInstances.size() << " slots." << G4endl;

    for (auto* other : fAllInstances) {
        if (!other || other == this) continue;
        if (!other->fScorer) continue;
        auto* srcGrid = other->fScorer->GetGrid();
        if (!srcGrid) continue;
        G4cout << "  Merging instance with " << srcGrid->GetNumberOfCells()
               << " cells..." << G4endl;
        dstGrid->MergeFrom(*srcGrid);
    }
}

/// @brief Checks whether the target volume center has moved since initialization.
/// @return true if the target has moved more than 1 µm.
bool AdaptiveScoringManager::HasTargetMoved() const {
    if (!fIsInitialized) return true;
    auto* geomMgr = fGeometryManager;
    if (!geomMgr) {
        G4cerr << "AdaptiveScoringManager::HasTargetMoved: GeometryManager not set." << G4endl;
        return false;
    }
    G4ThreeVector current = geomMgr->GetVolumeCenter(fTargetVolumeName);
    return (current - fLastCenter).mag2() > 1e-6;
}
