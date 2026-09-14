//==============================================================================
// G4CARE
// @file    CrossSectionExtractor.hh
// @brief   Lightweight extractor that delegates cross-section ColType queries
//          to CrossSectionCalculator.
// @details Maps each cross-section ColType enum to the appropriate
//   CrossSectionCalculator method (neutron, proton, photon, electron,
//   positron, muon, ion).  Validates that the UnifiedSource is a Hit and
//   that a G4Step and calculator are available.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CROSS_SECTION_EXTRACTOR_HH
#define CROSS_SECTION_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "CrossSectionCalculator.hh"

/// @brief Delegates cross-section extraction to CrossSectionCalculator.
class CrossSectionExtractor {
public:
    explicit CrossSectionExtractor(CrossSectionCalculator* xsCalc);
    ~CrossSectionExtractor() = default;

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src) const;

private:
    CrossSectionCalculator* fXSCalculator;
};

#endif // CROSS_SECTION_EXTRACTOR_HH