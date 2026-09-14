//==============================================================================
// G4CARE
// @file    SurfaceSampler.cc
// @brief   Implementation of uniform surface sampling via polyhedron tessellation
// @details Implements the SurfaceSampler class: tessellates a
//   G4Polyhedron into triangles, computes cumulative areas for
//   area-weighted random triangle selection, and samples points
//   uniformly using barycentric coordinates within the chosen triangle.
//   Supports facets with 3, 4 (split into two triangles), and more
//   vertices (fan triangulation from vertex 0). Quadrilaterals are
//   added via AddQuadrilateral().
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "SurfaceSampler.hh"
#include "G4LogicalVolume.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4AffineTransform.hh"
#include "G4Polyhedron.hh"
#include "Randomize.hh"
#include "G4ios.hh"

/// @brief Construct a triangle from three vertices and compute its area
/// @param a Vertex 0
/// @param b Vertex 1
/// @param c Vertex 2
SurfaceSampler::Triangle::Triangle(const G4ThreeVector& a, const G4ThreeVector& b, const G4ThreeVector& c)
    : v0(a), v1(b), v2(c)
{
    G4ThreeVector cross = (b - a).cross(c - a);
    area = 0.5 * cross.mag();
}

/// @brief Constructor from a physical volume pointer
///
/// Retrieves the solid's G4Polyhedron, applies the volume's affine
/// transform, and builds the triangle tessellation.
/// @param physVol Pointer to the physical volume
SurfaceSampler::SurfaceSampler(G4VPhysicalVolume* physVol) {
    if (!physVol) {
        G4cerr << "SurfaceSampler: null physical volume" << G4endl;
        return;
    }

    G4LogicalVolume* logVol = physVol->GetLogicalVolume();
    if (!logVol) {
        G4cerr << "SurfaceSampler: logical volume is null" << G4endl;
        return;
    }

    G4VSolid* solid = logVol->GetSolid();
    if (!solid) {
        G4cerr << "SurfaceSampler: solid is null" << G4endl;
        return;
    }

    G4Polyhedron* poly = solid->GetPolyhedron();
    if (!poly) {
        G4cerr << "SurfaceSampler: failed to get polyhedron from solid" << G4endl;
        return;
    }

    G4RotationMatrix* rot = physVol->GetRotation();
    const G4ThreeVector& trans = physVol->GetTranslation();
    G4AffineTransform transform;
    if (rot) {
        transform = G4AffineTransform(*rot, trans);
    } else {
        transform = G4AffineTransform(trans);
    }

    InitializeFromPolyhedron(poly, &transform);
}

/// @brief Constructor from a volume name
///
/// Looks up the physical volume in G4PhysicalVolumeStore by name.
/// @param volumeName Name of the physical volume
SurfaceSampler::SurfaceSampler(const G4String& volumeName) {
    G4PhysicalVolumeStore* store = G4PhysicalVolumeStore::GetInstance();
    if (!store) {
        G4cerr << "SurfaceSampler: PhysicalVolumeStore not available" << G4endl;
        return;
    }

    G4VPhysicalVolume* physVol = store->GetVolume(volumeName, false);
    if (!physVol) {
        G4cerr << "SurfaceSampler: volume '" << volumeName << "' not found" << G4endl;
        return;
    }

    *this = SurfaceSampler(physVol);
}

/// @brief Initialize tessellation from a G4Polyhedron
///
/// Iterates over all facets: 3-vertex → single triangle,
/// 4-vertex → two triangles, >4 → fan from vertex 0.
/// Builds cumulative area array for weighted sampling.
/// @param poly      The polyhedron to tessellate
/// @param transform Optional affine transform to apply to vertices
void SurfaceSampler::InitializeFromPolyhedron(G4Polyhedron* poly, const G4AffineTransform* transform) {
    if (!poly) return;

    G4int numFacets = poly->GetNoFacets();
    G4cout << "SurfaceSampler: processing " << numFacets << " facets" << G4endl;

    const G4int MAX_VERTICES = 12;
    G4int indices[MAX_VERTICES];
    G4int edgeFlags[MAX_VERTICES];
    G4int nVertices;

    for (G4int i = 0; i < numFacets; ++i) {
        poly->GetFacet(i, nVertices, indices, edgeFlags);
        if (nVertices < 3) continue;

        std::vector<G4Point3D> points(nVertices);
        for (G4int j = 0; j < nVertices; ++j) {
            points[j] = poly->GetVertex(indices[j]);
        }

        std::vector<G4ThreeVector> vecs(nVertices);
        for (G4int j = 0; j < nVertices; ++j) {
            G4ThreeVector v(points[j].x(), points[j].y(), points[j].z());
            if (transform) {
                v = transform->TransformPoint(v);
            }
            vecs[j] = v;
        }

        if (nVertices == 3) {
            fTriangles.emplace_back(vecs[0], vecs[1], vecs[2]);
        } else if (nVertices == 4) {
            fTriangles.emplace_back(vecs[0], vecs[1], vecs[2]);
            fTriangles.emplace_back(vecs[0], vecs[2], vecs[3]);
        } else {
            for (G4int j = 1; j < nVertices - 1; ++j) {
                fTriangles.emplace_back(vecs[0], vecs[j], vecs[j+1]);
            }
        }
    }

    if (fTriangles.empty()) {
        G4cerr << "SurfaceSampler: no triangles generated" << G4endl;
        return;
    }

    fTotalArea = 0.0;
    for (const auto& tri : fTriangles) {
        fTotalArea += tri.area;
    }

    fCumulativeAreas.resize(fTriangles.size());
    double cum = 0.0;
    for (size_t i = 0; i < fTriangles.size(); ++i) {
        cum += fTriangles[i].area;
        fCumulativeAreas[i] = cum / fTotalArea;
    }

    G4cout << "SurfaceSampler: created " << fTriangles.size()
           << " triangles, total area = " << fTotalArea / CLHEP::cm2 << " cm^2" << G4endl;

    fValid = true;
}

/// @brief Sample a uniformly random point on the surface
///
/// Selects a triangle with probability proportional to its area
/// (binary search in cumulative areas), then generates a uniformly
/// random point using barycentric coordinates.
/// @return Random point on the triangulated surface
G4ThreeVector SurfaceSampler::SamplePoint() {
    if (!fValid || fTriangles.empty()) {
        G4cerr << "SurfaceSampler: not initialized or empty" << G4endl;
        return G4ThreeVector(0,0,0);
    }

    double r = G4UniformRand();
    size_t idx = 0;
    while (idx < fCumulativeAreas.size() - 1 && r > fCumulativeAreas[idx]) {
        ++idx;
    }

    const Triangle& tri = fTriangles[idx];

    double a = G4UniformRand();
    double b = G4UniformRand();
    if (a + b > 1.0) {
        a = 1.0 - a;
        b = 1.0 - b;
    }
    double c = 1.0 - a - b;

    return a * tri.v0 + b * tri.v1 + c * tri.v2;
}