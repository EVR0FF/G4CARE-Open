#ifndef GEOMETRY_PROPERTY_REGISTRY_HH
#define GEOMETRY_PROPERTY_REGISTRY_HH

//==============================================================================
//
// G4CARE
//
// @file    GeometryPropertyRegistry.hh
// @brief   Volume property calculator (mass, density, radlen, etc.).
//
// @details
//   Builds a key-value property map from GEOMETRY.OBJECTS configuration
//   for each logical volume. Computes: mass (kg), volume (cm³),
//   density (g/cm³), temperature (K), pressure (atm), radiation length
//   (cm), nuclear interaction length (cm).
//
//   Configuration keys read:
//     GEOMETRY.OBJECTS (subsection names)
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

#include <string>
#include <map>
#include "G4SystemOfUnits.hh"

class G4LogicalVolume;
class ConfigManager;

//------------------------------------------------------------------------------
/// @class GeometryPropertyRegistry
/// @brief Singleton that computes and stores physical properties of logical
///        volumes from GEOMETRY.OBJECTS configuration.
//------------------------------------------------------------------------------
class GeometryPropertyRegistry {
public:
    GeometryPropertyRegistry() = default;
    ~GeometryPropertyRegistry() = default;

    /// @brief Build the property map from GEOMETRY.OBJECTS configuration.
    ///
    /// Iterates all logical volumes, matches them against configured object
    /// names, and computes mass, volume, density, temperature, pressure,
    /// radiation length, and nuclear interaction length for each match.
    ///
    /// @param cfg  ConfigManager instance.
    void Build(ConfigManager* cfg);

    /// @brief Get the full property map as read-only reference.
    ///
    /// @return Const reference to the property map.
    const std::map<std::string, double>& GetProperties() const { return fProperties; }

    /// @brief Retrieve a single property value by key.
    ///
    /// @param key         Property key (e.g. "target_mass").
    /// @param defaultVal  Default value if the key is not found (default: 0.0).
    /// @return            Property value or defaultVal.
    double Get(const std::string& key, double defaultVal = 0.0) const;

    /// @brief Returns the singleton instance.
    ///
    /// @return Pointer to the singleton.
    static GeometryPropertyRegistry* Instance();

    /// @brief Destroys the singleton instance.
    static void DeleteInstance();

private:
    /// @brief Computes properties for a single logical volume and stores them.
    ///
    /// @param prefix  Config object name used as property key prefix.
    /// @param lv      Pointer to the logical volume.
    void AddVolume(const std::string& prefix, const G4LogicalVolume* lv);

    std::map<std::string, double> fProperties;
    static GeometryPropertyRegistry* fInstance;
};

#endif