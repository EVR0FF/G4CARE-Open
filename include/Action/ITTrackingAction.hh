#ifndef IT_TRACKING_ACTION_HH
#define IT_TRACKING_ACTION_HH

//==============================================================================
//
// G4CARE
//
// @file    ITTrackingAction.hh
// @brief   Tracking action for chemical molecule tracks (G4IT).
//
// @details
//   Chemical molecules (radicals OH, e_aq, H, etc.) are NOT processed by
//   the standard G4UserTrackingAction. This class is called from
//   ITTrackingInteractivity for each G4IT track start/end.
//
//   PreUserTrackingAction: determines the chemical species index via
//   ChemSpeciesRegistry and increments TLS counters through
//   ChemistryExtractor.
//
//   PostUserTrackingAction: currently unused (counter already incremented
//   in Pre).
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

#include "G4UserTrackingAction.hh"

class G4Track;

//------------------------------------------------------------------------------
/// @class ITTrackingAction
/// @brief Tracking action for chemical (G4IT) tracks. Increments species
///        counters at track start.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class ITTrackingAction : public G4UserTrackingAction
{
public:
    ITTrackingAction();
    ~ITTrackingAction() override = default;

    /// @brief Called when a chemical track starts. Determines species index
    ///        and increments TLS counter.
    void PreUserTrackingAction(const G4Track*) override;

    /// @brief Called when a chemical track ends. Currently a no-op.
    void PostUserTrackingAction(const G4Track*) override;
};

#endif // IT_TRACKING_ACTION_HH