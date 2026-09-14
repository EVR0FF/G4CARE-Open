//==============================================================================
// G4CARE
// @file    FieldManager.hh
// @brief   Singleton manager that reads FIELD configuration from YAML,
//          constructs field objects (constant, parametric, grid), and
//          attaches G4FieldManager instances to logical volumes.
// @details Parses FIELD sections, creates FieldBase subclasses via factory
//   methods, and builds G4FieldManager with G4ChordFinder for each field.
//
//   Configuration keys read: FIELD.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef FIELD_MANAGER_HH
#define FIELD_MANAGER_HH

#include "G4FieldManager.hh"
#include "G4ChordFinder.hh"
#include "G4SystemOfUnits.hh"
#include <vector>
#include <memory>
#include <map>
#include "yaml-cpp/yaml.h"

class FieldBase;
class G4LogicalVolume;

/// @brief Builds and attaches electromagnetic fields to geometry volumes.
class FieldManager {
public:
    static FieldManager* Instance();
    static void DeleteInstance();

    void ApplyFields();

private:
    FieldManager();
    ~FieldManager();
    
    static FieldManager* fInstance;
    
    std::vector<std::unique_ptr<FieldBase>> fFields;
    
    /// Factory methods
    std::unique_ptr<FieldBase> CreateConstantField(const YAML::Node& node);
    std::unique_ptr<FieldBase> CreateParametricField(const YAML::Node& node);
    std::unique_ptr<FieldBase> CreateGridField(const YAML::Node& node);
    
    double GetUnitFactor(const std::string& unitStr, const std::string& dimension);
    
    /// Helper: ADDED SECOND ARGUMENT minStep
    G4FieldManager* BuildFieldManager(FieldBase* field, double minStep = 0.1*CLHEP::mm);
};

#endif 
