//==============================================================================
//
// G4CARE
//
// @file    VoxelizedPhantom.cc
// @brief   Implementation of VoxelizedPhantom.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#include "VoxelizedPhantom.hh"

#include "CTSeries.hh"
#include "HUToMaterialMap.hh"

#include "G4PhantomParameterisation.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4PVParameterised.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"

#include <cmath>
#include <iostream>
#include <unordered_map>
#include <utility>

VoxelizedPhantom::~VoxelizedPhantom() {
    delete fParam;
}

bool VoxelizedPhantom::Build(const CTSeries& ct, HUToMaterialMap& huMap,
                             G4LogicalVolume* worldLV, DicomAxes axes) {
    if (!ct.valid || ct.hu.empty() || ct.nx <= 0 || ct.ny <= 0 || ct.nz <= 0) {
        std::cerr << "[VoxelizedPhantom] Invalid CTSeries." << std::endl;
        return false;
    }
    if (!worldLV) {
        std::cerr << "[VoxelizedPhantom] Null world volume." << std::endl;
        return false;
    }

    const std::size_t nx = static_cast<std::size_t>(ct.nx);
    const std::size_t ny = static_cast<std::size_t>(ct.ny);
    const std::size_t nz = static_cast<std::size_t>(ct.nz);
    const std::size_t nVoxels = nx * ny * nz;

    // DICOM directions: row (IOP[0..2]), column (IOP[3..5]).
    double rowDir[3] = {ct.direction[0], ct.direction[1], ct.direction[2]};
    double colDir[3] = {ct.direction[3], ct.direction[4], ct.direction[5]};
    if (axes == DicomAxes::RAS) {
        for (int i = 0; i < 2; ++i) {  // flip X (Left->Right) and Y (Posterior->Anterior)
            rowDir[i] = -rowDir[i];
            colDir[i] = -colDir[i];
        }
    }
    // Slice normal = row x col.
    const double nrm[3] = {
        rowDir[1] * colDir[2] - rowDir[2] * colDir[1],
        rowDir[2] * colDir[0] - rowDir[0] * colDir[2],
        rowDir[0] * colDir[1] - rowDir[1] * colDir[0]
    };

    // Map each direction to a Geant4 axis (0=x,1=y,2=z) and a sign.
    auto MapToAxis = [](const double d[3]) {
        int axis = 0;
        double m = std::fabs(d[0]);
        if (std::fabs(d[1]) > m) { m = std::fabs(d[1]); axis = 1; }
        if (std::fabs(d[2]) > m) { axis = 2; }
        const int sign = (d[axis] >= 0.0) ? 1 : -1;
        return std::make_pair(axis, sign);
    };
    int colAxis, colSign, rowAxis, rowSign, nAxis, nSign;
    std::tie(colAxis, colSign) = MapToAxis(colDir);
    std::tie(rowAxis, rowSign) = MapToAxis(rowDir);
    std::tie(nAxis, nSign) = MapToAxis(nrm);

    if (colAxis == rowAxis || colAxis == nAxis || rowAxis == nAxis) {
        std::cerr << "[VoxelizedPhantom] Oblique acquisition: cannot build axis-aligned grid."
                  << std::endl;
        return false;
    }

    // Per-axis extent and half voxel size.
    std::size_t gN[3];
    G4double gHalf[3];
    gN[colAxis] = nx; gHalf[colAxis] = 0.5 * ct.dxMm * mm;
    gN[rowAxis] = ny; gHalf[rowAxis] = 0.5 * ct.dyMm * mm;
    gN[nAxis]   = nz; gHalf[nAxis]   = 0.5 * ct.dzMm * mm;

    // Which DICOM index each G4 axis corresponds to: 0=col(ix), 1=row(iy), 2=slice(iz).
    int var[3], varSign[3], varN[3];
    var[colAxis] = 0; varSign[colAxis] = colSign; varN[colAxis] = static_cast<int>(nx);
    var[rowAxis] = 1; varSign[rowAxis] = rowSign; varN[rowAxis] = static_cast<int>(ny);
    var[nAxis]   = 2; varSign[nAxis]   = nSign;   varN[nAxis]   = static_cast<int>(nz);

    // Store the mapping for later organ-label reordering.
    for (int a = 0; a < 3; ++a) {
        fGN[a] = gN[a];
        fVar[a] = var[a];
        fVarSign[a] = varSign[a];
        fVarN[a] = varN[a];
    }
    fNx = static_cast<int>(nx);
    fNy = static_cast<int>(ny);
    fNz = static_cast<int>(nz);

    // Distinct materials.
    fMaterials = huMap.GetMaterials();
    std::unordered_map<G4Material*, std::size_t> matToIdx;
    for (std::size_t k = 0; k < fMaterials.size(); ++k) matToIdx[fMaterials[k]] = k;

    // Per-voxel material index in G4 grid order.
    fMaterialIndices.assign(nVoxels, 0);
    int g[3];
    for (g[2] = 0; g[2] < static_cast<int>(gN[2]); ++g[2]) {
        for (g[1] = 0; g[1] < static_cast<int>(gN[1]); ++g[1]) {
            for (g[0] = 0; g[0] < static_cast<int>(gN[0]); ++g[0]) {
                int dicom[3] = {0, 0, 0};
                for (int a = 0; a < 3; ++a) {
                    const int idx = g[a];
                    dicom[var[a]] = (varSign[a] > 0) ? idx : (varN[a] - 1 - idx);
                }
                const short hu = ct.HU(dicom[0], dicom[1], dicom[2]);
                G4Material* m = huMap.GetMaterial(hu);
                auto it = matToIdx.find(m);
                const std::size_t mi = (it != matToIdx.end()) ? it->second : 0;
                const std::size_t lin = static_cast<std::size_t>(g[0])
                    + static_cast<std::size_t>(g[1]) * gN[0]
                    + static_cast<std::size_t>(g[2]) * gN[0] * gN[1];
                fMaterialIndices[lin] = mi;
            }
        }
    }

    // Parameterisation.
    fParam = new G4PhantomParameterisation();
    fParam->SetVoxelDimensions(gHalf[0], gHalf[1], gHalf[2]);
    fParam->SetNoVoxels(gN[0], gN[1], gN[2]);
    fParam->SetMaterials(fMaterials);
    fParam->SetMaterialIndices(fMaterialIndices.data());

    // Container volume (full phantom bounding box).
    auto* containerSolid = new G4Box("PhantomContainer", gN[0] * gHalf[0],
                                     gN[1] * gHalf[1], gN[2] * gHalf[2]);
    G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
    auto* containerLV = new G4LogicalVolume(containerSolid, air, "PhantomContainer_LV");

    // Container centre in the Geant4 frame.
    double origin[3] = {ct.originMm[0], ct.originMm[1], ct.originMm[2]};
    if (axes == DicomAxes::RAS) { origin[0] = -origin[0]; origin[1] = -origin[1]; }
    const double dicomSize[3] = {ct.dxMm * mm, ct.dyMm * mm, ct.dzMm * mm};
    const int dicomN[3] = {static_cast<int>(nx), static_cast<int>(ny), static_cast<int>(nz)};
    G4double center[3];
    for (int a = 0; a < 3; ++a) {
        const int v = var[a];
        center[a] = origin[a] + varSign[a] * (dicomN[v] - 1) * dicomSize[v] / 2.0;
    }
    const G4ThreeVector containerPos(center[0], center[1], center[2]);
    auto* containerPhys = new G4PVPlacement(nullptr, containerPos, containerLV,
                                            "PhantomContainer", worldLV, false, 0);

    // Voxel logical volume (material overridden by the parameterisation).
    auto* voxelSolid = new G4Box("PhantomVoxel", gHalf[0], gHalf[1], gHalf[2]);
    auto* voxelLV = new G4LogicalVolume(voxelSolid, fMaterials.front(), "PhantomVoxel_LV");
    fVoxelLV = voxelLV;

    fParam->BuildContainerSolid(containerPhys);
    fParam->CheckVoxelsFillContainer(gN[0] * gHalf[0], gN[1] * gHalf[1], gN[2] * gHalf[2]);

    auto* phantomPV = new G4PVParameterised("PhantomVoxels", voxelLV, containerLV,
                                            kXAxis, static_cast<G4int>(nVoxels), fParam);
    phantomPV->SetRegularStructureId(1);

    fContainerLV = containerLV;
    std::cout << "[VoxelizedPhantom] Built " << gN[0] << "x" << gN[1] << "x" << gN[2]
              << " voxel phantom (" << nVoxels << " voxels) at "
              << containerPos / mm << " mm (axes="
              << (axes == DicomAxes::RAS ? "RAS" : "LPS") << ")." << std::endl;
    return true;
}

void VoxelizedPhantom::SetOrganLabelsFromCT(const std::vector<int>& ctLabels) {
    const std::size_t nVoxels = fGN[0] * fGN[1] * fGN[2];
    fOrganLabels.assign(nVoxels, -1);
    const std::size_t expected = static_cast<std::size_t>(fNx) * fNy * fNz;
    if (ctLabels.size() != expected) return;

    int g[3];
    for (g[2] = 0; g[2] < static_cast<int>(fGN[2]); ++g[2]) {
        for (g[1] = 0; g[1] < static_cast<int>(fGN[1]); ++g[1]) {
            for (g[0] = 0; g[0] < static_cast<int>(fGN[0]); ++g[0]) {
                int dicom[3] = {0, 0, 0};
                for (int a = 0; a < 3; ++a) {
                    const int idx = g[a];
                    dicom[fVar[a]] = (fVarSign[a] > 0) ? idx : (fVarN[a] - 1 - idx);
                }
                const int ctLinear = dicom[2] * (fNx * fNy) + dicom[1] * fNx + dicom[0];
                const std::size_t g4Linear = static_cast<std::size_t>(g[0])
                    + static_cast<std::size_t>(g[1]) * fGN[0]
                    + static_cast<std::size_t>(g[2]) * fGN[0] * fGN[1];
                fOrganLabels[g4Linear] = ctLabels[ctLinear];
            }
        }
    }
}
