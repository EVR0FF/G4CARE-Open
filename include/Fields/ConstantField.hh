//==============================================================================
// G4CARE
// @file    ConstantField.hh
// @brief   Uniform (constant) electric and/or magnetic field in a spatial
//          region.
// @details Implements FieldBase for a constant field vector.  Supports pure
//   electric, pure magnetic, or combined fields.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CONSTANT_FIELD_HH
#define CONSTANT_FIELD_HH

#include "FieldBase.hh"
#include "G4ThreeVector.hh"

/// @brief Uniform constant E/B field.
class ConstantField : public FieldBase {
public:
    /// Constructor: name, field type, E vector (V/m), B vector (T).
    /// For pure magnetic field: type=kMagnetic, E={0,0,0}
    /// For pure electric: type=kElectric, B={0,0,0}
    ConstantField(const std::string& name, 
                  FieldType type,
                  const G4ThreeVector& eField, 
                  const G4ThreeVector& bField);
    
    ~ConstantField() override = default;

    void GetFieldValue(const G4double point[4], G4double* field) const override;
    
    FieldBase* Clone() const override;

private:
    G4ThreeVector fE; // Electric field
    G4ThreeVector fB; // Magnetic field
};

#endif
