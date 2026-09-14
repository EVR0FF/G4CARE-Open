//==============================================================================
// G4CARE
// @file    GValueScorer.cc
// @brief   Passive scorer that accumulates chemical-species counts in
//          logarithmically-spaced time bins and computes time-dependent
//          G-values (molecules per 100 eV) per species.
// @details GValueScorer takes a list of species names, a set of time bins,
//   and an EdepFetcher.  At each chemistry snapshot it records the current
//   time bin; at the end of the event the per-species counts for that bin
//   are accumulated.  Finalize writes a text file with G(t) = <N_i> / E_dep[100 eV].
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Analysis/Scoring/GValueScorer.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"
#include <cmath>
#include <fstream>
#include <algorithm>

/// @brief Constructs the scorer with species names, time bins, and an energy
///        deposition fetcher.
/// @param speciesNames List of chemical species names.
/// @param timeBins     Vector of time bin boundaries (in ps).
/// @param edepFetcher  Fetcher for total energy deposition per event.
GValueScorer::GValueScorer(const std::vector<std::string>& speciesNames,
                           const std::vector<G4double>& timeBins,
                           EdepFetcher* edepFetcher)
    : fSpeciesNames(speciesNames), fTimeBins(timeBins), fEdepFetcher(edepFetcher)
    , fCurrentBin(-1), fEventEdep(0.0), fEventCounts(speciesNames.size(), 0.0)
    , fEventCount(0), fRunEdepSum(0.0)
{
    G4int nb = (G4int)fTimeBins.size();
    fTimeBinCounts.resize(fSpeciesNames.size(), std::vector<G4double>(nb, 0.0));
    fTimeBinCounts2.resize(fSpeciesNames.size(), std::vector<G4double>(nb, 0.0));
}
/// @brief Resets all run-level accumulators.
void GValueScorer::ResetRun() {
    fEventCount=0; fRunEdepSum=0.0;
    for(auto& r:fTimeBinCounts) std::fill(r.begin(),r.end(),0.0);
    for(auto& r:fTimeBinCounts2) std::fill(r.begin(),r.end(),0.0);
}
/// @brief Resets event-level accumulators.
void GValueScorer::ResetEvent() {
    fCurrentBin=-1; fEventEdep=0.0;
    std::fill(fEventCounts.begin(), fEventCounts.end(), 0.0);
}
/// @brief Finds the time bin index for a given time.
/// @param t Time in ps.
/// @return Bin index, or -1 if t is before the first bin.
G4int GValueScorer::FindBin(G4double t) const {
    if(fTimeBins.empty()) return -1;
    if(t<fTimeBins.front()) return -1;
    for(size_t i=0;i+1<fTimeBins.size();++i)
        if(t>=fTimeBins[i]&&t<fTimeBins[i+1]) return (G4int)i;
    return (G4int)(fTimeBins.size()-1);
}
/// @brief Records the current time bin based on the chemistry simulation time.
/// @param currentTime  Current time in internal Geant4 units.
/// @param counter      G4MoleculeCounter (unused).
void GValueScorer::SnapshotAt(G4double currentTime, G4MoleculeCounter*) {
    G4int nb=FindBin(currentTime);
    if(nb<0||nb==fCurrentBin) return;
    fCurrentBin=nb;
    if(fEdepFetcher) fEventEdep=fEdepFetcher->GetTotalEdep();
}
/// @brief Adds the current event's species counts and energy deposit to the
///        run-level accumulators for the active time bin.
void GValueScorer::AccumulateEvent() {
    if(fCurrentBin<0||fCurrentBin>=(G4int)fTimeBins.size()) return;
    fEventCount++; fRunEdepSum+=fEventEdep;
    for(size_t i=0;i<fSpeciesNames.size();++i){
        G4double v=fEventCounts[i];
        fTimeBinCounts[i][fCurrentBin]+=v;
        fTimeBinCounts2[i][fCurrentBin]+=v*v;
    }
}
/// @brief Writes the time-dependent G-values to a text file.
/// @param outputFileName Base output filename (".txt" appended).
/// @param runID          Run ID (unused).
void GValueScorer::Finalize(const std::string& outputFileName, G4int) {
    if(fEventCount==0||fRunEdepSum<=0.0){ G4cout<<"[GValueScorer] No data"<<G4endl; return; }
    G4double meanEdep=fRunEdepSum/fEventCount;
    G4double e100=meanEdep/(100.0*CLHEP::eV);
    std::string tf=outputFileName+".txt";
    std::ofstream out(tf);
    if(out.is_open()){
        out<<"# G-values\n# <E_dep>="<<meanEdep/CLHEP::keV<<" keV, Events="<<fEventCount<<"\n# Time(ps)";
        for(auto& n:fSpeciesNames) out<<"\t"<<n;
        out<<"\n";
        for(G4int t=0;t<(G4int)fTimeBins.size();++t){
            G4double tc=(t+1<(G4int)fTimeBins.size())?0.5*(fTimeBins[t]+fTimeBins[t+1]):fTimeBins[t];
            out<<tc;
            for(size_t s=0;s<fSpeciesNames.size();++s){
                G4double mn=fTimeBinCounts[s][t]/fEventCount;
                out<<"\t"<<(e100>0?mn/e100:0.0);
            }
            out<<"\n";
        }
        out.close();
        G4cout<<"[GValueScorer] "<<tf<<" written"<<G4endl;
    } else { G4cerr<<"[GValueScorer] Cannot open "<<tf<<G4endl; }
}
/// @brief Generates logarithmically-spaced time bins.
/// @param tS Start time (ps).
/// @param tE End time (ps).
/// @param n  Number of bins.
/// @return Vector of bin boundaries.
std::vector<G4double> GValueScorer::GenerateLogBins(G4double tS,G4double tE,G4int n){
    std::vector<G4double> b;
    if(n<2||tS<=0||tE<=tS){ b.resize(n); G4double dt=(tE-tS)/(n-1); for(G4int i=0;i<n;++i) b[i]=tS+i*dt; return b; }
    b.resize(n);
    G4double ls=std::log10(tS),le=std::log10(tE),dl=(le-ls)/(n-1);
    for(G4int i=0;i<n;++i) b[i]=std::pow(10.0,ls+i*dl);
    return b;
}
