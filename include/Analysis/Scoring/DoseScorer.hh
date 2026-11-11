//==============================================================================
// G4CARE
// @file    DoseScorer.hh
// @brief   Voxel dose-grid scorer aligned with the DICOM CT phantom (RTDOSE).
// @details Accumulates per-step energy deposition into a 3D grid whose geometry
//   (dimensions, spacing, origin, orientation) matches the CT series used to
//   build the voxel phantom, so it can be exported as an RTDOSE DICOM object.
//   Multi-threaded safety follows the ChemistryExtractor pattern: each worker
//   thread writes only to its own slot (indexed by Geant4 thread id) and the
//   master merges all slots in EndOfRunAction — no mutexes, atomics or locks.
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#ifndef DOSE_SCORER_HH
#define DOSE_SCORER_HH

#include "G4ThreeVector.hh"

#include <cstddef>
#include <vector>

struct CTSeries;
struct RTDose;

/// @brief Accumulates energy deposition into a voxel dose grid for RTDOSE.
class DoseScorer {
public:
    static DoseScorer* Instance();

    /// @brief Configure the grid from the (possibly resampled/cropped) CT series.
    ///        Called by the master thread at geometry-build time.
    void Configure(const CTSeries& ct);

    bool IsConfigured() const { return fConfigured; }

    /// @brief Prepare per-thread slots (master thread, BeginOfRunAction).
    /// @param nThreads Number of worker slots (1 for sequential mode).
    void BeginOfRun(std::size_t nThreads);

    /// @brief Accumulate edep (MeV) at a world position (mm, LPS) into the
    ///        current thread's slot. Lock-free.
    void Deposit(const G4ThreeVector& posMm, double edepMeV);

    /// @brief Accumulate edep (MeV) directly into a voxel by its linear index
    ///        (iz*(nx*ny) + iy*nx + ix). Preferred exact path when the CT phantom
    ///        voxel sensitive detector is attached.
    void DepositVoxel(int voxelIndex, double edepMeV);

    /// @brief Enable the exact voxel-index path (set when the phantom voxel SD is
    ///        attached). When enabled, the position-based Deposit() is skipped.
    void SetVoxelIndexEnabled(bool on) { fUseVoxelIndex = on; }
    bool UsesVoxelIndex() const { return fUseVoxelIndex; }

    /// @brief Merge all thread slots (master thread, EndOfRunAction).
    void EndOfRun();

    /// @brief Build the merged dose grid (Gy) as an RTDose struct.
    /// @return false when not configured or nothing was accumulated.
    bool BuildRTDose(RTDose& out) const;

private:
    DoseScorer() = default;
    DoseScorer(const DoseScorer&) = delete;
    DoseScorer& operator=(const DoseScorer&) = delete;

    int    fNx = 0, fNy = 0, fNz = 0;
    double fDx = 0.0, fDy = 0.0, fDz = 0.0;   // voxel spacing (mm)
    double fCorner[3] = {0.0, 0.0, 0.0};      // corner of voxel (0,0,0), LPS mm
    double fCol[3] = {1.0, 0.0, 0.0};         // +ix direction (unit)
    double fRow[3] = {0.0, 1.0, 0.0};         // +iy direction (unit)
    double fSlc[3] = {0.0, 0.0, 1.0};         // +iz direction (unit)
    double fCenter[3] = {0.0, 0.0, 0.0};      // centre of voxel (0,0,0), LPS mm
    bool   fConfigured = false;
    bool   fUseVoxelIndex = false;             // exact voxel-index path enabled

    std::vector<std::vector<double>> fThreadGrids;  // [threadId][voxel] (MeV)
    std::vector<double> fMergedGrid;                // merged energy (MeV)
    bool fMerged = false;
};

#endif // DOSE_SCORER_HH
