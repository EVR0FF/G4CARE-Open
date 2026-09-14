//==============================================================================
// G4CARE
// @file    FieldBase.cc
// @brief   Implementation of the abstract FieldBase class: constructor and
//          DoesFieldChangeEnergy().
// @details FieldBase stores a name and FieldType enum.  DoesFieldChangeEnergy
//   returns true for electric and combined fields so Geant4 accounts for
//   energy loss/gain during charged-particle propagation.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "FieldBase.hh"

/// @brief Base constructor storing the field name and type.
/// @param name Field name.
/// @param type Field type (kMagnetic, kElectric, kCombined).
FieldBase::FieldBase(const std::string& name, FieldType type) 
    : G4ElectroMagneticField(), fName(name), fType(type) {}

/// @brief Returns true for electric/combined fields so Geant4 accounts for
///        energy change.
G4bool FieldBase::DoesFieldChangeEnergy() const {
    return (fType == FieldType::kElectric || fType == FieldType::kCombined);
}
