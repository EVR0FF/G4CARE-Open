//==============================================================================
// G4CARE
// @file    TypedRegistry.hh
// @brief   Template registry that maps values of type T to sequential integer
//          IDs and can create/populate a G4Analysis dictionary ntuple.
// @details Thread-safe: registration is performed only in the master thread
//   before worker threads launch; GetID is a read-only lock-free lookup.
//   Supports string, G4String, int, and double types for dictionary columns.
//
//   Configuration keys read: none (utility template).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef TYPED_REGISTRY_HH
#define TYPED_REGISTRY_HH

#include "G4String.hh"
#include "G4AnalysisManager.hh"
#include <map>
#include <vector>
#include <string>
#include <type_traits>

/// @brief Thread-safe bidirectional value↔ID registry with dictionary output.
template<typename T>
class TypedRegistry {
public:
    using ValueType = T;

    /// Thread-safe pre-registration.
    /// Called **only** in the master thread before worker threads start
    /// (before BeginRun).  Fills the map and returns the assigned ID.
    /// If the value is already registered, returns the existing ID.
    int Register(const T& value) {
        auto it = fMap.find(value);
        if (it != fMap.end()) return it->second;
        int id = static_cast<int>(fValues.size());
        fMap[value] = id;
        fValues.push_back(value);
        return id;
    }

    /// Thread-safe read-only lookup (lock-free).
    /// After filling via Register(), all worker threads only read.
    /// Returns -1 if the value is not found in the registry.
    int GetID(const T& value) const {
        auto it = fMap.find(value);
        return (it != fMap.end()) ? it->second : -1;
    }

    const T& GetValue(int id) const {
        static T empty{};
        if (id >= 0 && id < static_cast<int>(fValues.size()))
            return fValues[id];
        return empty;
    }

    size_t Size() const { return fValues.size(); }

    void Clear() {
        fMap.clear();
        fValues.clear();
    }

    /// Phase 1: create the ntuple structure (before OpenFile).
    /// Called in CreateDictionaryTrees (master and workers before file open).
    /// Returns the created ntuple ID, or -1 if no data exists.
    int CreateDictionary(G4AnalysisManager* man, const std::string& ntupleName) const {
        if (fValues.empty()) return -1;

        int ntupleId = man->CreateNtuple(ntupleName, ntupleName + " Dictionary");
        int idCol = man->CreateNtupleIColumn(ntupleId, "id");
        if constexpr (std::is_same_v<T, std::string>) {
            man->CreateNtupleSColumn(ntupleId, "value");
        } else if constexpr (std::is_same_v<T, G4String>) {
            man->CreateNtupleSColumn(ntupleId, "value");
        } else if constexpr (std::is_same_v<T, int>) {
            man->CreateNtupleIColumn(ntupleId, "value");
        } else if constexpr (std::is_same_v<T, double>) {
            man->CreateNtupleDColumn(ntupleId, "value");
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for dictionary");
        }
        man->FinishNtuple(ntupleId);
        return ntupleId;
    }

    /// Phase 2: fill the ntuple with data (after OpenFile).
    /// Called in FillDictionaryTrees (master and each worker).
    void FillDictionary(G4AnalysisManager* man, int ntupleId) const {
        if (ntupleId < 0 || fValues.empty()) return;

        for (size_t i = 0; i < fValues.size(); ++i) {
            man->FillNtupleIColumn(ntupleId, 0, static_cast<int>(i));
            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, G4String>) {
                man->FillNtupleSColumn(ntupleId, 1, fValues[i]);
            } else if constexpr (std::is_same_v<T, int>) {
                man->FillNtupleIColumn(ntupleId, 1, fValues[i]);
            } else if constexpr (std::is_same_v<T, double>) {
                man->FillNtupleDColumn(ntupleId, 1, fValues[i]);
            }
            man->AddNtupleRow(ntupleId);
        }
    }

private:
    std::map<T, int> fMap;
    std::vector<T> fValues;
};

#endif // TYPED_REGISTRY_HH