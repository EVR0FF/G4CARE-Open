//==============================================================================
// G4CARE
// @file    AdaptiveScorer.cc
// @brief   Gradient-adaptive spatial scorer that dynamically refines an
//          octree-like mesh based on energy-deposit gradients, variance,
//          and statistical criteria.
// @details AdaptiveScorer uses an AdaptiveGrid<ScoringData> to accumulate
//   energy deposit and NIEL per spatial cell.  After a configurable number
//   of events, it checks for large neighbour gradients, high relative
//   variance, and absolute energy thresholds to decide which cells to split
//   or merge.  Results can be exported to BeamAnalysis ntuples or VTK files.
//
//   Configuration keys read: via constructor parameters (MAX_DEPTH,
//     MIN_EVENTS_TO_SPLIT, GRADIENT_THRESHOLD, MIN_EVENTS_TO_MERGE,
//     SPLIT_COOLDOWN, MERGE_COOLDOWN, MIN_EDEP_FOR_SPLIT, MAX_REL_UNCERTAINTY,
//     MAX_TOTAL_NODES, REFINE_INTERVAL).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "AdaptiveScorer.hh"
#include "BeamAnalysis.hh"
#include "G4SystemOfUnits.hh"
#include "G4PhysicalConstants.hh"
#include "G4Navigator.hh"
#include "G4ios.hh"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <map>

/// @brief Constructs the scorer with all refinement parameters.
/// @param name           Sensitive detector name.
/// @param hcName         Hits collection name.
/// @param maxD           Maximum tree depth.
/// @param minEv          Minimum events before a cell can be split.
/// @param gradThresh     Relative gradient threshold for neighbour-based split.
/// @param minMerge       Minimum events before a cell can be merged.
/// @param splitCool      Cooldown (in events) between splits.
/// @param mergeCool      Cooldown (in events) between merges.
/// @param minEdep        Minimum energy deposit to consider for splitting.
/// @param maxUnc         Maximum allowed relative uncertainty.
/// @param maxNodes       Hard limit on total number of grid nodes.
/// @param refineInterval Number of events between refinement passes (0 = every event).
AdaptiveScorer::AdaptiveScorer(const G4String& name, 
                               const G4String& hcName,
                               size_t maxD,
                               size_t minEv,
                               G4double gradThresh,
                               size_t minMerge,
                               size_t splitCool,
                               size_t mergeCool,
                               G4double minEdep,
                               G4double maxUnc,
                               size_t maxNodes,
                               size_t refineInterval)
    : G4VSensitiveDetector(name), 
      fMaxDepth(maxD), 
      fMinEventsToSplit(minEv), 
      fMinEventsToMerge(minMerge),
      fGradientThreshold(gradThresh),
      fSplitCooldown(splitCool),
      fMergeCooldown(mergeCool),
      fMinEdepForSplit(minEdep),
      fMaxRelUncertainty(maxUnc),
      fMaxTotalNodes(maxNodes),
      fRefineInterval(refineInterval),
      fEventCounterSinceRefine(0),
      fBoundsSet(false)
{
    collectionName.insert(hcName);
    grid = new AdaptiveGrid<ScoringData>(maxD, minEv, minMerge,
                                         splitCool, mergeCool,
                                         minEdep, maxUnc, maxNodes);
    hitCollection = nullptr;
}

AdaptiveScorer::~AdaptiveScorer() {
    delete grid;
}

// -------------------------------------------------------------------
//  Clone() — thread-local copy for Geant4 MT
// -------------------------------------------------------------------

/// @brief Creates a thread-local clone for multi-threaded Geant4.
/// @return Pointer to the cloned AdaptiveScorer.
G4VSensitiveDetector* AdaptiveScorer::Clone() const {
    auto* clone = new AdaptiveScorer(SensitiveDetectorName,
                                     collectionName.empty() ? "adaptGridHits" : *collectionName.begin(),
                                     fMaxDepth, fMinEventsToSplit, fGradientThreshold,
                                     fMinEventsToMerge, fSplitCooldown, fMergeCooldown,
                                     fMinEdepForSplit, fMaxRelUncertainty, fMaxTotalNodes,
                                     fRefineInterval);

    if (fBoundsSet) {
        clone->SetGridBounds(fGridMin, fGridMax);
        clone->InitializeGrid();
    }
    return clone;
}

// -------------------------------------------------------------------
//  SetGridBounds / InitializeGrid
// -------------------------------------------------------------------

/// @brief Sets the spatial bounding box for the adaptive grid.
/// @param min Lower corner of the bounding box.
/// @param max Upper corner of the bounding box.
void AdaptiveScorer::SetGridBounds(const G4ThreeVector& min, const G4ThreeVector& max) {
    fGridMin = min;
    fGridMax = max;
    fBoundsSet = true;
}

/// @brief Initializes the grid with the previously set bounds.
void AdaptiveScorer::InitializeGrid() {
    if (fBoundsSet && grid) {
        grid->Initialize(fGridMin, fGridMax);
    }
}

/// @brief Called at the start of each event to initialize the hit collection.
/// @param HCE Hit collection of this event.
void AdaptiveScorer::Initialize(G4HCofThisEvent* HCE) {
    // Create hit collection (if event history needs to be stored).
    // In the adaptive grid we write directly to nodes, so the collection can be
    // empty or used for debugging.
}

/// @brief Processes a single G4Step: accumulates energy deposit and NIEL
///        into the grid cell containing the step's position.
/// @param step  Current G4Step.
/// @param hist  Touchable history (unused).
/// @return true if the step was processed.
G4bool AdaptiveScorer::ProcessHits(G4Step* step, G4TouchableHistory*) {
    static int callCount = 0;
    callCount++;
    
    G4double edep = step->GetTotalEnergyDeposit();
    if (edep == 0.0) return false;

    G4StepPoint* prePoint = step->GetPreStepPoint();
    G4ThreeVector pos = prePoint->GetPosition();
    G4double weight = step->GetTrack()->GetWeight();

    size_t index;
    if (!grid->GetCellIndex(pos, index)) {
        return false;
    }

    ScoringData* data = grid->GetData(index);
    if (data) {
        data->Add(edep, weight);
        
        // NIEL — non-ionizing energy loss (nuclear stopping, displacements)
        G4double niel = step->GetNonIonizingEnergyDeposit();
        if (niel > 0.0) {
            data->AddNIEL(niel, weight);
        }
    }

    // Increment eventCount in the node and split if needed
    grid->AddEvent(pos);

    return true;
}

/// @brief Called at the end of each event.  Triggers periodic grid refinement
///        (gradient check, variance split, edep split, low-statistics merge).
/// @param HCE Hit collection (unused).
void AdaptiveScorer::EndOfEvent(G4HCofThisEvent*) {
    fEventCounterSinceRefine++;

    // Periodic refine: if REFINE_INTERVAL=0 — every event (old behavior),
    // otherwise — every fRefineInterval events
    bool doRefine = (fRefineInterval == 0) || (fEventCounterSinceRefine >= fRefineInterval);

    if (doRefine) {
        fEventCounterSinceRefine = 0;

        // Guard: if the grid is already at its limit, skip refinement
        if (fMaxTotalNodes > 0 && grid->GetTotalNodes() >= fMaxTotalNodes) {
            static int warnCount = 0;
            if (warnCount++ < 3) {
                G4cout << "AdaptiveScorer: Node limit reached (" << fMaxTotalNodes
                       << "). Refinement disabled for remaining events." << G4endl;
            }
            return;
        }

        // --- Gradient-based refinement ---
        CheckAndRefineGradients();
        
        // Additional split strategies
        grid->CheckAndRefineVariance();
        grid->CheckAndRefineEdep();
        
        // Merge cells with low statistics
        grid->CheckAndMergeAll();
    }
}

/// @brief Scans all leaf cells for large energy-deposit gradients compared
///        to their 6 nearest neighbors and marks steep cells for splitting.
void AdaptiveScorer::CheckAndRefineGradients() {
    const auto& leaves = grid->GetLeafIndices();
    std::vector<size_t> cellsToSplit;

    for (size_t leafIdx : leaves) {
        ScoringData* data = grid->GetData(leafIdx);
        if (!data || data->edep() == 0.0) continue;

        G4ThreeVector center = grid->GetCellCenter(leafIdx);
        G4double size = grid->GetCellSize(leafIdx);
        G4double halfSize = size * 0.5;

        // 6 directions: +/-X, +/-Y, +/-Z
        int offsets[6][3] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
        const G4double epsilon = 0.001 * mm;  // microscopic offset from boundary

        for (auto& off : offsets) {
            G4ThreeVector neighborPos = center;
            neighborPos.setX(center.x() + off[0] * (halfSize + epsilon));
            neighborPos.setY(center.y() + off[1] * (halfSize + epsilon));
            neighborPos.setZ(center.z() + off[2] * (halfSize + epsilon));

            size_t nIdx;
            if (grid->GetCellIndex(neighborPos, nIdx)) {
                ScoringData* nData = grid->GetData(nIdx);
                if (nData && nData->edep() > 0.0) {
                    G4double maxVal = std::max(data->edep(), nData->edep());
                    G4double diff = std::abs(data->edep() - nData->edep());
                    if (diff / maxVal > fGradientThreshold) {
                        cellsToSplit.push_back(leafIdx);
                        cellsToSplit.push_back(nIdx);
                        break; // one steep neighbor is enough
                    }
                }
            }
        }
    }

    // Remove duplicates and split
    std::sort(cellsToSplit.begin(), cellsToSplit.end());
    cellsToSplit.erase(std::unique(cellsToSplit.begin(), cellsToSplit.end()), cellsToSplit.end());

    if (!cellsToSplit.empty()) {
        for (size_t idx : cellsToSplit) {
            grid->RefineCell(idx);
        }
    }
}

/// @brief Exports all grid cell data (center, size, Edep, NIEL, uncertainty)
///        to the BeamAnalysis ntuple system.
/// @param beamAnalysis BeamAnalysis singleton.
/// @param world        World physical volume for coordinate transformation (optional).
void AdaptiveScorer::ExportToBeamAnalysis(BeamAnalysis* beamAnalysis,
                                          G4VPhysicalVolume* world) const {
    if (!grid || !beamAnalysis) return;

    const auto& leaves = grid->GetLeafIndices();
    std::map<std::string, double> values;

    // Filter leaves with data
    std::vector<size_t> validLeaves;
    for (size_t idx : leaves) {
        ScoringData* data = grid->GetData(idx);
        if (data && data->edep() > 0.0) validLeaves.push_back(idx);
    }

    // Navigator for global coordinates (if world is provided)
    G4Navigator* navigator = nullptr;
    if (world) {
        navigator = new G4Navigator();
        navigator->SetWorldVolume(world);
    }

    for (size_t idx : validLeaves) {
        ScoringData* data = grid->GetData(idx);
        if (!data) continue;

        G4ThreeVector center = grid->GetCellCenter(idx);

        // Try to determine global coordinates via navigator
        if (navigator) {
            G4VPhysicalVolume* vol = navigator->LocateGlobalPointAndSetup(center);
            if (vol && vol->GetRotation()) {
                G4AffineTransform transform(vol->GetRotation()->inverse(),
                                            vol->GetObjectTranslation());
                center = transform.TransformPoint(center);
            }
        }

        double size = grid->GetCellSize(idx);

        values.clear();
        values["CellX"]       = center.x();
        values["CellY"]       = center.y();
        values["CellZ"]       = center.z();
        values["CellSize"]    = size;
        values["Edep"]        = data->edep();
        values["NIEL"]        = data->niel();
        values["Weight"]      = data->weight();
        values["Count"]       = static_cast<double>(data->count);
        values["Uncertainty"] = data->RelativeUncertainty();

        beamAnalysis->Fill("AdaptiveScoring", values);
    }

    delete navigator;

    G4cout << "AdaptiveScorer: Exported " << validLeaves.size()
           << " cells to BeamAnalysis." << G4endl;
}

/// @brief Returns the accumulated energy deposit at a given spatial position.
/// @param pos Position in global coordinates.
/// @return Energy deposit (internal Geant4 units), or 0.0 if not found.
G4double AdaptiveScorer::GetEdepAt(const G4ThreeVector& pos) const {
    if (!grid) return 0.0;

    size_t idx;
    if (grid->GetCellIndex(pos, idx)) {
        ScoringData* data = grid->GetData(idx);
        if (data) return data->edep();
    }
    return 0.0;
}

/// @brief Resets all scoring data, preserving the mesh structure.
void AdaptiveScorer::Reset() {
    if (grid) {
        grid->ResetData();
        G4cout << "AdaptiveScorer: Scoring data cleared. Mesh structure retained." << G4endl;
    }
}

/// @brief Exports the grid to a VTK file for visualization.
/// @param filename         Output filename.
/// @param world            World physical volume (optional, for filtering).
/// @param targetVolumeName Optional volume name filter.
void AdaptiveScorer::ExportToVTK(const std::string& filename, G4VPhysicalVolume* world,
                                 const std::string& targetVolumeName) const {
    if (!grid) return;
    grid->ExportToVTK(filename, world, targetVolumeName);
    G4cout << "AdaptiveScorer: Grid exported to " << filename
           << (targetVolumeName.empty() ? "" : " (filtered: " + targetVolumeName + ")")
           << G4endl;
}
