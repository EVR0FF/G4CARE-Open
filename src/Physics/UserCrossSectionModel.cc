//==============================================================================
// G4CARE
// @file    UserCrossSectionModel.cc
// @brief   Implementation of user-defined cross-section EM model
// @details Implements ComputeCrossSectionPerAtom by iterating over
//   registered ICrossSectionSource instances in priority order and
//   returning the first valid (≥ 0) cross section. The cross-section
//   value is converted from barn to Geant4 native units.
//   SampleSecondaries is a no-op since this model only overrides
//   cross-section calculation, not final-state generation.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "UserCrossSectionModel.hh"
#include "G4ParticleDefinition.hh"
#include "G4MaterialCutsCouple.hh"
#include "G4Material.hh"
#include "G4SystemOfUnits.hh"

/// @brief Constructor
/// @param name        Model name
/// @param particle    Target particle definition
/// @param processName Process name for cross-section lookup
/// @param sources     List of ICrossSectionSource instances (priority-ordered)
UserCrossSectionModel::UserCrossSectionModel(
    const G4String& name,
    const G4ParticleDefinition* particle,
    const G4String& processName,
    const std::vector<ICrossSectionSource*>& sources)
    : G4VEmModel(name),
      fParticle(particle),
      fSources(sources),
      fProcessName(processName) {}

/// @brief Initialize the model (no-op)
/// @param particle Target particle definition (unused)
/// @param cuts     Production cuts (unused)
void UserCrossSectionModel::Initialise(const G4ParticleDefinition*,
                                       const G4DataVector&)
{}

/// @brief Compute cross section per atom from user-defined sources
///
/// Obtains the current material-cuts couple, then queries each
/// registered ICrossSectionSource in order. Returns the first
/// valid cross section (≥ 0), converted from barn to Geant4 units.
/// @param particle      Particle definition (unused)
/// @param kineticEnergy Kinetic energy [GeV]
/// @param Z             Atomic number (unused)
/// @param A             Atomic mass (unused)
/// @param cutEnergy     Production cut energy (unused)
/// @param maxEnergy     Maximum energy (unused)
/// @return Cross section per atom [Geant4 native units]
G4double UserCrossSectionModel::ComputeCrossSectionPerAtom(
    const G4ParticleDefinition* /*particle*/,
    G4double kineticEnergy,
    G4double /*Z*/,
    G4double /*A*/,
    G4double /*cutEnergy*/,
    G4double /*maxEnergy*/)
{
    // Get the current material-cuts couple through the protected base-class method
    const G4MaterialCutsCouple* couple = this->CurrentCouple();
    if (!couple) return 0.0;

    const G4Material* material = couple->GetMaterial();
    if (!material) return 0.0;

    G4double xsBarn = -1.0;
    for (auto* source : fSources) {
        xsBarn = source->GetCrossSectionPerAtom(material, fProcessName, kineticEnergy / MeV);
        if (xsBarn >= 0.0) return xsBarn * CLHEP::barn;
    }
    return 0.0;
}