//==============================================================================
//
// G4CARE
//
// @file    TrackingAction.cc
// @brief   Per-track action: ion production recording, secondary NTuple
//          fill, and track-level ExprTK script execution.
//
// @details
//   Implements G4UserTrackingAction.
//
//   PreUserTrackingAction():
//   - Fills the "secondary" NTuple tree with track-start data.
//   - If the particle is a general ion, fills "ion_production" and
//     registers the isotope (PDG, mass, lifetime) in UserEventInformation
//     for later recording in the "activation" NTuple by EventAction.
//
//   PostUserTrackingAction():
//   - Executes track-level ExprTK scripts via ExprsManager if EXPRS
//     blocks are active and the track ended in a recognised volume.
//     Builds FilterVars from the track data via DataExtractor.
//
//   Configuration keys read: none (uses registered volumes from
//   GeometryManager and active EXPRS blocks from ConfigManager).
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

#include "TrackingAction.hh"
#include "BeamAnalysis.hh"
#include "ExprsManager.hh"
#include "G4Track.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VTouchable.hh"
#include "G4Ions.hh"
#include "UserEventInformation.hh"
#include "G4RunManager.hh"
#include "UnifiedSource.hh"
#include "FilterVarRegistry.hh"
#include "DataExtractor.hh"

TrackingAction::TrackingAction() : G4UserTrackingAction() {}

/// @brief Called by Geant4 just before a new track starts being processed.
///
/// @param track Pointer to the G4Track about to be simulated.
///
/// Fills the "secondary" NTuple tree via BeamAnalysis.  For general ions
/// (G4IonTable particles), additionally fills the "ion_production" NTuple
/// and appends the isotope to UserEventInformation so that its PDG code,
/// mass, and lifetime are recorded in the "activation" NTuple by
/// EventAction::EndOfEventAction().
void TrackingAction::PreUserTrackingAction(const G4Track* track) {

    BeamAnalysis::Instance()->Fill("secondary", track);

    if (track->GetParticleDefinition()->IsGeneralIon()) {
        BeamAnalysis::Instance()->Fill("ion_production", track);
    }

    if (track->GetParticleDefinition()->IsGeneralIon()) {
        G4int pdg = track->GetParticleDefinition()->GetPDGEncoding();
        double mass = track->GetParticleDefinition()->GetPDGMass();
        double lifeTime = track->GetParticleDefinition()->GetPDGLifeTime();

        G4Event* event = const_cast<G4Event*>(G4RunManager::GetRunManager()->GetCurrentEvent());
        UserEventInformation* eventInfo = static_cast<UserEventInformation*>(event->GetUserInformation());
        if (!eventInfo) {
            eventInfo = new UserEventInformation();
            event->SetUserInformation(eventInfo);
        }
        eventInfo->AddIsotope(pdg, mass, lifeTime);
    }
}

/// @brief Called by Geant4 just after a track has finished.
///
/// @param track Pointer to the G4Track that has just ended.
///
/// If EXPRS blocks are active, attempts to determine the logical volume
/// where the track ended (via NextVolume or initial Touchable).  Builds
/// FilterVars from the track data and executes track-level ExprTK scripts
/// via ExprsManager::TryExecuteTrackScripts().
void TrackingAction::PostUserTrackingAction(const G4Track* track) {
    if (!track) return;
    auto* sm = ExprsManager::Instance();
    if (sm->GetBlocks().empty()) return;

    // Obtain the logical volume where the track ended.
    // Try NextVolume first, fall back to the initial pre-step volume.
    G4VPhysicalVolume* pv = nullptr;
    const G4VTouchable* touchable = track->GetNextTouchable();
    if (touchable) pv = touchable->GetVolume();
    if (!pv) {
        // Fallback: use initial volume
        const G4VTouchable* initTouch = track->GetTouchable();
        if (initTouch) pv = initTouch->GetVolume();
    }
    if (!pv) return;

    const G4LogicalVolume* lv = pv->GetLogicalVolume();
    if (!lv) return;

    UnifiedSource src(track);
    FilterVars filterVars{};
    auto* de = BeamAnalysis::Instance()->GetDataExtractor();
    if (de) {
        de->BuildFilterVars(filterVars, src, -1, -1, -1, nullptr);
        sm->TryExecuteTrackScripts(lv, filterVars);
    }
}