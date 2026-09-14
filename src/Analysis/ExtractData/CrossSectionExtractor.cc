//==============================================================================
// G4CARE
// @file    CrossSectionExtractor.cc
// @brief   Extracts cross-section column values by delegating to
//          CrossSectionCalculator for a given G4Step.
// @details This lightweight extractor maps ColType enum values to the
//   corresponding CrossSectionCalculator methods (neutron, proton, photon,
//   electron, positron, muon, and ion cross-sections).  It validates that
//   the UnifiedSource is a Hit type and that a valid G4Step and calculator
//   are available before extraction.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "CrossSectionExtractor.hh"

/// @brief Constructs the extractor with a pointer to the cross-section calculator.
/// @param xsCalc CrossSectionCalculator instance (may be nullptr).
CrossSectionExtractor::CrossSectionExtractor(CrossSectionCalculator* xsCalc)
    : fXSCalculator(xsCalc) {}

/// @brief Extracts a cross-section column value for the given source and ColType.
/// @param type Column type specifying which cross-section to return.
/// @param src  UnifiedSource providing the G4Step (must be Hit type).
/// @return Cross-section value in the appropriate units, or 0.0 if not applicable.
double CrossSectionExtractor::Get(ColType type, const UnifiedSource& src) const {
    if (src.GetType() != UnifiedSource::Type::Hit || !fXSCalculator) return 0.0;

    const G4Step* step = src.GetStep();
    if (!step) return 0.0;

    switch (type) {
        case ColType::NeutronCaptureXS:    return fXSCalculator->GetNeutronCaptureXS(step);
        case ColType::NeutronElasticXS:   return fXSCalculator->GetNeutronElasticXS(step);
        case ColType::NeutronInelasticXS: return fXSCalculator->GetNeutronInelasticXS(step);
        case ColType::NeutronFissionXS:   return fXSCalculator->GetNeutronFissionXS(step);
        case ColType::NeutronTotalXS:     return fXSCalculator->GetNeutronTotalXS(step);
        case ColType::NeutronThermalScatteringXS: return fXSCalculator->GetNeutronThermalScatteringXS(step);

        case ColType::PhotonTotalXS:          return fXSCalculator->GetPhotonTotalXS(step);
        case ColType::PhotonPhotoElectricXS:  return fXSCalculator->GetPhotonPhotoElectricXS(step);
        case ColType::PhotonComptonXS:        return fXSCalculator->GetPhotonComptonXS(step);
        case ColType::PhotonConversionXS:     return fXSCalculator->GetPhotonConversionXS(step);
        case ColType::PhotonRayleighXS:       return fXSCalculator->GetPhotonRayleighXS(step);
        case ColType::PhotonNuclearXS:        return fXSCalculator->GetPhotonNuclearXS(step);
        case ColType::PhotonMuonPairXS:       return fXSCalculator->GetPhotonMuonPairXS(step);

        case ColType::ElectronIonisationXS:   return fXSCalculator->GetElectronIonisationXS(step);
        case ColType::ElectronBremsstrahlungXS: return fXSCalculator->GetElectronBremsstrahlungXS(step);
        case ColType::ElectronExcitationXS:   return fXSCalculator->GetElectronExcitationXS(step);
        case ColType::ElectronElasticXS:      return fXSCalculator->GetElectronElasticXS(step);

        case ColType::PositronIonisationXS:   return fXSCalculator->GetPositronIonisationXS(step);
        case ColType::PositronBremsstrahlungXS: return fXSCalculator->GetPositronBremsstrahlungXS(step);
        case ColType::PositronAnnihilationXS: return fXSCalculator->GetPositronAnnihilationXS(step);

        case ColType::MuonIonisationXS:       return fXSCalculator->GetMuonIonisationXS(step);
        case ColType::MuonBremsstrahlungXS:   return fXSCalculator->GetMuonBremsstrahlungXS(step);
        case ColType::MuonPairProductionXS:   return fXSCalculator->GetMuonPairProductionXS(step);
        case ColType::MuonNuclearXS:          return fXSCalculator->GetMuonNuclearXS(step);

        case ColType::ProtonTotalXS:          return fXSCalculator->GetProtonTotalXS(step);
        case ColType::ProtonElasticXS:        return fXSCalculator->GetProtonElasticXS(step);
        case ColType::ProtonInelasticXS:      return fXSCalculator->GetProtonInelasticXS(step);

        case ColType::IonIonisationXS:        return fXSCalculator->GetIonDEDX(step);
        case ColType::IonInelasticXS:         return fXSCalculator->GetIonInelasticXS(step);
        case ColType::IonElasticXS:           return fXSCalculator->GetIonElasticXS(step);

        default: return 0.0;
    }
}