//==============================================================================
// G4CARE
// @file    SurfaceRegistry.cc
// @brief   Registry that maps G4OpticalSurface pointers and surface names
//          to unique integer IDs and stores per-surface properties.
// @details SurfaceRegistry is the optical-surface analogue of DetectorRegistry.
//   It assigns sequential IDs to registered surfaces and provides lookup by
//   pointer or name, plus property retrieval by ID.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "SurfaceRegistry.hh"

/// @brief Registers an optical surface with its name and properties.
/// @param surf  G4OpticalSurface pointer.
/// @param name  Surface name.
/// @param props Surface properties.
/// @return Assigned surface ID, or -1 if surf is null.
int SurfaceRegistry::RegisterSurface(G4OpticalSurface* surf, const std::string& name, const SurfaceProperties& props) {
    if (!surf) return -1;
    auto it = fNameToID.find(name);
    if (it != fNameToID.end()) return it->second;
    int id = static_cast<int>(fSurfaces.size());
    fNameToID[name] = id;
    fSurfaceToID[surf] = id;
    fSurfaces.emplace_back(surf, props);
    return id;
}

/// @brief Returns the surface ID for a given optical surface pointer.
/// @param surf Optical surface (may be nullptr).
/// @return Surface ID, or -1 if not found.
int SurfaceRegistry::GetSurfaceID(G4OpticalSurface* surf) const {
    if (!surf) return -1;
    auto it = fSurfaceToID.find(surf);
    if (it != fSurfaceToID.end()) return it->second;
    auto nameIt = fNameToID.find(surf->GetName());
    if (nameIt != fNameToID.end()) return nameIt->second;
    return -1;
}

/// @brief Returns the surface ID for a given surface name.
/// @param name Surface name.
/// @return Surface ID, or -1 if not found.
int SurfaceRegistry::GetSurfaceID(const std::string& name) const {
    auto it = fNameToID.find(name);
    return (it != fNameToID.end()) ? it->second : -1;
}

/// @brief Returns the properties for a given surface ID.
/// @param id Surface ID.
/// @return Reference to SurfaceProperties; empty properties if ID is invalid.
const SurfaceProperties& SurfaceRegistry::GetProperties(int id) const {
    static SurfaceProperties empty;
    if (id >= 0 && id < static_cast<int>(fSurfaces.size())) {
        return fSurfaces[id].second;
    }
    return empty;
}

/// @brief Clears all registered surfaces.
void SurfaceRegistry::Clear() {
    fSurfaceToID.clear();
    fNameToID.clear();
    fSurfaces.clear();
}