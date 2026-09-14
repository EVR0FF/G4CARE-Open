//==============================================================================
// G4CARE
// @file    CrossSectionCalculator.hh
// @brief   Declares the CrossSectionCalculator class that wraps Geant4
//          cross-section classes for neutrons, protons, photons, electrons,
//          positrons, muons, and ions.
// @details Provides per-step methods (extracting particle, energy, material, Z
//   from G4Step) and per-material "ForMaterial" overloads for dictionary
//   generation.  All values are returned in barn (or MeV/mm for ion dE/dx).
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CROSS_SECTION_CALCULATOR_HH
#define CROSS_SECTION_CALCULATOR_HH

#include "G4String.hh"
#include "G4SystemOfUnits.hh"
#include <memory>

class G4Step;
class G4Material;
class G4DynamicParticle;
class G4Element;

/// @brief Wraps Geant4 built-in cross-section classes for per-step and
///        per-material extraction.
class CrossSectionCalculator {
public:
    CrossSectionCalculator() = default;
    ~CrossSectionCalculator() = default;

    // ====================== Neutrons ======================
    double GetNeutronTotalXS(const G4Step* step);
    double GetNeutronElasticXS(const G4Step* step);
    double GetNeutronInelasticXS(const G4Step* step);
    double GetNeutronCaptureXS(const G4Step* step);
    double GetNeutronFissionXS(const G4Step* step);
    double GetNeutronThermalScatteringXS(const G4Step* step);

    // ====================== Protons ======================
    double GetProtonTotalXS(const G4Step* step);
    double GetProtonElasticXS(const G4Step* step);
    double GetProtonInelasticXS(const G4Step* step);

    // ====================== Photons ======================
    double GetPhotonTotalXS(const G4Step* step);
    double GetPhotonPhotoElectricXS(const G4Step* step);
    double GetPhotonComptonXS(const G4Step* step);
    double GetPhotonConversionXS(const G4Step* step);
    double GetPhotonRayleighXS(const G4Step* step);
    double GetPhotonNuclearXS(const G4Step* step);
    double GetPhotonMuonPairXS(const G4Step* step);

    // ====================== Electrons ======================
    double GetElectronIonisationXS(const G4Step* step);
    double GetElectronBremsstrahlungXS(const G4Step* step);
    double GetElectronExcitationXS(const G4Step* step);
    double GetElectronElasticXS(const G4Step* step);

    // ====================== Positrons ======================
    double GetPositronIonisationXS(const G4Step* step);
    double GetPositronBremsstrahlungXS(const G4Step* step);
    double GetPositronAnnihilationXS(const G4Step* step);

    // ====================== Muons ======================
    double GetMuonIonisationXS(const G4Step* step);
    double GetMuonBremsstrahlungXS(const G4Step* step);
    double GetMuonPairProductionXS(const G4Step* step);
    double GetMuonNuclearXS(const G4Step* step);
    double GetMuonCaptureXS(const G4Step* step);

    // ====================== Ions ======================
    double GetIonDEDX(const G4Step* step);
    double GetIonInelasticXS(const G4Step* step);
    double GetIonElasticXS(const G4Step* step);

    // ====================== Cross-section dictionary ======================
    double GetNeutronTotalXSForMaterial(G4Material* mat, double energy);
    double GetNeutronCaptureXSForMaterial(G4Material* mat, double energy);
    double GetProtonTotalXSForMaterial(G4Material* mat, double energy);
    double GetPhotonTotalXSForMaterial(G4Material* mat, double energy);

private:
    // Helper methods
    G4Material* GetMaterial(const G4Step* step) const;
    double GetKineticEnergy(const G4Step* step) const;
    G4DynamicParticle CreateDynamicParticle(const G4Step* step) const;
    G4int GetElementZ(const G4Material* mat) const;
    double GetAtomicMass(const G4Material* mat) const;
    G4Element* GetFirstElement(const G4Material* mat) const;
    G4double GetEffectiveZ(const G4Material* mat) const;
};

#endif // CROSS_SECTION_CALCULATOR_HH
