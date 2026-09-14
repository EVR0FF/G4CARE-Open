//==============================================================================
// G4CARE
// @file    ConvergenceValidator.hh
// @brief   Validates statistical convergence of per-voxel tallies using
//          Geant4 G4ConvergenceTester metrics (R, VOV, FOM, shift, efficiency).
// @details Maintains a map of voxel indices to G4ConvergenceTester instances.
//   After adding scores, CheckAllVoxels computes mean, relative error, VOV,
//   FOM, shift, and efficiency for each voxel and checks them against MCNP-style
//   criteria (R < 0.10 and VOV < 0.10 for volumetric sources).
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CONVERGENCE_VALIDATOR_HH
#define CONVERGENCE_VALIDATOR_HH

#include "G4ConvergenceTester.hh"
#include "RegularGrid.hh"
#include <map>
#include <vector>
#include <string>

/// @brief Per-voxel convergence result structure.
struct VoxelConvergenceResult {
    int voxelIndex;
    G4double mean;
    G4double relativeError; // R
    G4double vov;
    G4double fom;
    G4double shift;
    G4double efficiency;
    bool isPassed; // Manual check: R < 0.10, VOV < 0.10
};

/// @brief Validates statistical convergence of per-voxel tallies.
class ConvergenceValidator {
public:
    ConvergenceValidator() = default;

    /// Add a score value (e.g., fluence) for a given voxel.
    void AddScore(int voxelIndex, G4double value);

    /// Check convergence for all voxels that received data.
    std::vector<VoxelConvergenceResult> CheckAllVoxels();

    /// Write a convergence report to a file.
    void WriteReport(const std::string& filename,
                     const std::vector<VoxelConvergenceResult>& results) const;

    /// Print detailed result for each tester to G4cout.
    void ShowAllResults() const;

private:
    std::map<int, G4ConvergenceTester> fTesters;
};

#endif
