//==============================================================================
// G4CARE
// @file    ConstantField.cc
// @brief   Implementation of a uniform (constant) electric and/or magnetic
//          field in a spatial region.
// @details Returns the same 6-component field vector {Ex, Ey, Ez, Bx, By, Bz}
//   at every point in space.  Clone() provides a thread-local copy for MT.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ConstantField.hh"

/// @brief Constructs a constant field with name, type, E and B vectors.
/// @param name   Field name.
/// @param type   Field type (kMagnetic, kElectric, kCombined).
/// @param eField Electric field vector (V/m).
/// @param bField Magnetic field vector (T).
ConstantField::ConstantField(const std::string& name, 
                             FieldType type,
                             const G4ThreeVector& eField, 
                             const G4ThreeVector& bField)
    : FieldBase(name, type), fE(eField), fB(bField) {}

/// @brief Fills the 6-component array {Ex, Ey, Ez, Bx, By, Bz} with the
///        stored field values.
/// @param point Position-time array (unused — field is uniform).
/// @param[out] field Output array of 6 doubles.
void ConstantField::GetFieldValue(const G4double /*point*/[4], G4double* field) const {
    // Fill the 6-component array: {Ex, Ey, Ez, Bx, By, Bz}
    
    // Electric part (first 3)
    field[0] = fE.x();
    field[1] = fE.y();
    field[2] = fE.z();
    
    // Magnetic part (next 3)
    field[3] = fB.x();
    field[4] = fB.y();
    field[5] = fB.z();
}

/// @brief Creates a thread-local copy for Geant4 MT.
/// @return Pointer to a new ConstantField identical to this one.
FieldBase* ConstantField::Clone() const {
    return new ConstantField(*this);
}
