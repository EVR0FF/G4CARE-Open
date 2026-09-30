//==============================================================================
//
// G4CARE
//
// @file    VoxelizedPhantom.hh
// @brief   Builds a Geant4 voxel phantom from a CT series (G4PhantomParameterisation).
//
// @details
//   Converts a CTSeries (Hounsfield volume + geometry metadata) into a
//   parameterised G4PVParameterised volume: a container box completely filled
//   by uniform voxels, each assigned a material from an HUToMaterialMap.
//   Uses G4RegularNavigation (via SetRegularStructureId(1)) for fast voxel
//   tracking.  The DICOM patient (LPS) frame is used directly as the Geant4
//   frame (no mirroring); the container is centred on the voxel bounding box.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef VOXELIZED_PHANTOM_HH
#define VOXELIZED_PHANTOM_HH

#include "G4ThreeVector.hh"

#include <cstddef>
#include <string>
#include <vector>

class CTSeries;
class HUToMaterialMap;
class G4LogicalVolume;
class G4Material;
class G4PhantomParameterisation;

/// @brief Patient-coordinate convention for the Geant4 frame.
enum class DicomAxes {
    LPS,  ///< Use the DICOM patient frame directly (default).
    RAS   ///< Flip X (Left->Right) and Y (Posterior->Anterior).
};

/// @brief Builds a voxel phantom from CT data.
class VoxelizedPhantom {
public:
    VoxelizedPhantom() = default;
    ~VoxelizedPhantom();

    /// @brief Build the voxel phantom and place it inside worldLV.
    /// @param axes Coordinate convention (LPS by default).
    bool Build(const CTSeries& ct, HUToMaterialMap& huMap, G4LogicalVolume* worldLV,
               DicomAxes axes = DicomAxes::LPS);

    /// @return The phantom container logical volume (nullptr before Build).
    G4LogicalVolume* GetContainerLV() const { return fContainerLV; }
    /// @return The voxel logical volume (nullptr before Build); attach an SD here.
    G4LogicalVolume* GetVoxelLV() const { return fVoxelLV; }

    /// @brief Store per-voxel organ labels in CT-series order and reorder them
    ///        to the Geant4 copy-number order of the phantom voxels.
    void SetOrganLabelsFromCT(const std::vector<int>& ctLabels);

    /// @brief Store organ names (ROI names, indexed by the labels).
    void SetOrganNames(std::vector<std::string> names) { fOrganNames = std::move(names); }
    /// @return Per-voxel organ labels (CTSeries order: iz*(nx*ny)+iy*nx+ix).
    const std::vector<int>& GetOrganLabels() const { return fOrganLabels; }
    /// @return Organ names (ROI names).
    const std::vector<std::string>& GetOrganNames() const { return fOrganNames; }

private:
    G4LogicalVolume* fContainerLV = nullptr;
    G4LogicalVolume* fVoxelLV = nullptr;
    G4PhantomParameterisation* fParam = nullptr;
    std::vector<G4Material*> fMaterials;         ///< distinct voxel materials
    std::vector<std::size_t> fMaterialIndices;   ///< per-voxel material index
    std::vector<int> fOrganLabels;               ///< per-voxel organ label (G4 copy-number order)
    std::vector<std::string> fOrganNames;        ///< ROI names

    // Axis mapping + grid dims (set in Build; used to reorder labels).
    std::size_t fGN[3] = {0, 0, 0};
    int fVar[3] = {0, 0, 0};
    int fVarSign[3] = {0, 0, 0};
    int fVarN[3] = {0, 0, 0};
    int fNx = 0, fNy = 0, fNz = 0;
};

#endif // VOXELIZED_PHANTOM_HH
