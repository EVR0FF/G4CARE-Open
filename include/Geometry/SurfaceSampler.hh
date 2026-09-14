#ifndef SURFACESAMPLER_HH
#define SURFACESAMPLER_HH

//==============================================================================
// G4CARE
// @file    SurfaceSampler.hh
// @brief   Uniform random sampling on the surface of Geant4 volumes
// @details Tessellates a G4VPhysicalVolume's solid surface into
//   triangles via G4Polyhedron, computes cumulative areas, and
//   samples points uniformly by area-weighted random selection
//   followed by barycentric interpolation within the chosen triangle.
//   Supports construction from a physical volume pointer or
//   by volume name (lookup via G4PhysicalVolumeStore).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4ThreeVector.hh"
#include "G4VSolid.hh"
#include "G4Polyhedron.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4AffineTransform.hh"
#include "Randomize.hh"
#include <vector>
#include <memory>

class SurfaceSampler {
public:
    /// @brief Constructor from a physical volume pointer
    ///
    /// Builds the triangle tessellation from the volume's solid
    /// polyhedron, applying the volume's affine transform.
    /// @param physVol Pointer to the physical volume
    SurfaceSampler(G4VPhysicalVolume* physVol);

    /// @brief Constructor from a volume name
    ///
    /// Looks up the physical volume by name in G4PhysicalVolumeStore
    /// and delegates to the pointer constructor.
    /// @param volumeName Name of the physical volume
    SurfaceSampler(const G4String& volumeName);

    /// @brief Destructor
    virtual ~SurfaceSampler() = default;

    /// @brief Sample a random point on the triangulated surface
    ///
    /// Selects a triangle with probability proportional to its area,
    /// then generates a uniformly random barycentric point within it.
    /// @return Random point on the surface in global coordinates
    G4ThreeVector SamplePoint();

    /// @brief Check if the sampler was initialized successfully
    /// @return true if the triangle tessellation is valid
    bool IsValid() const { return fValid; }

private:
    /// @brief Triangle with precomputed area for area-weighted sampling
    struct Triangle {
        G4ThreeVector v0, v1, v2;  ///< Vertex coordinates
        G4double area;              ///< Precomputed triangle area

        /// @brief Construct a triangle from three vertices
        /// @param a Vertex 0
        /// @param b Vertex 1
        /// @param c Vertex 2
        Triangle(const G4ThreeVector& a, const G4ThreeVector& b, const G4ThreeVector& c);
    };

    /// @brief Initialize tessellation from a G4Polyhedron
    ///
    /// Iterates over facets, converts to triangles, and builds
    /// cumulative area array for weighted sampling.
    /// @param poly      The polyhedron to tessellate
    /// @param transform Optional affine transform to apply to vertices
    void InitializeFromPolyhedron(G4Polyhedron* poly, const G4AffineTransform* transform = nullptr);

    /// @brief Add a quadrilateral as two triangles
    /// @param v0 First vertex
    /// @param v1 Second vertex
    /// @param v2 Third vertex
    /// @param v3 Fourth vertex
    void AddQuadrilateral(const G4ThreeVector& v0, const G4ThreeVector& v1,
                          const G4ThreeVector& v2, const G4ThreeVector& v3);

    /// @brief All triangles in the tessellation
    std::vector<Triangle> fTriangles;

    /// @brief Cumulative normalized areas for binary search
    std::vector<G4double> fCumulativeAreas;

    /// @brief Total surface area
    G4double fTotalArea = 0.0;

    /// @brief Validity flag
    bool fValid = false;
};

#endif