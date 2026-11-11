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
    std::vector<float> floatValues;        ///< Float voxel data (PET SUV / float pixels).
    bool floatData = false;                ///< true when floatValues holds the data.
    int nx = 0;                            ///< Voxels along x (column index).
    int ny = 0;                            ///< Voxels along y (row index).
    int nz = 0;                            ///< Voxels along z (slice index).
    double dxMm = 0.0;                     ///< Voxel spacing along x (mm).
    double dyMm = 0.0;                     ///< Voxel spacing along y (mm).
    double dzMm = 0.0;                     ///< Voxel spacing along z (mm).
    double originMm[3] = {0.0, 0.0, 0.0};  ///< Corner of voxel (0,0,0), LPS (mm).
    /// Row-major 3x3 orientation (DICOM LPS frame):
    /// [0..2] = row direction (+iy), [3..5] = column direction (+ix),
    /// [6..8] = slice normal (+iz) = column x row.
    /// NOTE: readers swap gdcm's IOP order ([first row, first column]) into this
    /// [row, column] layout; writers swap it back.
    double direction[9] = {1,0,0, 0,1,0, 0,0,1};
    double rescaleSlope = 1.0;             ///< Modality LUT slope (HU = slope*raw + intercept).
    double rescaleIntercept = 0.0;         ///< Modality LUT intercept.
    std::string modality = "CT";           ///< DICOM modality.
    std::string patientName;               ///< Patient's Name (0010,0010).
    std::string patientId;                 ///< Patient ID (0010,0020).
    std::string studyId;                   ///< Study ID (0020,0010).
    std::string seriesNumber;              ///< Series Number (0020,0011).
    std::string institutionName;           ///< Institution Name (0008,0080).
    std::string acquisitionDate;           ///< Acquisition Date (0008,0022).
    std::string acquisitionTime;           ///< Acquisition Time (0008,0032).
    std::string studyInstanceUID;          ///< Study Instance UID (0020,000D).
    std::string seriesInstanceUID;         ///< Series Instance UID (0020,000E).
    std::string frameOfReferenceUID;       ///< Frame of Reference UID (0020,0052).
    std::string sopClassUID;               ///< SOP Class UID of the slices (0008,0016).
    std::vector<std::string> sliceSopInstanceUIDs;  ///< Per-slice SOP Instance UID (0008,0018), slice order.
    bool valid = false;                    ///< true if the series was read successfully.
    bool tilted = false;                   ///< true if slice normal is not axis-aligned (gantry tilt).
    double tiltAngleDeg = 0.0;             ///< Tilt angle (degrees) of the slice normal from an axis.

    /// @brief Total number of voxels.
    std::size_t TotalVoxels() const { return floatData ? floatValues.size() : hu.size(); }

    /// @brief Access a voxel by (x,y,z) indices.
    short HU(int ix, int iy, int iz) const {
        return hu[static_cast<std::size_t>(iz) * (nx * ny) +
                  static_cast<std::size_t>(iy) * nx + ix];
    }

    /// @brief Access a voxel value as float (int16 or float32 data).
    float ValueAt(int ix, int iy, int iz) const {
        const std::size_t idx = static_cast<std::size_t>(iz) * (nx * ny) +
                                static_cast<std::size_t>(iy) * nx + ix;
        return floatData ? floatValues[idx] : static_cast<float>(hu[idx]);
    }
};

#endif // CT_SERIES_HH
