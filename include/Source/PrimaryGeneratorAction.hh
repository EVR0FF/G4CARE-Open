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
#include <string>
#include <vector>

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

    /// @brief Most-recently constructed instance (used by the source sampler).
    static PrimaryGeneratorAction* Instance() { return fgInstance; }

    /// @brief Delegate primary vertex generation to SourceManager.
    virtual void GeneratePrimaries(G4Event*) override;

    /// @brief Apply GPS macros before first event (reserved for future use).
    void ApplyMacros();

    /// @brief One sampled primary particle (GUI source preview).
    struct SampledPrimary {
        double x = 0, y = 0, z = 0;      // position (mm)
        double dx = 0, dy = 0, dz = 0;   // direction (unit vector)
        double energy = 0;               // kinetic energy (MeV)
        double time = 0;                 // emission time (ns)
        double weight = 1;               // particle weight
        std::string particle;            // particle name
    };

    /// @brief Sample N primaries from one source by name (SOURCE.<name> key).
    /// @param name  Source name (the SOURCE subsection key).
    /// @param n     Number of primaries to generate.
    /// @param found Optional output: set to true if the source exists.
    /// @return      Sampled primaries (empty if the source was not found or
    ///              produced no valid primaries — check @p found to distinguish).
    std::vector<SampledPrimary> SampleSource(const std::string& name, int n, bool* found = nullptr);

private:
    static PrimaryGeneratorAction* fgInstance;
    std::unique_ptr<SourceManager> fSourceManager;
};

#endif