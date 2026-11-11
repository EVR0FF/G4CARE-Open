//==============================================================================
// G4CARE
// @file    ParticleExtractor.hh
// @brief   Extracts particle properties (PDG code, mass, charge, name, spin,
//          parity, quantum numbers, lifetimes, ion Z/A/excitation) via
//          G4ParticleDefinition.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef PARTICLE_EXTRACTOR_HH
#define PARTICLE_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"

/// @brief Extracts PDG, mass, charge, quantum numbers, and ion properties.
class ParticleExtractor {
public:
    ParticleExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;
};

#endif // PARTICLE_EXTRACTOR_HH
