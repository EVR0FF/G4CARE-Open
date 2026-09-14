//==============================================================================
//
// G4CARE
//
// @file    SourceManager.cc
// @brief   Weighted collection of particle sources with mode selection.
//
// @details
//   Manages a set of Source objects registered by PrimaryGeneratorAction.
//   Two operation modes:
//   - kFlow: all sources fire in every event.
//   - kEvent: one source selected per event by roulette-wheel sampling.
//
//   Configuration keys read: SOURCE_MODE (via PrimaryGeneratorAction).
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

#include "SourceManager.hh"
#include "Randomize.hh"
#include "G4Event.hh"
#include "G4ios.hh"

SourceManager::SourceManager() = default;

/// @brief Register a source with a weight and name.
/// @param source Unique pointer to the source (ownership transferred).
/// @param weight Relative weight for event-mode selection (>0 required).
/// @param name   Human-readable name for lookup via GetSourceByName.
///
/// Recalculates the cumulative weight distribution after insertion.
void SourceManager::AddSource(std::unique_ptr<Source> source, double weight, const std::string& name) {
    if (weight <= 0.0) {
        G4cerr << "SourceManager: source weight <= 0, ignoring." << G4endl;
        return;
    }
    
    fSourceMap[name] = source.get();
    fSources.emplace_back(std::move(source), weight);
    fTotalWeight += weight;
    UpdateCumulativeWeights();
}

Source* SourceManager::GetSourceByName(const std::string& name) const {
    auto it = fSourceMap.find(name);
    if (it != fSourceMap.end()) {
        return it->second;
    }
    return nullptr;
}

void SourceManager::UpdateCumulativeWeights() {
    fCumulativeWeights.clear();
    double cum = 0.0;
    for (const auto& pair : fSources) {
        cum += pair.second / fTotalWeight;
        fCumulativeWeights.push_back(cum);
    }
}

void SourceManager::GeneratePrimaries(G4Event* event) {
    if (fSources.empty()) return;

    if (fMode == SourceMode::kEvent) {
        double r = G4UniformRand();
        for (size_t i = 0; i < fCumulativeWeights.size(); ++i) {
            if (r <= fCumulativeWeights[i]) {
                fSources[i].first->GeneratePrimaries(event);
                return;
            }
        }
        fSources.back().first->GeneratePrimaries(event);
    } else {
        for (auto& pair : fSources) {
            pair.first->GeneratePrimaries(event);
        }
    }
}