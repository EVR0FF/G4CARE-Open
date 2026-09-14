//==============================================================================
// G4CARE
// @file    GValueScorer.hh
// @brief   Passive scorer that accumulates chemical-species counts in
//          logarithmically-spaced time bins and computes time-dependent
//          G-values (molecules per 100 eV) per species.
// @details Takes a list of species names, time bins, and an EdepFetcher.
//   At each chemistry snapshot the current time bin is recorded; at end of
//   event the per-species counts are accumulated.  Finalize writes a text
//   file with G(t) = <N_i> / E_dep[100 eV].
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef GVALUE_SCORER_HH
#define GVALUE_SCORER_HH

#include <string>
#include <vector>
#include "globals.hh"

class G4MoleculeCounter;
class G4MoleculeCounterManager;

/// @brief Abstract interface for fetching total energy deposition.
class EdepFetcher {
public:
    virtual ~EdepFetcher() = default;
    virtual G4double GetTotalEdep() const = 0;
};

/// @brief Accumulates species counts in time bins and computes G-values.
class GValueScorer {
public:
    GValueScorer(const std::vector<std::string>& speciesNames,
                 const std::vector<G4double>& timeBins,
                 EdepFetcher* edepFetcher = nullptr);
    ~GValueScorer() = default;

    void ResetRun();
    void ResetEvent();
    void SnapshotAt(G4double currentTime, G4MoleculeCounter* counter = nullptr);
    void AccumulateEvent();
    void Finalize(const std::string& outputFileName, G4int runID = 0);

    const std::vector<std::string>& GetSpeciesNames() const { return fSpeciesNames; }
    const std::vector<G4double>& GetTimeBins() const { return fTimeBins; }
    G4int GetCurrentBin() const { return fCurrentBin; }
    void SetEdepFetcher(EdepFetcher* fetcher) { fEdepFetcher = fetcher; }

    static std::vector<G4double> GenerateLogBins(G4double tStart, G4double tEnd, G4int nBins);

private:
    G4int FindBin(G4double t) const;

    std::vector<std::string> fSpeciesNames;
    std::vector<G4double>   fTimeBins;
    EdepFetcher*             fEdepFetcher;

    G4int    fCurrentBin;
    G4double fEventEdep;
    std::vector<G4double> fEventCounts;

    G4int    fEventCount;
    G4double fRunEdepSum;
    std::vector<G4double> fRunEdepSum2;
    std::vector<std::vector<G4double>> fTimeBinCounts;
    std::vector<std::vector<G4double>> fTimeBinCounts2;
};

#endif