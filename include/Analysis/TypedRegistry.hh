//==============================================================================
// G4CARE
// @file    TypedRegistry.hh
// @brief   Template registry that maps values of type T to sequential integer
//          IDs for fast, lock-free lookup.
// @details Thread-safe: registration is performed before worker threads
//   launch; GetID is a read-only lock-free lookup.
//   Supports string, G4String, int, and double types.
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
#include <map>
#include <vector>
#include <string>

/// @brief Thread-safe bidirectional value↔ID registry.
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

private:
    std::map<T, int> fMap;
    std::vector<T> fValues;
};

#endif // TYPED_REGISTRY_HH