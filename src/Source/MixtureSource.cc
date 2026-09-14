//==============================================================================
//
// G4CARE
//
// @file    MixtureSource.cc
// @brief   Weighted blend of multiple already-initialised particle sources.
//
// @details
//   Builds a mixture from a list of Source pointers obtained from
//   SourceManager.  Each component source is assigned a weight, and
//   at event generation one source is selected by roulette-wheel sampling.
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

#include "MixtureSource.hh"
#include "Randomize.hh"
#include "G4ios.hh"

/// @brief Construct a mixture source from named components resolved via SourceManager.
/// @param name       Display name for this mixture.
/// @param components List of (sourceName, weight) pairs.
/// @param mgr        SourceManager that already contains the referenced sources.
///
/// Builds the cumulative distribution for roulette-wheel selection.
/// Sources with non-positive weight or not found in the manager are skipped.
MixtureSource::MixtureSource(const std::string& name,
                             const std::vector<std::pair<std::string, double>>& components,
                             SourceManager* mgr)
    : fComponents()
{
    fName = name;
    double totalWeight = 0.0;
    for (const auto& comp : components) {
        Source* src = mgr->GetSourceByName(comp.first);
        if (!src) {
            G4cerr << "MixtureSource: source '" << comp.first << "' not found. Skipping." << G4endl;
            continue;
        }
        if (comp.second <= 0.0) {
            G4cerr << "MixtureSource: weight <= 0 for component '" << comp.first << "', skipping." << G4endl;
            continue;
        }
        fComponents.emplace_back(src, comp.second);
        totalWeight += comp.second;
    }

    if (fComponents.empty()) {
        G4cerr << "MixtureSource: no valid components." << G4endl;
        return;
    }

    double cum = 0.0;
    for (const auto& comp : fComponents) {
        cum += comp.second / totalWeight;
        fCumulativeWeights.push_back(cum);
    }
}

/// @brief Generate primary particles by selecting one component source by weight.
///
/// Uses roulette-wheel sampling (G4UniformRand) against the cumulative
/// weight distribution built in the constructor.  Delegates to the
/// selected source's GeneratePrimaries().
///
/// @param event Current Geant4 event.
void MixtureSource::GeneratePrimaries(G4Event* event) {
    if (fComponents.empty()) return;

    double r = G4UniformRand();
    for (size_t i = 0; i < fCumulativeWeights.size(); ++i) {
        if (r <= fCumulativeWeights[i]) {
            fComponents[i].first->GeneratePrimaries(event);
            return;
        }
    }

    fComponents.back().first->GeneratePrimaries(event);
}