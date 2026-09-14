//==============================================================================
// G4CARE
// @file    ParticleExtractor.hh
// @brief   Extracts particle properties (PDG code, mass, charge, name, spin,
//          parity, quantum numbers, lifetimes, ion Z/A/excitation) via
//          G4ParticleDefinition and TypedRegistries.
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
#include "TypedRegistry.hh"

/// @brief Extracts PDG, mass, charge, quantum numbers, and ion properties.
class ParticleExtractor {
public:
    ParticleExtractor(TypedRegistry<std::string>* particleNameReg = nullptr,
                      TypedRegistry<int>* pdgReg = nullptr,
                      TypedRegistry<double>* massReg = nullptr,
                      TypedRegistry<double>* chargeReg = nullptr)
        : fParticleNameReg(particleNameReg),
          fPDGReg(pdgReg),
          fMassReg(massReg),
          fChargeReg(chargeReg) {}

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;

private:
    TypedRegistry<std::string>* fParticleNameReg;
    TypedRegistry<int>* fPDGReg;
    TypedRegistry<double>* fMassReg;
    TypedRegistry<double>* fChargeReg;
};

#endif // PARTICLE_EXTRACTOR_HH