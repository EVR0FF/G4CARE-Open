//==============================================================================
// G4CARE
// @file    GeometryExtractor.cc
// @brief   Extracts geometry-related column values (volume ID, volume name,
//          copy number, region ID/name, touchable path) from Geant4 step data.
// @details GeometryExtractor resolves volume and region identifiers either as
//   numeric IDs (for ntuple columns) or as string names via the VolumeMaterialRegistry
//   and associated TypedRegistries.  It handles both step-level (Hit) and
//   other UnifiedSource types.
//
//   Configuration keys read: none (registry-driven).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "GeometryExtractor.hh"

/// @brief Extracts a numeric geometry column value for the given source.
/// @param type  Column type (VolumeID, VolumeName-as-ID, CopyNo, RegionID,
///              RegionName-as-ID, TouchablePath).
/// @param src   UnifiedSource providing volume/region pointers.
/// @param volId Volume ID from the registry (used for VolumeID/VolumeName).
/// @return Extracted numeric value, or 0.0/-1.0 if not available.
double GeometryExtractor::Get(ColType type, const UnifiedSource& src, int volId) const {
    switch (type) {
        case ColType::VolumeID:
            return static_cast<double>(volId);

        case ColType::VolumeName:
            if (fVolumeNameReg && volId >= 0) {
                return static_cast<double>(fVolumeNameReg->GetID(fRegistry->GetVolumeName(volId)));
            }
            return 0.0;

        case ColType::CopyNo: {
            auto* pv = src.GetPhysicalVolume();
            return pv ? static_cast<double>(pv->GetCopyNo()) : -1.0;
        }

        case ColType::RegionID: {
            auto* region = src.GetRegion();
            return region ? static_cast<double>(region->GetInstanceID()) : -1.0;
        }

        case ColType::RegionName:
            if (fRegionNameReg) {
                auto* region = src.GetRegion();
                if (region) {
                    return static_cast<double>(fRegionNameReg->GetID(region->GetName()));
                }
            }
            return 0.0;

        case ColType::TouchablePath:
            return 0.0;

        default:
            return 0.0;
    }
}

/// @brief Extracts a string geometry column value for the given source.
/// @param type Column type (VolumeName, RegionName, TouchablePath).
/// @param src  UnifiedSource providing volume/region pointers.
/// @return Extracted string, or empty string if not available.
std::string GeometryExtractor::GetString(ColType type, const UnifiedSource& src) const {
    switch (type) {
        case ColType::VolumeName: {
            auto* lv = src.GetLogicalVolume();
            if (lv) return lv->GetName();
            return "";
        }
        case ColType::RegionName: {
            auto* region = src.GetRegion();
            if (region) return region->GetName();
            return "";
        }
        case ColType::TouchablePath:
            return "";
        default:
            return "";
    }
}