//==============================================================================
//
// G4CARE
//
// @file    MaterialManager.hh
// @brief   Custom material builder from YAML configuration.
//
// @details
//   Reads the `materials` block from YAML config and constructs
//   G4Material, G4Element, and G4Isotope objects via Geant4 NIST
//   manager.  Supports elemental composition, isotope mixtures,
//   and optional optical properties via G4MaterialPropertiesTable.
//
//   Configuration keys read:
//     materials.<name>.name, .density, .temperature, .pressure, .state
//     materials.<name>.elements.<el>.name, .Z, .abundance
//     materials.<name>.elements.<el>.isotopes.<iso>.name, .A, .mass, .abundance
//     materials.<name>.optical (YAML node)
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#ifndef MATERIAL_MANAGER_HH
#define MATERIAL_MANAGER_HH

#include "G4Material.hh"
#include "G4Element.hh"
#include "G4Isotope.hh"
#include "G4SystemOfUnits.hh"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <map>

/// @brief Isotope definition read from YAML.
struct IsotopeDef {
    std::string name;           ///< Isotope name.
    G4int A = 0;                ///< Mass number.
    G4double molarMass = 0.0;   ///< Molar mass (g/mol).
    G4double abundance = 0.0;   ///< Isotopic abundance (fraction).
};

/// @brief Element definition read from YAML.
struct ElementDef {
    std::string name;            ///< Element name.
    G4int Z = 0;                 ///< Atomic number.
    G4double abundance = 0.0;    ///< Mass fraction in material.
    std::vector<IsotopeDef> isotopes; ///< Isotopic composition.
};

/// @brief Material definition read from YAML.
struct MaterialDef {
    std::string name;            ///< Material name.
    G4double density = 0.0;      ///< Density (g/cm3).
    G4double temperature = 273.15; ///< Temperature (Kelvin).
    G4double pressure = 1.0;     ///< Pressure (atmosphere).
    G4State state = kStateUndefined; ///< Aggregate state.
    std::vector<ElementDef> elements; ///< Elemental composition.
};

/// @brief Builds custom G4Material objects from YAML `materials` block.
class MaterialManager {
public:
    static MaterialManager* Instance();
    static void DeleteInstance();

    /// @brief Build all materials defined in the `materials` config block.
    /// @return true on success.
    bool BuildMaterials();

    /// @brief Load materials from pre-parsed definitions.
    void LoadMaterials(const std::vector<MaterialDef>& definitions);

    /// @brief Get a material by name.
    G4Material* GetMaterial(const std::string& name) const;

private:
    MaterialManager();
    ~MaterialManager();
    
    static MaterialManager* fInstance;

    /// @brief Build optical properties table from YAML `optical` node.
    void BuildOpticalProperties(G4Material* mat, const YAML::Node& opticalNode);
    /// @brief Create a G4Material from a MaterialDef.
    G4Material* CreateMaterial(const MaterialDef& def);
    /// @brief Create a G4Element from an ElementDef.
    G4Element* CreateElement(const std::string& matName, const ElementDef& def);
};

#endif
