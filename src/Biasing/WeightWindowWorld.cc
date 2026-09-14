//==============================================================================
//
// G4CARE
//
// @file    WeightWindowWorld.cc
// @brief   Parallel world for Weight Window biasing (scoring mesh).
//
// @details
//   Implements a G4VUserParallelWorld containing a regular grid of scoring
//   cells (via G4PVParameterised) or GDML-defined mesh.  Used by
//   WeightWindowManager to track particle fluence for importance biasing.
//
//   Configuration keys read: none (receives grid/GDML from WeightWindowManager).
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "WeightWindowWorld.hh"
#include "WeightWindowManager.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4SystemOfUnits.hh"
#include "G4GDMLParser.hh"
#include "G4ios.hh"
#include "G4NistManager.hh"
#include "G4PVParameterised.hh"

/// @brief Parameterisation based entirely on RegularGrid.
class CellParameterisation : public G4VPVParameterisation {
public:
    CellParameterisation(const RegularGrid<double>* grid) : fGrid(grid) {}
    
    void ComputeTransformation(const G4int copyNo, G4VPhysicalVolume* physVol) const override {
        physVol->SetTranslation(fGrid->GetVoxelCenter(copyNo));
        physVol->SetRotation(nullptr);
    }
    
private:
    const RegularGrid<double>* fGrid;
};

WeightWindowWorld::WeightWindowWorld(G4String worldName)
    : G4VUserParallelWorld(worldName)
{}

WeightWindowWorld::~WeightWindowWorld() = default;

void WeightWindowWorld::SetGrid(const RegularGrid<double>* grid) {
    fGrid = grid;
    fUseGdml = false;
}

void WeightWindowWorld::SetGdmlFile(const G4String& filename) {
    fGdmlFile = filename;
    fUseGdml = true;
}

void WeightWindowWorld::Construct() {
    G4cout << "WeightWindowWorld::Construct() started" << G4endl;

    G4VPhysicalVolume* ghostWorld = GetWorld();
    G4LogicalVolume* worldLogical = ghostWorld->GetLogicalVolume();

    // GDML mode (load pre-built geometry)
    if (fUseGdml) {
        G4GDMLParser parser;
        parser.Read(fGdmlFile);
        G4VPhysicalVolume* gdmlWorld = parser.GetWorldVolume();
        if (gdmlWorld) {
            G4LogicalVolume* gdmlLogical = gdmlWorld->GetLogicalVolume();
            new G4PVPlacement(nullptr, G4ThreeVector(), gdmlLogical,
                              "gdmlMesh", worldLogical, false, 0);
            G4cout << "WeightWindowWorld: GDML mesh loaded from " << fGdmlFile << G4endl;
        } else {
            G4cerr << "WeightWindowWorld: failed to load GDML file " << fGdmlFile << G4endl;
        }
        return;
    }

    // Regular grid mode
    if (!fGrid) {
        G4cerr << "WeightWindowWorld: No grid or GDML file specified!" << G4endl;
        return;
    }

    auto* nist = G4NistManager::Instance();
    auto* vacuum = nist->FindOrBuildMaterial("G4_Galactic");

    G4double dx = fGrid->GetDx();
    G4double dy = fGrid->GetDy();
    G4double dz = fGrid->GetDz();

    G4Box* solidCell = new G4Box("cell_solid", dx/2, dy/2, dz/2);
    G4LogicalVolume* logicCell = new G4LogicalVolume(solidCell, vacuum, "cell_logic");

    CellParameterisation* param = new CellParameterisation(fGrid);

    G4int totalVoxels = fGrid->GetTotalVoxels();
    new G4PVParameterised("cell_phys", logicCell, worldLogical,
                          kUndefined, totalVoxels, param);

    G4cout << "WeightWindowWorld: Created " << totalVoxels 
           << " cells via G4PVParameterised using RegularGrid." << G4endl;
}