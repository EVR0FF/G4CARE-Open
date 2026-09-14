#ifndef Run_HH
#define Run_HH

//==============================================================================
// G4CARE
// @file    Run.hh
// @brief   Custom G4Run with chemistry voxel and molecule aggregation
// @details Extends G4Run with thread-local ChemVoxel and MoleculeRecord
//   containers. During EndOfRunAction each worker thread stores its
//   ChemVoxel and molecule data into the Run object, and after Merge()
//   the master thread aggregates all worker data into a single ChemVoxel
//   and combined molecule vector.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4Run.hh"
#include "ChemVoxel.hh"
#include "MoleculeRecord.hh"
#include "G4Threading.hh"
#include <vector>

class Run : public G4Run
{
public:
    /// @brief Default constructor
    Run() : G4Run() {}

    /// @brief Destructor
    virtual ~Run() = default;

    /// @brief Merge worker run data into the master run
    ///
    /// Aggregates ChemVoxel counts and energy deposit, and appends
    /// molecule records from the worker thread to the master.
    /// @param aRun Pointer to the worker G4Run to merge
    virtual void Merge(const G4Run* aRun) override;

    /// @brief Store thread-local ChemVoxel into the Run object
    ///
    /// Called by worker threads in EndOfRunAction.
    /// @param voxel The ChemVoxel to store
    void SetChemVoxel(const ChemVoxel& voxel) { fChemVoxel = voxel; }

    /// @brief Get the aggregated ChemVoxel
    ///
    /// Called by the master thread after Merge().
    /// @return Const reference to the aggregated ChemVoxel
    const ChemVoxel& GetChemVoxel() const { return fChemVoxel; }

    /// @brief Add a molecule record (worker threads call in EndOfEvent)
    /// @param rec The MoleculeRecord to add
    void AddMolecule(const MoleculeRecord& rec) { fMolecules.push_back(rec); }

    /// @brief Get all collected molecules (master reads after Merge)
    /// @return Const reference to the vector of MoleculeRecord
    const std::vector<MoleculeRecord>& GetMolecules() const { return fMolecules; }

private:
    ChemVoxel fChemVoxel;
    std::vector<MoleculeRecord> fMolecules;
};

#endif