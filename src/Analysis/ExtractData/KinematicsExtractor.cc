//==============================================================================
// G4CARE
// @file    KinematicsExtractor.cc
// @brief   Extracts kinematic quantities (energy, momentum, position, time,
//          beta, pseudorapidity, scattering angle, polarization) from Geant4
//          step and track data.
// @details KinematicsExtractor handles basic kinematic columns (kinetic / total
//   energy, mass, charge), pre-/post-step position and momentum components,
//   global/local/proper time, transverse momentum, pseudorapidity, beta,
//   scattering angle, and polarization.  Beta is computed differently for
//   Hit, Secondary, and Primary source types using velocity or momentum/mass.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "KinematicsExtractor.hh"
#include "G4SystemOfUnits.hh"
#include "G4StepPoint.hh"
#include "G4PhysicalConstants.hh"

/// @brief Extracts a kinematic column value from the given source.
/// @param type Column type (KineticEnergy, TotalEnergy, Mass, Charge,
///        PreKineticEnergy, PosX/Y/Z, PrePosX/Y/Z, PostPosX/Y/Z, GlobalTime,
///        LocalTime, ProperTime, Px/Py/Pz, DirX/Y/Z, PrePx/Py/Pz, PostPx/Py/Pz,
///        Pt, Pseudorapidity, Beta, ScatteringAngle, PolX/Y/Z).
/// @param src  UnifiedSource providing track/step data.
/// @return Extracted value in common units (MeV, mm, ns, rad), or 0.0 if not available.
double KinematicsExtractor::Get(ColType type, const UnifiedSource& src) const {
    switch (type) {
        // ========== Basic kinematic ==========
        case ColType::KineticEnergy:
            return src.GetKineticEnergy() / MeV;

        case ColType::TotalEnergy:
            return (src.GetKineticEnergy() + src.GetMass()) / MeV;

        case ColType::Mass:
            return src.GetMass() / MeV;

        case ColType::Charge:
            return src.GetCharge();

        case ColType::PreKineticEnergy:
            return src.GetPreKineticEnergy() / MeV;

        case ColType::PostKineticEnergy:
            return src.GetKineticEnergy() / MeV;

        // ========== Position and time ==========
        case ColType::PosX:
            return src.GetPosition().x() / mm;
        case ColType::PosY:
            return src.GetPosition().y() / mm;
        case ColType::PosZ:
            return src.GetPosition().z() / mm;

        case ColType::PrePosX: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetPosition().x() / mm : 0.0;
        }
        case ColType::PrePosY: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetPosition().y() / mm : 0.0;
        }
        case ColType::PrePosZ: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetPosition().z() / mm : 0.0;
        }
        case ColType::PostPosX: {
            auto* post = src.GetPostStepPoint();
            return post ? post->GetPosition().x() / mm : src.GetPosition().x() / mm;
        }
        case ColType::PostPosY: {
            auto* post = src.GetPostStepPoint();
            return post ? post->GetPosition().y() / mm : src.GetPosition().y() / mm;
        }
        case ColType::PostPosZ: {
            auto* post = src.GetPostStepPoint();
            return post ? post->GetPosition().z() / mm : src.GetPosition().z() / mm;
        }

        case ColType::GlobalTime:
            return src.GetGlobalTime() / ns;
        case ColType::LocalTime:
            return src.GetLocalTime() / ns;
        case ColType::ProperTime:
            return src.GetProperTime() / ns;

        // ========== Momentum components ==========
        case ColType::Px:
            return src.GetMomentum().x() / MeV;
        case ColType::Py:
            return src.GetMomentum().y() / MeV;
        case ColType::Pz:
            return src.GetMomentum().z() / MeV;

        case ColType::DirX:
            return src.GetMomentumDirection().x();
        case ColType::DirY:
            return src.GetMomentumDirection().y();
        case ColType::DirZ:
            return src.GetMomentumDirection().z();
        case ColType::DirTheta: {
            G4ThreeVector dir = src.GetMomentumDirection();
            return (dir.mag() > 0) ? dir.theta() : 0.0;
        }
        case ColType::DirPhi: {
            G4ThreeVector dir = src.GetMomentumDirection();
            return (dir.mag() > 0) ? dir.phi() : 0.0;
        }

        case ColType::PrePx: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().x() / MeV : 0.0;
        }
        case ColType::PrePy: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().y() / MeV : 0.0;
        }
        case ColType::PrePz: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().z() / MeV : 0.0;
        }
        case ColType::PostPx:
            return src.GetMomentum().x() / MeV; // same as Px
        case ColType::PostPy:
            return src.GetMomentum().y() / MeV;
        case ColType::PostPz:
            return src.GetMomentum().z() / MeV;

        // ========== Advanced kinematics ==========
        case ColType::Pt:
            return src.GetMomentum().perp() / MeV;

        case ColType::Pseudorapidity: {
            G4ThreeVector p = src.GetMomentum();
            return (p.mag() > 0.0) ? p.eta() : 0.0;
        }

        case ColType::Beta:
            return ComputeBeta(src);

        case ColType::ScatteringAngle: {
            if (src.GetType() != UnifiedSource::Type::Hit) return 0.0;
            auto* pre = src.GetPreStepPoint();
            auto* post = src.GetPostStepPoint();
            if (!pre || !post) return 0.0;
            G4ThreeVector dirIn = pre->GetMomentumDirection();
            G4ThreeVector dirOut = post->GetMomentumDirection();
            return dirIn.angle(dirOut);
        }

        // ========== Polarization ==========
        case ColType::PolX:
            return src.GetPolarization().x();
        case ColType::PolY:
            return src.GetPolarization().y();
        case ColType::PolZ:
            return src.GetPolarization().z();

        default:
            return 0.0;
    }
}

/// @brief Computes β = v/c from the source, using velocity for Hit/Sec types,
///        or momentum/mass for Primary type.
/// @param src UnifiedSource providing velocity, momentum, and mass.
/// @return β (dimensionless), or 0.0 if data is unavailable.
double KinematicsExtractor::ComputeBeta(const UnifiedSource& src) const {
    // For Hit: use velocity from pre‑step point
    if (src.GetType() == UnifiedSource::Type::Hit) {
        auto* pre = src.GetPreStepPoint();
        if (pre) return pre->GetVelocity() / CLHEP::c_light;
        return 0.0;
    }

    // For Secondary: use velocity from track if available, else compute from momentum and mass
    if (src.GetType() == UnifiedSource::Type::Secondary) {
        auto* track = src.GetTrack();
        if (!track) return 0.0;
        // Some physics lists may set velocity via G4Track::GetVelocity() – use it if >0
        double v = track->GetVelocity();
        if (v > 0.0) return v / CLHEP::c_light;
        // Fallback: beta = p / sqrt(p^2 + m^2) / c (p in MeV/c)
        double p = track->GetMomentum().mag();
        double m = track->GetParticleDefinition()->GetPDGMass();
        if (p <= 0.0 || m <= 0.0) return 0.0;
        double totalE = std::sqrt(p*p + m*m);
        return (p / totalE); // since c = 1 in CLHEP units (p and m in energy units)
    }

    // For Primary: compute from momentum and mass
    if (src.GetType() == UnifiedSource::Type::Primary) {
        double p = src.GetMomentum().mag();
        double m = src.GetMass();
        if (p <= 0.0 || m <= 0.0) return 0.0;
        double totalE = std::sqrt(p*p + m*m);
        return (p / totalE);
    }

    return 0.0;
}