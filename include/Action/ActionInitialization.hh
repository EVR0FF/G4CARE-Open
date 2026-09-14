#ifndef ACTIONINITIALIZATION_HH
#define ACTIONINITIALIZATION_HH

//==============================================================================
// G4CARE
// @file    ActionInitialization.hh
// @brief   Geant4 user action initialization factory
// @details Inherits G4VUserActionInitialization and registers all UserActions
//   in each thread (master and worker). The master thread additionally
//   configures Weight Windows, Adaptive Scoring and chemistry molecule
//   counters.
//   Configuration keys read:
//   - NTHREADS                       Number of worker threads
//   - PHYSICS.ADVANCED.WEIGHT_WINDOW.ENABLE  Enable weight-window biasing
//   - CHEMISTRY.PRIMARY_KILLER.ENABLE        Enable primary-killer filter
//   - CHEMISTRY.PRIMARY_KILLER.ELOSS_MIN     Minimum energy loss [GeV]
//   - CHEMISTRY.PRIMARY_KILLER.ELOSS_MAX     Maximum energy loss [GeV]
//   - CHEMISTRY.PRIMARY_KILLER.SIZE          Killer box half-size [mm]
//   - CHEMISTRY.SCHEDULER_VERBOSE            Scheduler verbosity level
//   - CHEMISTRY.END_TIME                      Chemistry end time [ns]
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4VUserActionInitialization.hh"
#include "PhysicsManager.hh"

class GeometryManager;

//==============================================================================
/// @class ActionInitialization
/// @brief  Geant4 user action factory
///
/// Registers PrimaryGeneratorAction, RunAction, EventAction,
/// SteppingAction, TrackingAction in each thread. Manages WeightWindow
/// biasing setup, AdaptiveScoringManager and chemistry interactivity.
//==============================================================================
class ActionInitialization : public G4VUserActionInitialization {
public:
    /// @brief Constructor
    /// @param geomManager Pointer to GeometryManager
    /// @param physMgr     Pointer to PhysicsManager
    ActionInitialization(GeometryManager* geomManager, PhysicsManager* physMgr);

    /// @brief Destructor
    virtual ~ActionInitialization() override = default;

    /// @brief Build for master thread
    ///
    /// Called once by Geant4 in the master thread.
    /// Configures RunAction, WeightWindowManager (weights),
    /// AdaptiveScoringManager (pre-allocation), ActivationSource (global data),
    /// G4MoleculeCounterManager.
    virtual void BuildForMaster() const override;

    /// @brief Build for worker thread
    ///
    /// Called by Geant4 in each worker thread.
    /// Registers PrimaryGeneratorAction, EventAction, TrackingAction,
    /// SteppingAction, RunAction, DigitizerModule.
    /// When chemistry is activated, registers TimeStepAction,
    /// ChemistryTrackingManager, ITSteppingAction, G4MoleculeCounter.
    virtual void Build() const override;

private:
    /// @brief Pointer to geometry manager
    GeometryManager* fGeometryManager;

    /// @brief Pointer to physics manager
    PhysicsManager*  fPhysicsManager;
};

#endif