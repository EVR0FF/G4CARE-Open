#ifndef IT_STEPPING_ACTION_HH
#define IT_STEPPING_ACTION_HH

//==============================================================================
//
// G4CARE
//
// @file    ITSteppingAction.hh
// @brief   Stepping action for chemical molecule tracks (G4IT).
//
// @details
//   G4IT tracks (radicals OH, e_aq, H, etc.) are NOT processed by
//   the standard G4UserSteppingAction. This class is called from
//   ITTrackingInteractivity::AppendStep for each chemical track step.
//   Increments thread-local counters of chemical species via
//   ChemistryExtractor::GlobalAccumulate().
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

#include "G4UserSteppingAction.hh"
#include <cstddef>

class G4Step;

//------------------------------------------------------------------------------
/// @class ITSteppingAction
/// @brief Stepping action for chemical (G4IT) tracks. Accumulates species
///        counters per step.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class ITSteppingAction : public G4UserSteppingAction
{
public:
    ITSteppingAction();
    ~ITSteppingAction() override = default;

    /// @brief Called for each step of a chemical track.
    void UserSteppingAction(const G4Step*) override;
};

#endif // IT_STEPPING_ACTION_HH