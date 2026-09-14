//==============================================================================
// G4CARE
// @file    UserEventInformation.hh
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

#ifndef USER_EVENT_INFORMATION_HH
#define USER_EVENT_INFORMATION_HH

#include "G4VUserEventInformation.hh"
#include <unordered_map>
#include <tuple>

/// @brief Per-event accumulation of energy deposit per volume and isotope counts.
class UserEventInformation : public G4VUserEventInformation {
public:
    UserEventInformation();
    virtual ~UserEventInformation() = default;

    // Energy deposition
    void AddEdep(int volId, double edep_MeV);
    const std::unordered_map<int, double>& GetEdepPerVolume() const { return fEdepPerVolume; }

    // Isotopes
    void AddIsotope(int pdgCode, double mass_MeV, double lifeTime_sec);
    const std::unordered_map<int, std::tuple<double, double, int>>& GetIsotopes() const { return fIsotopes; }

    void Clear();
    virtual void Print() const override;

private:
    std::unordered_map<int, double> fEdepPerVolume;
    std::unordered_map<int, std::tuple<double, double, int>> fIsotopes;
};

#endif