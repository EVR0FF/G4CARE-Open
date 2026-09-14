//==============================================================================
//
// G4CARE
//
// @file    EventAction.cc
// @brief   Event-level actions: chemistry stage trigger, dose accumulation,
//          ExprTK event scripts, and isotope NTuple recording.
//
// @details
//   BeginOfEventAction() notifies G4DNAChemistryManager of the event start.
//
//   EndOfEventAction() performs in order:
//   1. Triggers the Geant4-DNA chemistry stage via G4DNAChemistryManager::Run().
//   2. Notifies G4DNAChemistryManager of event end.
//   3. Delegates to SteppingAction::AccumulateEventEnergy() for dose control.
//   4. Executes event-level ExprTK scripts via ExprsManager.
//   5. Records isotope production data from UserEventInformation into the
//      "activation" NTuple tree via BeamAnalysis.
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

#include "EventAction.hh"
#include "UserEventInformation.hh"
#include "BeamAnalysis.hh"
#include "G4RunManager.hh"
#include "G4DigiManager.hh"
#include "RunAction.hh"
#include "SteppingAction.hh"
#include "G4Scheduler.hh"
#include "G4DNAChemistryManager.hh"
#include "ConfigManager.hh"
#include "ExprsManager.hh"
#include "G4LogicalVolume.hh"

EventAction::EventAction() = default;
EventAction::~EventAction() = default;

/// @brief Notify G4DNAChemistryManager of the event start.
/// @param event Current Geant4 event.
///
/// G4DNAChemistryManager (and through it G4MoleculeCounterManager)
/// is informed that a new event has begun so that chemistry molecule
/// accounting is reset.
void EventAction::BeginOfEventAction(const G4Event* event) {
    // Notify G4DNAChemistryManager (and through it — G4MoleculeCounterManager)
    if (G4DNAChemistryManager::GetInstanceIfExists() != nullptr) {
        G4DNAChemistryManager::GetInstanceIfExists()->BeginOfEventAction(event);
    }
}

/// @brief Finalise the event: run chemistry stage, accumulate dose,
///        execute ExprTK event scripts, and record isotope data.
///
/// @param event Current Geant4 event.
///
/// **Processing order:**
/// 1. **Chemistry stage** — G4DNAChemistryManager::Run() must be called
///    before reading event info.  On the very first event the chemistry
///    activation flag (CHEMISTRY.ENABLE) is checked.
/// 2. **G4DNAChemistryManager::EndOfEventAction()** — notifies the
///    chemistry manager of event end (required for G4MoleculeCounter
///    data population).
/// 3. **Dose accumulation** — SteppingAction::AccumulateEventEnergy()
///    transfers thread-local event energy to the global atomic counter
///    and checks the dose threshold.
/// 4. **EXPRS event scripts** — per-volume ExprTK event-level scripts
///    are executed via ExprsManager::TryExecuteEventScripts() for each
///    logical volume that received energy deposition.
/// 5. **Isotope NTuple** — isotope production data collected by
///    UserEventInformation is written to the "activation" NTuple tree
///    via BeamAnalysis::Fill().
void EventAction::EndOfEventAction(const G4Event* event) {
    // ── Chemistry stage: MUST be BEFORE checking eventInfo! ──
    // G4DNAChemistryManager::Run() starts G4Scheduler.
    // G4DNAChemistryManager::EndOfEventAction(event) populates G4MoleculeCounter.
    static bool sChemChecked = false;
    if (!sChemChecked) {
        auto* cfg = ConfigManager::Instance();
        if (cfg->GetBool("CHEMISTRY.ENABLE", false)) {
            G4bool activated = G4DNAChemistryManager::IsActivated();
            G4cout << "[EventAction] Chem activated=" << (activated ? "true" : "false") << G4endl;
            if (activated) {
                G4DNAChemistryManager::Instance()->Run();
            }
        }
        sChemChecked = true;
    } else {
        if (G4DNAChemistryManager::IsActivated()) {
            G4DNAChemistryManager::Instance()->Run();
        }
    }

    // Notify chemistry manager of event end BEFORE checking eventInfo!
    // Without this, G4MoleculeCounter does not receive molecule data.
    if (G4DNAChemistryManager::GetInstanceIfExists() != nullptr) {
        G4DNAChemistryManager::GetInstanceIfExists()->EndOfEventAction(event);
    }

    // Flush thread-local event energy BEFORE checking eventInfo!
    SteppingAction::AccumulateEventEnergy();

    // ── EXPRS: count events per volume ──
    {
        extern thread_local std::set<const G4LogicalVolume*> g_tlEventVolumes;
        extern thread_local bool g_tlHadInteraction;
        auto* sm = ExprsManager::Instance();
        if (!sm->GetBlocks().empty()) {
            for (const auto* lv : g_tlEventVolumes) {
                sm->EventEnd(lv, g_tlHadInteraction);
                sm->TryExecuteEventScripts(lv);
            }
            g_tlEventVolumes.clear();
            g_tlHadInteraction = false;
        }
    }

    // ── DigitizerModule: process digitization ──
    G4DigiManager::GetDMpointer()->Digitize("DigitizerModule");

    UserEventInformation* eventInfo = static_cast<UserEventInformation*>(event->GetUserInformation());
    if (!eventInfo) return;

    const auto& isotopes = eventInfo->GetIsotopes();
    if (!isotopes.empty()) {
        auto* analysis = BeamAnalysis::Instance();
        G4int eventId = event->GetEventID();
        G4int runId = analysis->GetCurrentRunId();

        for (const auto& pair : isotopes) {
            int pdg = pair.first;
            double mass = std::get<0>(pair.second);
            double lifeTime = std::get<1>(pair.second);
            int count = std::get<2>(pair.second);

            std::map<std::string, double> values;
            values["event_id"] = eventId;
            values["run_id"] = runId;
            values["isotope_pdg"] = pdg;
            values["isotope_count"] = count;
            values["isotope_mass"] = mass / CLHEP::MeV;
            values["isotope_lifetime"] = lifeTime / CLHEP::second;

            analysis->Fill("activation", values);
        }
    }

    eventInfo->Clear();
}