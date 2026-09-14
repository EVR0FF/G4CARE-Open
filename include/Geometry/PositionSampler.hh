#ifndef POSITIONSAMPLER_HH
#define POSITIONSAMPLER_HH

//==============================================================================
// G4CARE
// @file    PositionSampler.hh
// @brief   Uniform position sampling within Geant4 volumes
// @details Provides static methods for uniform random position sampling
//   inside G4VSolid, G4VPhysicalVolume, and G4LogicalVolume objects.
//   Uses rejection sampling with conservative bounding boxes computed
//   via G4VSolid::CalculateExtent. Maintains a thread-local transform
//   cache mapping physical volumes to their global affine transforms
//   for efficient local→global coordinate conversion.
//   Falls back to surface points after maxTries rejection attempts.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4ThreeVector.hh"
#include "G4VSolid.hh"
#include "G4VPhysicalVolume.hh"
#include "G4AffineTransform.hh"
#include <vector>
#include <unordered_map>

class G4Navigator;

class PositionSampler {
public:
    /// @brief Destructor (cleans up thread-local transform cache)
    ~PositionSampler() { Cleanup(); }

    /// @brief Uniform random point inside a solid (rejection sampling)
    /// @param solid    The G4VSolid to sample
    /// @param maxTries Maximum rejection attempts before fallback
    /// @return Uniform random point in local coordinates
    static G4ThreeVector SampleUniformInSolid(G4VSolid* solid, int maxTries = 100000);

    /// @brief Random point inside a physical volume (local sample → global)
    /// @param pv The physical volume to sample
    /// @return Random point in global coordinates
    static G4ThreeVector SampleInPhysicalVolume(G4VPhysicalVolume* pv);

    /// @brief Random point inside a logical volume (finds a physical instance)
    ///
    /// Searches G4PhysicalVolumeStore for volumes using this logical
    /// volume, picks one uniformly, and samples inside it.
    /// @param lv The logical volume to sample
    /// @return Random point in global coordinates
    static G4ThreeVector SampleInLogicalVolume(G4LogicalVolume* lv);

    /// @brief Precompute global transforms for all physical volumes
    ///
    /// Must be called after geometry is fully constructed.
    /// Rebuilds the thread-local transform cache by traversing the
    /// geometry tree from the world volume.
    static void PrecomputeTransforms();

    /// @brief Cleanup thread-local transform cache
    static void Cleanup();

    /// @brief Check if the transform cache is initialized
    /// @return true if the cache exists and is non-empty
    static G4bool IsInitialized() {
        return fgTransformCache != nullptr && !fgTransformCache->empty();
    }

private:
    /// @brief Thread-local cache: physical volume → global affine transform
    static G4ThreadLocal std::unordered_map<G4VPhysicalVolume*, G4AffineTransform>* fgTransformCache;

    /// @brief Thread-local world physical volume pointer
    static G4ThreadLocal G4VPhysicalVolume* fgWorldPV;

    /// @brief Recursively cache transforms for a volume and its daughters
    /// @param pv              Current physical volume
    /// @param parentTransform Accumulated parent transform
    static void CacheVolumeTransform(G4VPhysicalVolume* pv, const G4AffineTransform& parentTransform);

    /// @brief Convert a local point to global coordinates using the cache
    /// @param localPos Point in local volume coordinates
    /// @param pv       The physical volume
    /// @return Point in global coordinates
    static G4ThreeVector TransformToGlobal(const G4ThreeVector& localPos, G4VPhysicalVolume* pv);

    /// @brief Compute a conservative bounding box for a solid
    ///
    /// Uses G4VSolid::CalculateExtent for each axis. Falls back to
    /// a conservative 10 m box if extent calculation fails.
    /// @param solid The solid to bound
    /// @param xmin  Output: minimum X [mm]
    /// @param xmax  Output: maximum X [mm]
    /// @param ymin  Output: minimum Y [mm]
    /// @param ymax  Output: maximum Y [mm]
    /// @param zmin  Output: minimum Z [mm]
    /// @param zmax  Output: maximum Z [mm]
    /// @return true if bounding box was calculated
    static bool CalculateConservativeBoundingBox(G4VSolid* solid,
                                                  G4double& xmin, G4double& xmax,
                                                  G4double& ymin, G4double& ymax,
                                                  G4double& zmin, G4double& zmax);
};

#endif