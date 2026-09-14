#ifndef SURFACE_MANAGER_HH
#define SURFACE_MANAGER_HH

//==============================================================================
//
// G4CARE
//
// @file    SurfaceManager.hh
// @brief   Optical surface builder from YAML configuration.
//
// @details
//   Singleton that reads the `surfaces` configuration block and constructs
//   G4OpticalSurface, G4LogicalSkinSurface, and G4LogicalBorderSurface
//   objects. Supports glisur, unified, LUT, DAVIS, and dichroic models.
//
//   Configuration keys read:
//     surfaces.<id>.name
//     surfaces.<id>.type
//     surfaces.<id>.volume
//     surfaces.<id>.phys_volume1
//     surfaces.<id>.phys_volume2
//     surfaces.<id>.properties
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

#include "G4LogicalBorderSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4OpticalSurface.hh"
#include "G4MaterialPropertiesTable.hh"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
/// @class SurfaceManager
/// @brief Singleton that builds optical surfaces from the `surfaces` config
///        block.
//------------------------------------------------------------------------------
class SurfaceManager {
public:
    /// @brief Returns the singleton instance.
    static SurfaceManager* Instance();
    /// @brief Deletes the singleton instance.
    static void DeleteInstance();

    /// @brief Builds all optical surfaces from the `surfaces` configuration.
    ///
    /// Reads surface definitions via ConfigManager::GetSubsections("surfaces"),
    /// creates G4OpticalSurface objects, and attaches them as
    /// G4LogicalSkinSurface or G4LogicalBorderSurface.
    ///
    /// @return true on success.
    bool BuildSurfaces();

private:
    SurfaceManager();
    ~SurfaceManager();
    
    static SurfaceManager* fInstance;

    /// @brief Creates a G4OpticalSurface from a YAML properties node.
    ///
    /// @param name       Surface name.
    /// @param propsNode  YAML node containing properties (model, finish, type, etc.).
    /// @return           Pointer to created G4OpticalSurface, or nullptr.
    G4OpticalSurface* CreateOpticalSurface(const std::string& name, const YAML::Node& propsNode);

    /// @brief Finds a logical volume by name (uses G4LogicalVolumeStore).
    ///
    /// @param name  Volume name.
    /// @return      Pointer to G4LogicalVolume, or nullptr.
    G4LogicalVolume* FindVolume(const std::string& name);

    SurfaceManager(const SurfaceManager&) = delete;
    SurfaceManager& operator=(const SurfaceManager&) = delete;
};

#endif