//==============================================================================
//
// G4CARE
//
// @file    PrimaryGeneratorAction.hh
// @brief   Source manager factory — creates all particle sources from YAML.
//
// @details
//   Implements G4VUserPrimaryGeneratorAction.  Reads DATA and SOURCE
//   config blocks and instantiates GPS, ParticleGun, Radioactive,
//   Activation, and Mixture sources.  Loads external data files and
//   registers them as ExprTK aliases.
//
//   Configuration keys read:
//     DATA.<name>.*, SOURCE.<name>.*, SOURCE_MODE
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#ifndef PRIMARYGENERATORACTION_HH
#define PRIMARYGENERATORACTION_HH

#include "G4VUserPrimaryGeneratorAction.hh"
#include "SourceManager.hh"
#include <memory>

class ObjectManager;

/// @brief Primary generator action — creates all particle sources from YAML config.
///
/// Reads DATA block to load external data files (CSV/ROOT/IAEA) and SOURCE
/// block to instantiate source objects.  Delegates event generation to
/// SourceManager which dispatches to individual sources.
class PrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
public:
    PrimaryGeneratorAction(ObjectManager* objMgr);
    virtual ~PrimaryGeneratorAction();

    /// @brief Delegate primary vertex generation to SourceManager.
    virtual void GeneratePrimaries(G4Event*) override;

    /// @brief Apply GPS macros before first event (reserved for future use).
    void ApplyMacros();

private:
    std::unique_ptr<SourceManager> fSourceManager;
};

#endif