//==============================================================================
// G4CARE
// @file    FieldBase.hh
// @brief   Abstract base class for electromagnetic fields (constant, grid,
//          parametric) with a common interface for Geant4 field propagation.
// @details FieldBase extends G4ElectroMagneticField and adds a user-defined
//   FieldType enum (kMagnetic, kElectric, kCombined), a name string, and a
//   Clone() factory method for thread-local copies in MT mode.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef FIELD_BASE_HH
#define FIELD_BASE_HH

#include "G4ElectroMagneticField.hh" // CHANGED: was G4Field.hh
#include <string>

/// Internal enum for field types.
enum class FieldType {
    kMagnetic,
    kElectric,
    kCombined
};

/// Inherit from G4ElectroMagneticField for compatibility with G4EqMagElectricField.
class FieldBase : public G4ElectroMagneticField {
public:
    FieldBase(const std::string& name, FieldType type = FieldType::kMagnetic);

    virtual ~FieldBase() = default;

    // point[4] = {x, y, z, t}, field[6] = {Ex, Ey, Ez, Bx, By, Bz}
    void GetFieldValue(const G4double point[4], G4double* field) const override = 0;

    /// Tells Geant4 whether the field changes particle energy.
    G4bool DoesFieldChangeEnergy() const override;

    const std::string& GetName() const { return fName; }
    
    /// Returns the field kind (magnetic, electric, or combined).
    FieldType GetFieldKind() const { return fType; }
    
    virtual FieldBase* Clone() const = 0;

protected:
    std::string fName;
    FieldType fType;
};

#endif