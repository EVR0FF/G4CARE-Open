//==============================================================================
// G4CARE
// @file    ParticleExtractor.cc
// @brief   Extracts particle-related column values (PDG code, mass, charge,
//          name, spin, parity, lifetimes, ion Z/A/excitation/isomer) from
//          G4ParticleDefinition.
// @details ParticleExtractor resolves particle properties either via
//   TypedRegistries (PDG, mass, charge, particle name) or directly from the
//   G4ParticleDefinition.  Ion-specific columns (Z, A, excitation energy,
//   isomer level) are obtained from the UnifiedSource.
//
//   Configuration keys read: none (registry-driven).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ParticleExtractor.hh"
#include "G4SystemOfUnits.hh"
#include "G4Ions.hh"

/// @brief Extracts a numeric particle column value for the given source.
/// @param type Column type (PDGCode, Mass, Charge, ParticleName-as-ID, Spin,
///        Parity, Conjugation, Isospin, Isospin3, GParity, Lifetime, Width,
///        LeptonNumber, BaryonNumber, Z, A, Excitation, IsomerLevel).
/// @param src  UnifiedSource providing G4ParticleDefinition.
/// @return Extracted value, or 0.0 if not available.
double ParticleExtractor::Get(ColType type, const UnifiedSource& src) const {
    auto* def = src.GetParticleDefinition();
    if (!def) return 0.0;

    switch (type) {
        case ColType::PDGCode:
            if (fPDGReg) return static_cast<double>(fPDGReg->GetID(def->GetPDGEncoding()));
            return static_cast<double>(def->GetPDGEncoding());

        case ColType::Mass:
            if (fMassReg) return static_cast<double>(fMassReg->GetID(def->GetPDGMass()));
            return def->GetPDGMass() / MeV;

        case ColType::Charge:
            if (fChargeReg) return static_cast<double>(fChargeReg->GetID(def->GetPDGCharge()));
            return def->GetPDGCharge();

        case ColType::ParticleName:
            if (fParticleNameReg) {
                return static_cast<double>(fParticleNameReg->GetID(def->GetParticleName()));
            }
            return 0.0;

        case ColType::ParticleSpin:
            return static_cast<double>(def->GetPDGiSpin());

        case ColType::ParticleParity:
            return static_cast<double>(def->GetPDGiParity());

        case ColType::ParticleConjugation:
            return static_cast<double>(def->GetPDGiConjugation());

        case ColType::ParticleIsospin:
            return static_cast<double>(def->GetPDGiIsospin());

        case ColType::ParticleIsospin3:
            return static_cast<double>(def->GetPDGiIsospin3());

        case ColType::ParticleGParity:
            return static_cast<double>(def->GetPDGiGParity());

        case ColType::ParticleLifetime:
            return def->GetPDGLifeTime() / second;

        case ColType::ParticleWidth:
            return def->GetPDGWidth() / MeV;

        case ColType::ParticleLeptonNumber:
            return static_cast<double>(def->GetLeptonNumber());

        case ColType::ParticleBaryonNumber:
            return static_cast<double>(def->GetBaryonNumber());

        // Ions
        case ColType::Z:
            return static_cast<double>(src.GetAtomicNumber());

        case ColType::A:
            return static_cast<double>(src.GetAtomicMass());

        case ColType::Excitation:
            return src.GetExcitationEnergy() / MeV;

        case ColType::IsomerLevel:
            return static_cast<double>(src.GetIsomerLevel());

        default:
            return 0.0;
    }
}

/// @brief Extracts a string particle column value (ParticleName) from the source.
/// @param type Column type.
/// @param src  UnifiedSource providing G4ParticleDefinition.
/// @return Particle name, or empty string if not available.
std::string ParticleExtractor::GetString(ColType type, const UnifiedSource& src) const {
    auto* def = src.GetParticleDefinition();
    if (!def) return "";

    switch (type) {
        case ColType::ParticleName:
            return def->GetParticleName();
        default:
            return "";
    }
}