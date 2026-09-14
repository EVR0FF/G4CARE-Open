//==============================================================================
//
// G4CARE
//
// @file    ParticleUtils.hh
// @brief   Utility functions for particle creation and direction manipulation.
//
// @details
//   Shared utilities used by ParticleGunSource, GenericVertexGenerator, and
//   other sources:
//   - ParseParticles() — reads per-particle definitions from YAML config
//     subsections and precompiles ExprTK expressions.
//   - RotateToBase() — rotates local direction vectors via Rodrigues' formula.
//   - CreateParticleFast() — creates G4PrimaryParticle from compiled
//     FastExpr evaluators.
//
//   Configuration keys read: none directly.
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

#ifndef PARTICLEUTILS_HH
#define PARTICLEUTILS_HH

#include "ParticleGunSource.hh"
#include "ConfigManager.hh"
#include "ExpressionEvaluator.hh"

#include <vector>
#include <map>
#include <string>

class G4PrimaryParticle;
class ExpressionEvaluator;

/// @brief Shared particle utilities used by source classes.
namespace ParticleUtils {
    
    /// @brief Parse per-particle definitions from a YAML config subsection.
    /// @param cfg         ConfigManager instance.
    /// @param basePath    YAML path prefix (e.g. "SOURCE.e_gun.PARTICLES").
    /// @param allVarNames Variable names available in ExprTK context.
    /// @param eval        ExpressionEvaluator for compilation.
    /// @return            Vector of ParticleDesc indexed by numeric key.
    std::vector<ParticleGunSource::ParticleDesc> ParseParticles(
        ConfigManager* cfg,
        const std::string& basePath,
        const std::vector<std::string>& allVarNames,
        ExpressionEvaluator* eval);

    /// @brief Create a G4PrimaryParticle from a pre-parsed ParticleDesc.
    /// @param pdesc    Particle descriptor with compiled expression slots.
    /// @param event_id Current event ID.
    /// @param v_i      Vertex index.
    /// @param p_i      Particle index.
    /// @return         New G4PrimaryParticle, or nullptr on error.
    G4PrimaryParticle* CreateParticleFast(const ParticleGunSource::ParticleDesc& pdesc,
                                           double event_id, double v_i, double p_i);
}

#endif