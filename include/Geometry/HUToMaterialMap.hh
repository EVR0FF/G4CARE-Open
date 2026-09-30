//==============================================================================
//
// G4CARE
//
// @file    HUToMaterialMap.hh
// @brief   Maps Hounsfield Units (CT) to G4Material via a calibrated table.
//
// @details
//   Provides a piecewise HU -> material mapping used when converting a CT
//   series into a voxel phantom.  A default table (air / lung / soft tissue /
//   bone) is built from the NIST material database; the list of distinct
//   materials is exposed so that VoxelizedPhantom can build a compact
//   G4PhantomParameterisation material table.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef HU_TO_MATERIAL_MAP_HH
#define HU_TO_MATERIAL_MAP_HH

#include <vector>

class G4Material;

/// @brief Piecewise HU -> G4Material calibration map.
class HUToMaterialMap {
public:
    HUToMaterialMap();
    ~HUToMaterialMap() = default;

    /// @brief Build the built-in default HU -> NIST material table.
    void LoadDefault();

    /// @brief Get the material for a Hounsfield value.
    G4Material* GetMaterial(short hu) const;

    /// @brief Distinct materials used by the map (for phantom material table).
    const std::vector<G4Material*>& GetMaterials() const { return fMaterials; }

private:
    struct Bin {
        short huMin = 0;
        short huMax = 0;
        G4Material* material = nullptr;
    };
    std::vector<Bin> fBins;
    std::vector<G4Material*> fMaterials;
};

#endif // HU_TO_MATERIAL_MAP_HH
