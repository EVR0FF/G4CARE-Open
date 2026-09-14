//==============================================================================
// G4CARE
// @file    TrackExtractor.hh
// @brief   Extracts track-level quantities (length, status, vertex info,
//          mean free path, at-rest rates) from G4Track data.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef TRACK_EXTRACTOR_HH
#define TRACK_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"

/// @brief Extracts track length, status, vertex, MFP, and at-rest rates.
class TrackExtractor {
public:
    TrackExtractor() = default;
    ~TrackExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;
};

#endif // TRACK_EXTRACTOR_HH