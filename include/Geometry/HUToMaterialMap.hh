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

#include <string>
#include <vector>

class G4Material;

/// @brief Piecewise HU -> G4Material calibration map.
class HUToMaterialMap {
public:
    HUToMaterialMap();
    ~HUToMaterialMap() = default;

    /// @brief Build the built-in default HU -> NIST material table.
    void LoadDefault();

    /// @brief Load a custom HU -> material table from a text file.
    ///
    /// GATE-style format: one "minHU maxHU MaterialName" per line, with an
    /// optional 4th column "density_g_per_cm3". Lines starting with '#' and
    /// blank lines are ignored. Density, when omitted, is the material's NIST
    /// density; when given, a new material with that density is built from the
    /// same element composition.
    ///
    /// @return false if the file could not be read or had no valid rows
    ///         (the caller keeps the previously loaded/default table).
    bool LoadFromFile(const std::string& path);

    /// @brief Load a HU -> density calibration curve (g/cm3), GATE-style.
    ///
    /// Format: one "HU density_g_per_cm3" per line (ascending HU), '#' comments
    /// and blank lines ignored. Density is piecewise-linearly interpolated.
    /// Used by LoadDefault() instead of the anchor density interpolation.
    ///
    /// @return false if the file could not be read or had fewer than 2 points.
    bool LoadDensityTable(const std::string& path);

    /// @brief Get the material for a Hounsfield value.
    G4Material* GetMaterial(short hu) const;

    /// @brief Get the index (into GetMaterials()) for a Hounsfield value. O(1).
    int MaterialIndex(short hu) const;

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
    std::vector<int> fLutIndex;   ///< O(1) HU -> material index lookup.

    std::vector<double> fDensityH;   ///< density-curve HU breakpoints (ascending).
    std::vector<double> fDensityD;   ///< density-curve densities (g/cm3).

    /// @brief Piecewise-linear density at a Hounsfield value (density table).
    double DensityAt(short hu) const;

    static constexpr short kHuMin = -1024;
    static constexpr short kHuMax = 3071;
};

#endif // HU_TO_MATERIAL_MAP_HH
