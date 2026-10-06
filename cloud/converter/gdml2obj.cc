//==============================================================================
// gdml2obj — convert a GDML geometry to a Wavefront OBJ mesh.
//
// Used by the G4CARE cloud to produce a browser-viewable 3D preview of a GDML
// artifact. Tessellates every solid via Geant4's HepPolyhedron and writes the
// world-space triangle/quad mesh (units: mm).
//
// Build (adjust paths):
//   g++ -std=c++17 gdml2obj.cc -o gdml2obj $(geant4-config --cflags) $(geant4-config --libs)
//
// Usage:
//   gdml2obj input.gdml output.obj
//==============================================================================

#include "G4GDMLParser.hh"
#include "G4VPhysicalVolume.hh"
#include "G4LogicalVolume.hh"
#include "G4VSolid.hh"
#include "G4Polyhedron.hh"
#include "G4ThreeVector.hh"
#include "G4RotationMatrix.hh"
#include "G4Transform3D.hh"
#include "G4Point3D.hh"

#include <iostream>
#include <fstream>
#include <string>
#include <cstddef>

namespace {
int gVertexBase = 0;

void writePolyhedron(const G4Polyhedron& poly, const std::string& name, std::ostream& out) {
    const int nv = poly.GetNoVertices();
    out << "o " << name << "\n";
    for (int i = 1; i <= nv; ++i) {
        G4Point3D v = poly.GetVertex(i);
        out << "v " << v.x() << " " << v.y() << " " << v.z() << "\n";
    }
    const int nf = poly.GetNoFacets();
    for (int i = 1; i <= nf; ++i) {
        G4int n = 0;
        G4int nodes[4] = {0, 0, 0, 0};
        poly.GetFacet(i, n, nodes);
        out << "f";
        for (int j = 0; j < n; ++j) {
            out << " " << (nodes[j] + gVertexBase);
        }
        out << "\n";
    }
    gVertexBase += nv;
}

void walk(const G4VPhysicalVolume* pv, const G4Transform3D& global, std::ostream& out) {
    G4LogicalVolume* lv = pv->GetLogicalVolume();
    G4Polyhedron* poly = lv->GetSolid()->GetPolyhedron();
    if (poly) {
        G4Polyhedron transformed(*poly);
        transformed.Transform(global);
        writePolyhedron(transformed, lv->GetSolid()->GetName(), out);
    }

    const G4RotationMatrix* rot = pv->GetObjectRotation();
    const G4ThreeVector& trans = pv->GetObjectTranslation();
    G4Transform3D local = G4Translate3D(trans);
    if (rot) local = local * G4Rotate3D(*rot);
    const G4Transform3D childGlobal = global * local;

    for (std::size_t i = 0; i < lv->GetNoDaughters(); ++i) {
        walk(lv->GetDaughter(i), childGlobal, out);
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: gdml2obj <input.gdml> <output.obj>\n";
        return 1;
    }

    G4GDMLParser parser;
    parser.Read(argv[1]);
    G4VPhysicalVolume* world = parser.GetWorldVolume();
    if (!world) {
        std::cerr << "gdml2obj: no world volume in " << argv[1] << "\n";
        return 1;
    }

    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "gdml2obj: cannot open output " << argv[2] << "\n";
        return 1;
    }

    out << "# gdml2obj — generated from " << argv[1] << "\n";
    G4Transform3D identity;
    walk(world, identity, out);

    std::cerr << "gdml2obj: wrote " << gVertexBase << " vertices to " << argv[2] << "\n";
    return 0;
}
