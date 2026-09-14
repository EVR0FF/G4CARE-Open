//==============================================================================
// G4CARE
// @file    StageManager.hh
// @brief   Manages multi-stage simulation runs with different geometries,
//          event counts, physics/chemistry configurations, and property
//          tracking across stages.
// @details StageManager supports configuring a sequence of simulation stages,
//   each with its own detector geometry, number of events, optional physics
//   and chemistry macro paths, and chemistry state save/load flags.  It also
//   integrates with PropertyTracker to log property changes between stages.
//
//   Configuration keys read: STAGES.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef STAGE_MANAGER_HH
#define STAGE_MANAGER_HH

#include "G4RunManager.hh"
#include "G4VUserDetectorConstruction.hh"
#include "ConfigManager.hh"
#include "PropertyTracker.hh"
#include <vector>
#include <string>

class RunAction;
class GeometryManager; 

/// @brief Manages multi-stage simulation runs with property tracking.
class StageManager {
public:
    /// @brief Constructs the manager with the Geant4 run manager.
    /// @param runMgr Pointer to the G4RunManager.
    StageManager(G4RunManager* runMgr);
    ~StageManager() = default;

    /// @brief Adds a simulation stage with the given geometry and event count.
    /// @param geom               Detector construction for this stage.
    /// @param nEvents            Number of events to simulate.
    /// @param physicsMacro       Optional physics macro path.
    /// @param chemistryMacro     Optional chemistry macro path.
    /// @param saveChemistryState Whether to save chemistry state at end of stage.
    /// @param loadChemistryState Whether to load chemistry state at start of stage.
    void AddStage(G4VUserDetectorConstruction* geom, int nEvents,
                  const std::string& physicsMacro = "",
                  const std::string& chemistryMacro = "",
                  bool saveChemistryState = false,
                  bool loadChemistryState = false);

    /// @brief Loads stage configuration from a YAML/JSON file.
    void LoadConfiguration(const std::string& configFileName);

    /// @brief Sets the RunAction to be used for all stages.
    void SetRunAction(RunAction* ra) { fRunAction = ra; }

    /// @brief Initializes the PropertyTracker with ntuple column IDs.
    void InitializePropertyTracker(int histNtupleId, int colRunId,
                                    int colPropName, int colValue);

    /// @brief Executes all configured stages sequentially.
    void RunAll();

private:
    G4RunManager* fRunManager;
    RunAction* fRunAction = nullptr;
    PropertyTracker fPropertyTracker;

    /// @brief Describes a single simulation stage.
    struct Stage {
        G4VUserDetectorConstruction* geometry;
        int events;
        std::string physicsMacro;
        std::string chemistryMacro;
        bool saveChemState;
        bool loadChemState;
    };
    std::vector<Stage> fStages;
    /// @brief Queries the current value of a named configuration property.
    std::string GetCurrentPropertyValue(const std::string& propertyName) const;
    int fStageCounter = 0;
};

#endif
