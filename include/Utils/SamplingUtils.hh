//==============================================================================
//
// G4CARE
//
// @file    SamplingUtils.hh
// @brief   Direction and position sampling utilities for particle sources.
//
// @details
//   Provides helper functions for energy, direction, and position
//   sampling used by ParticleGunSource and other source types:
//   - SampleEnergy() — Gaussian energy sampling.
//   - SampleDirection() — fixed, isotropic, cone, or Gaussian directions.
//   - SamplePosition() — Gaussian, uniform_circle, uniform_rectangle,
//     annulus, or custom 2D radial profiles.
//
//   Configuration keys read: none.
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

#ifndef SAMPLINGUTILS_HH
#define SAMPLINGUTILS_HH

#include "G4ThreeVector.hh"
#include "G4String.hh"

/// @brief Utility functions for energy, direction, and position sampling.
namespace SamplingUtils {
    
    /// @brief Sample kinetic energy from a Gaussian distribution.
    /// @param mean  Mean energy.
    /// @param sigma Standard deviation (0 → return mean).
    /// @return      Sampled energy.
    double SampleEnergy(double mean, double sigma);

    /// @brief Sample a particle emission direction.
    /// @param mode       "fixed", "isotropic", "cone", or "gauss".
    /// @param baseDir    Reference direction (beam axis).
    /// @param coneAngle  Half-opening angle for "cone" mode (radians).
    /// @param sigmaTheta Angular spread sigma for "gauss" mode (radians).
    /// @param sigmaPhi   Azimuthal spread sigma for "gauss" mode (radians).
    /// @return           Sampled unit direction vector.
    G4ThreeVector SampleDirection(
        const G4String& mode,
        const G4ThreeVector& baseDir,
        double coneAngle, 
        double sigmaTheta,
        double sigmaPhi
    );

    /// @brief Sample a 2D position offset in the transverse plane.
    /// @param type        "gaussian", "uniform_circle", "uniform_rectangle",
    ///                    "annulus", or "custom".
    /// @param basePos     Reference centre position.
    /// @param sigmaX      Gaussian sigma in X.
    /// @param sigmaY      Gaussian sigma in Y.
    /// @param radius      Outer radius for circle/annulus/custom modes.
    /// @param innerRadius Inner radius for "annulus" mode.
    /// @param widthX      Half-width for "uniform_rectangle" mode.
    /// @param widthY      Half-width for "uniform_rectangle" mode.
    /// @return            Sampled position = basePos + offset.
    G4ThreeVector SamplePosition(
        const G4String& type,
        const G4ThreeVector& basePos,
        double sigmaX, double sigmaY,
        double radius, double innerRadius,
        double widthX, double widthY
    );
}

#endif
