//==============================================================================
// G4CARE
// @file    GeometryExtractor.hh
// @brief   Extracts geometry-related columns (volume ID/name, copy number,
//          region ID/name, touchable path) from UnifiedSource.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef GEOMETRY_EXTRACTOR_HH
#define GEOMETRY_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "VolumeMaterialRegistry.hh"
#include "TypedRegistry.hh"

/// @brief Extracts volume, region, and touchable-path data.
class GeometryExtractor {
public:
    GeometryExtractor(VolumeMaterialRegistry* registry,
                      TypedRegistry<std::string>* volumeNameReg = nullptr,
                      TypedRegistry<std::string>* regionNameReg = nullptr,
                      TypedRegistry<std::string>* touchablePathReg = nullptr)
        : fRegistry(registry), fVolumeNameReg(volumeNameReg), fRegionNameReg(regionNameReg), fTouchablePathReg(touchablePathReg) {}

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src, int volId) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;

private:
    VolumeMaterialRegistry* fRegistry;
    TypedRegistry<std::string>* fVolumeNameReg;
    TypedRegistry<std::string>* fRegionNameReg;
    TypedRegistry<std::string>* fTouchablePathReg;
};

#endif // GEOMETRY_EXTRACTOR_HH