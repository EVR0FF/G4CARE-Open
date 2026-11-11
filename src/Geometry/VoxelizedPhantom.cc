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
#include "DicomService.hh"
#include "HUToMaterialMap.hh"
#include "NestedPhantomParameterisation.hh"

#include "G4PhantomParameterisation.hh"
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4PVParameterised.hh"
#include "G4PVReplica.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <utility>

VoxelizedPhantom::~VoxelizedPhantom() {
    delete fParam;
    delete fNestedParam;
}

bool VoxelizedPhantom::Setup(const CTSeries& ct, HUToMaterialMap& huMap, DicomAxes axes) {
    if (!ct.valid || ct.hu.empty() || ct.nx <= 0 || ct.ny <= 0 || ct.nz <= 0) {
        std::cerr << "[VoxelizedPhantom] Invalid CTSeries." << std::endl;
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
    // Slice normal = column x row (DICOM: increasing column x increasing row).
    const double nrm[3] = {
        colDir[1] * rowDir[2] - colDir[2] * rowDir[1],
        colDir[2] * rowDir[0] - colDir[0] * rowDir[2],
        colDir[0] * rowDir[1] - colDir[1] * rowDir[0]
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

    // Store the mapping for later organ-label reordering + volume building.
    for (int a = 0; a < 3; ++a) {
        fGN[a] = gN[a];
        fHalfSize[a] = gHalf[a];
        fVar[a] = var[a];
        fVarSign[a] = varSign[a];
        fVarN[a] = varN[a];
    }
    fNx = static_cast<int>(nx);
    fNy = static_cast<int>(ny);
    fNz = static_cast<int>(nz);

    // Distinct materials (HU map + any override materials).
    fMaterials = huMap.GetMaterials();
    for (G4Material* om : fOverrideMaterials) {
        if (om && std::find(fMaterials.begin(), fMaterials.end(), om) == fMaterials.end()) {
            fMaterials.push_back(om);
        }
    }
    std::unordered_map<G4Material*, std::size_t> matToIdx;
    for (std::size_t k = 0; k < fMaterials.size(); ++k) matToIdx[fMaterials[k]] = k;

    // Per-voxel material index in G4 grid order.
    fMaterialIndices.assign(nVoxels, 0);
    const auto t0 = std::chrono::steady_clock::now();
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
                std::size_t mi = static_cast<std::size_t>(huMap.MaterialIndex(hu));
                if (!fOverrideLabels.empty()) {
                    const std::size_t ctLinear = static_cast<std::size_t>(dicom[2]) * nx * ny
                        + static_cast<std::size_t>(dicom[1]) * nx + static_cast<std::size_t>(dicom[0]);
                    const int lbl = fOverrideLabels[ctLinear];
                    if (lbl >= 0 && lbl < static_cast<int>(fOverrideMaterials.size())
                        && fOverrideMaterials[lbl]) {
                        auto it = matToIdx.find(fOverrideMaterials[lbl]);
                        if (it != matToIdx.end()) mi = it->second;
                    }
                }
                const std::size_t lin = static_cast<std::size_t>(g[0])
                    + static_cast<std::size_t>(g[1]) * gN[0]
                    + static_cast<std::size_t>(g[2]) * gN[0] * gN[1];
                fMaterialIndices[lin] = mi;
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::cout << "[VoxelizedPhantom] material assignment: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms for " << nVoxels << " voxels" << std::endl;

    // Container centre in the Geant4 frame.
    double origin[3] = {ct.originMm[0], ct.originMm[1], ct.originMm[2]};
    {
        const double* woff = DicomService::Instance()->GetWorldOffset();
        for (int i = 0; i < 3; ++i) origin[i] -= woff[i];
    }
    if (axes == DicomAxes::RAS) { origin[0] = -origin[0]; origin[1] = -origin[1]; }
    const double dicomSize[3] = {ct.dxMm * mm, ct.dyMm * mm, ct.dzMm * mm};
    const int dicomN[3] = {static_cast<int>(nx), static_cast<int>(ny), static_cast<int>(nz)};
    for (int a = 0; a < 3; ++a) {
        const int v = var[a];
        fContainerPos[a] = origin[a] + varSign[a] * (dicomN[v] - 1) * dicomSize[v] / 2.0;
    }
    return true;
}

bool VoxelizedPhantom::Build(const CTSeries& ct, HUToMaterialMap& huMap,
                             G4LogicalVolume* worldLV, DicomAxes axes,
                             bool skipEqualMaterials) {
    if (!worldLV) {
        std::cerr << "[VoxelizedPhantom] Null world volume." << std::endl;
        return false;
    }
    if (!Setup(ct, huMap, axes)) return false;

    // Parameterisation.
    fParam = new G4PhantomParameterisation();
    fParam->SetVoxelDimensions(fHalfSize[0], fHalfSize[1], fHalfSize[2]);
    fParam->SetNoVoxels(fGN[0], fGN[1], fGN[2]);
    fParam->SetMaterials(fMaterials);
    fParam->SetMaterialIndices(fMaterialIndices.data());
    fParam->SetSkipEqualMaterials(skipEqualMaterials);

    // Container volume (full phantom bounding box).
    auto* containerSolid = new G4Box("PhantomContainer", fGN[0] * fHalfSize[0],
                                     fGN[1] * fHalfSize[1], fGN[2] * fHalfSize[2]);
    G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
    auto* containerLV = new G4LogicalVolume(containerSolid, air, "PhantomContainer_LV");

    auto* containerVis = new G4VisAttributes(G4Colour(0.95, 0.82, 0.35, 0.30));
    containerVis->SetForceWireframe(true);
    containerVis->SetForceSolid(false);
    containerLV->SetVisAttributes(containerVis);

    const G4ThreeVector containerPos(fContainerPos[0], fContainerPos[1], fContainerPos[2]);
    auto* containerPhys = new G4PVPlacement(nullptr, containerPos, containerLV,
                                            "PhantomContainer", worldLV, false, 0);

    // Voxel logical volume (material overridden by the parameterisation).
    auto* voxelSolid = new G4Box("PhantomVoxel", fHalfSize[0], fHalfSize[1], fHalfSize[2]);
    auto* voxelLV = new G4LogicalVolume(voxelSolid, fMaterials.front(), "PhantomVoxel_LV");
    fVoxelLV = voxelLV;

    auto* voxelVis = new G4VisAttributes(G4Colour(0.95, 0.82, 0.35, 1.0));
    voxelVis->SetForceSolid(true);
    voxelLV->SetVisAttributes(voxelVis);

    fParam->BuildContainerSolid(containerPhys);
    fParam->CheckVoxelsFillContainer(fGN[0] * fHalfSize[0], fGN[1] * fHalfSize[1],
                                     fGN[2] * fHalfSize[2]);

    const std::size_t nVoxels = fGN[0] * fGN[1] * fGN[2];
    auto* phantomPV = new G4PVParameterised("PhantomVoxels", voxelLV, containerLV,
                                            kXAxis, static_cast<G4int>(nVoxels), fParam);
    phantomPV->SetRegularStructureId(1);

    fContainerLV = containerLV;
    std::cout << "[VoxelizedPhantom] Built " << fGN[0] << "x" << fGN[1] << "x" << fGN[2]
              << " voxel phantom (" << nVoxels << " voxels) at "
              << containerPos / mm << " mm (axes="
              << (axes == DicomAxes::RAS ? "RAS" : "LPS") << ")." << std::endl;
    return true;
}

bool VoxelizedPhantom::BuildNested(const CTSeries& ct, HUToMaterialMap& huMap,
                                   G4LogicalVolume* worldLV, DicomAxes axes) {
    if (!worldLV) {
        std::cerr << "[VoxelizedPhantom] Null world volume." << std::endl;
        return false;
    }
    if (!Setup(ct, huMap, axes)) return false;

    const G4double hx = fHalfSize[0], hy = fHalfSize[1], hz = fHalfSize[2];
    const G4double fullX = 2.0 * hx * static_cast<double>(fGN[0]);
    const G4double fullZ = 2.0 * hz * static_cast<double>(fGN[2]);
    G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

    // Container (mother).
    auto* containerSolid = new G4Box("PhantomContainer", fullX / 2.0,
                                     fGN[1] * hy, fullZ / 2.0);
    auto* containerLV = new G4LogicalVolume(containerSolid, air, "PhantomContainer_LV");
    auto* containerVis = new G4VisAttributes(G4Colour(0.95, 0.82, 0.35, 0.30));
    containerVis->SetForceWireframe(true);
    containerVis->SetForceSolid(false);
    containerLV->SetVisAttributes(containerVis);

    const G4ThreeVector containerPos(fContainerPos[0], fContainerPos[1], fContainerPos[2]);
    new G4PVPlacement(nullptr, containerPos, containerLV,
                      "PhantomContainer", worldLV, false, 0);

    // Y replica: full X x voxel Y x full Z.
    auto* ySolid = new G4Box("PhantomY", fullX / 2.0, hy, fullZ / 2.0);
    auto* yLV = new G4LogicalVolume(ySolid, air, "PhantomY_LV");
    new G4PVReplica("PhantomY", yLV, containerLV, kYAxis,
                    static_cast<G4int>(fGN[1]), 2.0 * hy);

    // X replica: voxel X x voxel Y x full Z.
    auto* xSolid = new G4Box("PhantomX", hx, hy, fullZ / 2.0);
    auto* xLV = new G4LogicalVolume(xSolid, air, "PhantomX_LV");
    new G4PVReplica("PhantomX", xLV, yLV, kXAxis,
                    static_cast<G4int>(fGN[0]), 2.0 * hx);

    // Z parameterised: voxel X x voxel Y x voxel Z.
    auto* voxelSolid = new G4Box("PhantomVoxel", hx, hy, hz);
    auto* voxelLV = new G4LogicalVolume(voxelSolid, fMaterials.front(), "PhantomVoxel_LV");
    fVoxelLV = voxelLV;
    auto* voxelVis = new G4VisAttributes(G4Colour(0.95, 0.82, 0.35, 1.0));
    voxelVis->SetForceSolid(true);
    voxelLV->SetVisAttributes(voxelVis);

    fNestedParam = new NestedPhantomParameterisation(fGN[0], fGN[1], fGN[2], hz,
                                                     fMaterials, fMaterialIndices);
    auto* phantomPV = new G4PVParameterised("PhantomVoxels", voxelLV, xLV,
                                            kZAxis, static_cast<G4int>(fGN[2]), fNestedParam);
    phantomPV->SetRegularStructureId(1);

    fContainerLV = containerLV;
    std::cout << "[VoxelizedPhantom] Built nested " << fGN[0] << "x" << fGN[1] << "x" << fGN[2]
              << " voxel phantom at " << containerPos / mm << " mm (axes="
              << (axes == DicomAxes::RAS ? "RAS" : "LPS") << ")." << std::endl;
    return true;
}

void VoxelizedPhantom::SetMaterialOverrides(const std::vector<int>& ctLabels,
                                              const std::vector<G4Material*>& labelMaterials) {
    fOverrideLabels = ctLabels;
    fOverrideMaterials = labelMaterials;
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
