//==============================================================================
// G4CARE
// @file    FieldManager.cc
// @brief   Singleton manager that reads FIELD configuration from YAML,
//          constructs field objects (constant, parametric, grid), and
//          attaches G4FieldManager instances to logical volumes.
// @details Reads FIELDS configuration from ConfigManager, creates ConstantField,
//   ParametricField, or GridField instances via factory methods, constructs
//   G4FieldManager with G4ChordFinder (ClassicalRK4 stepper), and attaches
//   them to logical volumes by name.
//
//   Configuration keys read: FIELDS.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "FieldManager.hh"
#include "ConstantField.hh"
#include "ParametricField.hh"
#include "GridField.hh"
#include "ConfigManager.hh"

// Geant4 headers
#include "G4PhysicalVolumeStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4TransportationManager.hh"
#include "G4FieldManager.hh"
#include "G4ChordFinder.hh"
#include "G4EqMagElectricField.hh"
#include "G4ClassicalRK4.hh"
#include "G4IntegrationDriver.hh" // Template driver
#include "G4SystemOfUnits.hh"

#include "yaml-cpp/yaml.h"
#include <iostream>
#include <algorithm>
#include <memory>

FieldManager* FieldManager::fInstance = nullptr;

/// @brief Returns the singleton instance.
FieldManager* FieldManager::Instance() {
    if (fInstance == nullptr) {
        fInstance = new FieldManager();
    }
    return fInstance;
}

FieldManager::FieldManager() {}
FieldManager::~FieldManager() { fFields.clear(); }

/// @brief Reads the FIELDS YAML section and applies all configured
///        electromagnetic fields to volumes or globally.
void FieldManager::ApplyFields() {
    G4cout << "=== FieldManager: Loading fields from config ===" << G4endl;

    auto* config = ConfigManager::Instance();
    YAML::Node rootNode = config->GetNode("FIELDS"); 
    
    if (!rootNode || !rootNode.IsSequence()) {
        G4cout << "FieldManager: No 'FIELDS' section found. Skipping." << G4endl;
        return;
    }

    int fieldCount = 0;

    for (const auto& node : rootNode) {
        if (!node["name"] || !node["type"]) {
            G4cerr << "FieldManager: Invalid config. Missing 'name' or 'type'." << G4endl;
            continue;
        }

        std::string name = node["name"].as<std::string>();
        std::string type = node["type"].as<std::string>();
        std::string volumeName = node["volume"].IsDefined() ? node["volume"].as<std::string>() : "";

        std::unique_ptr<FieldBase> field = nullptr;

        try {
            if (type == "constant") {
                field = CreateConstantField(node);
            } else if (type == "parametric") {
                field = CreateParametricField(node);
            } else if (type == "grid") {
                field = CreateGridField(node);
            } else {
                G4cerr << "FieldManager: Unknown type '" << type << "'." << G4endl;
                continue;
            }
        } catch (const std::exception& e) {
            G4cerr << "FieldManager: Error creating '" << name << "': " << e.what() << G4endl;
            continue;
        }

        if (!field) continue;

        G4cout << "Created field: " << name << " (Type: ";
        if (field->GetFieldKind() == FieldType::kMagnetic) G4cout << "Magnetic";
        else if (field->GetFieldKind() == FieldType::kElectric) G4cout << "Electric";
        else G4cout << "Combined";
        G4cout << ")" << G4endl;

        // Attach to volume
        if (!volumeName.empty()) {
            G4LogicalVolume* logVol = G4LogicalVolumeStore::GetInstance()->GetVolume(volumeName);
            if (!logVol) {
                G4cerr << "FieldManager: Volume '" << volumeName << "' not found." << G4endl;
                continue; 
            }

            auto* localFieldMgr = BuildFieldManager(field.get());
            logVol->SetFieldManager(localFieldMgr, true); 
            G4cout << "  -> Attached to volume: " << volumeName << G4endl;
        } else {
            // Global field
            G4FieldManager* globalFieldMgr = G4TransportationManager::GetTransportationManager()->GetFieldManager();
            
            globalFieldMgr->SetDetectorField(field.get(), 1);
            
            // Set up equation of motion for global field
            if (field->GetFieldKind() != FieldType::kMagnetic) {
                // Create equation, stepper and driver manually
                auto* equation = new G4EqMagElectricField(field.get());
                auto* stepper = new G4ClassicalRK4(equation);
                // Use template driver G4IntegrationDriver<T>
                auto* driver = new G4IntegrationDriver<G4ClassicalRK4>(0.1*mm, stepper);
                auto* chordFinder = new G4ChordFinder(driver);
                
                globalFieldMgr->SetChordFinder(chordFinder);
                G4cout << "  -> Global Field: Set EqMagElectricField & ChordFinder (via Driver)" << G4endl;
            } else {
                if (!globalFieldMgr->GetChordFinder()) {
                     globalFieldMgr->CreateChordFinder(nullptr);
                }
            }
            
            G4cout << "  -> Set as Global Field" << G4endl;
        }

        fFields.push_back(std::move(field));
        fieldCount++;
    }

    G4cout << "=== FieldManager: Loaded " << fieldCount << " fields ===" << G4endl;
}

// --- Helper: Build Field Manager with correct Equation ---

/// @brief Constructs a G4FieldManager with the appropriate equation of motion
///        and chord finder for the given field type.
/// @param field   Field object (cannot be null).
/// @param minStep Minimum integration step.
/// @return Newly allocated G4FieldManager (caller owns).
G4FieldManager* FieldManager::BuildFieldManager(FieldBase* field, double minStep) {
    auto* fieldMgr = new G4FieldManager();
    fieldMgr->SetDetectorField(field, 1);

    if (field->GetFieldKind() != FieldType::kMagnetic) {
        // Electric field present → use G4EqMagElectricField
        auto* equation = new G4EqMagElectricField(field); 
        auto* stepper = new G4ClassicalRK4(equation);
        // Use template driver
        auto* driver = new G4IntegrationDriver<G4ClassicalRK4>(minStep, stepper);
        auto* chordFinder = new G4ChordFinder(driver);
        
        fieldMgr->SetChordFinder(chordFinder);
    } else {
        // Magnetic only → standard ChordFinder constructor
        fieldMgr->CreateChordFinder(nullptr); 
    }

    return fieldMgr;
}

// --- Factories ---

/// @brief Creates a ConstantField from a YAML node.
/// @param node YAML node with keys: name, type, e_value, b_value, value, e_unit, b_unit, unit.
/// @return Unique pointer to a ConstantField.
std::unique_ptr<FieldBase> FieldManager::CreateConstantField(const YAML::Node& node) {
    std::string unit = node["unit"].IsDefined() ? node["unit"].as<std::string>() : "tesla";
    
    G4ThreeVector eVec(0,0,0);
    G4ThreeVector bVec(0,0,0);
    bool hasE = false;
    bool hasB = false;

    if (node["e_value"]) {
        auto vec = node["e_value"].as<std::vector<double>>();
        if (vec.size() == 3) {
            std::string eUnit = node["e_unit"].IsDefined() ? node["e_unit"].as<std::string>() : "volt/cm";
            double factor = GetUnitFactor(eUnit, "electric");
            eVec = G4ThreeVector(vec[0]*factor, vec[1]*factor, vec[2]*factor);
            hasE = true;
        }
    }

    if (node["b_value"]) {
        auto vec = node["b_value"].as<std::vector<double>>();
        if (vec.size() == 3) {
            std::string bUnit = node["b_unit"].IsDefined() ? node["b_unit"].as<std::string>() : unit;
            double factor = GetUnitFactor(bUnit, "magnetic");
            bVec = G4ThreeVector(vec[0]*factor, vec[1]*factor, vec[2]*factor);
            hasB = true;
        }
    } else if (node["value"]) { 
        auto vec = node["value"].as<std::vector<double>>();
        if (vec.size() == 3) {
            double factor = GetUnitFactor(unit, "magnetic");
            bVec = G4ThreeVector(vec[0]*factor, vec[1]*factor, vec[2]*factor);
            hasB = true;
        }
    }

    FieldType type = FieldType::kMagnetic;
    if (hasE && hasB) type = FieldType::kCombined;
    else if (hasE) type = FieldType::kElectric;

    std::string name = node["name"].as<std::string>();
    return std::make_unique<ConstantField>(name, type, eVec, bVec);
}

/// @brief Creates a ParametricField from a YAML node.
/// @param node YAML node with keys: name, e_equations, b_equations, equations, parameters.
/// @return Unique pointer to a ParametricField.
std::unique_ptr<FieldBase> FieldManager::CreateParametricField(const YAML::Node& node) {
    std::array<std::string, 3> eEq = {"0", "0", "0"};
    std::array<std::string, 3> bEq = {"0", "0", "0"};
    bool hasE = false;
    bool hasB = false;

    if (node["e_equations"]) {
        auto eqs = node["e_equations"].as<std::vector<std::string>>();
        if (eqs.size() == 3) { eEq = {eqs[0], eqs[1], eqs[2]}; hasE = true; }
    }
    if (node["b_equations"]) {
        auto eqs = node["b_equations"].as<std::vector<std::string>>();
        if (eqs.size() == 3) { bEq = {eqs[0], eqs[1], eqs[2]}; hasB = true; }
    } else if (node["equations"]) { 
        auto eqs = node["equations"].as<std::vector<std::string>>();
        if (eqs.size() == 3) { bEq = {eqs[0], eqs[1], eqs[2]}; hasB = true; }
    }

    std::map<std::string, double> params;
    if (node["parameters"]) {
        for (const auto& kv : node["parameters"]) {
            params[kv.first.as<std::string>()] = kv.second.as<double>();
        }
    }

    FieldType type = FieldType::kMagnetic;
    if (hasE && hasB) type = FieldType::kCombined;
    else if (hasE) type = FieldType::kElectric;

    std::string name = node["name"].as<std::string>();
    return std::make_unique<ParametricField>(name, eEq, bEq, params);
}

/// @brief Creates a GridField from a YAML node.
/// @param node YAML node with keys: name, e_file, b_file, file, interpolation, e_unit, b_unit.
/// @return Unique pointer to a GridField.
std::unique_ptr<FieldBase> FieldManager::CreateGridField(const YAML::Node& node) {
    std::string eFile = node["e_file"].IsDefined() ? node["e_file"].as<std::string>() : "";
    std::string bFile = node["b_file"].IsDefined() ? node["b_file"].as<std::string>() : "";
    
    if (eFile.empty() && bFile.empty() && node["file"].IsDefined()) {
        bFile = node["file"].as<std::string>(); 
    }

    if (eFile.empty() && bFile.empty()) {
        throw std::runtime_error("GridField requires 'e_file' and/or 'b_file'");
    }

    std::string interp = node["interpolation"].IsDefined() ? node["interpolation"].as<std::string>() : "trilinear";
    
    std::string eUnit = node["e_unit"].IsDefined() ? node["e_unit"].as<std::string>() : "volt/cm";
    std::string bUnit = node["b_unit"].IsDefined() ? node["b_unit"].as<std::string>() : "tesla";
    
    double eScale = GetUnitFactor(eUnit, "electric");
    double bScale = GetUnitFactor(bUnit, "magnetic");

    std::string name = node["name"].as<std::string>();
    
    return std::make_unique<GridField>(name, eFile, bFile, interp, eScale, bScale);
}

/// @brief Converts a unit string to a Geant4 unit factor.
/// @param unitStr Unit string ("tesla", "T", "gauss", "G", "volt/m", "V/m", "volt/cm", "V/cm").
/// @param dim     Dimension: "magnetic" or "electric".
/// @return Unit factor in internal Geant4 units.
double FieldManager::GetUnitFactor(const std::string& unitStr, const std::string& dim) {
    if (dim == "magnetic") {
        if (unitStr == "tesla" || unitStr == "T") return tesla;
        if (unitStr == "gauss" || unitStr == "G") return gauss;
    } else if (dim == "electric") {
        if (unitStr == "volt/m" || unitStr == "V/m") return volt/m;
        if (unitStr == "volt/cm" || unitStr == "V/cm") return volt/cm;
    }
    return 1.0;
}