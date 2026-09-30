//==============================================================================
//
// G4CARE
//
// @file    DICOMReader.hh
// @brief   Facade for reading DICOM data (CT series, RTSTRUCT, RTPLAN, RTDOSE).
//
// @details
//   Entry point of the DICOM import pipeline.  Provides a uniform API to load
//   the four DICOM object kinds needed for patient-dose simulation:
//     - CT image series  -> voxel Hounsfield grid (RegularGrid<short>)
//     - RTSTRUCT         -> organ contours -> voxel organ labels
//     - RTPLAN           -> treatment beams (gantry/couch/collimator, energy,
//                           isocenter)
//     - RTDOSE           -> reference dose grid (Gy)
//
//   The implementation is guarded by G4CARE_HAS_DICOM: without GDCM the
//   methods are no-ops returning false.  Enable DICOM support with the CMake
//   option -DG4CARE_WITH_DICOM=ON, which finds GDCM, links it, and defines
//   G4CARE_HAS_DICOM.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef DICOM_READER_HH
#define DICOM_READER_HH

#include <string>

#include "CTSeries.hh"
#include "RTStruct.hh"
#include "RTPlan.hh"
#include "RTDose.hh"

/// @brief Facade for reading DICOM data into G4CARE-native structures.
class DICOMReader {
public:
    DICOMReader() = default;
    ~DICOMReader() = default;

    /// @brief Read a CT image series into a voxel Hounsfield grid.
    /// @param directory Directory containing the DICOM slice files.
    /// @param out       Output CTSeries (Hounsfield volume + geometry metadata).
    /// @return true on success. (Phase 1)
    bool ReadCTSeries(const std::string& directory, CTSeries& out);

    /// @brief Read an RTSTRUCT object into organ contours.
    /// @param file Path to the .dcm RTSTRUCT file.
    /// @param out  Output RTStruct (ROI contours).
    /// @return true on success. (Phase 3)
    bool ReadRTStruct(const std::string& file, RTStruct& out);

    /// @brief Read an RTPLAN object into treatment beams.
    /// @param file Path to the .dcm RTPLAN file.
    /// @param out  Output RTPlan (beams with energy/angles/isocenter).
    /// @return true on success. (Phase 4)
    bool ReadRTPlan(const std::string& file, RTPlan& out);

    /// @brief Read an RTDOSE object into a dose grid.
    /// @param file Path to the .dcm RTDOSE file.
    /// @param out  Output RTDose (dose values in Gy).
    /// @return true on success. (Phase 5)
    bool ReadRTDose(const std::string& file, RTDose& out);
};

#endif // DICOM_READER_HH
