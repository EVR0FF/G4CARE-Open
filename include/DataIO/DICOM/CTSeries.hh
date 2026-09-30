//==============================================================================
//
// G4CARE
//
// @file    CTSeries.hh
// @brief   CT image series data: Hounsfield voxel grid + geometry metadata.
//
// @details
//   Plain data holder produced by DICOMReader::ReadCTSeries().  Stores the HU
//   values of a full CT series as a flat array (x fastest, linear index
//   iz*(nx*ny) + iy*nx + ix) together with the voxel spacing, the position of
//   the (0,0,0) voxel corner and the slice orientation.  All lengths are in
//   millimetres in the DICOM patient (LPS) frame; conversion to Geant4 units
//   and coordinate axes is done later by VoxelizedPhantom.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef CT_SERIES_HH
#define CT_SERIES_HH

#include <vector>
#include <string>
#include <cstddef>

/// @brief CT image series data (Hounsfield voxel grid + geometry metadata).
struct CTSeries {
    std::vector<short> hu;                 ///< HU voxel data (linear index).
    int nx = 0;                            ///< Voxels along x (column index).
    int ny = 0;                            ///< Voxels along y (row index).
    int nz = 0;                            ///< Voxels along z (slice index).
    double dxMm = 0.0;                     ///< Voxel spacing along x (mm).
    double dyMm = 0.0;                     ///< Voxel spacing along y (mm).
    double dzMm = 0.0;                     ///< Voxel spacing along z (mm).
    double originMm[3] = {0.0, 0.0, 0.0};  ///< Corner of voxel (0,0,0), LPS (mm).
    /// Row-major 3x3 orientation: [0..2] = row direction, [3..5] = column
    /// direction, [6..8] = slice normal (DICOM LPS frame).
    double direction[9] = {1,0,0, 0,1,0, 0,0,1};
    double rescaleSlope = 1.0;             ///< Modality LUT slope (HU = slope*raw + intercept).
    double rescaleIntercept = 0.0;         ///< Modality LUT intercept.
    std::string modality = "CT";           ///< DICOM modality.
    bool valid = false;                    ///< true if the series was read successfully.
    bool tilted = false;                   ///< true if slice normal is not axis-aligned (gantry tilt).
    double tiltAngleDeg = 0.0;             ///< Tilt angle (degrees) of the slice normal from an axis.

    /// @brief Total number of voxels.
    std::size_t TotalVoxels() const { return hu.size(); }

    /// @brief Access a voxel by (x,y,z) indices.
    short HU(int ix, int iy, int iz) const {
        return hu[static_cast<std::size_t>(iz) * (nx * ny) +
                  static_cast<std::size_t>(iy) * nx + ix];
    }
};

#endif // CT_SERIES_HH
