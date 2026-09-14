#ifndef PRIMARY_KILLER_HH
#define PRIMARY_KILLER_HH

//==============================================================================
//
// G4CARE
//
// @file    PrimaryKiller.hh
// @brief   Primary particle killer based on energy deposition thresholds.
//
// @details
//   Implements the mechanism from Geant4-DNA examples chem4/5/6:
//   - Tracks primary particle energy deposition.
//   - At eLossMin: kills the track and starts the chemistry stage.
//   - At eLossMax (exceeded in single step): aborts the event.
//   - Optional SIZE: kills particles outside the virtual volume.
//
//   Installed via SetUserAction in ActionInitialization.
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

#include "G4UserSteppingAction.hh"
#include "G4ThreeVector.hh"
#include "globals.hh"

class G4VPhysicalVolume;

//------------------------------------------------------------------------------
/// @class PrimaryKiller
/// @brief Stepping action that terminates primary particles at energy
///        deposition thresholds.
///
/// @details
/// Works as a regular SteppingAction: Geant4 calls UserSteppingAction
/// on every step. Accumulates the primary particle's energy deposit
/// and kills the track once eLossMin is reached, or aborts the event
/// if eLossMax is exceeded in a single step.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class PrimaryKiller : public G4UserSteppingAction {
public:
    /// @brief Constructor.
    ///
    /// @param eLossMin  Minimum accumulated energy deposit to kill [MeV].
    /// @param eLossMax  Maximum energy deposit in single step [MeV].
    /// @param sizeX     Virtual volume half-size X [mm] (0 = disabled).
    /// @param sizeY     Virtual volume half-size Y [mm].
    /// @param sizeZ     Virtual volume half-size Z [mm].
    PrimaryKiller(G4double eLossMin = 0.0,
                  G4double eLossMax = 0.0,
                  G4double sizeX   = 0.0,
                  G4double sizeY   = 0.0,
                  G4double sizeZ   = 0.0);

    virtual ~PrimaryKiller() = default;

    /// @brief Called on each step. Checks thresholds and volume bounds.
    virtual void UserSteppingAction(const G4Step* step) override;

    // ---- Setters ----

    /// @brief Set the minimum energy deposit threshold.
    void SetELossMin(G4double val)  { fELossMin = val; }
    /// @brief Set the maximum single-step energy threshold.
    void SetELossMax(G4double val)  { fELossMax = val; }
    /// @brief Set virtual volume size [mm]. All 3 > 0 to enable volume cuts.
    void SetSize(G4double x, G4double y, G4double z) {
        fSizeX = x; fSizeY = y; fSizeZ = z;
        fUseVolumeCuts = (x > 0.0 && y > 0.0 && z > 0.0);
    }
    /// @brief Enable or disable the killer.
    void SetEnabled(bool val)       { fEnabled = val; }

    // ---- Getters ----

    /// @brief Total accumulated energy deposit of the primary track [MeV].
    G4double GetTotalEdep() const   { return fTotalEdep; }
    /// @brief Whether the current event was aborted.
    G4bool   IsEventAborted() const { return fEventAborted; }
    /// @brief Whether the killer is enabled.
    G4bool   IsEnabled() const      { return fEnabled; }

    /// @brief Reset accumulated energy deposit (called at event start).
    void ResetEvent();

private:
    /// @brief Check whether a position is inside the virtual volume.
    G4bool IsInsideVolume(const G4ThreeVector& pos) const;

    // ---- Parameters ----
    G4bool    fEnabled;
    G4double  fELossMin;              // [MeV] min accumulated Edep to kill
    G4double  fELossMax;              // [MeV] max Edep in single step
    G4bool    fUseVolumeCuts;
    G4double  fSizeX, fSizeY, fSizeZ; // [mm] virtual volume half-sizes

    // ---- Event state ----
    G4int     fPrimaryTrackID;
    G4double  fTotalEdep;             // accumulated Edep of primary [MeV]
    G4bool    fEventAborted;
    G4bool    fPrimaryKilled;
};

#endif // PRIMARY_KILLER_HH