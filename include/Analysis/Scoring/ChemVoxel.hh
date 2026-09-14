//==============================================================================
// G4CARE
// @file    ChemVoxel.hh
// @brief   Voxel structure for chemical scoring: accumulates per-species
//          molecule counts and energy deposit.
// @details ChemVoxel holds a vector of species counts (indexed by species ID)
//   and a total energy deposit.  Used by ChemistryExtractor for global
//   thread-local accumulation and G-value calculation.
//
//   Configuration keys read: none (data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CHEM_VOXEL_HH
#define CHEM_VOXEL_HH

#include <vector>
#include <cstddef>
#include <algorithm>

/// @brief Voxel for chemical scoring: accumulates per-species molecule counts
///        and energy deposit.
struct ChemVoxel {
    // Molecule counts per species in the voxel
    std::vector<double> counts;

    // Energy deposit in the voxel (sum over all particles)
    double energyDeposit = 0.0;

    ChemVoxel() = default;

    /// @param numSpecies Total number of known species in the registry.
    explicit ChemVoxel(size_t numSpecies)
        : counts(numSpecies, 0.0) {}

    /// Increment the counter for speciesIndex by 1.
    void Increment(size_t speciesIndex) {
        if (speciesIndex < counts.size()) {
            counts[speciesIndex] += 1.0;
        }
    }

    /// Reset all counters and energy deposit to zero.
    void Reset() {
        std::fill(counts.begin(), counts.end(), 0.0);
        energyDeposit = 0.0;
    }

    /// Total number of molecules in the voxel.
    double TotalCount() const {
        double sum = 0.0;
        for (double c : counts) sum += c;
        return sum;
    }

    /// Access counter by index.
    double operator[](size_t i) const {
        return (i < counts.size()) ? counts[i] : 0.0;
    }
};

#endif // CHEM_VOXEL_HH
