//==============================================================================
// G4CARE
// @file    DetectorEffectsExtractor.hh
// @brief   Extracts detector-effect quantities (smeared energy, visible energy,
//          dose, LET, step grammage, optical wavelength, boundary status).
// @details Applies configurable energy smearing (relative, absolute, none),
//   computes visible energy via Birks saturation, dose, LET (MeV/µm),
//   step grammage (g/cm²), and optical photon wavelength.
//
//   Configuration keys read: ENERGY_SMEAR_FWHM_REL, ENERGY_SMEAR_SIGMA_ABS,
//     ENERGY_SMEAR_TYPE.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DETECTOR_EFFECTS_EXTRACTOR_HH
#define DETECTOR_EFFECTS_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include <string>

class ConfigManager;
class G4EmSaturation;

/// @brief Extracts smeared/visible energy, dose, LET, grammage, wavelength.
class DetectorEffectsExtractor {
public:
    DetectorEffectsExtractor(const ConfigManager* cfg = nullptr);
    double Get(ColType type, const UnifiedSource& src) const;

private:
    double fSmearFWHM;
    double fSmearSigmaAbs;
    std::string fSmearType;
    G4EmSaturation* fEmSaturation;
};

#endif