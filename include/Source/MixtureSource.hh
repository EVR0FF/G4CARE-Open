//==============================================================================
//
// G4CARE
//
// @file    MixtureSource.hh
// @brief   Weighted blend of multiple already-initialised particle sources.
//
// @details
//   Builds a mixture from Source pointers obtained from SourceManager.
//   Each component source is assigned a weight; at event generation one
//   source is selected by roulette-wheel sampling against the cumulative
//   weight distribution.
//
//   Used by PrimaryGeneratorAction for SOURCE.<name>.type = "mixture".
//
//   Configuration keys read: SOURCE.<name>.components.<idx>.source, .weight.
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

#ifndef MIXTURE_SOURCE_HH
#define MIXTURE_SOURCE_HH

#include "Source.hh"
#include "SourceManager.hh"
#include <vector>
#include <string>

/// @brief Weighted mixture source — selects one component per event.
///
/// Components are resolved by name from SourceManager at construction time.
/// At event generation, a roulette-wheel draw selects the active source.
class MixtureSource : public Source {
public:

    /// @brief Construct a mixture from named component sources.
    /// @param name       Display name for the mixture.
    /// @param components List of (sourceName, weight) pairs.
    /// @param mgr        SourceManager that already contains the referenced sources.
    MixtureSource(const std::string& name,
                  const std::vector<std::pair<std::string, double>>& components,
                  SourceManager* mgr);
    virtual ~MixtureSource() = default;

    /// @brief Select one component source by roulette-wheel sampling
    ///        and delegate primary vertex generation to it.
    virtual void GeneratePrimaries(G4Event* event) override;

private:
    std::vector<std::pair<Source*, double>> fComponents;
    std::vector<double> fCumulativeWeights;
};

#endif