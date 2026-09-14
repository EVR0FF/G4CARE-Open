#ifndef RADIOACTIVE_PHYSICSLIST_HH
#define RADIOACTIVE_PHYSICSLIST_HH

//==============================================================================
// G4CARE
// @file    RadioactivePhysicsList.hh
// @brief   Modular physics list for radioactive decay simulations
// @details Registers standard electromagnetic (EM option4), hadronic
//   (FTFP_BERT), decay, stopping, ion, and radioactive-decay physics.
//   Enables atomic rearrangement (ARM), fluorescence, and Auger
//   de-excitation. Used as the default physics list configuration
//   for activation and decay simulations with a 1 ns mean-life
//   threshold for G4NuclideTable.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4VModularPhysicsList.hh"

class RadioactivePhysicsList : public G4VModularPhysicsList
{
public:
    /// @brief Constructor: registers all physics modules
    RadioactivePhysicsList();

    /// @brief Destructor
    ~RadioactivePhysicsList() override = default;

    /// @brief Construct particles via base class
    void ConstructParticle() override;

    /// @brief Construct processes: enables ARM, fluorescence, Auger
    void ConstructProcess() override;

    /// @brief Set production cuts via base class
    void SetCuts() override;

private:
    /// @brief Atomic Rearrangement flag (enabled)
    G4bool fARMflag = true;
};

#endif