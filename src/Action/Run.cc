//==============================================================================
// G4CARE
// @file    Run.cc
// @brief   Implementation of custom G4Run with ChemVoxel merging
// @details Implements Run::Merge() which aggregates thread-local ChemVoxel
//   data and molecule records from worker threads into the master.
//   G4MoleculeCounter MT merging is handled automatically by
//   G4MoleculeCounterManager with SetAccumulateCounterIntoMaster(true),
//   so no manual AbsorbWorkerManagerCounters() call is needed.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Run.hh"
#include "G4ios.hh"
#include "G4SystemOfUnits.hh"

/// @brief Merge worker run data into the master run
///
/// Aggregates thread-local ChemVoxel counts and energy deposit from
/// worker runs into the master run. Also appends molecule records.
/// G4MoleculeCounter MT merging is automatic via
/// SetAccumulateCounterIntoMaster(true) in ActionInitialization,
/// therefore manual AbsorbWorkerManagerCounters() is not called
/// (it would cause an exception when resetting counters between runs).
/// If the master ChemVoxel is empty, it is initialized from the
/// first worker's voxel.
/// @param aRun Pointer to the worker G4Run (cast to Run*)
void Run::Merge(const G4Run* aRun)
{
    if (aRun == this) return;
    const Run* other = static_cast<const Run*>(aRun);
    if (!other) return;

    G4Run::Merge(aRun);

    // G4MoleculeCounter MT merging is handled automatically
    // via SetAccumulateCounterIntoMaster(true) in ActionInitialization.
    // Manual AbsorbWorkerManagerCounters() call is not needed here
    // and would cause an exception when resetting counters between runs.

    // If the master ChemVoxel is empty, initialize from the first worker
    if (fChemVoxel.counts.empty() && !other->fChemVoxel.counts.empty()) {
        fChemVoxel = other->fChemVoxel;
        G4cout << "[Run::Merge] Initialized master ChemVoxel from worker"
               << " n=" << fChemVoxel.counts.size()
               << " edep=" << fChemVoxel.energyDeposit / CLHEP::eV << " eV" << G4endl;
        return;
    }

    // Aggregate ChemVoxel: sum counts + energyDeposit from all worker runs
    size_t n = std::min(fChemVoxel.counts.size(), other->fChemVoxel.counts.size());
    for (size_t i = 0; i < n; ++i) {
        fChemVoxel.counts[i] += other->fChemVoxel.counts[i];
    }
    fChemVoxel.energyDeposit += other->fChemVoxel.energyDeposit;
    G4cout << "[Run::Merge] Aggregated worker"
           << " n=" << other->fChemVoxel.counts.size()
           << " edep=" << other->fChemVoxel.energyDeposit / CLHEP::eV << " eV"
           << " total edep=" << fChemVoxel.energyDeposit / CLHEP::eV << " eV" << G4endl;

    // Merge molecule records from worker threads
    fMolecules.insert(fMolecules.end(),
                      other->fMolecules.begin(),
                      other->fMolecules.end());
}