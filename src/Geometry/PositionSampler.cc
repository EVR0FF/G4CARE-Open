//==============================================================================
// G4CARE
// @file    PositionSampler.cc
// @brief   Implementation of uniform position sampling within Geant4 volumes
// @details Implements rejection-based uniform sampling inside G4VSolid
//   objects and local→global coordinate conversion via a thread-local
//   affine transform cache. The cache is built by traversing the
//   geometry tree from the world volume. Conservative bounding boxes
//   are computed via G4VSolid::CalculateExtent with a 10 m fallback.
//   After maxTries rejection failures, falls back to GetPointOnSurface().
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "PositionSampler.hh"
#include "G4VoxelLimits.hh"
#include "G4RandomTools.hh"
#include "G4SystemOfUnits.hh"
#include "G4GeometryTolerance.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4TransportationManager.hh"
#include "G4Navigator.hh"
#include "G4AffineTransform.hh"
#include "G4ios.hh"
#include <cmath>

/// @brief Thread-local transform cache: physical volume → global affine transform
G4ThreadLocal std::unordered_map<G4VPhysicalVolume*, G4AffineTransform>* PositionSampler::fgTransformCache = nullptr;

/// @brief Thread-local world physical volume pointer
G4ThreadLocal G4VPhysicalVolume* PositionSampler::fgWorldPV = nullptr;

/// @brief Precompute global transforms for all physical volumes
///
/// Allocates/clears the thread-local cache and recursively traverses
/// the geometry tree from the world volume, computing cumulative
/// affine transforms for each physical volume.
void PositionSampler::PrecomputeTransforms() {
    if (!fgTransformCache) {
        fgTransformCache = new std::unordered_map<G4VPhysicalVolume*, G4AffineTransform>();
    } else {
        fgTransformCache->clear();
    }

    G4PhysicalVolumeStore* store = G4PhysicalVolumeStore::GetInstance();
    if (!store) {
        G4cerr << "PositionSampler::PrecomputeTransforms: PhysicalVolumeStore not available!" << G4endl;
        return;
    }
    fgWorldPV = nullptr;
    for (auto pv : *store) {
        if (pv->GetName() == "World") {
            fgWorldPV = pv;
            break;
        }
    }
    if (!fgWorldPV) {
        G4cerr << "PositionSampler::PrecomputeTransforms: World volume not found in store!" << G4endl;
        return;
    }

    G4AffineTransform identityTransform;
    CacheVolumeTransform(fgWorldPV, identityTransform);

    G4cout << "PositionSampler: Cached " << fgTransformCache->size()
           << " volume transforms for thread " << G4Threading::G4GetThreadId() << G4endl;
}

/// @brief Recursively cache transforms for a volume and its daughters
///
/// Stores the parentTransform for the given PV, then recurses into
/// daughters with the compound transform.
/// @param pv              Current physical volume
/// @param parentTransform Accumulated parent affine transform
void PositionSampler::CacheVolumeTransform(G4VPhysicalVolume* pv,
                                           const G4AffineTransform& parentTransform) {
    if (!pv) return;
    (*fgTransformCache)[pv] = parentTransform;

    G4LogicalVolume* lv = pv->GetLogicalVolume();
    if (!lv) return;

    for (size_t i = 0; i < lv->GetNoDaughters(); ++i) {
        G4VPhysicalVolume* daughter = lv->GetDaughter(i);
        G4AffineTransform localTransform;
        if (daughter->GetRotation()) {
            localTransform = G4AffineTransform(*daughter->GetRotation(), daughter->GetTranslation());
        } else {
            localTransform = G4AffineTransform(daughter->GetTranslation());
        }
        G4AffineTransform daughterTransform = parentTransform * localTransform;
        CacheVolumeTransform(daughter, daughterTransform);
    }
}

/// @brief Cleanup thread-local transform cache
void PositionSampler::Cleanup() {
    if (fgTransformCache) {
        delete fgTransformCache;
        fgTransformCache = nullptr;
    }
    fgWorldPV = nullptr;
}

/// @brief Convert a local point to global coordinates using the cache
///
/// Looks up the PV's global transform in the thread-local cache.
/// If the cache is empty, triggers PrecomputeTransforms().
/// @param localPos Point in local volume coordinates
/// @param pv       The physical volume
/// @return Point in global coordinates
G4ThreeVector PositionSampler::TransformToGlobal(const G4ThreeVector& localPos,
                                                G4VPhysicalVolume* pv) {
    if (!pv) return localPos;

    if (!fgTransformCache || fgTransformCache->empty()) {
        PrecomputeTransforms();
        if (!fgTransformCache || fgTransformCache->empty()) {
            G4Exception("PositionSampler::TransformToGlobal", "PS0001", FatalException,
                        "Unable to initialize transform cache. Geometry may not be ready.");
            return localPos;
        }
    }

    auto it = fgTransformCache->find(pv);
    if (it != fgTransformCache->end()) {
        return it->second.TransformPoint(localPos);
    }

    G4Exception("PositionSampler::TransformToGlobal", "PS0002", JustWarning,
                ("Volume " + pv->GetName() + " not found in cache").c_str());
    return localPos;
}

/// @brief Uniform random point inside a solid (rejection sampling)
///
/// Computes a conservative bounding box via CalculateExtent, then
/// generates random points until one falls inside the solid.
/// Falls back to GetPointOnSurface() after maxTries attempts.
/// @param solid    The G4VSolid to sample
/// @param maxTries Maximum rejection attempts
/// @return Uniform random point in local coordinates
G4ThreeVector PositionSampler::SampleUniformInSolid(G4VSolid* solid, int maxTries) {
    if (!solid) {
        G4cerr << "PositionSampler: solid is null!" << G4endl;
        return G4ThreeVector(0,0,0);
    }

    G4double xmin, xmax, ymin, ymax, zmin, zmax;

    if (!CalculateConservativeBoundingBox(solid, xmin, xmax, ymin, ymax, zmin, zmax)) {
        G4cerr << "PositionSampler: Cannot calculate bounding box for solid: "
               << solid->GetName() << G4endl;
        return G4ThreeVector(0,0,0);
    }

    const G4double safety = 1e-6 * mm;
    xmin -= safety; xmax += safety;
    ymin -= safety; ymax += safety;
    zmin -= safety; zmax += safety;

    int tries = 0;
    for (tries = 0; tries < maxTries; ++tries) {
        G4ThreeVector point(
            G4RandFlat::shoot(xmin, xmax),
            G4RandFlat::shoot(ymin, ymax),
            G4RandFlat::shoot(zmin, zmax)
        );

        if (solid->Inside(point) == kInside) {
            return point;
        }
    }

    G4cerr << "PositionSampler: Failed to sample point in solid '"
           << solid->GetName() << "' after " << maxTries << " attempts" << G4endl;

    try {
        G4ThreeVector surfacePoint = solid->GetPointOnSurface();
        return surfacePoint;
    } catch (...) {
        G4cerr << "PositionSampler: Fallback also failed for solid: "
               << solid->GetName() << G4endl;
    }

    return G4ThreeVector(0,0,0);
}

/// @brief Compute a conservative bounding box for a solid
///
/// Uses G4VSolid::CalculateExtent for each axis. Falls back to
/// a ±10 m box if any axis calculation fails.
/// @param solid The solid to bound
/// @param xmin  Output: minimum X [mm]
/// @param xmax  Output: maximum X [mm]
/// @param ymin  Output: minimum Y [mm]
/// @param ymax  Output: maximum Y [mm]
/// @param zmin  Output: minimum Z [mm]
/// @param zmax  Output: maximum Z [mm]
/// @return true (always succeeds due to fallback)
bool PositionSampler::CalculateConservativeBoundingBox(G4VSolid* solid,
                                                      G4double& xmin, G4double& xmax,
                                                      G4double& ymin, G4double& ymax,
                                                      G4double& zmin, G4double& zmax) {
    if (!solid) return false;

    G4VoxelLimits voxelLimits;
    G4AffineTransform identityTransform;

    G4bool okX = solid->CalculateExtent(kXAxis, voxelLimits, identityTransform, xmin, xmax);
    G4bool okY = solid->CalculateExtent(kYAxis, voxelLimits, identityTransform, ymin, ymax);
    G4bool okZ = solid->CalculateExtent(kZAxis, voxelLimits, identityTransform, zmin, zmax);

    if (okX && okY && okZ) {
        return true;
    }

    const G4double conservativeSize = 10.0 * m;

    if (!okX) { xmin = -conservativeSize; xmax = conservativeSize; }
    if (!okY) { ymin = -conservativeSize; ymax = conservativeSize; }
    if (!okZ) { zmin = -conservativeSize; zmax = conservativeSize; }

    return true;
}

/// @brief Random point inside a physical volume (local sample → global)
///
/// Samples uniformly in the logical volume's solid, then converts
/// to global coordinates using the transform cache.
/// @param pv The physical volume to sample
/// @return Random point in global coordinates
G4ThreeVector PositionSampler::SampleInPhysicalVolume(G4VPhysicalVolume* pv) {
    if (!pv) return G4ThreeVector(0,0,0);

    G4LogicalVolume* lv = pv->GetLogicalVolume();
    if (!lv) return G4ThreeVector(0,0,0);
    G4ThreeVector localPos = SampleUniformInSolid(lv->GetSolid());
    return TransformToGlobal(localPos, pv);
}

/// @brief Random point inside a logical volume (finds a physical instance)
///
/// Searches G4PhysicalVolumeStore for physical volumes using this
/// logical volume, picks one uniformly at random, and samples inside.
/// @param lv The logical volume to sample
/// @return Random point in global coordinates
G4ThreeVector PositionSampler::SampleInLogicalVolume(G4LogicalVolume* lv) {
    if (!lv) {
        G4cerr << "PositionSampler: Logical volume is null!" << G4endl;
        return G4ThreeVector(0,0,0);
    }

    G4PhysicalVolumeStore* pvStore = G4PhysicalVolumeStore::GetInstance();
    if (!pvStore) {
        G4cerr << "PositionSampler: Physical volume store is null!" << G4endl;
        return G4ThreeVector(0,0,0);
    }

    std::vector<G4VPhysicalVolume*> matchingVolumes;
    for (auto pv : *pvStore) {
        if (pv->GetLogicalVolume() == lv) {
            matchingVolumes.push_back(pv);
        }
    }

    if (matchingVolumes.empty()) {
        G4cerr << "PositionSampler: No physical volume found for logical volume: "
               << lv->GetName() << G4endl;
        return G4ThreeVector(0,0,0);
    }

    size_t idx = static_cast<size_t>(G4UniformRand() * matchingVolumes.size());
    if (idx >= matchingVolumes.size()) idx = matchingVolumes.size() - 1;

    return SampleInPhysicalVolume(matchingVolumes[idx]);
}