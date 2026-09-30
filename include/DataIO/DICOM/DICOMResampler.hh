//==============================================================================
//
// G4CARE
//
// @file    DICOMResampler.hh
// @brief   Resamples a (possibly gantry-tilted) CT series onto an axis-aligned grid.
//
// @details
//   G4PhantomParameterisation can only represent an axis-aligned regular grid,
//   so a tilted/oblique CT series (slice normal not aligned with a coordinate
//   axis) must be resampled before building the phantom.  ResampleToAxisAligned
//   performs a trilinear interpolation of the Hounsfield values onto a regular
//   axis-aligned grid covering the tilted volume's bounding box.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef DICOM_RESAMPLER_HH
#define DICOM_RESAMPLER_HH

#include "CTSeries.hh"

namespace DICOMResampler {

/// @brief Resample a (possibly tilted) CT series onto an axis-aligned grid.
/// @param in  Input series (may be tilted).
/// @param out Output series; axis-aligned (direction = identity), tilted = false.
/// @return true on success.
bool ResampleToAxisAligned(const CTSeries& in, CTSeries& out);

} // namespace DICOMResampler

#endif // DICOM_RESAMPLER_HH
