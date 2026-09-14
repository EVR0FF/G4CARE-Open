#ifndef USER_CROSS_SECTION_MODEL_HH
#define USER_CROSS_SECTION_MODEL_HH

//==============================================================================
// G4CARE
// @file    UserCrossSectionModel.hh
// @brief   Custom electromagnetic model using user-defined cross sections
// @details Extends G4VEmModel to override cross-section calculations
//   with data from one or more ICrossSectionSource implementations.
//   Queries sources in priority order and returns the first valid
//   cross section (≥ 0). Used to inject externally computed cross
//   sections (e.g., BEB ionization) into Geant4's EM physics.
//   The SampleSecondaries method is a no-op—this model only overrides
//   cross sections, not final-state generation.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4VEmModel.hh"
#include "ICrossSectionSource.hh"
#include <vector>

class G4MaterialCutsCouple;

class UserCrossSectionModel : public G4VEmModel {
public:
    /// @brief Constructor
    /// @param name        Model name
    /// @param particle    Target particle definition
    /// @param processName Process name (e.g., "ioni", "excit")
    /// @param sources     List of cross-section data sources (priority-ordered)
    UserCrossSectionModel(const G4String& name,
                          const G4ParticleDefinition* particle,
                          const G4String& processName,
                          const std::vector<ICrossSectionSource*>& sources);

    /// @brief Initialize the model (currently a no-op)
    /// @param particle Target particle definition
    /// @param cuts     Production cuts
    void Initialise(const G4ParticleDefinition* particle,
                    const G4DataVector& cuts) override;

    /// @brief Compute cross section per atom from user-defined sources
    ///
    /// Queries each ICrossSectionSource in order and returns the first
    /// valid cross section (≥ 0). Converts from barn to Geant4 units.
    /// @param particle      Particle definition (unused)
    /// @param kineticEnergy Kinetic energy [GeV]
    /// @param Z             Atomic number (unused)
    /// @param A             Atomic mass (unused)
    /// @param cutEnergy     Production cut (unused)
    /// @param maxEnergy     Maximum energy (unused)
    /// @return Cross section per atom [Geant4 units]
    G4double ComputeCrossSectionPerAtom(
        const G4ParticleDefinition* particle,
        G4double kineticEnergy,
        G4double Z,
        G4double A,
        G4double cutEnergy = 0.0,
        G4double maxEnergy = DBL_MAX) override;

    /// @brief Sample secondaries (no-op: only cross sections are overridden)
    void SampleSecondaries(std::vector<G4DynamicParticle*>*,
                       const G4MaterialCutsCouple*,
                       const G4DynamicParticle*,
                       G4double, G4double) override {}

private:
    /// @brief Target particle definition
    const G4ParticleDefinition* fParticle;

    /// @brief Cross-section data sources
    std::vector<ICrossSectionSource*> fSources;

    /// @brief Process name for cross-section lookup
    G4String fProcessName;
};

#endif