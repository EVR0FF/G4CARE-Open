//==============================================================================
//
// G4CARE
//
// @file    ITTrackingAction.cc
// @brief   Tracking action for chemical molecule tracks (G4IT).
//
// @details
//   Determines the chemical species index via ChemSpeciesRegistry and
//   increments thread-local counters through ChemistryExtractor at
//   track start. Called from ITTrackingInteractivity for each G4IT track.
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

#include "ITTrackingAction.hh"
#include "ChemistryExtractor.hh"
#include "ChemVoxel.hh"
#include "ChemSpeciesRegistry.hh"
#include "BeamAnalysis.hh"
#include "G4Track.hh"
#include "G4Threading.hh"
#include "G4ios.hh"

ITTrackingAction::ITTrackingAction()
    : G4UserTrackingAction()
{
}

//------------------------------------------------------------------------------
// @brief Called when a chemical track starts. Determines species index and
//        increments the thread-local counter via ChemistryExtractor.
//------------------------------------------------------------------------------
void ITTrackingAction::PreUserTrackingAction(const G4Track* track)
{
    if (!track) return;

    const G4String& pName = track->GetParticleDefinition()->GetParticleName();

    auto* bm = BeamAnalysis::Instance();
    if (!bm) return;

    auto* reg = bm->GetChemSpeciesRegistry();
    if (!reg || reg->Size() == 0) return;

    int id = reg->GetID(pName);
    if (id < 0) return; // Not a chemical molecule — skip.

    static G4int debugCount = 0;
    if (++debugCount <= 5) {
        G4cout << "[ITTrackingAction::Pre] track#" << debugCount
               << " pName=" << pName
               << " regSize=" << reg->Size()
               << " id=" << id
               << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
    }

    // Create a ChemVoxel with a single increment for this species.
    ChemVoxel voxel(reg->Size());
    voxel.Increment(static_cast<size_t>(id));

    // Accumulate in the thread-local slot of the global accumulator
    // (each thread writes only to its own slot — lock-free).
    ChemistryExtractor::GlobalAccumulate(voxel);
}

//------------------------------------------------------------------------------
// @brief Called when a chemical track ends. Currently a no-op.
//        The counter is already incremented in PreUserTrackingAction
//        when the molecule is created.
//------------------------------------------------------------------------------
void ITTrackingAction::PostUserTrackingAction(const G4Track*)
{
    // Counter already incremented in PreUserTrackingAction at molecule birth.
    // PostUserTrackingAction can optionally be used to log the final
    // position or lifetime of the molecule.
}