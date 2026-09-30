//==============================================================================
//
// G4CARE
//
// @file    RTDose.hh
// @brief   RTDOSE data: reference dose grid (Gy).
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef RT_DOSE_HH
#define RT_DOSE_HH

#include <cstddef>
#include <string>
#include <vector>

/// @brief RTDOSE data (reference dose grid).
struct RTDose {
    std::vector<double> doseGy;            ///< Dose values (Gy), linear index iz*(nx*ny)+iy*nx+ix.
    int nx = 0, ny = 0, nz = 0;           ///< Grid dimensions (cols, rows, frames).
    double dxMm = 0.0, dyMm = 0.0, dzMm = 0.0;  ///< Voxel spacing (mm).
    double originMm[3] = {0.0, 0.0, 0.0}; ///< Position of first voxel centre (mm, LPS).
    double direction[9] = {1,0,0, 0,1,0, 0,0,1}; ///< Row/col direction cosines.
    double doseGridScaling = 1.0;         ///< Scaling factor applied to raw values.
    std::string doseUnits = "GY";
    bool valid = false;

    /// @brief Total number of voxels.
    std::size_t TotalVoxels() const { return doseGy.size(); }
    /// @brief Access a voxel by (x,y,z) indices.
    double Dose(int ix, int iy, int iz) const {
        return doseGy[static_cast<std::size_t>(iz) * (nx * ny)
                      + static_cast<std::size_t>(iy) * nx + ix];
    }
};

#endif // RT_DOSE_HH
