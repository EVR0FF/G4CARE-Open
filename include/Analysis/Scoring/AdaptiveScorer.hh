#ifndef ADAPTIVE_SCORER_HH
#define ADAPTIVE_SCORER_HH

#include "G4VSensitiveDetector.hh"
#include "G4Step.hh"
#include "G4HCofThisEvent.hh"
#include "AdaptiveGrid.hh"
#include <string>
#include <vector>
//==============================================================================
// G4CARE
// @file    AdaptiveScorer.hh
// @brief   Gradient-adaptive sensitive detector that dynamically refines an
//          octree-like scoring mesh based on energy-deposit gradients,
//          variance, and statistical criteria.
// @details Uses an AdaptiveGrid<ScoringData> to accumulate Edep and NIEL per
//   spatial cell.  After a configurable number of events it splits cells with
//   large neighbor gradients, high variance, or high absolute Edep, and merges
//   low-statistics cells.  Results can be exported to BeamAnalysis or VTK.
//
//   Configuration keys read: via constructor parameters.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include <map>

/// Scoring cell data structure (weighted statistics).
/// Σ(w*E), Σ(w²*E²), Σ(w*NIEL), Σ(w²*NIEL²), Σw
struct ScoringData {
    G4double sum_wE     = 0.0;  // Σ (w * Edep)     — weighted energy deposition
    G4double sum_w2E2   = 0.0;  // Σ (w² * Edep²)   — for Edep variance
    G4double sum_wNIEL   = 0.0;  // Σ (w * NIEL)     — weighted NIEL
    G4double sum_w2NIEL2 = 0.0;  // Σ (w² * NIEL²)   — for NIEL variance
    G4double sum_w      = 0.0;  // Σ w              — total weight
    int count = 0;              // Number of hits (for split/merge statistics)
    
    void Add(G4double eDeposited, G4double weight) {
        G4double wE = weight * eDeposited;
        sum_wE   += wE;
        sum_w2E2 += weight * weight * eDeposited * eDeposited;
        sum_w    += weight;
        count++;
    }
    
    void AddNIEL(G4double niel, G4double weight) {
        G4double wN = weight * niel;
        sum_wNIEL   += wN;
        sum_w2NIEL2 += weight * weight * niel * niel;
        // weight и count не трогаем — они уже в Add()
    }
    
    void Reset() {
        sum_wE     = 0.0;
        sum_w2E2   = 0.0;
        sum_wNIEL   = 0.0;
        sum_w2NIEL2 = 0.0;
        sum_w      = 0.0;
        count      = 0;
    }
    
    /// Relative uncertainty of Edep (coefficient of variation).
    G4double RelativeUncertainty() const {
        if (count <= 1 || sum_wE <= 0.0 || sum_w2E2 <= 0.0) return 1.0;
        return std::sqrt(sum_w2E2) / sum_wE;
    }
    
    // For backward compatibility and VTK export
    G4double edep()   const { return sum_wE; }
    G4double edep2()  const { return sum_w2E2; }
    G4double niel()   const { return sum_wNIEL; }
    G4double niel2()  const { return sum_w2NIEL2; }
    G4double weight() const { return sum_w; }
};

class AdaptiveScorer : public G4VSensitiveDetector {
public:
    AdaptiveScorer(const G4String& name, 
                   const G4String& hitsCollectionName,
                   size_t maxDepth,
                   size_t minEventsToSplit,
                   G4double gradientThreshold,
                   size_t minEventsToMerge = 20,
                   size_t splitCooldown = 10,
                   size_t mergeCooldown = 50,
                   G4double minEdepForSplit = 0.0,
                   G4double maxRelUncertainty = 1.0,
                   size_t maxTotalNodes = 0,
                   size_t refineInterval = 0);

    virtual ~AdaptiveScorer();

    // --- G4VSensitiveDetector interface ---
    void Initialize(G4HCofThisEvent* HCE) override;
    G4bool ProcessHits(G4Step* step, G4TouchableHistory* history) override;
    void EndOfEvent(G4HCofThisEvent* HCE) override;

    /// Thread-local copy for Geant4 MT mode.
    /// Creates a clone with identical parameters and grid structure,
    /// but with clean data (ScoringData zeroed out).
    G4VSensitiveDetector* Clone() const override;

    // --- Grid configuration ---
    void SetGridBounds(const G4ThreeVector& min, const G4ThreeVector& max);
    void InitializeGrid();

    // --- Grid access ---
    const AdaptiveGrid<ScoringData>* GetGrid() const { return grid; }
    AdaptiveGrid<ScoringData>* GetGrid() { return grid; }

    /// Reset data (before a new run).
    void Reset();

    /// Export to VTK.
    void ExportToVTK(const std::string& filename, G4VPhysicalVolume* world,
                     const std::string& targetVolumeName = "") const;

    /// Export cell data to BeamAnalysis via Fill(map).
    /// @param beamAnalysis  Pointer to BeamAnalysis.
    /// @param world         World physical volume for coordinate transformation.
    void ExportToBeamAnalysis(class BeamAnalysis* beamAnalysis,
                               G4VPhysicalVolume* world = nullptr) const;

    /// Energy deposition at a point (grid interpolation).
    G4double GetEdepAt(const G4ThreeVector& pos) const;

private:
    AdaptiveGrid<ScoringData>* grid;
    std::vector<ScoringData>* hitCollection; // Local buffer for the current event (optional)
    
    // Parameters
    size_t fMaxDepth;
    size_t fMinEventsToSplit;
    size_t fMinEventsToMerge;
    G4double fGradientThreshold;
    size_t fSplitCooldown;
    size_t fMergeCooldown;
    G4double fMinEdepForSplit;
    G4double fMaxRelUncertainty;
    size_t fMaxTotalNodes;
    size_t fRefineInterval;
    size_t fEventCounterSinceRefine; // event counter for periodic refinement

    G4ThreeVector fGridMin; // Grid bounds (stored for Clone)
    G4ThreeVector fGridMax;
    bool fBoundsSet;        // true if SetGridBounds has been called

    /// Helper method for gradient-based cell refinement.
    void CheckAndRefineGradients();
};

#endif // ADAPTIVE_SCORER_HH