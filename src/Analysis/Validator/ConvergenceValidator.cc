//==============================================================================
// G4CARE
// @file    ConvergenceValidator.cc
// @brief   Validates statistical convergence of per-voxel tallies using
//          Geant4 G4ConvergenceTester metrics (R, VOV, FOM, shift, efficiency).
// @details ConvergenceValidator maintains a map of voxel indices to
//   G4ConvergenceTester instances.  After adding scores, CheckAllVoxels
//   computes mean, relative error, VOV, FOM, shift, and efficiency for each
//   voxel and checks them against MCNP-style criteria (R < 0.10 and VOV < 0.10
//   for volumetric sources).  WriteReport outputs a text file, and
//   ShowAllResults prints a summary to G4cout.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ConvergenceValidator.hh"
#include <fstream>
#include "G4ios.hh"

/// @brief Adds a score to the convergence tester for a given voxel.
/// @param voxelIndex Voxel index.
/// @param value      Score value to add.
void ConvergenceValidator::AddScore(int voxelIndex, G4double value) {
    fTesters[voxelIndex].AddScore(value);
}

/// @brief Checks convergence for all voxels and returns per-voxel results.
/// @return Vector of VoxelConvergenceResult structs.
std::vector<VoxelConvergenceResult> ConvergenceValidator::CheckAllVoxels() {
    std::vector<VoxelConvergenceResult> results;

    for (auto& [idx, tester] : fTesters) {
        // Force statistics recomputation (in case AddScore was called after last check)
        tester.ComputeStatistics();

        VoxelConvergenceResult res;
        res.voxelIndex    = idx;
        res.mean          = tester.GetMean();
        res.relativeError = tester.GetR();
        res.vov           = tester.GetVOV();
        res.fom           = tester.GetFOM();
        res.shift         = tester.GetShift();
        res.efficiency    = tester.GetEfficiency();

        // Manual check using MCNP criteria:
        // R < 0.10 for volumetric sources, VOV < 0.10
        res.isPassed = (res.relativeError < 0.10 && res.vov < 0.10);

        results.push_back(res);
    }
    return results;
}

/// @brief Writes a convergence report to a text file.
/// @param filename Output filename.
/// @param results  Vector of convergence results to write.
void ConvergenceValidator::WriteReport(const std::string& filename,
                                       const std::vector<VoxelConvergenceResult>& results) const {
    std::ofstream out(filename);
    out << "# Voxel Convergence Report\n";
    out << "# Index Mean R VOV FOM Shift Eff Passed\n";
    for (const auto& r : results) {
        out << r.voxelIndex << " "
            << r.mean << " "
            << r.relativeError << " "
            << r.vov << " "
            << r.fom << " "
            << r.shift << " "
            << r.efficiency << " "
            << r.isPassed << "\n";
    }
}

/// @brief Prints convergence results for all voxels to G4cout.
void ConvergenceValidator::ShowAllResults() const {
    for (const auto& pair : fTesters) {
        G4cout << "\n--- Voxel " << pair.first << " ---" << G4endl;
        const_cast<G4ConvergenceTester&>(pair.second).ShowResult();
    }
}