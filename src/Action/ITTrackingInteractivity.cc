//==============================================================================
//
// G4CARE
//
// @file    ITTrackingInteractivity.cc
// @brief   Interactivity layer for intercepting G4IT chemical molecule steps
//          and tracks.
//
// @details
//   Delegates G4Scheduler callbacks to user stepping and tracking actions.
//   Manages trajectory storage for chemical tracks.
//
//   This component uses the Geant4-DNA extension developed by the Geant4
//   Collaboration.
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

#include "ITTrackingInteractivity.hh"

#include "G4Event.hh"
#include "G4EventManager.hh"
#include "G4IT.hh"
#include "G4RichTrajectory.hh"
#include "G4TrackingInformation.hh"
#include "G4TrajectoryContainer.hh"
#include "G4Track.hh"
#include "G4Step.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VTrajectory.hh"
#include "G4VisManager.hh"
#include "G4ios.hh"

class G4Trajectory_Lock
{
    friend class ITTrackingInteractivity;
    G4Trajectory_Lock() : fpTrajectory(nullptr) {}
    ~G4Trajectory_Lock() {}
    G4VTrajectory* fpTrajectory;
};

ITTrackingInteractivity::ITTrackingInteractivity()
    : G4ITTrackingInteractivity()
    , fpUserSteppingAction(nullptr)
    , fpUserTrackingAction(nullptr)
    , fStoreTrajectory(0)
{
}

ITTrackingInteractivity::~ITTrackingInteractivity()
{
}

//------------------------------------------------------------------------------
// @brief Initialize. Enables trajectory storage.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::Initialize()
{
    fStoreTrajectory = 1;
}

//------------------------------------------------------------------------------
// @brief Called when a chemical track starts. Delegates to tracking action.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::StartTracking(G4Track* track)
{
    if (!track) return;

    const G4String& pName = track->GetParticleDefinition()->GetParticleName();
    if (pName == "H2O" || pName == "H2O(NORMAL)") return;

    if (fpUserTrackingAction) {
        fpUserTrackingAction->PreUserTrackingAction(track);
    }

    G4IT* it = GetIT(track);
    if (!it) return;
    G4TrackingInformation* trackingInfo = it->GetTrackingInfo();
    if (!trackingInfo) return;
    G4Trajectory_Lock* trajectory_lock = trackingInfo->GetTrajectory_Lock();

    if (fStoreTrajectory && !trajectory_lock) {
        trajectory_lock = new G4Trajectory_Lock();
        trackingInfo->SetTrajectory_Lock(trajectory_lock);
        trajectory_lock->fpTrajectory = new G4RichTrajectory(track);
    }
}

//------------------------------------------------------------------------------
// @brief Called on each chemical step. Delegates to stepping action.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::AppendStep(G4Track* track, G4Step* step)
{
    if (fpUserSteppingAction) {
        fpUserSteppingAction->UserSteppingAction(step);
    }

    if (fStoreTrajectory) {
        G4IT* it = GetIT(track);
        if (!it) return;
        G4TrackingInformation* trackingInfo = it->GetTrackingInfo();
        if (!trackingInfo) return;
        G4Trajectory_Lock* trajectory_lock = trackingInfo->GetTrajectory_Lock();
        if (trajectory_lock && trajectory_lock->fpTrajectory) {
            trajectory_lock->fpTrajectory->AppendStep(step);
        }
    }
}

//------------------------------------------------------------------------------
// @brief Called when a chemical track ends. Delegates to tracking action.
//        Inserts trajectory into event container.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::EndTracking(G4Track* track)
{
    const G4String& pName = track->GetParticleDefinition()->GetParticleName();
    if (pName == "H2O" || pName == "H2O(NORMAL)") return;

    if (fpUserTrackingAction) {
        fpUserTrackingAction->PostUserTrackingAction(track);
    }

    G4IT* it = GetIT(track);
    if (!it) return;
    G4TrackingInformation* trackingInfo = it->GetTrackingInfo();
    if (!trackingInfo) return;
    G4Trajectory_Lock* trajectory_lock = trackingInfo->GetTrajectory_Lock();

    if (trajectory_lock) {
        G4VTrajectory*& trajectory = trajectory_lock->fpTrajectory;
        if (fStoreTrajectory && trajectory) {
            G4TrackStatus istop = track->GetTrackStatus();
            if (istop != fStopButAlive && istop != fSuspend) {
                G4Event* currentEvent = G4EventManager::GetEventManager()->GetNonconstCurrentEvent();
                if (currentEvent) {
                    G4TrajectoryContainer* trajectoryContainer = currentEvent->GetTrajectoryContainer();
                    if (!trajectoryContainer) {
                        trajectoryContainer = new G4TrajectoryContainer;
                        currentEvent->SetTrajectoryContainer(trajectoryContainer);
                    }
                    trajectoryContainer->insert(trajectory);
                }
            }
        }
        delete trajectory_lock;
        // Do NOT call trackingInfo->SetTrajectory_Lock(nullptr) —
        // this causes a segfault in multi-threaded G4Scheduler.
    }
}

//------------------------------------------------------------------------------
// @brief Finalize. Currently a no-op.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::Finalize()
{
}

//------------------------------------------------------------------------------
// @brief Set the user stepping action for chemical tracks.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::SetUserAction(G4UserSteppingAction* act)
{
    fpUserSteppingAction = act;
}

//------------------------------------------------------------------------------
// @brief Set the user tracking action for chemical tracks.
//------------------------------------------------------------------------------
void ITTrackingInteractivity::SetUserAction(G4UserTrackingAction* act)
{
    fpUserTrackingAction = act;
}