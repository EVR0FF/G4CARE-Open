//==============================================================================
// G4CARE
// @file    StepExtractor.hh
// @brief   Extracts step-level quantities (Edep, NIEL, DPA, step length,
//          time delta, momentum, status, safety, etc.) from G4Step data.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef STEP_EXTRACTOR_HH
#define STEP_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"

/// @brief Extracts Edep, NIEL, DPA, step length, momentum, safety, etc.
class StepExtractor {
public:
    StepExtractor() = default;
    ~StepExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src, int eventId, int volId, int matId) const;
};

#endif