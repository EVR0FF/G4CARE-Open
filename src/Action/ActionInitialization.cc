//==============================================================================
// G4CARE
// @file    ActionInitialization.cc
// @brief   Implementation of Geant4 user action initialization factory
// @details Implements BuildForMaster() and Build() for ActionInitialization.
//   The master thread sets up RunAction, WeightWindowManager,
//   AdaptiveScoringManager, ActivationSource global data, PrimaryKiller
//   parameters and G4MoleculeCounterManager.
//   Worker threads register PrimaryGeneratorAction, EventAction,
//   TrackingAction, SteppingAction, RunAction, DigitizerModule, and
//   optionally chemistry actions (TimeStepAction, ITTrackingInteractivity
//   with ITSteppingAction/ITTrackingAction, G4MoleculeCounter).
//   DigitizerModule is created in GeometryManager::ConstructSDandField()
//   after all sensitive detectors are registered.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ActionInitialization.hh"
#include "GeometryManager.hh"
#include "ObjectManager.hh"
#include "PrimaryGeneratorAction.hh"
#include "RunAction.hh"
#include "TrackingAction.hh"
#include "SteppingAction.hh"
#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "PositionSampler.hh"
#include "G4ios.hh"
#include "ActivationSource.hh"
#include "WeightWindowManager.hh"
#include "EventAction.hh"
#include "PhysicsManager.hh"
#include "DigitizerModule.hh"
#include "G4DigiManager.hh"
#include "AdaptiveScoringManager.hh"
#include "ITTrackingInteractivity.hh"
#include "ITSteppingAction.hh"
#include "ITTrackingAction.hh"
#include "TimeStepAction.hh"
#include "G4DNAChemistryManager.hh"
#include "G4Scheduler.hh"
#include "G4MoleculeCounter.hh"
#include "G4MoleculeCounterManager.hh"
#include "G4H2O.hh"
#include "Biasing/PrimaryKiller.hh"
#include "Analysis/Scoring/GValueScorer.hh"
#include "G4SystemOfUnits.hh"
#include "G4UImanager.hh"

/// @brief Constructor
/// @param gm Pointer to GeometryManager
/// @param pm Pointer to PhysicsManager
ActionInitialization::ActionInitialization(GeometryManager* gm, PhysicsManager* pm)
    : fGeometryManager(gm), fPhysicsManager(pm) {}

/// @brief Build actions for the master thread
///
/// Pre-allocates AdaptiveScoringManager for all threads, configures
/// G4MoleculeCounter for chemistry (GfactorCounter, ignoring H2O),
/// sets up PrimaryKiller parameters from config, creates RunAction,
/// and optionally loads weight-window statistics or weights.
void ActionInitialization::BuildForMaster() const {
    ObjectManager* objMgr = fGeometryManager ? fGeometryManager->GetObjectManager() : nullptr;
    int nThreads = ConfigManager::Instance()->GetInt("NTHREADS", 1);
    AdaptiveScoringManager::PreAllocate(nThreads);

    if (G4DNAChemistryManager::IsActivated()) {
        G4MoleculeCounterManager::Instance()->SetResetCountersBeforeEvent(true);
        G4MoleculeCounterManager::Instance()->SetAccumulateCounterIntoMaster(true);
        auto counter = std::make_unique<G4MoleculeCounter>("GfactorCounter");
        counter->IgnoreMolecule(G4H2O::Definition());
        G4MoleculeCounterManager::Instance()->RegisterCounter(std::move(counter));
    }

    if (ConfigManager::Instance()->GetBool("CHEMISTRY.PRIMARY_KILLER.ENABLE", false)) {
        auto* cfg = ConfigManager::Instance();
        G4double elossMin = cfg->GetValueWithUnits("CHEMISTRY.PRIMARY_KILLER.ELOSS_MIN", 0.0);
        G4double elossMax = cfg->GetValueWithUnits("CHEMISTRY.PRIMARY_KILLER.ELOSS_MAX", 0.0);
        auto sizeVec = cfg->GetDoubleVector("CHEMISTRY.PRIMARY_KILLER.SIZE");
        G4double sx = (sizeVec.size() >= 3) ? sizeVec[0] : 0.0;
        G4double sy = (sizeVec.size() >= 3) ? sizeVec[1] : 0.0;
        G4double sz = (sizeVec.size() >= 3) ? sizeVec[2] : 0.0;
        G4cout << "[BuildForMaster] PrimaryKiller: eLossMin="
               << elossMin / CLHEP::keV << " keV" << G4endl;
    }

    RunAction* runAction = new RunAction(nullptr, fGeometryManager, objMgr);
    SetUserAction(runAction);

    if (ConfigManager::Instance()->GetBool("PHYSICS.ADVANCED.WEIGHT_WINDOW.ENABLE", false)) {
        auto* wwm = WeightWindowManager::Instance();
        if (wwm->GetAutoMode() == "apply") wwm->LoadStatisticsAndComputeWeights();
        else wwm->LoadWeights();
    }
    ActivationSource::PrepareGlobalData("activation_data");
    G4cout << "ActionInitialization::BuildForMaster() done." << G4endl;
}

/// @brief Build actions for each worker thread
///
/// Registers the full set of per-thread user actions:
/// PrimaryGeneratorAction, EventAction, TrackingAction, SteppingAction,
/// RunAction. Optionally configures regional DNA
/// physics, PrimaryKiller filter, and chemistry pipeline
/// (G4Scheduler, TimeStepAction, ITTrackingInteractivity,
/// ITSteppingAction, ITTrackingAction, G4MoleculeCounter).
void ActionInitialization::Build() const {
    ObjectManager* objMgr = fGeometryManager ? fGeometryManager->GetObjectManager() : nullptr;
    SetUserAction(new PrimaryGeneratorAction(objMgr));
    SetUserAction(new EventAction());
    TrackingAction* tracking = new TrackingAction();
    SetUserAction(tracking);
    SetUserAction(new SteppingAction());
    SetUserAction(new RunAction(nullptr, fGeometryManager, objMgr, tracking));

    if (fPhysicsManager && fPhysicsManager->NeedsRegionalDNA())
        fPhysicsManager->ConfigureDNARegions();
        fPhysicsManager->ApplyEmRegions();

    if (ConfigManager::Instance()->GetBool("CHEMISTRY.PRIMARY_KILLER.ENABLE", false)) {
        auto* cfg = ConfigManager::Instance();
        G4double emin = cfg->GetValueWithUnits("CHEMISTRY.PRIMARY_KILLER.ELOSS_MIN", 0.0);
        G4double emax = cfg->GetValueWithUnits("CHEMISTRY.PRIMARY_KILLER.ELOSS_MAX", 0.0);
        auto szv = cfg->GetDoubleVector("CHEMISTRY.PRIMARY_KILLER.SIZE");
        G4double sx = (szv.size() >= 3) ? szv[0] : 0.0;
        G4double sy = (szv.size() >= 3) ? szv[1] : 0.0;
        G4double sz = (szv.size() >= 3) ? szv[2] : 0.0;
        auto* pk = new PrimaryKiller(emin, emax, sx, sy, sz);
        SetUserAction(pk);
        G4cout << "[Build] PrimaryKiller: eLossMin=" << emin / CLHEP::keV << " keV" << G4endl;
    }

    if (G4DNAChemistryManager::IsActivated()) {
        int schedVerbose = ConfigManager::Instance()->GetInt("CHEMISTRY.SCHEDULER_VERBOSE", 0);
        if (schedVerbose > 0)
            G4UImanager::GetUIpointer()->ApplyCommand("/scheduler/verbose " + std::to_string(schedVerbose));

        G4Scheduler::Instance()->SetUserAction(new TimeStepAction);
        double endTime = ConfigManager::Instance()->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
        G4Scheduler::Instance()->SetEndTime(endTime);

        auto itInteractivity = new ITTrackingInteractivity();
        itInteractivity->SetUserAction(new ITSteppingAction);
        itInteractivity->SetUserAction(new ITTrackingAction);
        G4Scheduler::Instance()->SetInteractivity(itInteractivity);

        auto counter = std::make_unique<G4MoleculeCounter>("GfactorCounter");
        counter->IgnoreMolecule(G4H2O::Definition());
        G4MoleculeCounterManager::Instance()->RegisterCounter(std::move(counter));
        G4cout << "[Build] Chemistry registered (endTime=" << endTime / CLHEP::ps << " ps)" << G4endl;
    }
}