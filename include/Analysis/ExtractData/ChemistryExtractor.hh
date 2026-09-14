#ifndef CHEMISTRY_EXTRACTOR_HH
#define CHEMISTRY_EXTRACTOR_HH

//==============================================================================
//
// G4CARE
//
// @file    ChemistryExtractor.hh
// @brief   Extracts chemistry data from steps/tracks and manages thread-safe
//          global accumulation of species counters.
//
// @details
//   Provides extraction of chemical species data (SpeciesID, G-factors,
//   etc.) from UnifiedSource objects. Manages a lock-free multi-threaded
//   global accumulator: each thread writes to its own slot, master merges
//   all slots in EndOfRunAction.
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

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "ChemSpeciesRegistry.hh"
#include "ChemVoxel.hh"

#include <vector>
#include <string>

//------------------------------------------------------------------------------
/// @class ChemistryExtractor
/// @brief Extracts chemistry data and manages thread-safe global accumulation
///        of species counters and energy deposits.
///
/// @note This component uses the Geant4-DNA extension.
//------------------------------------------------------------------------------
class ChemistryExtractor {
public:
    /// @brief Constructor.
    /// @param registry  ChemSpeciesRegistry (may be nullptr — then SpeciesID always 0).
    explicit ChemistryExtractor(ChemSpeciesRegistry* registry = nullptr);
    ~ChemistryExtractor() = default;

    /// @brief Extract a chemistry-related value from a UnifiedSource.
    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;

    /// @brief Accumulate species counters from a ChemVoxel (called in EndOfEvent).
    ///        Thread-safe: each thread accumulates in its own slot.
    void Accumulate(const ChemVoxel& counters);

    /// @brief Reset accumulated counters (called in BeginOfRun).
    void ResetAccumulation();

    /// @brief Reinitialize internal accumulator for the current registry size.
    ///        Called in BeginOfRunAction after species registration.
    void Reinitialize();

    /// @brief Get accumulated data (for NTuple output at EndOfRun).
    const ChemVoxel& GetAccumulated() const;

    /// @brief Set accumulated data (used by master after GlobalMerge).
    void SetAccumulated(const ChemVoxel& v);

    /// @brief Prepare G-factor calculation (no-op: computed on-the-fly).
    void CalculateAllGFactors() { /* G-factors computed on-the-fly */ }

    /// @brief Get G-factor for a species (computed on-the-fly).
    [[nodiscard]] double GetGFactor(int speciesIndex) const {
        return CalculateGFactor(speciesIndex);
    }

    /// @brief Calculate G-factor: G = N_species / E_dep(eV) * 100 eV.
    ///        Returns 0.0 if index is invalid or energy is zero.
    [[nodiscard]] double CalculateGFactor(int speciesIndex) const;

    /// @brief Number of registered chemical species.
    [[nodiscard]] size_t GetNumSpecies() const;

    /// @brief Species name by index (for NTuple output).
    [[nodiscard]] std::string GetSpeciesName(int speciesIndex) const;

    // ── Static methods for global multi-threaded aggregation ──

    /// @brief Initialize the global thread-local pool (called ONCE).
    /// @param nSpecies  Number of species in the registry.
    /// @param nThreads  Maximum number of threads.
    static void InitGlobalAccumulator(size_t nSpecies, size_t nThreads);

    /// @brief Accumulate a ChemVoxel in the thread-local slot (worker thread).
    ///        Each thread writes ONLY to its own slot — lock-free.
    static void GlobalAccumulate(const ChemVoxel& voxel);

    /// @brief Accumulate energy deposit in the thread-local slot.
    ///        Used to collect E_dep from physical particles in the chemistry volume.
    static void GlobalAccumulateEdep(double edep);

    /// @brief Merge all thread-local slots into a result ChemVoxel.
    ///        Called ONLY by the master thread in EndOfRunAction.
    static void GlobalMerge(ChemVoxel& out);

    /// @brief Reset all thread-local slots (called by master in BeginOfRunAction).
    static void GlobalReset();

    /// @brief Size of the global pool (0 = not initialized).
    static size_t GlobalPoolSize();

private:
    ChemSpeciesRegistry* fRegistry = nullptr;
    ChemVoxel fAccumulated;   // local accumulation (used for Get() on-the-fly)

    /// @brief Check whether a particle name matches a given chemical signature.
    static bool IsSpecies(const UnifiedSource& src, const std::string& name);
};

#endif