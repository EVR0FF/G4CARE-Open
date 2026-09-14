//==============================================================================
// G4CARE
// @file    KinematicsExtractor.hh
// @brief   Extracts kinematic quantities (energy, momentum, position, time,
//          beta, pseudorapidity, scattering angle, polarization).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef KINEMATICS_EXTRACTOR_HH
#define KINEMATICS_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"

/// @brief Extracts kinematic data: energy, momentum, position, time, beta, etc.
class KinematicsExtractor {
public:
    KinematicsExtractor() = default;
    ~KinematicsExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;

private:
    // Helper for beta calculation (velocity/c)
    [[nodiscard]] double ComputeBeta(const UnifiedSource& src) const;
};

#endif // KINEMATICS_EXTRACTOR_HH