//==============================================================================
//
// G4CARE
//
// @file    EventAction.hh
// @brief   Event-level actions: chemistry stage trigger, dose accumulation,
//          ExprTK event scripts, and isotope NTuple recording.
//
// @details
//   Implements G4UserEventAction.  BeginOfEventAction() notifies
//   G4DNAChemistryManager of the event start.  EndOfEventAction()
//   triggers the Geant4-DNA chemistry stage, accumulates energy for
//   dose control, executes event-level ExprTK scripts, and records
//   isotope production data from UserEventInformation.
//
//   Configuration keys read:
//     CHEMISTRY.ENABLE
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

#ifndef EVENTACTION_HH
#define EVENTACTION_HH

#include "G4UserEventAction.hh"
#include "G4Event.hh"

/// @brief Event-level user action: chemistry stage, dose, ExprTK, isotopes.
///
/// Coordinates the sequence of operations that must occur at event
/// boundaries: chemistry-stepping synchronisation, dose accumulation,
/// per-volume ExprTK script execution, and isotope NTuple recording.
class EventAction : public G4UserEventAction {
public:
    EventAction();
    virtual ~EventAction();

    /// @brief Called by Geant4 at the start of each event.
    ///        Notifies G4DNAChemistryManager.
    virtual void BeginOfEventAction(const G4Event* event) override;

    /// @brief Called by Geant4 at the end of each event.
    ///
    /// Runs the chemistry stage (G4DNAChemistryManager::Run()),
    /// notifies chemistry manager of event end, flushes
    /// thread-local dose data to the global accumulator, executes
    /// event-level ExprTK scripts, and writes isotope data to the
    /// "activation" NTuple.
    virtual void EndOfEventAction(const G4Event* event) override;
};

#endif