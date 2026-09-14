//==============================================================================
//
// G4CARE
//
// @file    GeometryManager.hh
// @brief   Top-level geometry construction: world, target, objects, SDs.
//
// @details
//   Implements G4VUserDetectorConstruction.  Construct() builds the
//   world volume, optional target box, and delegates object placement
//   to ObjectManager.  ConstructSDandField() registers SensitiveDetectors
//   for the target and user-defined objects, and initializes the
//   AdaptiveScoringManager.
//
//   Supports GDML-based geometry via GEOMETRY_FILE, programmatic
//   world/target sizing via WORLD_SIZE_*/TARGET_* top-level keys, and
//   parallel Weight Window world for biasing.
//
//   Configuration keys read:
//     GEOMETRY_FILE
//     WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z
//     WORLD_MATERIAL
//     TARGET_SIZE, TARGET_MATERIAL
//     TARGET_POS_X, TARGET_POS_Y, TARGET_POS_Z
//     TARGET_IS_SENSITIVE
//     SENSITIVE_VOLUMES
//     PHYSICS.ADVANCED.WEIGHT_WINDOW.ENABLE
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

#ifndef GEOMETRY_MANAGER_HH
#define GEOMETRY_MANAGER_HH

#include "G4VUserDetectorConstruction.hh"
#include "G4GDMLParser.hh"
#include "WeightWindowWorld.hh"
#include "WeightWindowManager.hh"
#include "DetectorRegistry.hh"
#include "RegularGrid.hh"
#include "G4VPhysicalVolume.hh"
#include <map>
#include <string>
#include <memory>

class ObjectManager;
class G4LogicalVolume;

/// @brief Top-level geometry construction: world, target, objects, SDs.
///
/// Builds the world volume (programmatic or GDML), optional target box,
/// delegates OBJECT block objects to ObjectManager, registers
/// SensitiveDetectors for target and user-defined objects.
class GeometryManager : public G4VUserDetectorConstruction {
public:
    GeometryManager();
    virtual ~GeometryManager();

    /// @brief Construct world, target and objects.
    virtual G4VPhysicalVolume* Construct() override;
    /// @brief Register SensitiveDetectors (MT-safe via Geant4 cloning).
    virtual void ConstructSDandField() override;

    /// @brief Find a logical volume by name (exact LV, PV→LV, partial).
    G4LogicalVolume* FindLogicalVolume(const G4String& name);
    /// @brief Get the world-center position of a volume by name.
    G4ThreeVector GetVolumeCenter(const G4String& volName) const;
    /// @return The world physical volume.
    G4VPhysicalVolume* GetWorldVolume() const { return fWorldPV; }
    /// @return The ObjectManager instance.
    ObjectManager* GetObjectManager() const { return fObjectManager.get(); }
    /// @return The DetectorRegistry instance.
    DetectorRegistry* GetDetectorRegistry() const { return fDetectorRegistry.get(); }

    /// @brief Set an external GDML file to override default geometry.
    void SetGeometryFile(const G4String& filename) { fGeometryFile = filename; }

    /// @brief Move the target box to a new position.
    void MoveTarget(const G4ThreeVector& newPos);
    /// @brief Get the current target box position.
    G4ThreeVector GetTargetPosition() const;

private:
    G4VPhysicalVolume* fTargetPV;                       ///< Target placement volume.
    G4VPhysicalVolume* fWorldPV;                         ///< World placement volume.
    G4GDMLParser fParser;                                ///< GDML file parser.
    std::unique_ptr<ObjectManager> fObjectManager;       ///< Manages OBJECT block objects.
    std::unique_ptr<DetectorRegistry> fDetectorRegistry; ///< Registry for SensitiveDetectors.
    G4String fGeometryFile;                              ///< Optional GDML file path.
    std::string Trim(const std::string& s);               ///< String trim helper.
};

#endif
