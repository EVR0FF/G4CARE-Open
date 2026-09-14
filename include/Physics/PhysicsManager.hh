#ifndef PHYSICSMANAGER_HH
#define PHYSICSMANAGER_HH

//==============================================================================
//
// G4CARE
//
// @file    PhysicsManager.hh
// @brief   Physics list construction and module management.
//
// @details
//   Creates Geant4 physics lists from YAML configuration. Reads
//   PHYSICS.BASIC.LIST for the base physics list and iterates over
//   PHYSICS.ADVANCED subsections to register/replace physics modules.
//   Supports loading custom constructors from shared libraries.
//
//   Configuration keys read:
//     PHYSICS.BASIC.LIST
//     PHYSICS.BASIC.MACRO
//     PHYSICS.ADVANCED.<name>.ENABLE
//     PHYSICS.ADVANCED.<name>.MACRO
//     PHYSICS.ADVANCED.<name>.LIBRARY
//     PHYSICS.ADVANCED.<name>.CONSTRUCTOR
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

#include "globals.hh"
#include "ICrossSectionSource.hh"
#include "UserCrossSectionModel.hh"
#include <vector>
#include <map>
#include <string>

class G4VModularPhysicsList;
class G4VPhysicsConstructor;

//------------------------------------------------------------------------------
/// @class PhysicsManager
/// @brief Manages physics list creation and module registration from YAML
///        configuration.
///
/// @details
/// Reads PHYSICS.BASIC.LIST to select a base physics list from
/// G4PhysListFactory. Iterates PHYSICS.ADVANCED subsections and
/// registers builtin modules (STEPLIMITER, OPTICS, EM variants, DNA,
/// etc.) or loads custom constructors from shared libraries.
//------------------------------------------------------------------------------
class PhysicsManager {
public:
    PhysicsManager();
    ~PhysicsManager();

    /// @brief Create the complete physics list from configuration.
    G4VModularPhysicsList* CreatePhysicsList();

    /// @brief Whether chemistry-related modules are enabled (EM_DNA, EM_DNA_CHEM, etc.).
    bool IsChemistryEnabled() const { return fChemistryEnabled; }

    /// @brief Whether regional DNA physics (option8) is needed.
    bool NeedsRegionalDNA() const { return fUseRegionalDNA; }

    /// @brief Configure per-region DNA physics for volumes containing "water".
    void ConfigureDNARegions();

    /// @brief Apply EM physics lists to regions from PHYSICS.EM_REGIONS config.
    void ApplyEmRegions();

    /// @brief Add a cross-section data source for user models.
    void AddCrossSectionSource(ICrossSectionSource* src);
    void RegisterCrossSectionModels();

    /// @brief Register a user-defined EM cross-section model.
    void RegisterUserCrossSection(
        G4VEmModel* model,
        const G4ParticleDefinition* particle,
        const G4String& processName);

private:

    struct ModuleInfo {
        G4String name;
        G4String macro;
        G4String library;
        G4String constructorName;
        G4bool enabled;
    };

    /// @brief EM region configuration: region name, EM physics list, and optional volume names.
    struct EmRegionInfo {
        G4String regionName;
        G4String emListName;
        std::vector<G4String> volumeNames;
    };

    bool fChemistryEnabled = false;
    bool fUseRegionalDNA = false;

    /// @brief Read PHYSICS configuration from ConfigManager.
    void ReadConfiguration();

    /// @brief Create base physics list from G4PhysListFactory.
    G4VModularPhysicsList* CreateBaseList(const G4String& baseName);

    /// @brief Process a single ADVANCED module (builtin or custom).
    void ProcessModule(const ModuleInfo& module, G4VModularPhysicsList* physList);

    /// @brief Load a custom physics constructor from a shared library.
    G4VPhysicsConstructor* LoadCustomConstructor(const G4String& libPath, const G4String& ctorName);

    /// @brief Load a full physics list from a shared library (returns G4VModularPhysicsList*).
    G4VModularPhysicsList* LoadCustomPhysicsList(const G4String& libPath, const G4String& ctorName);

    G4String fBaseListName;
    G4String fBaseMacro;
    std::vector<ModuleInfo> fModules;
    std::vector<EmRegionInfo> fEmRegions;

    std::vector<void*> fLibraryHandles;
    std::vector<ICrossSectionSource*> fCrossSectionSources;
};

#endif