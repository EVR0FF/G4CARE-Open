//==============================================================================
//
// G4CARE
//
// @file    NestedPhantomParameterisation.hh
// @brief   G4VNestedParameterisation for the voxel phantom (GATE-style).
//
// @details
//   X and Y are G4PVReplica axes; Z is the parameterised axis. The material
//   of each voxel is resolved from (ix, iy, iz) into the flat per-voxel
//   material-index grid.
//
//==============================================================================

#ifndef NESTED_PHANTOM_PARAMETERISATION_HH
#define NESTED_PHANTOM_PARAMETERISATION_HH

#include "G4VNestedParameterisation.hh"

#include <cstddef>
#include <vector>

class G4Material;

/// @brief Nested (2-level) parameterisation for a regular voxel grid.
class NestedPhantomParameterisation : public G4VNestedParameterisation {
public:
    NestedPhantomParameterisation(std::size_t nx, std::size_t ny, std::size_t nz,
                                  double halfZ,
                                  const std::vector<G4Material*>& materials,
                                  const std::vector<std::size_t>& indices);
    ~NestedPhantomParameterisation() override = default;

    using G4VPVParameterisation::ComputeMaterial;

    void ComputeTransformation(const G4int copyNo,
                               G4VPhysicalVolume* physVol) const override;
    G4Material* ComputeMaterial(G4VPhysicalVolume* currentVol, const G4int copyNo,
                                const G4VTouchable* parentTouch) override;
    G4int GetNumberOfMaterials() const override;
    G4Material* GetMaterial(G4int idx) const override;

private:
    std::size_t fNx = 0;
    std::size_t fNy = 0;
    std::size_t fNz = 0;
    double fHalfZ = 0.0;
    const std::vector<G4Material*>& fMaterials;
    const std::vector<std::size_t>& fIndices;
};

#endif // NESTED_PHANTOM_PARAMETERISATION_HH
