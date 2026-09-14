//==============================================================================
// G4CARE
// @file    SurfaceRegistry.hh
// @brief   Registry that maps G4OpticalSurface pointers and surface names
//          to unique integer IDs and stores per-surface properties.
// @details Optical-surface analogue of DetectorRegistry.  Assigns sequential
//   IDs to registered surfaces and provides lookup by pointer or name, plus
//   property retrieval by ID.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef SURFACE_REGISTRY_HH
#define SURFACE_REGISTRY_HH

#include "G4OpticalSurface.hh"
#include <map>
#include <vector>
#include <string>

/// @brief Per-surface optical properties.
struct SurfaceProperties {
    G4OpticalSurfaceModel model = unified;
    G4OpticalSurfaceFinish finish = polished;
    G4SurfaceType type = dielectric_metal;
    double reflectivity = 0.0;
    double efficiency = 1.0;
    double sigma_alpha = 0.0;
};

/// @brief Maps optical surfaces to integer IDs with per-surface properties.
class SurfaceRegistry {
public:
    SurfaceRegistry() = default;
    ~SurfaceRegistry() = default;

    int RegisterSurface(G4OpticalSurface* surf, const std::string& name, const SurfaceProperties& props);
    int GetSurfaceID(G4OpticalSurface* surf) const;
    int GetSurfaceID(const std::string& name) const; 
    const SurfaceProperties& GetProperties(int id) const;
    const std::vector<std::pair<G4OpticalSurface*, SurfaceProperties>>& GetAll() const { return fSurfaces; }
    void Clear();

private:
    std::map<std::string, int> fNameToID; 
    std::map<G4OpticalSurface*, int> fSurfaceToID;
    std::vector<std::pair<G4OpticalSurface*, SurfaceProperties>> fSurfaces;
};

#endif