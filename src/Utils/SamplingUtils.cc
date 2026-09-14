//==============================================================================
//
// G4CARE
//
// @file    SamplingUtils.cc
// @brief   Direction and position sampling utilities for particle sources.
//
// @details
//   Provides helper functions for sampling particle emission directions
//   and positions with various distributions:
//
//   - SampleEnergy() — Gaussian energy sampling around a mean value.
//   - SampleDirection() — direction sampling with modes:
//     fixed, isotropic, cone, gauss.
//   - SamplePosition() — 2D position sampling with modes:
//     gaussian, uniform_circle, uniform_rectangle, annulus, custom.
//
//   Uses Rodrigues' rotation formula (RotateToBase) to transform locally
//   sampled directions into the global reference frame defined by the
//   source/beam axis.
//
//   Configuration keys read: none (receives parameters from calling sources).
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

#include "SamplingUtils.hh"
#include "Randomize.hh"
#include "G4RandomDirection.hh"
#include <cmath>

namespace {

    /// @brief Rotate a local direction into the reference frame of a base direction
    ///        using Rodrigues' rotation formula.
    ///
    /// @param localDir Direction in the local (0,0,1) frame.
    /// @param baseDir  Target base direction (unit vector).
    /// @return         Rotated direction in the baseDir frame.
    ///
    /// Handles two edge cases efficiently:
    /// - Nearly parallel → returns localDir unchanged.
    /// - Nearly antiparallel → flips y and z components.
    G4ThreeVector RotateToBase(const G4ThreeVector& localDir, const G4ThreeVector& baseDir) {
        
        const G4ThreeVector z(0,0,1);
        double cosTheta = baseDir.z();

        // Edge case: baseDir is nearly parallel to z-axis
        if (cosTheta > 0.999999) return localDir;
        // Edge case: baseDir is nearly antiparallel to z-axis
        if (cosTheta < -0.999999) return G4ThreeVector(localDir.x(), -localDir.y(), -localDir.z());

        // General case: Rodrigues rotation
        G4ThreeVector axis = z.cross(baseDir).unit();
        G4ThreeVector u = axis.cross(baseDir).unit();
        G4ThreeVector v = axis;

        return localDir.x() * u + localDir.y() * v + localDir.z() * baseDir;
    }
}

/// @brief Sample kinetic energy from a Gaussian distribution.
///
/// @param mean  Mean energy value.
/// @param sigma Standard deviation.  If sigma > 0, a Gaussian sample is drawn;
///              otherwise @p mean is returned unchanged.
/// @return      Sampled energy.
double SamplingUtils::SampleEnergy(double mean, double sigma) {
    if (sigma > 0) return G4RandGauss::shoot(mean, sigma);
    return mean;
}

/// @brief Sample a particle emission direction.
///
/// @param mode       Direction mode: "fixed", "isotropic", "cone", "gauss".
/// @param baseDir    Reference direction (beam axis for cone/gauss, returned as-is
///                   for "fixed").
/// @param coneAngle  Half-opening angle for "cone" mode (radians).
/// @param sigmaTheta Angular spread standard deviation for "gauss" mode (radians).
/// @param sigmaPhi   Azimuthal spread standard deviation for "gauss" mode (radians).
/// @return           Sampled unit direction vector.
///
/// Mode descriptions:
/// - "fixed": returns @p baseDir unchanged.
/// - "isotropic": uniform sampling over the full sphere via G4RandomDirection.
/// - "cone": uniform sampling within a cone of half-angle @p coneAngle around
///   @p baseDir.  The cosine of the polar angle is sampled uniformly in
///   [cos(coneAngle), 1] to ensure uniform distribution over the solid angle.
/// - "gauss": Gaussian sampling in theta and phi with standard deviations
///   @p sigmaTheta and @p sigmaPhi, centred on @p baseDir.
///
/// Both "cone" and "gauss" modes use RotateToBase() to transform the locally
/// sampled direction into the global @p baseDir frame.
G4ThreeVector SamplingUtils::SampleDirection(
    const G4String& mode,
    const G4ThreeVector& baseDir,
    double coneAngle,
    double sigmaTheta,
    double sigmaPhi)
{
    if (mode == "fixed") return baseDir;

    if (mode == "isotropic") {
        // Uniform sampling over the full 4π sphere
        return G4RandomDirection();
    }

    if (mode == "cone") {
        if (coneAngle <= 0) return baseDir;
        // Uniform sampling over solid angle δΩ = 2π(1 - cosθ_max)
        double cosThetaMax = std::cos(coneAngle);
        double zeta = G4UniformRand();
        double cosTheta = 1.0 - zeta * (1.0 - cosThetaMax);
        double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);
        double phi = 2.0 * M_PI * G4UniformRand();
        
        G4ThreeVector localDir(sinTheta * std::cos(phi),
                               sinTheta * std::sin(phi),
                               cosTheta);
        return RotateToBase(localDir, baseDir);
    }

    if (mode == "gauss") {
        double dTheta = (sigmaTheta > 0) ? G4RandGauss::shoot(0.0, sigmaTheta) : 0.0;
        double dPhi = (sigmaPhi > 0) ? G4RandGauss::shoot(0.0, sigmaPhi) : 0.0;
        
        G4ThreeVector localDir(std::sin(dTheta) * std::cos(dPhi),
                               std::sin(dTheta) * std::sin(dPhi),
                               std::cos(dTheta));
        
        return RotateToBase(localDir, baseDir);
    }

    return baseDir;
}

/// @brief Sample a 2D position offset in the transverse plane.
///
/// @param type        Position distribution type:
///                    "gaussian", "uniform_circle", "uniform_rectangle",
///                    "annulus", "custom".
/// @param basePos     Reference centre position.
/// @param sigmaX      Gaussian sigma in X (mm), for "gaussian" mode.
/// @param sigmaY      Gaussian sigma in Y (mm), for "gaussian" mode.
/// @param radius      Outer radius (mm) for "uniform_circle", "annulus", "custom".
/// @param innerRadius Inner radius (mm) for "annulus" mode.
/// @param widthX      Rectangle half-width in X (mm), for "uniform_rectangle".
/// @param widthY      Rectangle half-width in Y (mm), for "uniform_rectangle".
/// @return            Sampled position = @p basePos + offset.
///
/// Distribution details:
/// - "gaussian": independent Gaussian sampling in X and Y.
/// - "uniform_circle": uniform areal density within a circle of radius @p radius
///   (r = radius × √U, where U ~ Uniform(0,1)).
/// - "uniform_rectangle": uniform sampling within a rectangle of
///   [−widthX/2, +widthX/2] × [−widthY/2, +widthY/2].
/// - "annulus": uniform sampling in an annulus between @p innerRadius
///   and @p radius (r = √(r²_min + U × (r²_max − r²_min))).
/// - "custom": r = radius × U^0.3 (empirical radial profile).
G4ThreeVector SamplingUtils::SamplePosition(
    const G4String& type,
    const G4ThreeVector& basePos,
    double sigmaX, double sigmaY,
    double radius, double innerRadius,
    double widthX, double widthY)
{
    G4ThreeVector offset(0,0,0);

    if (type == "gaussian") {
        double x = (sigmaX > 0) ? G4RandGauss::shoot(0.0, sigmaX) : 0.0;
        double y = (sigmaY > 0) ? G4RandGauss::shoot(0.0, sigmaY) : 0.0;
        offset = G4ThreeVector(x, y, 0);
    }
    else if (type == "uniform_circle") {
        // r = R × √U ensures uniform areal density
        double r = radius * std::sqrt(G4UniformRand());
        double phi = 2.0 * M_PI * G4UniformRand();
        offset = G4ThreeVector(r * std::cos(phi), r * std::sin(phi), 0);
    }
    else if (type == "uniform_rectangle") {
        double x = (G4UniformRand() - 0.5) * widthX;
        double y = (G4UniformRand() - 0.5) * widthY;
        offset = G4ThreeVector(x, y, 0);
    }
    else if (type == "annulus") {
        // Uniform areal density in annulus: r = √(r²_min + U × (r²_max − r²_min))
        double r2_min = innerRadius * innerRadius;
        double r2_max = radius * radius;
        double r = std::sqrt(r2_min + G4UniformRand() * (r2_max - r2_min));
        double phi = 2.0 * M_PI * G4UniformRand();
        offset = G4ThreeVector(r * std::cos(phi), r * std::sin(phi), 0);
    }
    else if (type == "custom") {
        double r = radius * std::pow(G4UniformRand(), 0.3);
        double phi = 2.0 * M_PI * G4UniformRand();
        offset = G4ThreeVector(r * std::cos(phi), r * std::sin(phi), 0);
    }

    return basePos + offset;
}