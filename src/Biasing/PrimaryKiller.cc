//==============================================================================
//
// G4CARE
//
// @file    PrimaryKiller.cc
// @brief   Primary particle killer based on energy deposition thresholds.
//
// @details
//   Implements the mechanism from Geant4-DNA examples chem4/5/6:
//   tracks primary particle energy deposition, kills the track
//   at eLossMin, aborts the event at eLossMax (single step), and
//   optionally confines particles to a virtual volume.
//
//   This component uses the Geant4-DNA extension developed by the Geant4
//   Collaboration.
//
//   Configuration keys read:
//     CHEMISTRY.PRIMARY_KILLER.ENABLE
//     CHEMISTRY.PRIMARY_KILLER.ELOSS_MIN
//     CHEMISTRY.PRIMARY_KILLER.ELOSS_MAX
//     CHEMISTRY.PRIMARY_KILLER.SIZE
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

#include "Biasing/PrimaryKiller.hh"
#include "G4Step.hh"
#include "G4Track.hh"
#include "G4EventManager.hh"
#include "G4RunManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"

//------------------------------------------------------------------------------
// @brief Constructor.
//
// @param eLossMin  Minimum accumulated energy deposit to kill [MeV].
// @param eLossMax  Maximum energy deposit in single step [MeV].
// @param sizeX     Virtual volume half-size X [mm] (0 = disabled).
// @param sizeY     Virtual volume half-size Y [mm].
// @param sizeZ     Virtual volume half-size Z [mm].
//------------------------------------------------------------------------------
PrimaryKiller::PrimaryKiller(G4double eLossMin, G4double eLossMax,
                              G4double sizeX, G4double sizeY, G4double sizeZ)
    : fEnabled(false)
    , fELossMin(eLossMin)
    , fELossMax(eLossMax)
    , fUseVolumeCuts(false)
    , fSizeX(sizeX), fSizeY(sizeY), fSizeZ(sizeZ)
    , fPrimaryTrackID(-1)
    , fTotalEdep(0.0)
    , fEventAborted(false)
    , fPrimaryKilled(false)
{
    if (sizeX > 0.0 && sizeY > 0.0 && sizeZ > 0.0)
        fUseVolumeCuts = true;

    // Auto-enable if thresholds are set.
    if (eLossMin > 0.0 || eLossMax > 0.0)
        fEnabled = true;
}

//------------------------------------------------------------------------------
// @brief Reset accumulated energy deposit at the start of each event.
//------------------------------------------------------------------------------
void PrimaryKiller::ResetEvent() {
    fPrimaryTrackID = -1;
    fTotalEdep      = 0.0;
    fEventAborted   = false;
    fPrimaryKilled  = false;
}

//------------------------------------------------------------------------------
// @brief Check whether a position is inside the virtual volume.
//
// @param pos  3D position to check.
// @return     True if inside volume (or volume cuts disabled).
//------------------------------------------------------------------------------
G4bool PrimaryKiller::IsInsideVolume(const G4ThreeVector& pos) const {
    if (!fUseVolumeCuts) return true;
    G4double hx = fSizeX * 0.5;
    G4double hy = fSizeY * 0.5;
    G4double hz = fSizeZ * 0.5;
    return (std::abs(pos.x()) <= hx &&
            std::abs(pos.y()) <= hy &&
            std::abs(pos.z()) <= hz);
}

//------------------------------------------------------------------------------
// @brief Called on each step. Checks energy thresholds and volume bounds.
//
// @param step  Current Geant4 step.
//------------------------------------------------------------------------------
void PrimaryKiller::UserSteppingAction(const G4Step* step) {
    if (!fEnabled || fEventAborted) return;

    G4Track* track = step->GetTrack();
    if (!track) return;

    G4int trackID = track->GetTrackID();

    // Identify primary particle by the first track.
    if (fPrimaryTrackID < 0) {
        // Primary = first track with ParentID == 0.
        if (track->GetParentID() == 0) {
            fPrimaryTrackID = trackID;
        } else {
            // This is a secondary — skip.
            return;
        }
    }

    G4double edepThisStep = step->GetTotalEnergyDeposit();

    // --- Virtual volume check (setSize) ---
    if (fUseVolumeCuts && !IsInsideVolume(track->GetPosition())) {
        // Track left the virtual volume.
        track->SetTrackStatus(fStopAndKill);
        G4cout << "[PrimaryKiller] Track " << trackID
               << " killed (outside volume) at "
               << track->GetPosition() << G4endl;
        return;
    }

    // Only check thresholds for the primary particle.
    if (trackID != fPrimaryTrackID) return;

    // --- eLossMax: exceeded in a single step → abort event ---
    if (fELossMax > 0.0 && edepThisStep > fELossMax) {
        G4EventManager::GetEventManager()->AbortCurrentEvent();
        fEventAborted = true;
        G4cout << "[PrimaryKiller] Event ABORTED: step E_dep="
               << edepThisStep / CLHEP::keV << " keV > eLossMax="
               << fELossMax / CLHEP::keV << " keV" << G4endl;
        return;
    }

    // --- Accumulate energy deposit ---
    fTotalEdep += edepThisStep;

    // --- eLossMin: accumulated enough energy → kill primary ---
    if (!fPrimaryKilled && fELossMin > 0.0 && fTotalEdep >= fELossMin) {
        track->SetTrackStatus(fStopAndKill);
        fPrimaryKilled = true;
        G4cout << "[PrimaryKiller] Primary killed: total E_dep="
               << fTotalEdep / CLHEP::keV << " keV >= eLossMin="
               << fELossMin / CLHEP::keV << " keV" << G4endl;

        // G4Scheduler automatically starts the chemistry stage when
        // all physical tracks have been processed (via StackingAction).
    }
}