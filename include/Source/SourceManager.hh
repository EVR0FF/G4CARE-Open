#ifndef SOURCEMANAGER_HH
#define SOURCEMANAGER_HH

//==============================================================================
// G4CARE
// @file    SourceManager.hh
// @brief   Manager for multiple particle sources with weighted mixing
// @details Owns a collection of Source objects and dispatches event
//   generation across them. Supports two modes:
//   - kFlow: each event uses one source selected by weight
//   - kEvent: all sources fire per event (cumulative primary generation)
//   Maintains cumulative weights for efficient weighted random selection.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Source.hh"
#include <vector>
#include <map>
#include <memory>
#include <string>

class SourceManager {
public:
    /// @brief Source mixing mode
    enum class SourceMode {
        kFlow,   ///< One source per event, selected by weight
        kEvent   ///< All sources fire per event
    };

    /// @brief Default constructor
    SourceManager();

    /// @brief Destructor
    ~SourceManager() = default;

    /// @brief Add a source with a given weight and name
    /// @param source Unique pointer to the Source
    /// @param weight Weight for random selection
    /// @param name   Name identifier for lookup
    void AddSource(std::unique_ptr<Source> source, double weight, const std::string& name);

    /// @brief Find a source by name
    /// @param name Source name to search for
    /// @return Pointer to the Source, or nullptr if not found
    Source* GetSourceByName(const std::string& name) const;

    /// @brief Set the source mixing mode
    /// @param mode SourceMode::kFlow or kEvent
    void SetMode(SourceMode mode) { fMode = mode; }

    /// @brief Generate primary particles for the given event
    /// @param event Pointer to the G4Event to fill
    void GeneratePrimaries(G4Event* event);

    /// @brief Check if no sources are registered
    /// @return true if the source list is empty
    bool Empty() const { return fSources.empty(); }

private:
    /// @brief Recompute cumulative weight array for efficient selection
    void UpdateCumulativeWeights();

    /// @brief Vector of (source, weight) pairs
    std::vector<std::pair<std::unique_ptr<Source>, double>> fSources;

    /// @brief Map from name to raw Source pointer for fast lookup
    std::map<std::string, Source*> fSourceMap;

    /// @brief Cumulative weight array for binary-search selection
    std::vector<double> fCumulativeWeights;

    /// @brief Current mixing mode
    SourceMode fMode = SourceMode::kFlow;

    /// @brief Total sum of all source weights
    double fTotalWeight = 0.0;
};

#endif