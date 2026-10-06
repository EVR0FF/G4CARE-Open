//==============================================================================
//
// G4CARE — example custom geometry shared library (.so).
//
// Compile (adjust paths):
//   g++ -std=c++20 -fPIC -shared \
//       -I $GEANT4_INSTALL/include/Geant4 \
//       ExampleGeometry.cc -o libExampleGeometry.so \
//       -L $GEANT4_INSTALL/lib -lG4geometry -lG4global -lG4materials
//
// Then use in a config:
//   GEOMETRY:
//     LIBRARY: "libExampleGeometry.so"
//     CONSTRUCTOR: "ExampleGeometry"
//
// The exported factory symbol is "createExampleGeometry" (extern "C").
//
//==============================================================================

#include "G4VUserDetectorConstruction.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4NistManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"

class ExampleGeometry : public G4VUserDetectorConstruction {
public:
    G4VPhysicalVolume* Construct() override {
        G4NistManager* nist = G4NistManager::Instance();
        G4Material* air = nist->FindOrBuildMaterial("G4_AIR");

        G4Box* worldSolid = new G4Box("World", 1 * m, 1 * m, 1 * m);
        G4LogicalVolume* worldLV = new G4LogicalVolume(worldSolid, air, "World");
        return new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "World",
                                 nullptr, false, 0);
    }
};

extern "C" G4VUserDetectorConstruction* createExampleGeometry() {
    return new ExampleGeometry();
}
