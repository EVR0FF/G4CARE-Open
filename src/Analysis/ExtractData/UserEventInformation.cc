//==============================================================================
// G4CARE
// @file    UserEventInformation.cc
// @brief   Per-event user information holding energy-deposit-per-volume
//          and isotope production tallies.
// @details UserEventInformation extends G4VUserEventInformation to accumulate
//   energy deposition per logical volume ID and count produced isotopes
//   (by PDG code, mass, and lifetime).  It is attached to G4Event via
//   SetUserInformation and cleared between events.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "UserEventInformation.hh"
#include "G4ios.hh"

/// @brief Default constructor.
UserEventInformation::UserEventInformation()
    : G4VUserEventInformation()
{
}

/// @brief Accumulates energy deposition for a given volume.
/// @param volId    Logical volume ID.
/// @param edep_MeV Energy deposited in MeV.
void UserEventInformation::AddEdep(int volId, double edep_MeV) {
    fEdepPerVolume[volId] += edep_MeV;
}

/// @brief Records an isotope produced in this event.
/// @param pdgCode     PDG code of the isotope.
/// @param mass_MeV    Mass in MeV.
/// @param lifeTime_sec Lifetime in seconds.
void UserEventInformation::AddIsotope(int pdgCode, double mass_MeV, double lifeTime_sec) {
    auto it = fIsotopes.find(pdgCode);
    if (it == fIsotopes.end()) {
        fIsotopes[pdgCode] = std::make_tuple(mass_MeV, lifeTime_sec, 1);
    } else {
        std::get<2>(it->second) += 1;
    }
}

/// @brief Clears all accumulated tallies.
void UserEventInformation::Clear() {
    fEdepPerVolume.clear();
    fIsotopes.clear();
}

/// @brief Prints a summary of accumulated data to G4cout.
void UserEventInformation::Print() const {
    G4cout << "UserEventInformation: " << fEdepPerVolume.size() 
           << " volumes with energy deposit, "
           << fIsotopes.size() << " isotope types" << G4endl;
}