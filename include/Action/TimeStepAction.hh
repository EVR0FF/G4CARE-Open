#ifndef TIME_STEP_ACTION_HH
#define TIME_STEP_ACTION_HH

//==============================================================================
//
// G4CARE
//
// @file    TimeStepAction.hh
// @brief   Chemical stage time-step management for Geant4-DNA.
//
// @details
//   Required component for G4Scheduler — without it the scheduler does
//   not know the integration step size for the chemical stage.
//   Integrates GValueScorer to compute G(t) at each time step.
//
//   This component uses the Geant4-DNA extension developed by the Geant4
//   Collaboration.
//
//   Configuration keys read:
//     CHEMISTRY.MAX_TIME_STEP
//     CHEMISTRY.G_VALUE.ENABLE
//     CHEMISTRY.G_VALUE.SPECIES
//     CHEMISTRY.G_VALUE.N_OF_TIME_BINS
//     CHEMISTRY.G_VALUE.TIME_BINS
//     CHEMISTRY.START_TIME
//     CHEMISTRY.END_TIME
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

#include "G4UserTimeStepAction.hh"
#include "G4Track.hh"
#include <vector>
#include <memory>

class GValueScorer;

//------------------------------------------------------------------------------
/// @class TimeStepAction
/// @brief Manages chemical stage time steps and G-Value scoring.
///
/// @details
/// Sets up time-step intervals based on CHEMISTRY.MAX_TIME_STEP,
/// and optionally initialises GValueScorer for tracking chemical
/// yields over time.
///
/// Design based on the official chem3 example (Geant4-DNA).
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class TimeStepAction : public G4UserTimeStepAction
{
public:
    /// @brief Constructor. Reads CHEMISTRY.MAX_TIME_STEP and G_VALUE config.
    TimeStepAction();

    /// @brief Destructor. Defined in .cc (needs complete type of GValueScorer).
    ~TimeStepAction() override;

    /// @brief Called after each time step. Triggers GValueScorer snapshot.
    void UserPostTimeStepAction() override;

    /// @brief Called after each chemical reaction.
    ///
    /// @param reactantA First reactant track.
    /// @param reactantB Second reactant track.
    /// @param products  Vector of product tracks.
    void UserReactionAction(const G4Track& reactantA,
                            const G4Track& reactantB,
                            const std::vector<G4Track*>* products) override;

    /// @brief Set GValueScorer (takes ownership).
    ///
    /// @param scorer Raw pointer to GValueScorer.
    void SetGValueScorer(GValueScorer* scorer);

    /// @brief Get the current GValueScorer.
    ///
    /// @return Pointer to GValueScorer, or nullptr.
    GValueScorer* GetGValueScorer() const { return fGValueScorer.get(); }

private:
    std::unique_ptr<GValueScorer> fGValueScorer;
};

#endif // TIME_STEP_ACTION_HH