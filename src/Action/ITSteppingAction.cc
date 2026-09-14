//==============================================================================
//
// G4CARE
//
// @file    ITSteppingAction.cc
// @brief   Stepping action for chemical molecule tracks (G4IT).
//
// @details
//   Accumulates energy deposit for the chemical stage and records
//   chemical track coordinates via BeamAnalysis::Fill("chem_tracks").
//   Called from ITTrackingInteractivity::AppendStep for each step
//   of chemical tracks processed by G4Scheduler.
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

#include "ITSteppingAction.hh"
#include "ChemistryExtractor.hh"
#include "ChemVoxel.hh"
#include "ChemSpeciesRegistry.hh"
#include "BeamAnalysis.hh"
#include "G4Step.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include "G4ios.hh"

ITSteppingAction::ITSteppingAction()
    : G4UserSteppingAction()
{
}

//------------------------------------------------------------------------------
// @brief Called for each step of a chemical track. Accumulates energy deposit
//        and fills chem_tracks.
//------------------------------------------------------------------------------
void ITSteppingAction::UserSteppingAction(const G4Step* step)
{
    if (!step) return;

    G4Track* track = step->GetTrack();
    if (!track) return;

    // Get the chemical species name from the molecule definition.
    const G4String& pName = track->GetParticleDefinition()->GetParticleName();

    auto* bm = BeamAnalysis::Instance();
    if (!bm) return;

    // Diagnostic (limited output).
    static G4int debugCount = 0;
    if (++debugCount <= 5) {
        G4double edep = step->GetTotalEnergyDeposit();
        G4double t = track->GetGlobalTime();
        G4cout << "[ITStepping] #" << debugCount
               << " pName=" << pName
               << " t=" << t/ps << " ps"
               << " tid=" << G4Threading::G4GetThreadId() << G4endl;
    }

    // Accumulate energy deposit for the chemical stage.
    // (SteppingAction::UserSteppingAction is not called for G4IT track steps.)
    double edep = step->GetTotalEnergyDeposit();
    if (edep > 0.0) {
        ChemistryExtractor::GlobalAccumulateEdep(edep);
    }

    // Record chemical track for radical coordinate analysis.
    bm->Fill("chem_tracks", step);
}