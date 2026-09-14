//==============================================================================
// G4CARE
// @file    PropertyTracker.cc
// @brief   Tracks changes in named string properties across runs and writes
//          change events to a property_history ntuple.
// @details PropertyTracker keeps a last-known-value map for a configurable set
//   of property names.  Each call to Update() compares the current value
//   against the last recorded value; if a change is detected, a new row is
//   added to the property_history ntuple with the run ID, property name, and
//   new value.
//
//   Configuration keys read: none (property names are set at initialization).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "PropertyTracker.hh"
#include "G4AnalysisManager.hh"
#include "G4ios.hh"

/// @brief Initializes the tracker with the list of property names to monitor.
/// @param trackedProperties Vector of property name strings.
void PropertyTracker::Initialize(const std::vector<std::string>& trackedProperties) {
    fTrackedProperties = trackedProperties;
    fLastValues.clear();
    for (const auto& prop : trackedProperties) {
        fLastValues[prop] = "";
    }
    fInitialized = true;
}

/// @brief Checks all tracked properties for changes and fills the
///        property_history ntuple for any that have been modified.
/// @param runID         Current run ID.
/// @param currentValues Map of property name → current value.
void PropertyTracker::Update(int runID, const std::map<std::string, std::string>& currentValues) {
    if (!fInitialized) return;

    auto* man = G4AnalysisManager::Instance();
    if (fHistoryNtupleId < 0) return;

    for (const auto& [prop, lastVal] : fLastValues) {
        auto it = currentValues.find(prop);
        std::string newVal = (it != currentValues.end()) ? it->second : "";
        if (newVal != lastVal) {

            man->FillNtupleIColumn(fHistoryNtupleId, fColRunId, runID);
            man->FillNtupleSColumn(fHistoryNtupleId, fColPropName, prop);
            man->FillNtupleSColumn(fHistoryNtupleId, fColValue, newVal);
            man->AddNtupleRow(fHistoryNtupleId);

            fLastValues[prop] = newVal;
        }
    }
}

/// @brief Sets the ntuple and column IDs used to write history records.
/// @param ntupleId     Ntuple ID for property_history.
/// @param colRunId     Column index for "run_id".
/// @param colPropName  Column index for "property_name".
/// @param colValue     Column index for "value".
void PropertyTracker::SetHistoryNtupleId(int ntupleId, int colRunId, int colPropName, int colValue) {
    fHistoryNtupleId = ntupleId;
    fColRunId = colRunId;
    fColPropName = colPropName;
    fColValue = colValue;
}