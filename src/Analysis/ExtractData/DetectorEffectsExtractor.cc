//==============================================================================
// G4CARE
// @file    DetectorEffectsExtractor.cc
// @brief   Extracts detector-effect quantities (smeared energy, visible energy,
//          dose, LET, step grammage, optical wavelength, boundary status) from
//          Geant4 step data.
// @details DetectorEffectsExtractor applies a configurable energy smearing
//   model (relative, absolute, or none), computes visible energy via Geant4
//   Birks saturation, calculates local dose, LET (MeV/µm), step grammage
//   (g/cm²), optical photon wavelength from energy, and step boundary flag.
//
//   Configuration keys read: ENERGY_SMEAR_FWHM_REL, ENERGY_SMEAR_SIGMA_ABS,
//     ENERGY_SMEAR_TYPE.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "DetectorEffectsExtractor.hh"
#include "ConfigManager.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"
#include "G4EmSaturation.hh"
#include "G4LossTableManager.hh"

/// @brief Constructs the extractor and reads energy smearing settings from
///        the configuration.
/// @param cfg Configuration manager (may be nullptr for defaults).
DetectorEffectsExtractor::DetectorEffectsExtractor(const ConfigManager* cfg)
    : fSmearFWHM(0.07),
      fSmearSigmaAbs(0.0),
      fSmearType("relative")
{
    if (cfg) {
        fSmearFWHM = cfg->GetDouble("ENERGY_SMEAR_FWHM_REL", 0.07);
        fSmearSigmaAbs = cfg->GetValueWithUnits("ENERGY_SMEAR_SIGMA_ABS", 0.0) / MeV;
        fSmearType = cfg->GetString("ENERGY_SMEAR_TYPE", "relative");
    }
    fEmSaturation = G4LossTableManager::Instance()->EmSaturation();
}

/// @brief Extracts a detector-effect column value (SmearedEdep, VisibleEdep,
///        DoseGy, LET, StepGrammage, OpticalWavelength, BoundaryStatus).
/// @param type Column type specifying which quantity to extract.
/// @param src  UnifiedSource providing the G4Step (must be Hit type).
/// @return Extracted value, or 0.0 if not applicable.
double DetectorEffectsExtractor::Get(ColType type, const UnifiedSource& src) const {
    if (src.GetType() != UnifiedSource::Type::Hit) return 0.0;

    switch (type) {
        case ColType::SmearedEdep: {
            double edep = src.GetTotalEnergyDeposit() / MeV;
            if (edep <= 0.0) return 0.0;
            double sigma = 0.0;
            if (fSmearType == "relative") {
                sigma = (fSmearFWHM * std::sqrt(edep)) / 2.355;
            } else if (fSmearType == "absolute") {
                sigma = fSmearSigmaAbs;
            } else {
                // "none" — no smearing
                return edep;
            }
            double smeared = CLHEP::RandGauss::shoot(G4Random::getTheEngine(), edep, sigma);
            return std::max(0.0, smeared);
        }

        case ColType::VisibleEdep: {
            if (fEmSaturation) {
                return fEmSaturation->VisibleEnergyDepositionAtAStep(src.GetStep()) / MeV;
            }
            return src.GetTotalEnergyDeposit() / MeV;
        }

        case ColType::DoseGy: {
            double edep = src.GetTotalEnergyDeposit();
            if (edep <= 0.0) return 0.0;
            auto* lv = const_cast<G4LogicalVolume*>(src.GetLogicalVolume());
            if (!lv) return 0.0;
            double mass = lv->GetMass(false, false, nullptr); // в кг
            if (mass <= 0.0) return 0.0;
            // 1 MeV = 1.602176634e-13 J
            return (edep * 1.602176634e-13) / mass; // Gy
        }

        case ColType::LET: {
            double stepLen = src.GetStepLength();
            if (stepLen <= 0.0) return 0.0;
            return (src.GetTotalEnergyDeposit() / MeV) / (stepLen / um); // MeV/µm
        }

        case ColType::StepGrammage: {
            auto* mat = src.GetMaterial();
            if (!mat) return 0.0;
            double step_cm = src.GetStepLength() / cm;
            if (step_cm <= 0.0) return 0.0;
            double density = mat->GetDensity() / (g/cm3);
            return density * step_cm; // g/cm²
        }

        case ColType::OpticalWavelength: {
            auto* def = src.GetParticleDefinition();
            if (!def || def->GetParticleName() != "opticalphoton") return 0.0;
            double energy_eV = src.GetKineticEnergy() / eV;
            if (energy_eV <= 0.0) return 0.0;
            return 1239.84193 / energy_eV; // nm
        }

        case ColType::BoundaryStatus:
            return (src.GetStepStatus() == fGeomBoundary) ? 1.0 : 0.0;

        default:
            return 0.0;
    }
}