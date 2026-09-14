//==============================================================================
//
// G4CARE
//
// @file    SteppingAction.hh
// @brief   Per-step action: energy deposition accumulation, dose control,
//          and ExprTK expression execution.
//
// @details
//   Implements G4UserSteppingAction.  On each Geant4 step it:
//   - Accumulates energy deposition in chemistry volumes for dose
//     monitoring (global atomic accumulator sAccumulatedEnergyJ).
//   - Fills the "g4care" NTuple via BeamAnalysis.
//   - If EXPRS blocks exist, builds FilterVars and executes step-level
//     ExprTK scripts via ExprsManager.
//
//   At event end, AccumulateEventEnergy() atomically merges the
//   per-event (thread-local) energy into the global counter and
//   checks the dose threshold.
//
//   Configuration keys read:
//     CHEMISTRY.VOLUME
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

#ifndef STEPPING_ACTION_HH
#define STEPPING_ACTION_HH

#include "G4UserSteppingAction.hh"
#include <atomic>
#include <string>
#include <unordered_set>

class GeometryManager;

/// @brief Per-step action for energy deposition, dose control, and ExprTK
///        script execution.
///
/// Accumulates energy deposit per chemistry volume (CHEMISTRY.VOLUME),
/// fills NTuple data, and executes step-level ExprTK scripts.
/// Uses lock-free atomic accumulators for multi-threaded safety.
class SteppingAction : public G4UserSteppingAction {
public:
    SteppingAction();
    ~SteppingAction() override = default;

    /// @brief Geant4 callback executed on every simulation step.
    /// @param step Pointer to the current G4Step.
    void UserSteppingAction(const G4Step* step) override;

    /// @brief Transfer thread-local event energy to the global atomic
    ///        accumulator and check dose threshold.
    ///
    /// Called from EventAction::EndOfEventAction().  Aborts the run
    /// via G4RunManager if the cumulative dose exceeds the threshold.
    static void AccumulateEventEnergy();

    /// @brief Initialise MAX_DOSE control parameters and chemistry volume list.
    ///
    /// Reads CHEMISTRY.VOLUME from the configuration and populates
    /// sChemVolumeNames.  Called from RunAction::BeginOfRunAction().
    /// @param gm Pointer to GeometryManager (used for target-mass lookup).
    static void InitializeDoseControl(GeometryManager* gm);

    /// @brief Current accumulated dose in the sensitive volume (Gy).
    /// @return Dose in Gray = total energy (J) / target mass (kg).
    static double GetCumulativeDoseGy();

    /// @brief Total mass of water volumes registered for chemistry (kg).
    /// @return Water mass computed during initialisation.
    static double GetWaterMassKg() { return sWaterMassKg.load(std::memory_order_relaxed); }

    /// @brief Set of logical volume names registered for chemistry EDEP.
    /// @return Reference to the static unordered_set of volume names.
    static const std::unordered_set<std::string>& GetChemVolumeNames() { return sChemVolumeNames; }

private:
    // Global lock-free energy accumulator (Joules)
    static std::atomic<double> sAccumulatedEnergyJ;
    static double              sDoseThresholdGy;
    static double              sTargetMassKg;
    static std::string         sSensitiveVolumeName;
    static std::atomic<bool>   sEnabled;

    // Chemistry volumes (for G-factor scoring): set of names and enable flag
    static std::unordered_set<std::string> sChemVolumeNames;
    static bool                sChemEnabled;
    static std::atomic<double> sWaterMassKg;
};

#endif