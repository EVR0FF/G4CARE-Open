//==============================================================================
//
// G4CARE
//
// @file    RTStruct.hh
// @brief   RTSTRUCT data: organ contours (ROIs) in patient coordinates.
//
// @details
//   Plain data holder produced by DICOMReader::ReadRTStruct().  Each ROI holds
//   a list of closed contours; a contour is a list of 3D points (x,y,z in mm,
//   DICOM LPS frame) lying on a CT slice plane.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef RT_STRUCT_HH
#define RT_STRUCT_HH

#include <string>
#include <vector>

/// @brief RTSTRUCT data (organ contours).
struct RTStruct {
    /// @brief A single closed contour (list of 3D points in mm, LPS).
    struct Contour {
        std::vector<double> x;
        std::vector<double> y;
        std::vector<double> z;
    };

    /// @brief A structure (organ / region of interest).
    struct ROI {
        int number = 0;              ///< ROINumber (unique id).
        std::string name;            ///< ROIName.
        std::vector<Contour> contours;
    };

    std::vector<ROI> rois;
    bool valid = false;

    /// @brief Find a ROI by name (nullptr if absent).
    const ROI* Find(const std::string& name) const {
        for (const auto& r : rois) {
            if (r.name == name) return &r;
        }
        return nullptr;
    }
};

#endif // RT_STRUCT_HH
