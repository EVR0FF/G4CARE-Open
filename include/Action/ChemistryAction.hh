#ifndef CHEMISTRY_ACTION_HH
#define CHEMISTRY_ACTION_HH

//==============================================================================
//
// G4CARE
//
// @file    ChemistryAction.hh
// @brief   Run-begin/end actions for the Geant4-DNA chemistry stage.
//
// @details
//   ChemistryAction configures G4DNAChemistryManager via UI commands:
//   loads the chemical species list, selects the time-step model,
//   sets environment parameters (temperature, pressure, density) and
//   reaction rate constants. Populates ChemSpeciesRegistry in all threads.
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

#include "G4UserRunAction.hh"

//------------------------------------------------------------------------------
/// @class ChemistryAction
/// @brief Manages Geant4-DNA chemistry initialization during a run.
///
/// @details
/// Configures G4DNAChemistryManager through UI commands. Performs
/// chemical species registration in worker threads and applies
/// chemistry parameters (time-step model, temperature, pressure,
/// density, time limits, reaction rates) in the master thread.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class ChemistryAction : public G4UserRunAction {
public:
    ChemistryAction() = default;
    virtual ~ChemistryAction() = default;

    /// @brief Initializes Geant4-DNA chemistry at the beginning of a run.
    ///
    /// Loads species from CHEMISTRY.SPECIES (or CHEMISTRY.CHEM_LIST),
    /// registers them in ChemSpeciesRegistry (all threads), and applies
    /// /chem/... UI commands (master only).
    ///
    /// @param run Pointer to the current Geant4 run.
    virtual void BeginOfRunAction(const G4Run* run) override;

    /// @brief Finalizes Geant4-DNA chemistry after a run.
    ///
    /// Reserved for future use. May save chemistry state via
    /// G4DNAChemistryManager::WriteInto().
    ///
    /// @param run Pointer to the current Geant4 run.
    virtual void EndOfRunAction(const G4Run* run) override;
};

#endif