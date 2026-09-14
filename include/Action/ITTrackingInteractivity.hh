#ifndef IT_TRACKING_INTERACTIVITY_HH
#define IT_TRACKING_INTERACTIVITY_HH

//==============================================================================
//
// G4CARE
//
// @file    ITTrackingInteractivity.hh
// @brief   Interactivity layer for intercepting G4IT chemical molecule steps
//          and tracks.
//
// @details
//   Chemical molecules (radicals OH, e_aq, H, etc.) are processed by
//   G4Scheduler, not by the standard Geant4 tracking. To intercept them,
//   G4ITTrackingInteractivity must be registered via G4Scheduler.
//
//   Delegates calls:
//     AppendStep     → G4UserSteppingAction  (ITSteppingAction)
//     StartTracking  → G4UserTrackingAction  (ITTrackingAction)
//     EndTracking    → G4UserTrackingAction
//
//   Design based on the official chem3 example (Geant4-DNA).
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

#include "G4ITTrackingInteractivity.hh"
#include "G4Track.hh"
#include "G4Step.hh"
#include "G4VTrajectory.hh"

class G4UserSteppingAction;
class G4UserTrackingAction;

//------------------------------------------------------------------------------
/// @class ITTrackingInteractivity
/// @brief Intercepts chemical molecule (G4IT) steps and tracks from
///        G4Scheduler and delegates to user actions.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class ITTrackingInteractivity : public G4ITTrackingInteractivity
{
public:
    ITTrackingInteractivity();
    ~ITTrackingInteractivity() override;

    void Initialize() override;
    /// @brief Called when a chemical track starts.
    void StartTracking(G4Track*) override;
    /// @brief Called on each chemical step.
    void AppendStep(G4Track* track, G4Step* step) override;
    /// @brief Called when a chemical track ends.
    void EndTracking(G4Track*) override;
    void Finalize() override;

    /// @brief Set the user stepping action for chemical tracks.
    void SetUserAction(G4UserSteppingAction*);
    /// @brief Set the user tracking action for chemical tracks.
    void SetUserAction(G4UserTrackingAction*);

private:
    G4UserSteppingAction* fpUserSteppingAction = nullptr;
    G4UserTrackingAction* fpUserTrackingAction = nullptr;
    int fStoreTrajectory = 0;
    int fVerboseLevel = 0;
    std::vector<G4VTrajectory*> fTrajectories;
};

#endif // IT_TRACKING_INTERACTIVITY_HH