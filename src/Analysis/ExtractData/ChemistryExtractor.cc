//==============================================================================
// G4CARE
// @file    ChemistryExtractor.cc
// @brief   Extracts chemistry-related quantities (species counts, G-factors,
//          radical yields) from Geant4-DNA chemistry tracks using a lock-free,
//          thread-local global accumulator pool.
// @details ChemistryExtractor provides a global, per-thread accumulation pool
//   for chemical species.  Each worker thread writes only to its own slot
//   (indexed by Geant4 thread ID), eliminating the need for mutexes, atomics,
//   or barriers.  InitGlobalAccumulator is called by the master thread in
//   BeginOfRunAction, which Geant4 guarantees to complete before any worker
//   EndOfEventAction.  GlobalMerge is called by the master in EndOfRunAction,
//   after all workers have finished.
//
//   Configuration keys read: none (registry-driven).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ChemistryExtractor.hh"
#include "ChemSpeciesRegistry.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include <string>
#include <vector>
#include <memory>

// ──── Global thread-local accumulator pool ────
// Each thread writes ONLY to its own slot (by threadID) — no races.
// InitGlobalAccumulator is called ONLY in the master's BeginOfRunAction,
// which Geant4 guarantees to execute BEFORE worker threads are launched.
// No mutexes, atomics, or locks required.
namespace {
    std::vector<ChemVoxel>* gGlobalAccum = nullptr;   // [threadID]
    size_t gNumSlots = 0;
    size_t gNumSpecies = 0;
}

/// @brief Initializes the global accumulator pool with `nThreads` slots,
///        each holding a ChemVoxel of `nSpecies` dimensions.
/// @param nSpecies Number of chemical species to track.
/// @param nThreads Number of Geant4 worker threads (or 1 for sequential).
void ChemistryExtractor::InitGlobalAccumulator(size_t nSpecies, size_t nThreads) {
    // Called ONLY by the master in BeginOfRunAction.
    // Geant4 guarantee: master completes BeginOfRunAction before the first
    // EndOfEvent of any worker.  Synchronization barrier at the framework level.
    // Therefore no synchronization primitives are needed.
    if (gGlobalAccum) {
        if (gNumSpecies == nSpecies && gNumSlots >= nThreads) {
            return; // Already initialized, size OK
        }
        delete gGlobalAccum;
        gGlobalAccum = nullptr;
    }
    gNumSpecies = nSpecies;
    gNumSlots = nThreads;
    gGlobalAccum = new std::vector<ChemVoxel>(nThreads, ChemVoxel(nSpecies));
}

/// @brief Accumulates energy deposition into the current thread's slot.
/// @param edep Energy deposited (internal Geant4 units).
void ChemistryExtractor::GlobalAccumulateEdep(double edep) {
    if (!gGlobalAccum) return;
    G4int rawTid = G4Threading::G4GetThreadId();
    // In single-threaded mode (G4RunManager, VISUALIZATION=true)
    // G4GetThreadId() returns -1.  Use slot 0.
    size_t tid = (rawTid < 0) ? 0 : static_cast<size_t>(rawTid);
    if (tid >= gNumSlots) return;
    (*gGlobalAccum)[tid].energyDeposit += edep;
}

/// @brief Accumulates a full ChemVoxel into the current thread's slot.
/// @param voxel ChemVoxel with species counts and energy deposition.
void ChemistryExtractor::GlobalAccumulate(const ChemVoxel& voxel) {
    // Each thread writes ONLY to its own slot [threadID] — no races.
    if (!gGlobalAccum) return;
    G4int rawTid = G4Threading::G4GetThreadId();
    size_t tid = (rawTid < 0) ? 0 : static_cast<size_t>(rawTid);
    if (tid >= gNumSlots) return;

    ChemVoxel& slot = (*gGlobalAccum)[tid];

    if (slot.counts.size() < voxel.counts.size()) {
        slot.counts.resize(voxel.counts.size(), 0.0);
    }

    size_t n = std::min(slot.counts.size(), voxel.counts.size());
    for (size_t i = 0; i < n; ++i) {
        slot.counts[i] += voxel.counts[i];
    }
    slot.energyDeposit += voxel.energyDeposit;
}

/// @brief Merges all thread-local accumulation slots into a single output
///        ChemVoxel.  Called ONLY by the master in EndOfRunAction.
/// @param[out] out Merged ChemVoxel (will be resized if needed).
void ChemistryExtractor::GlobalMerge(ChemVoxel& out) {
    // Called ONLY by the master in EndOfRunAction.
    // All workers have already finished — accessing slots is safe.
    if (!gGlobalAccum) return;

    if (out.counts.size() < gNumSpecies) {
        out.counts.resize(gNumSpecies, 0.0);
    }

    for (size_t tid = 0; tid < gNumSlots; ++tid) {
        const ChemVoxel& slot = (*gGlobalAccum)[tid];
        size_t n = std::min(out.counts.size(), slot.counts.size());
        for (size_t i = 0; i < n; ++i) {
            out.counts[i] += slot.counts[i];
        }
        out.energyDeposit += slot.energyDeposit;
    }
}

/// @brief Resets all thread-local slots to zero.  Called ONLY by the master
///        in BeginOfRunAction.
void ChemistryExtractor::GlobalReset() {
    // Called ONLY by the master in BeginOfRunAction.
    if (!gGlobalAccum) return;
    for (auto& slot : *gGlobalAccum) {
        slot.Reset();
    }
}

/// @brief Returns the number of slots in the global accumulator pool.
/// @return Number of thread slots, or 0 if not yet initialized.
size_t ChemistryExtractor::GlobalPoolSize() {
    return gGlobalAccum ? gNumSlots : 0;
}

// ──── Instance constructor / methods ────

/// @brief Constructs the extractor and pre-allocates the per-instance
///        accumulation ChemVoxel based on the registry size.
/// @param registry Chemical species registry (may be nullptr).
ChemistryExtractor::ChemistryExtractor(ChemSpeciesRegistry* registry)
    : fRegistry(registry)
{
    if (fRegistry) {
        fAccumulated = ChemVoxel(fRegistry->Size());
    }
}

/// @brief Accumulates species counts into the per-instance ChemVoxel.
/// @param counters ChemVoxel with counts to add.
void ChemistryExtractor::Accumulate(const ChemVoxel& counters) {
    size_t n = std::min(fAccumulated.counts.size(), counters.counts.size());
    for (size_t i = 0; i < n; ++i)
        fAccumulated.counts[i] += counters.counts[i];
    fAccumulated.energyDeposit += counters.energyDeposit;
}

/// @brief Resets the per-instance accumulation ChemVoxel to zero.
void ChemistryExtractor::ResetAccumulation() {
    fAccumulated.Reset();
}

/// @brief Re-allocates the per-instance ChemVoxel to match the current
///        registry size.
void ChemistryExtractor::Reinitialize() {
    if (fRegistry) {
        fAccumulated = ChemVoxel(fRegistry->Size());
    }
}

/// @brief Returns a const reference to the accumulated per-instance ChemVoxel.
/// @return Const reference to the accumulated ChemVoxel.
const ChemVoxel& ChemistryExtractor::GetAccumulated() const {
    return fAccumulated;
}

/// @brief Overwrites the per-instance ChemVoxel with the given value.
/// @param v ChemVoxel to copy.
void ChemistryExtractor::SetAccumulated(const ChemVoxel& v) {
    fAccumulated = v;
}

/// @brief Checks whether a unified source belongs to a given chemical species.
/// @param src  Unified source with a chemical species name.
/// @param name Species name to test.
/// @return true if the source's species name matches.
bool ChemistryExtractor::IsSpecies(const UnifiedSource& src, const std::string& name) {
    return src.GetChemicalSpeciesName() == name;
}

/// @brief Extracts a chemistry column value (radical indicator, species ID,
///        or G-factor) for a given source.
/// @param type Column type to extract.
/// @param src  Unified source providing the species name.
/// @return Extracted value, or 0.0 if not applicable.
double ChemistryExtractor::Get(ColType type, const UnifiedSource& src) const {
    switch (type) {
    case ColType::RadicalOH:
        return IsSpecies(src, "OH") ? 1.0 : 0.0;
    case ColType::RadicalH:
        return IsSpecies(src, "H") ? 1.0 : 0.0;
    case ColType::RadicalEaq:
        return IsSpecies(src, "e_aq") ? 1.0 : 0.0;
    case ColType::H2O2:
        return IsSpecies(src, "H2O2") ? 1.0 : 0.0;
    case ColType::H2:
        return IsSpecies(src, "H2") ? 1.0 : 0.0;

    case ColType::SpeciesID: {
        if (!fRegistry) return 0.0;
        std::string name = src.GetChemicalSpeciesName();
        if (name.empty()) return 0.0;
        return static_cast<double>(fRegistry->GetID(name));
    }

    case ColType::GOH: {
        if (!fRegistry) return 0.0;
        int idx = fRegistry->GetID("OH");
        if (idx < 0 || fAccumulated.energyDeposit <= 0.0) return 0.0;
        double N = fAccumulated.counts[static_cast<size_t>(idx)];
        double Edep_eV = fAccumulated.energyDeposit / CLHEP::eV;
        return (N / Edep_eV) * 100.0;
    }
    case ColType::GH: {
        if (!fRegistry) return 0.0;
        int idx = fRegistry->GetID("H");
        if (idx < 0 || fAccumulated.energyDeposit <= 0.0) return 0.0;
        double N = fAccumulated.counts[static_cast<size_t>(idx)];
        double Edep_eV = fAccumulated.energyDeposit / CLHEP::eV;
        return (N / Edep_eV) * 100.0;
    }
    case ColType::GEaq: {
        if (!fRegistry) return 0.0;
        int idx = fRegistry->GetID("e_aq");
        if (idx < 0 || fAccumulated.energyDeposit <= 0.0) return 0.0;
        double N = fAccumulated.counts[static_cast<size_t>(idx)];
        double Edep_eV = fAccumulated.energyDeposit / CLHEP::eV;
        return (N / Edep_eV) * 100.0;
    }

    case ColType::WaterLoss:
    case ColType::PorosityChange:
    case ColType::CompressiveStrengthLoss:
        return 0.0;

    default:
        return 0.0;
    }
}

/// @brief Calculates the G-factor (molecules per 100 eV) for a given species.
/// @param speciesIndex Index of the species in the registry.
/// @return G-factor, or 0.0 if index is out of range or no energy deposited.
double ChemistryExtractor::CalculateGFactor(int speciesIndex) const {
    if (!fRegistry || speciesIndex < 0) return 0.0;
    if (static_cast<size_t>(speciesIndex) >= fAccumulated.counts.size()) return 0.0;
    if (fAccumulated.energyDeposit <= 0.0) return 0.0;
    double N = fAccumulated.counts[static_cast<size_t>(speciesIndex)];
    double Edep_eV = fAccumulated.energyDeposit / CLHEP::eV;
    return (N / Edep_eV) * 100.0;
}

/// @brief Returns the number of species tracked in the registry.
/// @return Number of species, or 0 if registry is null.
size_t ChemistryExtractor::GetNumSpecies() const {
    if (!fRegistry) return 0;
    return fRegistry->Size();
}

/// @brief Returns the name of a species by its registry index.
/// @param speciesIndex Index of the species.
/// @return Species name, or empty string if index is out of range.
std::string ChemistryExtractor::GetSpeciesName(int speciesIndex) const {
    if (!fRegistry || speciesIndex < 0) return "";
    if (static_cast<size_t>(speciesIndex) >= fRegistry->Size()) return "";
    return fRegistry->GetValue(speciesIndex);
}