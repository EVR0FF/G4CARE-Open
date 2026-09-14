//==============================================================================
//
// G4CARE
//
// @file    ParticleUtils.cc
// @brief   Utility functions for particle creation and direction manipulation.
//
// @details
//   Shared utilities used by ParticleGunSource, GenericVertexGenerator, and
//   other sources:
//
//   - ParticleUtils::RotateToBase() — rotates a local direction vector
//     from the (0,0,1) reference frame into an arbitrary base direction
//     using Rodrigues' rotation formula.  Used for cone and Gaussian
//     emission profiles where particle directions are generated in a
//     local frame and then rotated to the beam axis.
//
//   - ParticleUtils::ParseParticles() — reads per-particle definitions
//     from a YAML config subsection (PARTICLES.<idx>) and precompiles
//     ExprTK expressions for type, energy, momentum/direction, cone
//     angle, polarisation, proper time, weight, and ion properties.
//     Supports three mutually exclusive momentum specifications:
//       a) kinetic energy + direction (cartesian or theta/phi),
//       b) cartesian momentum (px, py, pz),
//       c) zero-energy (radioactive ions).
//
//   - ParticleUtils::CreateParticleFast() — creates a G4PrimaryParticle
//     from a pre-parsed ParticleDesc using compiled FastExpr evaluators
//     for maximum throughput during event generation.
//
//   Configuration keys read: none directly (receives ConfigManager path
//   from the calling source).
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

#include "ParticleUtils.hh"
#include "G4ParticleTable.hh"
#include "G4IonTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "G4PrimaryParticle.hh"
#include "G4ios.hh"
#include "Randomize.hh"
#include "G4RandomDirection.hh"
#include "SamplingUtils.hh"

namespace ParticleUtils {

    /// @brief Rotate a local direction vector into the reference frame of a base direction.
    ///
    /// Given a vector @p localDir expressed in the (0,0,1) frame and a
    /// target @p baseDir, returns the vector expressed in the base direction's
    /// frame.  Uses Rodrigues' rotation formula with the cross product of
    /// (0,0,1) and @p baseDir as the rotation axis.
    ///
    /// @param localDir Direction in the local (0,0,1) reference frame.
    /// @param baseDir  Target base direction (must be a unit vector).
    /// @return         Direction vector expressed in the @p baseDir frame.
    ///
    /// Special cases:
    /// - If @p baseDir is nearly parallel to (0,0,1), @p localDir is returned
    ///   unchanged.
    /// - If @p baseDir is nearly antiparallel to (0,0,1), the x-component
    ///   is preserved and y/z are flipped.
    G4ThreeVector RotateToBase(const G4ThreeVector& localDir, const G4ThreeVector& baseDir) {
        const G4ThreeVector z(0,0,1);
        double cosTheta = baseDir.z();
        if (cosTheta > 0.999999) return localDir;
        if (cosTheta < -0.999999) return G4ThreeVector(localDir.x(), -localDir.y(), -localDir.z());

        G4ThreeVector axis = z.cross(baseDir).unit();
        G4ThreeVector u = axis.cross(baseDir).unit();
        G4ThreeVector v = axis;

        return localDir.x() * u + localDir.y() * v + localDir.z() * baseDir;
    }

    /// @brief Parse per-particle definitions from a YAML config subsection.
    ///
    /// Reads entries from `@p basePath.<idx>` where `<idx>` is a numeric
    /// string.  For each entry, compiles ExprTK expressions for type,
    /// energy, direction (cartesian or theta/phi), cone angle, polarisation
    /// components, proper time, weight, IAEA code, and ion properties
    /// (Z, A, excitation, magnetic moment, lifetime).
    ///
    /// @param cfg          ConfigManager instance providing key-value access.
    /// @param basePath     YAML path prefix (e.g. "SOURCE.e_gun.PARTICLES").
    /// @param allVarNames  List of variable names available in ExprTK context.
    /// @param eval         ExpressionEvaluator for compilation.
    /// @return             Vector of ParticleDesc (index = numeric key).
    ///
    /// The returned vector is sized to accommodate the largest numeric index
    /// found; missing indices are left with default-constructed ParticleDesc.
    /// The caller is responsible for calling FastExpr::Init() on each compiled
    /// expression before use.
    std::vector<ParticleGunSource::ParticleDesc> ParseParticles(
        ConfigManager* cfg,
        const std::string& basePath,
        const std::vector<std::string>& allVarNames,
        ExpressionEvaluator* eval)
    {
        std::vector<ParticleGunSource::ParticleDesc> result;
        auto particlesMap = cfg->GetSubsectionsMap(basePath);

        for (const auto& entry : particlesMap) {
            int idx;
            try {
                idx = std::stoi(entry.first);
            } catch (...) {
                G4cerr << "ParticleUtils: Invalid particle index '" << entry.first << "' in " << basePath << G4endl;
                continue;
            }
            if (result.size() <= idx) result.resize(idx + 1);
            ParticleGunSource::ParticleDesc& part = result[idx];

            part.type = cfg->GetString(basePath + "." + entry.first + ".particle", "");
            part.iaeaExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".iaea_code", ""), allVarNames, eval);
            part.energyExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".energy", ""), allVarNames, eval);
            part.pxExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".px", ""), allVarNames, eval);
            part.pyExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".py", ""), allVarNames, eval);
            part.pzExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".pz", ""), allVarNames, eval);
            part.dirXExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".direction.x", ""), allVarNames, eval);
            part.dirYExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".direction.y", ""), allVarNames, eval);
            part.dirZExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".direction.z", ""), allVarNames, eval);
            part.dirThetaExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".direction.theta", ""), allVarNames, eval);
            part.dirPhiExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".direction.phi", ""), allVarNames, eval);
            part.dirMode = cfg->GetString(basePath + "." + entry.first + ".direction_mode", "");
            part.polXExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".polarization.x", ""), allVarNames, eval);
            part.polYExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".polarization.y", ""), allVarNames, eval);
            part.polZExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".polarization.z", ""), allVarNames, eval);
            part.properTimeExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".proper_time", ""), allVarNames, eval);
            part.weightExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(basePath + "." + entry.first + ".weight", ""), allVarNames, eval);

            part.Z = cfg->GetInt(basePath + "." + entry.first + ".Z", 0);
            part.A = cfg->GetInt(basePath + "." + entry.first + ".A", 0);
            part.excitation = cfg->GetValueWithUnits(basePath + "." + entry.first + ".excitation", 0.0) / CLHEP::MeV;
            part.magnetic_moment = cfg->GetDouble(basePath + "." + entry.first + ".magnetic_moment", 0.0);
            part.lifetime = cfg->GetValueWithUnits(basePath + "." + entry.first + ".lifetime", 0.0) / CLHEP::second;

            part.coneAngle = cfg->GetValueWithUnits(basePath + "." + entry.first + ".cone_angle", 0.0) / CLHEP::rad;
            part.sigmaTheta = cfg->GetValueWithUnits(basePath + "." + entry.first + ".sigma_theta", 0.0) / CLHEP::rad;
            part.sigmaPhi = cfg->GetValueWithUnits(basePath + "." + entry.first + ".sigma_phi", 0.0) / CLHEP::rad;
        }
        return result;
    }

    /// @brief Create a G4PrimaryParticle from a parsed ParticleDesc using
    ///        precompiled FastExpr evaluators for maximum throughput.
    ///
    /// @param pdesc    Particle descriptor with compiled expression slots.
    /// @param event_id Current event ID (for ExprTK evaluation context).
    /// @param v_i      Vertex index (for ExprTK evaluation context).
    /// @param p_i      Particle index (for ExprTK evaluation context).
    /// @return         Newly allocated G4PrimaryParticle, or nullptr on
    ///                 unrecognised particle type.
    ///
    /// **Particle definition priority order:**
    /// 1. IAEA code expression (fastIaea) — maps numeric codes to
    ///    gamma(1), e-(2), e+(3), neutron(4), proton(5).
    /// 2. Z/A/excitation for ions — uses G4IonTable::GetIon().
    /// 3. Type string — G4ParticleTable::FindParticle().
    ///
    /// **Momentum specification priority order:**
    /// a) Kinetic energy + direction (cartesian fastDirX/Y/Z,
    ///    theta/phi fastDirTheta/Phi, or direction_mode:
    ///    fixed / isotropic / cone / gauss).
    /// b) Cartesian momentum (fastPx/fastPy/fastPz).
    /// c) Zero kinetic energy (used for radioactive ions where the
    ///    decay process generates the actual emissions).
    ///
    /// Polarisation, proper time, and weight are set if their
    /// corresponding FastExpr slots are compiled.
    G4PrimaryParticle* CreateParticleFast(const ParticleGunSource::ParticleDesc& pdesc,
                                    double event_id, double v_i, double p_i)
    {
        G4ParticleDefinition* def = nullptr;

        // --- Determine particle definition ---
        if (pdesc.fastIaea.comp) {
            double val = pdesc.fastIaea.Evaluate(event_id, v_i, p_i);
            int code = static_cast<int>(std::round(val));
            switch(code) {
                case 1: def = G4ParticleTable::GetParticleTable()->FindParticle("gamma"); break;
                case 2: def = G4ParticleTable::GetParticleTable()->FindParticle("e-"); break;
                case 3: def = G4ParticleTable::GetParticleTable()->FindParticle("e+"); break;
                case 4: def = G4ParticleTable::GetParticleTable()->FindParticle("neutron"); break;
                case 5: def = G4ParticleTable::GetParticleTable()->FindParticle("proton"); break;
                default:
                    G4cerr << "ParticleUtils: Unknown IAEA code: " << code << G4endl;
                    return nullptr;
            }
        } else if (pdesc.Z > 0 && pdesc.A > 0) {
            def = G4IonTable::GetIonTable()->GetIon(pdesc.Z, pdesc.A, pdesc.excitation);
        } else {
            def = G4ParticleTable::GetParticleTable()->FindParticle(pdesc.type);
        }
        if (!def) {
            G4cerr << "ParticleUtils: Unknown particle type." << G4endl;
            return nullptr;
        }

        G4double energy = 0.0;
        G4ThreeVector momentum;
        G4double mass = def->GetPDGMass();

        // --- Determine kinematics ---
        // Option (a): kinetic energy + direction
        if (pdesc.fastEnergy.comp) {
            energy = pdesc.fastEnergy.Evaluate(event_id, v_i, p_i);
            G4ThreeVector dir(0,0,1);

            if (pdesc.fastDirX.comp || pdesc.fastDirY.comp || pdesc.fastDirZ.comp) {
                double dx = pdesc.fastDirX.Evaluate(event_id, v_i, p_i);
                double dy = pdesc.fastDirY.Evaluate(event_id, v_i, p_i);
                double dz = pdesc.fastDirZ.Evaluate(event_id, v_i, p_i);
                dir = G4ThreeVector(dx, dy, dz);
                double mag = dir.mag();
                if (mag > 0) dir /= mag;
            }
            else if (pdesc.fastDirTheta.comp || pdesc.fastDirPhi.comp) {
                double theta = pdesc.fastDirTheta.Evaluate(event_id, v_i, p_i);
                double phi   = pdesc.fastDirPhi.Evaluate(event_id, v_i, p_i);
                dir = G4ThreeVector(std::sin(theta)*std::cos(phi),
                                    std::sin(theta)*std::sin(phi),
                                    std::cos(theta));
            }
            else if (!pdesc.dirMode.empty()) {
                G4ThreeVector baseDir(0,0,1);
                if (pdesc.dirMode == "fixed") {
                    // dir remains (0,0,1)
                } else if (pdesc.dirMode == "isotropic") {
                    dir = G4RandomDirection();
                } else if (pdesc.dirMode == "cone") {
                    double coneAngle = pdesc.coneAngle;
                    double cosThetaMax = std::cos(coneAngle);
                    double zeta = G4UniformRand();
                    double cosTheta = 1.0 - zeta * (1.0 - cosThetaMax);
                    double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);
                    double phi = 2.0 * M_PI * G4UniformRand();
                    dir = G4ThreeVector(sinTheta * std::cos(phi),
                                        sinTheta * std::sin(phi),
                                        cosTheta);
                    dir = RotateToBase(dir, baseDir);
                } else if (pdesc.dirMode == "gauss") {
                    double dTheta = (pdesc.sigmaTheta > 0) ? G4RandGauss::shoot(0.0, pdesc.sigmaTheta) : 0.0;
                    double dPhi   = (pdesc.sigmaPhi > 0) ? G4RandGauss::shoot(0.0, pdesc.sigmaPhi) : 0.0;
                    dir = G4ThreeVector(std::sin(dTheta) * std::cos(dPhi),
                                        std::sin(dTheta) * std::sin(dPhi),
                                        std::cos(dTheta));
                    dir = RotateToBase(dir, baseDir);
                }
            }

            G4double totalE = energy + mass;
            G4double p = (totalE > mass) ? std::sqrt(totalE*totalE - mass*mass) : 0.0;
            momentum = dir * p;
        }
        // Option (b): cartesian momentum
        else if (pdesc.fastPx.comp || pdesc.fastPy.comp || pdesc.fastPz.comp) {
            double px = pdesc.fastPx.Evaluate(event_id, v_i, p_i);
            double py = pdesc.fastPy.Evaluate(event_id, v_i, p_i);
            double pz = pdesc.fastPz.Evaluate(event_id, v_i, p_i);
            momentum = G4ThreeVector(px, py, pz);
            energy = std::sqrt(momentum.mag2() + mass*mass) - mass;
        }
        // Option (c): zero kinetic energy (radioactive ions)
        else {
            energy = 0.0;
            momentum = G4ThreeVector(0,0,0);
        }

        // --- Create the primary particle ---
        G4PrimaryParticle* primary = new G4PrimaryParticle(def);
        primary->SetKineticEnergy(energy);
        primary->SetMomentumDirection(momentum.unit());

        // --- Optional: polarisation ---
        if (pdesc.fastPolX.comp || pdesc.fastPolY.comp || pdesc.fastPolZ.comp) {
            double polX = pdesc.fastPolX.Evaluate(event_id, v_i, p_i);
            double polY = pdesc.fastPolY.Evaluate(event_id, v_i, p_i);
            double polZ = pdesc.fastPolZ.Evaluate(event_id, v_i, p_i);
            primary->SetPolarization(polX, polY, polZ);
        }

        // --- Optional: proper time ---
        if (pdesc.fastProperTime.comp) {
            double properTime = pdesc.fastProperTime.Evaluate(event_id, v_i, p_i);
            primary->SetProperTime(properTime);
        }

        // --- Optional: particle weight ---
        if (pdesc.fastWeight.comp) {
            double w = pdesc.fastWeight.Evaluate(event_id, v_i, p_i);
            primary->SetWeight(w);
        }

        return primary;
    }

}