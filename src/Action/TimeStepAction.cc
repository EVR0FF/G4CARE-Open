//==============================================================================
//
// G4CARE
//
// @file    TimeStepAction.cc
// @brief   Chemical stage time-step management for Geant4-DNA.
//
// @details
//   Implements G4UserTimeStepAction to control the integration step
//   size during the Geant4-DNA chemical stage. Reads MAX_TIME_STEP
//   to override default intervals, and optionally initialises
//   GValueScorer from CHEMISTRY.G_VALUE.* config for tracking
//   chemical yields over time.
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

#include "TimeStepAction.hh"
#include "Analysis/Scoring/GValueScorer.hh"
#include "ConfigManager.hh"
#include "G4Scheduler.hh"
#include "G4SystemOfUnits.hh"
#include "G4UnitsTable.hh"

TimeStepAction::~TimeStepAction() = default;

//------------------------------------------------------------------------------
// @brief Constructor. Sets time-step intervals and initialises GValueScorer.
//------------------------------------------------------------------------------
TimeStepAction::TimeStepAction()
    : G4UserTimeStepAction()
{
    double minStep = ConfigManager::Instance()->GetValueWithUnits("CHEMISTRY.MAX_TIME_STEP", 0.0);

    if (minStep > 0.0) {
        AddTimeStep(1 * picosecond, minStep);
        AddTimeStep(10 * picosecond, minStep);
        AddTimeStep(100 * picosecond, minStep);
        AddTimeStep(1000 * picosecond, minStep);
        AddTimeStep(10000 * picosecond, minStep);
    } else {
        AddTimeStep(1 * picosecond, 0.1 * picosecond);
        AddTimeStep(10 * picosecond, 1 * picosecond);
        AddTimeStep(100 * picosecond, 3 * picosecond);
        AddTimeStep(1000 * picosecond, 10 * picosecond);
        AddTimeStep(10000 * picosecond, 100 * picosecond);
    }

    // ── GValueScorer initialisation from YAML config ──
    auto* cfg = ConfigManager::Instance();
    bool gValueEnabled = cfg->GetBool("CHEMISTRY.G_VALUE.ENABLE", false);
    if (gValueEnabled) {
        auto gSpecies = cfg->GetStringVector("CHEMISTRY.G_VALUE.SPECIES");
        if (gSpecies.empty()) {
            gSpecies = cfg->GetStringVector("CHEMISTRY.SPECIES");
        }

        std::vector<G4double> timeBins;
        G4int nOfTimeBins = cfg->GetInt("CHEMISTRY.G_VALUE.N_OF_TIME_BINS", 0);
        if (nOfTimeBins > 0) {
            G4double tStart = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * CLHEP::ps);
            G4double tEnd   = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
            timeBins = GValueScorer::GenerateLogBins(tStart, tEnd, nOfTimeBins);
        } else {
            auto binStrings = cfg->GetStringVector("CHEMISTRY.G_VALUE.TIME_BINS");
            if (binStrings.empty()) {
                G4double tStart = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * CLHEP::ps);
                G4double tEnd   = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
                timeBins = GValueScorer::GenerateLogBins(tStart, tEnd, 50);
            } else {
                for (const auto& s : binStrings) {
                    timeBins.push_back(G4UnitDefinition::GetValueOf(s));
                }
            }
        }

        if (!gSpecies.empty() && !timeBins.empty()) {
            fGValueScorer.reset(new GValueScorer(gSpecies, timeBins, nullptr));
            G4cout << "[TimeStepAction] GValueScorer initialised: "
                   << gSpecies.size() << " species × "
                   << timeBins.size() << " time bins" << G4endl;
        }
    }
}

//------------------------------------------------------------------------------
// @brief Sets the GValueScorer (takes ownership).
//
// @param scorer  Raw pointer to GValueScorer.
//------------------------------------------------------------------------------
void TimeStepAction::SetGValueScorer(GValueScorer* scorer) {
    fGValueScorer.reset(scorer);
}

//------------------------------------------------------------------------------
// @brief Called after each chemical time step. Snapshots G-Values.
//------------------------------------------------------------------------------
void TimeStepAction::UserPostTimeStepAction()
{
    if (fGValueScorer) {
        G4double currentTime = G4Scheduler::Instance()->GetGlobalTime();
        fGValueScorer->SnapshotAt(currentTime);
    }
}

//------------------------------------------------------------------------------
// @brief Called after each chemical reaction. Currently a no-op.
//
// @param reactantA  First reactant track.
// @param reactantB  Second reactant track.
// @param products   Vector of product tracks.
//------------------------------------------------------------------------------
void TimeStepAction::UserReactionAction(const G4Track&,
                                         const G4Track&,
                                         const std::vector<G4Track*>*)
{
}