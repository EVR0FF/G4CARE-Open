//==============================================================================
//
// G4CARE
//
// @file    NestedPhantomParameterisation.cc
// @brief   Implementation of NestedPhantomParameterisation.
//
//==============================================================================

#include "NestedPhantomParameterisation.hh"

#include "G4Material.hh"
#include "G4ThreeVector.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VTouchable.hh"

NestedPhantomParameterisation::NestedPhantomParameterisation(
    std::size_t nx, std::size_t ny, std::size_t nz, double halfZ,
    const std::vector<G4Material*>& materials,
    const std::vector<std::size_t>& indices)
    : fNx(nx), fNy(ny), fNz(nz), fHalfZ(halfZ),
      fMaterials(materials), fIndices(indices) {}

void NestedPhantomParameterisation::ComputeTransformation(const G4int copyNo,
                                                          G4VPhysicalVolume* physVol) const {
    // Z position: (-Nz + 1 + 2*iz) * halfZ (GATE's nested convention).
    const double z = (2.0 * static_cast<double>(copyNo) + 1.0 - static_cast<double>(fNz)) * fHalfZ;
    physVol->SetTranslation(G4ThreeVector(0.0, 0.0, z));
}

G4Material* NestedPhantomParameterisation::ComputeMaterial(G4VPhysicalVolume* /*currentVol*/,
                                                           const G4int copyNo,
                                                           const G4VTouchable* parentTouch) {
    std::size_t ix = 0, iy = 0;
    if (parentTouch) {
        ix = static_cast<std::size_t>(parentTouch->GetReplicaNumber(0));
        iy = static_cast<std::size_t>(parentTouch->GetReplicaNumber(1));
    }
    const std::size_t iz = static_cast<std::size_t>(copyNo);
    const std::size_t lin = ix + iy * fNx + iz * fNx * fNy;
    if (lin >= fIndices.size()) return fMaterials.empty() ? nullptr : fMaterials.front();
    const std::size_t mi = fIndices[lin];
    return (mi < fMaterials.size()) ? fMaterials[mi] : (fMaterials.empty() ? nullptr : fMaterials.front());
}

G4int NestedPhantomParameterisation::GetNumberOfMaterials() const {
    return static_cast<G4int>(fMaterials.size());
}

G4Material* NestedPhantomParameterisation::GetMaterial(G4int idx) const {
    if (idx < 0 || idx >= static_cast<G4int>(fMaterials.size())) return nullptr;
    return fMaterials[static_cast<std::size_t>(idx)];
}
