//==============================================================================
// G4CARE
// @file    CrossSectionCalculator.cc
// @brief   Implementation of cross-section calculation methods for various
//          particles (neutrons, protons, photons, electrons, positrons, muons,
//          and ions) using Geant4 built-in cross-section classes.
// @details CrossSectionCalculator provides per-step and per-material cross-section
//   extraction for hadronic, electromagnetic, and muon processes.  Step-level
//   methods extract the particle, kinetic energy, material, and atomic number (Z)
//   from the G4Step, then delegate to the appropriate Geant4 cross-section class.
//   "ForMaterial" overloads accept a G4Material* and double energy directly, so
//   they can be used outside of event stepping (e.g. for cross-section dictionaries).
//
//   Configuration keys read: none (pure physics extraction).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "CrossSectionCalculator.hh"
#include "G4SystemOfUnits.hh"
#include "G4DynamicParticle.hh"
#include "G4ParticleDefinition.hh"
#include "G4Gamma.hh"
#include "G4Electron.hh"
#include "G4Positron.hh"
#include "G4MuonMinus.hh"
#include "G4Proton.hh"
#include "G4Neutron.hh"
#include "G4Step.hh"
#include "G4Material.hh"
#include "G4Element.hh"
#include "G4Ions.hh"
#include "G4KokoulinMuonNuclearXS.hh"
#include "G4HadronicProcessStore.hh"
#include "G4NeutronHPThermalScatteringData.hh"
#include "G4MuonMinusCapture.hh"
#include "G4EmCalculator.hh"
#include "G4BGGNucleonElasticXS.hh"
#include "G4BGGPionElasticXS.hh"
#include "G4GammaConversionToMuons.hh"
#include "G4NeutronCaptureXS.hh"
#include "G4NeutronInelasticXS.hh"
#include "G4ParticleInelasticXS.hh"
#include "G4PhotoNuclearCrossSection.hh"

// ======================================================================
// Helper methods
// ======================================================================

/// @brief Extracts the material from the pre-step point.
/// @param step Current Geant4 step.
/// @return Pointer to G4Material at the pre-step point.
G4Material* CrossSectionCalculator::GetMaterial(const G4Step* step) const {
    return step->GetPreStepPoint()->GetMaterial();
}

/// @brief Returns the post-step kinetic energy of the track.
/// @param step Current Geant4 step.
/// @return Kinetic energy in internal Geant4 units.
double CrossSectionCalculator::GetKineticEnergy(const G4Step* step) const {
    return step->GetPostStepPoint()->GetKineticEnergy();
}

/// @brief Constructs a G4DynamicParticle from the post-step state.
/// @param step Current Geant4 step.
/// @return G4DynamicParticle with the track's definition, direction, and energy.
G4DynamicParticle CrossSectionCalculator::CreateDynamicParticle(const G4Step* step) const {
    auto* part = step->GetTrack()->GetParticleDefinition();
    G4ThreeVector dir = step->GetPostStepPoint()->GetMomentumDirection();
    double energy = step->GetPostStepPoint()->GetKineticEnergy();
    return G4DynamicParticle(part, dir, energy);
}

/// @brief Retrieves the atomic number (Z) of the first element in the material.
/// @param mat Material whose first element's Z is queried.
/// @return Z of the first element, or 0 if the material has no elements.
G4int CrossSectionCalculator::GetElementZ(const G4Material* mat) const {
    if (!mat || mat->GetNumberOfElements() == 0) return 0;
    return mat->GetElement(0)->GetZ();
}

/// @brief Computes the mass-fraction-weighted effective atomic number of a material.
/// @param mat Material for which effective Z is computed.
/// @return Weighted effective Z, or 0.0 if the material has no elements.
G4double CrossSectionCalculator::GetEffectiveZ(const G4Material* mat) const {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    G4double effZ = 0.0;
    const G4ElementVector* elements = mat->GetElementVector();
    const G4double* fractions = mat->GetFractionVector();
    for (size_t i = 0; i < mat->GetNumberOfElements(); ++i) {
        effZ += fractions[i] * (*elements)[i]->GetZ();
    }
    return effZ;
}

/// @brief Computes the mass-fraction-weighted effective atomic mass (A) of a material.
/// @param mat Material for which effective A is computed.
/// @return Weighted effective A in g/mole, or 0.0 if the material has no elements.
double CrossSectionCalculator::GetAtomicMass(const G4Material* mat) const {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    // Compute effective A for composite materials
    G4double effA = 0.0;
    const G4ElementVector* elements = mat->GetElementVector();
    const G4double* fractions = mat->GetFractionVector();
    for (size_t i = 0; i < mat->GetNumberOfElements(); ++i) {
        effA += fractions[i] * (*elements)[i]->GetA() / (g/mole);
    }
    return effA;
}

/// @brief Returns a non-const pointer to the first element of the material.
/// @param mat Material whose first element is requested.
/// @return Pointer to the first G4Element, or nullptr if the material has none.
G4Element* CrossSectionCalculator::GetFirstElement(const G4Material* mat) const {
    if (!mat || mat->GetNumberOfElements() == 0) return nullptr;
    return const_cast<G4Element*>(mat->GetElement(0));
}

// ======================================================================
// Neutrons (HP models up to 20 MeV)
// ======================================================================

/// @brief Calculates the total neutron cross-section (capture + elastic + inelastic)
///        for energies up to 20 MeV using HP-class models.
/// @param step Current Geant4 step containing neutron track.
/// @return Total cross-section in barn, or 0.0 if not a neutron or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronTotalXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "neutron") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 20*MeV) return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;

    G4NeutronCaptureXS   capXS;
    G4BGGPionElasticXS   elaXS(G4Neutron::Neutron());
    G4NeutronInelasticXS inelXS;

    double cap  = capXS.GetElementCrossSection(&dyn, Z, mat);
    double ela  = elaXS.GetElementCrossSection(&dyn, Z, mat);
    double inel = inelXS.GetElementCrossSection(&dyn, Z, mat);
    return (cap + ela + inel) / barn;
}

/// @brief Calculates the elastic neutron cross-section using BGG pion elastic model
///        for energies up to 20 MeV.
/// @param step Current Geant4 step containing a neutron track.
/// @return Elastic cross-section in barn, or 0.0 if not a neutron or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronElasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "neutron") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 20*MeV) return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;
    G4BGGPionElasticXS elaXS(G4Neutron::Neutron());
    return elaXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

/// @brief Calculates the inelastic neutron cross-section for energies up to 20 MeV.
/// @param step Current Geant4 step containing a neutron track.
/// @return Inelastic cross-section in barn, or 0.0 if not a neutron or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronInelasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "neutron") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 20*MeV) return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;
    G4NeutronInelasticXS inelXS;
    return inelXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

/// @brief Calculates the neutron capture cross-section for energies up to 20 MeV.
/// @param step Current Geant4 step containing a neutron track.
/// @return Capture cross-section in barn, or 0.0 if not a neutron or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronCaptureXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "neutron") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 20*MeV) return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;
    G4NeutronCaptureXS capXS;
    return capXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

/// @brief Returns 0.0 — G4NeutronFissionXS was removed in Geant4 11.4.
/// @param step Current Geant4 step (unused).
/// @return Always 0.0.
double CrossSectionCalculator::GetNeutronFissionXS(const G4Step* /*step*/) {
    return 0.0; // G4NeutronFissionXS removed in Geant4 11.4
}

/// @brief Calculates the neutron thermal scattering cross-section for energies
///        up to 4 eV using HP thermal scattering data.
/// @param step Current Geant4 step containing a neutron track.
/// @return Thermal scattering cross-section in barn, or 0.0 if not a neutron or energy > 4 eV.
double CrossSectionCalculator::GetNeutronThermalScatteringXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "neutron") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 4*eV) return 0.0;

    G4Material* mat = GetMaterial(step);
    if (!mat) return 0.0;

    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;

    G4ThreeVector dir = step->GetPostStepPoint()->GetMomentumDirection();
    G4DynamicParticle dyn(part, dir, energy);

    static G4NeutronHPThermalScatteringData thermalData;
    return thermalData.GetCrossSection(&dyn, el, mat) / barn;
}

// ======================================================================
// Protons
// ======================================================================

/// @brief Calculates the total proton cross-section (elastic + inelastic)
///        using BGG nucleon elastic and particle inelastic models.
/// @param step Current Geant4 step containing a proton track.
/// @return Total cross-section in barn, or 0.0 if not a proton.
double CrossSectionCalculator::GetProtonTotalXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "proton") return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;

    G4BGGNucleonElasticXS   elaXS(G4Proton::Proton());
    G4ParticleInelasticXS   inelXS(G4Proton::Proton());

    double elas = elaXS.GetElementCrossSection(&dyn, Z, mat);
    double inel = inelXS.GetElementCrossSection(&dyn, Z, mat);
    return (elas + inel) / barn;
}

/// @brief Calculates the elastic proton cross-section using BGG nucleon
///        elastic model.
/// @param step Current Geant4 step containing a proton track.
/// @return Elastic cross-section in barn, or 0.0 if not a proton.
double CrossSectionCalculator::GetProtonElasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "proton") return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;
    G4BGGNucleonElasticXS elaXS(G4Proton::Proton());
    return elaXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

/// @brief Calculates the inelastic proton cross-section using the
///        G4ParticleInelasticXS model.
/// @param step Current Geant4 step containing a proton track.
/// @return Inelastic cross-section in barn, or 0.0 if not a proton.
double CrossSectionCalculator::GetProtonInelasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "proton") return 0.0;
    G4DynamicParticle dyn = CreateDynamicParticle(step);
    G4Material* mat = GetMaterial(step);
    G4int Z = GetElementZ(mat);
    if (Z == 0) return 0.0;
    G4ParticleInelasticXS inelXS(G4Proton::Proton());
    return inelXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

// ======================================================================
// Photons
// ======================================================================

/// @brief Calculates the total photon cross-section as the sum of
///        photoelectric, Compton, conversion, Rayleigh, photonuclear, and
///        muon-pair contributions.
/// @param step Current Geant4 step containing a gamma track.
/// @return Total cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonTotalXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;

    G4double Z = el->GetZ();
    G4double A = el->GetN();

    G4EmCalculator emCalc;

    double xs_phot  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "phot", Z, A);
    double xs_compt = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "compt", Z, A);
    double xs_conv  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "conv", Z, A);
    double xs_rayl  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "rayl", Z, A);

    G4DynamicParticle dyn(G4Gamma::Gamma(), G4ThreeVector(1,0,0), energy);
    G4PhotoNuclearCrossSection photoNucXS;
    G4GammaConversionToMuons gammaConv;

    double xs_nuclear = photoNucXS.GetElementCrossSection(&dyn, Z, mat) / barn;
    double xs_muon    = gammaConv.GetCrossSectionPerAtom(&dyn, el) / barn;

    return (xs_phot + xs_compt + xs_conv + xs_rayl + xs_nuclear + xs_muon) / barn;
}

/// @brief Calculates the photoelectric cross-section for photons.
/// @param step Current Geant4 step containing a gamma track.
/// @return Photoelectric cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonPhotoElectricXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4double Z = el->GetZ();
    G4double A = el->GetN();
    G4EmCalculator emCalc;
    return emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "phot", Z, A) / barn;
}

/// @brief Calculates the Compton scattering cross-section for photons.
/// @param step Current Geant4 step containing a gamma track.
/// @return Compton cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonComptonXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4double Z = el->GetZ();
    G4double A = el->GetN();
    G4EmCalculator emCalc;
    return emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "compt", Z, A) / barn;
}

/// @brief Calculates the pair-conversion (gamma → e⁺e⁻) cross-section for photons.
/// @param step Current Geant4 step containing a gamma track.
/// @return Conversion cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonConversionXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4double Z = el->GetZ();
    G4double A = el->GetN();
    G4EmCalculator emCalc;
    return emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "conv", Z, A) / barn;
}

/// @brief Calculates the Rayleigh (coherent) scattering cross-section for photons.
/// @param step Current Geant4 step containing a gamma track.
/// @return Rayleigh cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonRayleighXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4double Z = el->GetZ();
    G4double A = el->GetN();
    G4EmCalculator emCalc;
    return emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "rayl", Z, A) / barn;
}

/// @brief Calculates the photonuclear cross-section for photons.
/// @param step Current Geant4 step containing a gamma track.
/// @return Photonuclear cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonNuclearXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4DynamicParticle dyn(G4Gamma::Gamma(), G4ThreeVector(1,0,0), energy);
    G4PhotoNuclearCrossSection photoNucXS;
    return photoNucXS.GetElementCrossSection(&dyn, GetElementZ(mat), mat) / barn;
}

/// @brief Calculates the gamma → μ⁺μ⁻ conversion cross-section.
/// @param step Current Geant4 step containing a gamma track.
/// @return Muon-pair cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetPhotonMuonPairXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4DynamicParticle dyn(G4Gamma::Gamma(), G4ThreeVector(1,0,0), energy);
    G4GammaConversionToMuons gammaConv;
    return gammaConv.GetCrossSectionPerAtom(&dyn, el) / barn;
}

// ======================================================================
// Electrons
// ======================================================================

/// @brief Calculates the electron ionisation cross-section per atom.
/// @param step Current Geant4 step containing an electron track.
/// @return Ionisation cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetElectronIonisationXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Electron::Electron(), "ioni", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the electron bremsstrahlung cross-section per atom.
/// @param step Current Geant4 step containing an electron track.
/// @return Bremsstrahlung cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetElectronBremsstrahlungXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Electron::Electron(), "brem", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the electron elastic (multiple-scattering) cross-section per atom.
/// @param step Current Geant4 step containing an electron track.
/// @return Elastic cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetElectronElasticXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Electron::Electron(), "msc", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the DNA-level electron excitation cross-section per atom.
/// @param step Current Geant4 step containing an electron track.
/// @return Excitation cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetElectronExcitationXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Electron::Electron(), "DNAExcitation", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

// ======================================================================
// Positrons
// ======================================================================

/// @brief Calculates the positron ionisation cross-section per atom.
/// @param step Current Geant4 step containing a positron track.
/// @return Ionisation cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetPositronIonisationXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Positron::Positron(), "ioni", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the positron bremsstrahlung cross-section per atom.
/// @param step Current Geant4 step containing a positron track.
/// @return Bremsstrahlung cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetPositronBremsstrahlungXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Positron::Positron(), "brem", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the positron annihilation cross-section per atom.
/// @param step Current Geant4 step containing a positron track.
/// @return Annihilation cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetPositronAnnihilationXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4Positron::Positron(), "annihil", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

// ======================================================================
// Muons
// ======================================================================

/// @brief Calculates the muon ionisation cross-section per atom.
/// @param step Current Geant4 step containing a muon track.
/// @return Ionisation cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetMuonIonisationXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4MuonMinus::MuonMinus(), "ioni", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the muon bremsstrahlung cross-section per atom.
/// @param step Current Geant4 step containing a muon track.
/// @return Bremsstrahlung cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetMuonBremsstrahlungXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4MuonMinus::MuonMinus(), "brem", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the muon pair-production cross-section per atom.
/// @param step Current Geant4 step containing a muon track.
/// @return Pair-production cross-section in barn, or 0.0 if material has no atoms.
double CrossSectionCalculator::GetMuonPairProductionXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4EmCalculator emCalc;
    double macXS = emCalc.ComputeCrossSectionPerVolume(energy, G4MuonMinus::MuonMinus(), "muPairProd", mat);
    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    return (nAtoms > 0.0) ? (macXS / nAtoms) / barn : 0.0;
}

/// @brief Calculates the muon nuclear cross-section using the Kokoulin model.
/// @param step Current Geant4 step containing a muon track.
/// @return Muon nuclear cross-section in barn, or 0.0 if no valid element.
double CrossSectionCalculator::GetMuonNuclearXS(const G4Step* step) {
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4DynamicParticle dyn(G4MuonMinus::MuonMinus(), step->GetPostStepPoint()->GetMomentumDirection(), energy);
    G4KokoulinMuonNuclearXS muonNucXS;
    return muonNucXS.GetCrossSection(&dyn, el, mat) / barn;
}

/// @brief Calculates the muon capture cross-section for μ⁻ at low energies
///        (≤ 1 MeV) using the G4MuonMinusCapture process.
/// @param step Current Geant4 step containing a muon track.
/// @return Capture cross-section in barn, or 0.0 if not applicable.
double CrossSectionCalculator::GetMuonCaptureXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (part->GetParticleName() != "mu-") return 0.0;
    double energy = GetKineticEnergy(step);
    if (energy > 1*MeV) return 0.0;

    G4Material* mat = GetMaterial(step);
    if (!mat) return 0.0;

    const G4Track* track = step->GetTrack();
    if (!track) return 0.0;

    static G4MuonMinusCapture muonCapture;
    double mfp = muonCapture.GetMeanFreePath(*track, 0.0, nullptr);
    if (mfp <= 0.0 || mfp >= DBL_MAX) return 0.0;

    double nAtoms = mat->GetTotNbOfAtomsPerVolume();
    if (nAtoms <= 0.0) return 0.0;

    double sigma = 1.0 / (nAtoms * mfp);
    return sigma / barn;
}

// ======================================================================
// Ions
// ======================================================================

/// @brief Calculates the electronic stopping power (dE/dx) for an ion.
/// @param step Current Geant4 step containing a general ion track.
/// @return Electronic dE/dx in MeV/mm.
double CrossSectionCalculator::GetIonDEDX(const G4Step* step) {
    G4EmCalculator emCalc;
    return emCalc.ComputeElectronicDEDX(
        GetKineticEnergy(step),
        step->GetTrack()->GetParticleDefinition(),
        GetMaterial(step)) / (MeV/mm);
}

/// @brief Calculates the inelastic cross-section per atom for a general ion
///        using the hadronic process store.
/// @param step Current Geant4 step containing a general ion track.
/// @return Inelastic cross-section in barn, or 0.0 if not a general ion.
double CrossSectionCalculator::GetIonInelasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (!part->IsGeneralIon()) return 0.0;
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    double xs = G4HadronicProcessStore::Instance()->GetInelasticCrossSectionPerAtom(part, energy, el, mat);
    return xs / barn;
}

/// @brief Calculates the elastic cross-section per atom for a general ion
///        using the hadronic process store.
/// @param step Current Geant4 step containing a general ion track.
/// @return Elastic cross-section in barn, or 0.0 if not a general ion.
double CrossSectionCalculator::GetIonElasticXS(const G4Step* step) {
    auto* part = step->GetTrack()->GetParticleDefinition();
    if (!part->IsGeneralIon()) return 0.0;
    double energy = GetKineticEnergy(step);
    G4Material* mat = GetMaterial(step);
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    double xs = G4HadronicProcessStore::Instance()->GetElasticCrossSectionPerAtom(part, energy, el, mat);
    return xs / barn;
}

// ======================================================================
// Cross-section dictionary (ForMaterial overloads)
// ======================================================================

/// @brief Neutron total cross-section for a given material and energy
///        (capture + elastic + inelastic, HP models, ≤ 20 MeV).
/// @param mat Target material.
/// @param energy Kinetic energy of the neutron.
/// @return Total cross-section in barn, or 0.0 if material is invalid or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronTotalXSForMaterial(G4Material* mat, double energy) {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    if (energy > 20*MeV) return 0.0;
    G4ParticleDefinition* neutron = G4Neutron::Neutron();
    G4DynamicParticle dyn(neutron, G4ThreeVector(1,0,0), energy);
    G4int Z = GetElementZ(mat);

    G4NeutronCaptureXS   capXS;
    G4BGGPionElasticXS   elaXS(G4Neutron::Neutron());
    G4NeutronInelasticXS inelXS;

    double cap  = capXS.GetElementCrossSection(&dyn, Z, mat);
    double ela  = elaXS.GetElementCrossSection(&dyn, Z, mat);
    double inel = inelXS.GetElementCrossSection(&dyn, Z, mat);
    return (cap + ela + inel) / barn;
}

/// @brief Neutron capture cross-section for a given material and energy
///        (HP model, ≤ 20 MeV).
/// @param mat Target material.
/// @param energy Kinetic energy of the neutron.
/// @return Capture cross-section in barn, or 0.0 if material is invalid or energy > 20 MeV.
double CrossSectionCalculator::GetNeutronCaptureXSForMaterial(G4Material* mat, double energy) {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    if (energy > 20*MeV) return 0.0;
    G4ParticleDefinition* neutron = G4Neutron::Neutron();
    G4DynamicParticle dyn(neutron, G4ThreeVector(1,0,0), energy);
    G4int Z = GetElementZ(mat);
    G4NeutronCaptureXS capXS;
    return capXS.GetElementCrossSection(&dyn, Z, mat) / barn;
}

/// @brief Proton total cross-section for a given material and energy
///        (elastic + inelastic, BGG nucleon models).
/// @param mat Target material.
/// @param energy Kinetic energy of the proton.
/// @return Total cross-section in barn, or 0.0 if material is invalid.
double CrossSectionCalculator::GetProtonTotalXSForMaterial(G4Material* mat, double energy) {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    G4ParticleDefinition* proton = G4Proton::Proton();
    G4DynamicParticle dyn(proton, G4ThreeVector(1,0,0), energy);
    G4int Z = GetElementZ(mat);

    G4BGGNucleonElasticXS   elaXS(G4Proton::Proton());
    G4ParticleInelasticXS   inelXS(G4Proton::Proton());

    double elas = elaXS.GetElementCrossSection(&dyn, Z, mat) / barn;
    double inel = inelXS.GetElementCrossSection(&dyn, Z, mat) / barn;
    return elas + inel;
}

/// @brief Photon total cross-section for a given material and energy,
///        summing photoelectric, Compton, conversion, Rayleigh, photonuclear,
///        and muon-pair contributions.
/// @param mat Target material.
/// @param energy Kinetic energy of the photon.
/// @return Total cross-section in barn, or 0.0 if material is invalid.
double CrossSectionCalculator::GetPhotonTotalXSForMaterial(G4Material* mat, double energy) {
    if (!mat || mat->GetNumberOfElements() == 0) return 0.0;
    G4Element* el = GetFirstElement(mat);
    if (!el) return 0.0;
    G4double Z = el->GetZ();
    G4double A = el->GetN();

    G4EmCalculator emCalc;

    double xs_phot  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "phot", Z, A) / barn;
    double xs_compt = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "compt", Z, A) / barn;
    double xs_conv  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "conv", Z, A) / barn;
    double xs_rayl  = emCalc.ComputeCrossSectionPerAtom(energy, G4Gamma::Gamma(), "rayl", Z, A) / barn;

    G4DynamicParticle dyn(G4Gamma::Gamma(), G4ThreeVector(1,0,0), energy);
    G4PhotoNuclearCrossSection photoNucXS;
    G4GammaConversionToMuons gammaConv;

    double xs_nuclear = photoNucXS.GetElementCrossSection(&dyn, Z, mat) / barn;
    double xs_muon    = gammaConv.GetCrossSectionPerAtom(&dyn, el) / barn;
    return xs_phot + xs_compt + xs_conv + xs_rayl + xs_nuclear + xs_muon;
}