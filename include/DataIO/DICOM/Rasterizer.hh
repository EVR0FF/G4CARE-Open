//==============================================================================
//
// G4CARE
//
// @file    Rasterizer.hh
// @brief   Rasterizes RTSTRUCT contours onto a CT voxel grid (organ labels).
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef DICOM_RASTERIZER_HH
#define DICOM_RASTERIZER_HH

#include <vector>

struct CTSeries;
struct RTStruct;

namespace DICOMRasterizer {

/// @brief Rasterize organ contours onto the CT grid.
/// @param ct     CT series (defines the voxel grid + geometry).
/// @param rt     RTSTRUCT contours.
/// @param labels Output: one int per voxel (CTSeries order: iz*(nx*ny)+iy*nx+ix).
///               Value = index into rt.rois, or -1 for background.
/// @return true on success.
bool Rasterize(const CTSeries& ct, const RTStruct& rt, std::vector<int>& labels);

} // namespace DICOMRasterizer

#endif // DICOM_RASTERIZER_HH
