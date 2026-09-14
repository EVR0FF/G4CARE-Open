//==============================================================================
// G4CARE
// @file    PropertyTracker.hh
// @brief   Tracks changes in named string properties across runs and writes
//          change events to a property_history ntuple.
// @details PropertyTracker keeps a last-known-value map for a configurable
//   set of property names.  Each call to Update() compares current values
//   against the last recorded ones; if a change is detected, a new row is
//   added to the property_history ntuple.
//
//   Configuration keys read: none (property names set at initialization).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef PROPERTY_TRACKER_HH
#define PROPERTY_TRACKER_HH

#include <map>
#include <string>
#include <vector>
#include "G4AnalysisManager.hh"

class G4GenericAnalysisManager;
using G4AnalysisManager = G4GenericAnalysisManager;

/// @brief Monitors property changes across runs and logs events.
class PropertyTracker {
public:
    PropertyTracker() = default;
    ~PropertyTracker() = default;

    void Initialize(const std::vector<std::string>& trackedProperties);
    void Update(int runID, const std::map<std::string, std::string>& currentValues);
    void SetHistoryNtupleId(int ntupleId, int colRunId, int colPropName, int colValue);
    const std::vector<std::string>& GetTrackedProperties() const { return fTrackedProperties; }

private:
    std::map<std::string, std::string> fLastValues;
    std::vector<std::string> fTrackedProperties;
    bool fInitialized = false;
    int fHistoryNtupleId = -1;
    int fColRunId = -1;
    int fColPropName = -1;
    int fColValue = -1;
};

#endif