//==============================================================================
// G4CARE
// @file    ProcessExtractor.hh
// @brief   Extracts Geant4 process information (sub-type, type, packed ID,
//          name) from step-defining or creator processes.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef PROCESS_EXTRACTOR_HH
#define PROCESS_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"

/// @brief Extracts process sub-type, type, packed ID, and name.
class ProcessExtractor {
public:
    ProcessExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;
};

#endif