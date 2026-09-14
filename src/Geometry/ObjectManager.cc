//==============================================================================
//
// G4CARE
//
// @file    ObjectManager.cc
// @brief   Detector geometry builder from YAML configuration.
//
// @details
//   Reads GEOMETRY.OBJECTS subsections via ConfigManager, creates
//   G4VSolid, G4LogicalVolume and G4PVPlacement for each object.
//   Supports shapes: box, sphere, cylinder, tube, cone, torus, para.
//   Handles DIMENSIONS/POSITION (modern) and size/pos (legacy) formats.
//   Configures DIGITIZE parameters, production cuts, isotope composition,
//   and optionally attaches SensitiveDetector.
//
//   Configuration keys read from GEOMETRY.OBJECTS.<id>.*:
//     name, shape, material, DIMENSIONS, POSITION, size, pos.x/y/z,
//     isSensitive, DIGITIZE.*, cuts.*, max_step_size,
//     record_ion_production, isotopes, isotope, mother.
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "ObjectManager.hh"
#include "G4GDMLParser.hh"
#include "G4NistManager.hh"
#include "G4Box.hh"
#include "G4Sphere.hh"
#include "G4Tubs.hh"
#include "G4Cons.hh"
#include "G4Torus.hh"
#include "G4Polycone.hh"
#include "G4Para.hh"
#include "G4Trap.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4SDManager.hh"
#include "SensitiveDetector.hh"
#include "G4ios.hh"
#include "G4UnitsTable.hh" 
#include "G4Material.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "ConfigManager.hh"
#include "G4Region.hh"
#include "G4ProductionCuts.hh"
#include "G4UserLimits.hh"
#include "G4RegionStore.hh"
#include "BeamAnalysis.hh"
#include "DetectorRegistry.hh"

#include <regex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cctype>

namespace {
    bool IsNumber(const std::string& s) {
        std::regex number_regex(R"(^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$)");
        return std::regex_match(s, number_regex);
    }

        std::vector<std::string> ParseSizeString(std::string input) {
        std::vector<std::string> result;
        
        // Replace problematic characters with spaces to ensure clean tokenization
        std::replace(input.begin(), input.end(), '*', ' ');
        std::replace(input.begin(), input.end(), ',', ' ');
        std::replace(input.begin(), input.end(), '[', ' ');
        std::replace(input.begin(), input.end(), ']', ' ');

        std::istringstream iss(input);
        std::string token;
        std::vector<std::string> tokens;
        
        while (iss >> token) {
            tokens.push_back(token);
        }

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (IsNumber(tokens[i])) {
                if (i + 1 < tokens.size()) {
                    std::string unitCandidate = tokens[i+1];
                    if (G4UnitDefinition::IsUnitDefined(unitCandidate.c_str())) {
                        result.push_back(tokens[i] + " " + unitCandidate);
                        ++i; // Skip the unit token since we appended it
                    } else {
                        result.push_back(tokens[i]);
                    }
                } else {
                    result.push_back(tokens[i]);
                }
            } else {
                result.push_back(tokens[i]);
            }
        }
        return result;
    }
} // end anonymous namespace

ObjectManager::ObjectManager(){}

ObjectManager::~ObjectManager() {
    // Objects (G4LogicalVolume, G4PVPlacement) are managed by Geant4 kernel;
    // no manual deletion is required.
    fObjects.clear();
}

/// @brief Read GEOMETRY.OBJECTS from config and create ObjDesc entries.
void ObjectManager::CreateObjects() {
    auto* cfg = ConfigManager::Instance();

    std::vector<std::string> objIds = cfg->GetSubsections("GEOMETRY.OBJECTS");
    G4cout << "ObjectManager: Found " << objIds.size() << " objects in configuration" << G4endl;
    if (!objIds.empty()) {
        G4cout << "ObjectManager: GetSubsections returned:";
        for (const auto& id : objIds) G4cout << " " << id;
        G4cout << G4endl;
    }
    for (const auto& objId : objIds) {
        ObjDesc currentDesc;
        std::string baseKey = "GEOMETRY.OBJECTS." + objId + ".";

        // Name: explicit "name" field or object ID from YAML key
        currentDesc.name = cfg->GetString(baseKey + "name", "");
        if (currentDesc.name.empty()) {
            currentDesc.name = cfg->GetString(baseKey + "NAME", "");
        }
        if (currentDesc.name.empty()) {
            currentDesc.name = objId;  // fallback: use YAML key as object name
        }

        // GDML file: if set, shape/material/DIMENSIONS come from GDML
        currentDesc.gdmlFile = cfg->GetString(baseKey + "file", "");

        // Shape: "shape" or "SHAPE" (ignored if gdmlFile is set)
        currentDesc.shape = cfg->GetString(baseKey + "shape", "");
        if (currentDesc.shape.empty()) {
            currentDesc.shape = cfg->GetString(baseKey + "SHAPE", "box");
        }
        if (currentDesc.shape.empty()) currentDesc.shape = "box";

        // Material: "material", "MATERIAL" or default (ignored if gdmlFile is set)
        currentDesc.material = cfg->GetString(baseKey + "material", "");
        if (currentDesc.material.empty()) {
            currentDesc.material = cfg->GetString(baseKey + "MATERIAL", "G4_AIR");
        }
        if (currentDesc.material.empty()) currentDesc.material = "G4_AIR";
        currentDesc.isotope = cfg->GetString(baseKey + "isotope", "");
        currentDesc.isSensitive = cfg->GetBool(baseKey + "isSensitive", false);
        std::string digitizeBase = baseKey + "DIGITIZE.";
        if (cfg->HasKey(digitizeBase + "dead_time_ns")) {
            currentDesc.digitizeProps.deadTime_ns = cfg->GetValueWithUnits(digitizeBase + "dead_time_ns", 0.0) / ns;
            currentDesc.digitizeProps.deadTimeModel = cfg->GetString(digitizeBase + "dead_time_model", "nonparalyzable");
            currentDesc.digitizeProps.timeResFWHM_ns = cfg->GetValueWithUnits(digitizeBase + "time_res_fwhm_ns", 0.0) / ns;
            currentDesc.digitizeProps.energyResFWHM = cfg->GetDouble(digitizeBase + "energy_res_fwhm", 0.0);
            currentDesc.digitizeProps.energyResRefEnergy_MeV = cfg->GetDouble(digitizeBase + "energy_res_ref_energy_MeV", 1.0);
            currentDesc.digitizeProps.quantumEfficiency = cfg->GetDouble(digitizeBase + "quantum_efficiency", 1.0);
            currentDesc.digitizeProps.gain = cfg->GetDouble(digitizeBase + "gain", 1.0);
            currentDesc.digitizeProps.pileupModel = cfg->GetString(digitizeBase + "pileup_model", "none");
            currentDesc.digitizeProps.afterpulseProbability = cfg->GetDouble(digitizeBase + "afterpulse_probability", 0.0);
            currentDesc.digitizeProps.signalRiseTime_ns = cfg->GetValueWithUnits(digitizeBase + "signal_rise_ns", 0.0) / ns;
            currentDesc.digitizeProps.signalFallTime_ns = cfg->GetValueWithUnits(digitizeBase + "signal_fall_ns", 0.0) / ns;
            currentDesc.digitizeProps.signalModel = cfg->GetString(digitizeBase + "signal_model", "exponential");
            currentDesc.digitizeProps.pileupWindow_ns = cfg->GetValueWithUnits(digitizeBase + "pileup_window_ns", 0.0) / ns;
            currentDesc.digitizeProps.pileupBufferSize = cfg->GetInt(digitizeBase + "pileup_buffer_size", 100);
            currentDesc.digitizeProps.threshold_MeV = cfg->GetValueWithUnits(digitizeBase + "threshold", 0.0) / MeV;
            currentDesc.digitizeProps.noiseLevel_MeV = cfg->GetValueWithUnits(digitizeBase + "noise_level", 0.0) / MeV;
            currentDesc.digitizeProps.timeSmearModel = cfg->GetString(digitizeBase + "time_smear_model", "gaussian");
            currentDesc.digitizeProps.energySmearModel = cfg->GetString(digitizeBase + "energy_smear_model", "gaussian");
            // Landau parameters
            currentDesc.digitizeProps.timeLandauMPV_ns = cfg->GetValueWithUnits(digitizeBase + "time_landau_mpv_ns", 0.0) / ns;
            currentDesc.digitizeProps.timeLandauSigma_ns = cfg->GetValueWithUnits(digitizeBase + "time_landau_sigma_ns", 0.0) / ns;
            currentDesc.digitizeProps.energyLandauMPV_MeV = cfg->GetValueWithUnits(digitizeBase + "energy_landau_mpv_MeV", 0.0) / MeV;
            currentDesc.digitizeProps.energyLandauSigma_MeV = cfg->GetValueWithUnits(digitizeBase + "energy_landau_sigma_MeV", 0.0) / MeV;
            // Expressions
            currentDesc.digitizeProps.useTimeSmearExpression = cfg->GetBool(digitizeBase + "use_time_smear_expression", false);
            currentDesc.digitizeProps.timeSmearExpression = cfg->GetString(digitizeBase + "time_smear_expression", "");
            currentDesc.digitizeProps.useEnergySmearExpression = cfg->GetBool(digitizeBase + "use_energy_smear_expression", false);
            currentDesc.digitizeProps.energySmearExpression = cfg->GetString(digitizeBase + "energy_smear_expression", "");
            currentDesc.digitizeProps.useSignalTimeExpression = cfg->GetBool(digitizeBase + "use_signal_time_expression", false);
            currentDesc.digitizeProps.signalTimeExpression = cfg->GetString(digitizeBase + "signal_time_expression", "");
            currentDesc.digitizeProps.usePileupEnergyExpression = cfg->GetBool(digitizeBase + "use_pileup_energy_expression", false);
            currentDesc.digitizeProps.pileupEnergyExpression = cfg->GetString(digitizeBase + "pileup_energy_expression", "");
            currentDesc.digitizeProps.usePileupTimeExpression = cfg->GetBool(digitizeBase + "use_pileup_time_expression", false);
            currentDesc.digitizeProps.pileupTimeExpression = cfg->GetString(digitizeBase + "pileup_time_expression", "");
            // Additional effects
            currentDesc.digitizeProps.darkCountRate_kHz = cfg->GetDouble(digitizeBase + "dark_count_rate_kHz", 0.0);
            currentDesc.digitizeProps.crossTalkProbability = cfg->GetDouble(digitizeBase + "cross_talk_probability", 0.0);
            currentDesc.digitizeProps.recoveryTime_ns = cfg->GetValueWithUnits(digitizeBase + "recovery_time_ns", 0.0) / ns;
            // Detector type
            currentDesc.digitizeProps.calorimeterMode = cfg->GetBool(digitizeBase + "calorimeter_mode", false);
            currentDesc.digitizeProps.digitType = cfg->GetInt(digitizeBase + "digit_type", -1);
        }

        // Dimensions: try DIMENSIONS vector first (new format), then size (legacy)
        auto dimsStrVec = cfg->GetStringVector(baseKey + "DIMENSIONS");
        if (!dimsStrVec.empty() && dimsStrVec.size() >= 1) {
            currentDesc.rawSizes.clear();
            for (const auto& d : dimsStrVec) {
                auto parsed = ParseSizeString(d);
                for (const auto& p : parsed) currentDesc.rawSizes.push_back(p);
            }
        } else {
            auto dimsDblVec = cfg->GetDoubleVectorWithUnits(baseKey + "DIMENSIONS");
            if (!dimsDblVec.empty()) {
                currentDesc.rawSizes.clear();
                for (auto v : dimsDblVec) currentDesc.rawSizes.push_back(std::to_string(v));
            } else {
                // Fallback: size string (legacy key)
                std::string sizeStr = cfg->GetString(baseKey + "size", "");
                if (!sizeStr.empty()) {
                    currentDesc.rawSizes = ParseSizeString(sizeStr);
                } else {
                    currentDesc.rawSizes.clear();
                }
            }
        }

        // Position: try POSITION vector first (new format), then pos.x/y/z (legacy)
        auto posDblVec = cfg->GetDoubleVectorWithUnits(baseKey + "POSITION");
        if (posDblVec.size() >= 3) {
            currentDesc.pos.setX(posDblVec[0]);
            currentDesc.pos.setY(posDblVec[1]);
            currentDesc.pos.setZ(posDblVec[2]);
        } else {
            // Fallback: pos.x / pos.y / pos.z (legacy format)
            currentDesc.pos.setX(cfg->GetValueWithUnits(baseKey + "pos.x", 0.0));
            currentDesc.pos.setY(cfg->GetValueWithUnits(baseKey + "pos.y", 0.0));
            currentDesc.pos.setZ(cfg->GetValueWithUnits(baseKey + "pos.z", 0.0));
        }
        
        std::string cutsPath = "GEOMETRY.OBJECTS." + objId + ".cuts";
        std::vector<std::string> cutKeys = cfg->GetSectionKeys(cutsPath);
        for (const auto& particle : cutKeys) {
            G4String particleName = particle;
            G4double cutValue = cfg->GetValueWithUnits(cutsPath + "." + particle, 0.0);
            if (cutValue > 0.0) {
                currentDesc.fCuts[particleName] = cutValue;
            }
        }
        currentDesc.fMaxStepSize = cfg->GetValueWithUnits(baseKey + "max_step_size", 0.0);
        currentDesc.fRecordIonProduction = cfg->GetBool(baseKey + "record_ion_production", false);
        
        std::string isotopesStr = cfg->GetString(baseKey + "isotopes", "");
        if (!isotopesStr.empty()) {
            std::vector<std::string> pairs = cfg->Split(isotopesStr, ',');
            for (const auto& pair : pairs) {
                std::string trimmed = cfg->Trim(pair);
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string isotope = cfg->Trim(trimmed.substr(0, colon));
                    std::string weightStr = cfg->Trim(trimmed.substr(colon + 1));
                    try {
                        double weight = std::stod(weightStr);
                        if (weight > 0) {
                            currentDesc.isotopes.emplace_back(isotope, weight);
                        } else {
                            G4cerr << "ObjectManager: Weight <= 0 for isotope " << isotope
                                   << " in object '" << currentDesc.name << "', ignoring." << G4endl;
                        }
                    } catch (...) {
                        G4cerr << "ObjectManager: Invalid weight for isotope " << isotope
                               << " in object '" << currentDesc.name << "', ignoring." << G4endl;
                    }
                } else {
                    G4cerr << "ObjectManager: Invalid isotope format (missing ':'): '" << trimmed
                           << "' in object '" << currentDesc.name << "', ignoring." << G4endl;
                }
            }
        } else {

            std::string isotope = cfg->GetString(baseKey + "isotope", "");
            if (!isotope.empty()) {
                currentDesc.isotopes.emplace_back(isotope, 1.0);
            }
        }

        // REPLICA configuration
        std::string replicaBase = baseKey + "REPLICA.";
        if (cfg->HasKey(replicaBase + "count")) {
            currentDesc.replica.count = cfg->GetInt(replicaBase + "count", 0);
            currentDesc.replica.axis = cfg->GetString(replicaBase + "axis", "z");
            currentDesc.replica.step = cfg->GetValueWithUnits(replicaBase + "step", 0.0);
            currentDesc.replica.offset = cfg->GetValueWithUnits(replicaBase + "offset", 0.0);
        }

        // CHILDREN — parse nested objects recursively
        std::string childrenBase = baseKey + "CHILDREN.";
        std::vector<std::string> childIds = cfg->GetSubsections(baseKey + "CHILDREN");
        for (const auto& childId : childIds) {
            ObjDesc childDesc;
            std::string childKey = childrenBase + childId + ".";

            // Parse child properties (recursive — children can have their own REPLICA/CHILDREN)
            // We'll process them iteratively at the top level
            childDesc.name = cfg->GetString(childKey + "name", childId);
            childDesc.shape = cfg->GetString(childKey + "shape", "box");
            childDesc.material = cfg->GetString(childKey + "material", "G4_AIR");
            childDesc.isSensitive = cfg->GetBool(childKey + "isSensitive", false);

            auto childDims = cfg->GetStringVector(childKey + "DIMENSIONS");
            if (!childDims.empty()) {
                childDesc.rawSizes.clear();
                for (const auto& d : childDims) {
                    auto parsed = ParseSizeString(d);
                    for (const auto& p : parsed) childDesc.rawSizes.push_back(p);
                }
            }

            auto childPos = cfg->GetDoubleVectorWithUnits(childKey + "POSITION");
            if (childPos.size() >= 3) {
                childDesc.pos.setX(childPos[0]);
                childDesc.pos.setY(childPos[1]);
                childDesc.pos.setZ(childPos[2]);
            }

            // DIGITIZE for child
            std::string childDigi = childKey + "DIGITIZE.";
            if (cfg->HasKey(childDigi + "digit_type")) {
                childDesc.digitizeProps.digitType = cfg->GetInt(childDigi + "digit_type", -1);
                childDesc.digitizeProps.threshold_MeV = cfg->GetValueWithUnits(childDigi + "threshold", 0.0) / MeV;
                childDesc.digitizeProps.quantumEfficiency = cfg->GetDouble(childDigi + "quantum_efficiency", 1.0);
                childDesc.digitizeProps.calorimeterMode = cfg->GetBool(childDigi + "calorimeter_mode", false);
                childDesc.digitizeProps.energyResFWHM = cfg->GetDouble(childDigi + "energy_res_fwhm", 0.0);
                childDesc.digitizeProps.timeResFWHM_ns = cfg->GetValueWithUnits(childDigi + "time_res_fwhm_ns", 0.0) / ns;
                childDesc.digitizeProps.deadTime_ns = cfg->GetValueWithUnits(childDigi + "dead_time_ns", 0.0) / ns;
                childDesc.digitizeProps.noiseLevel_MeV = cfg->GetValueWithUnits(childDigi + "noise_level", 0.0) / MeV;
                childDesc.digitizeProps.gain = cfg->GetDouble(childDigi + "gain", 1.0);
                childDesc.digitizeProps.pileupModel = cfg->GetString(childDigi + "pileup_model", "none");
            }

            currentDesc.children.push_back(childDesc);
        }

        fObjects.push_back(currentDesc);
        fIndexMap[currentDesc.name] = fObjects.size() - 1;
        G4cout << "ObjectManager: Added object '" << currentDesc.name << "' (id=" << objId
               << ", replica=" << (currentDesc.replica.count > 0 ? "yes" : "no")
               << ", children=" << currentDesc.children.size() << ")" << G4endl;

    }
    G4cout << "ObjectManager: Total " << fObjects.size() << " objects created" << G4endl;
}

/// @brief Create G4VSolid/G4LogicalVolume/G4PVPlacement for each ObjDesc
///        and place them into the mother volume.
/// @param mother Mother logical volume (typically World or GDML world).
void ObjectManager::PlaceObjects(G4LogicalVolume* mother) {
    if (!mother) {
        G4cout << "ObjectManager: mother logical volume is null, can't place objects." << G4endl;
        return;
    }
    
    G4NistManager* nist = G4NistManager::Instance();
    G4SDManager* sdManager = G4SDManager::GetSDMpointer();
    DetectorRegistry* detReg = BeamAnalysis::Instance()->GetDetectorRegistry();

    int copyNo = 1;
    for (auto& obj : fObjects) {
        G4LogicalVolume* lvolFromGDML = nullptr;

        //==================================================================
        // GDML file import: if gdmlFile is set, load geometry+material from GDML.
        // shape/material/DIMENSIONS from YAML are ignored. POSITION/mother/
        // isSensitive/DIGITIZE/cuts still come from YAML.
        //==================================================================
        if (!obj.gdmlFile.empty()) {
            G4cout << "ObjectManager: loading object '" << obj.name
                   << "' from GDML: " << obj.gdmlFile << G4endl;

            G4GDMLParser gdmlParser;
            gdmlParser.Read(obj.gdmlFile);
            G4VPhysicalVolume* gdmlWorld = gdmlParser.GetWorldVolume();
            if (!gdmlWorld) {
                G4cerr << "ObjectManager: ERROR - GDML file '" << obj.gdmlFile
                       << "' returned null world. Skipping object '" << obj.name << "'." << G4endl;
                continue;
            }

            // Extract the logical volume from the GDML world
            G4LogicalVolume* gdmlWorldLV = gdmlWorld->GetLogicalVolume();
            if (!gdmlWorldLV) {
                G4cerr << "ObjectManager: ERROR - GDML world has no logical volume for '"
                       << obj.name << "'. Skipping." << G4endl;
                continue;
            }

            // The GDML "world" contains our object as a daughter.
            // Extract the first (and ideally only) daughter logical volume.
            int nDaughters = gdmlWorldLV->GetNoDaughters();
            if (nDaughters == 0) {
                G4cerr << "ObjectManager: ERROR - GDML world in '" << obj.gdmlFile
                       << "' has no daughters. Cannot extract object '" << obj.name
                       << "'. Skipping." << G4endl;
                continue;
            }

            if (nDaughters > 1) {
                G4cout << "ObjectManager: GDML world in '" << obj.gdmlFile
                       << "' has " << nDaughters
                       << " daughters. Using the first one for object '" << obj.name << "'."
                       << G4endl;
            }

            G4VPhysicalVolume* firstDaughter = gdmlWorldLV->GetDaughter(0);
            if (!firstDaughter) {
                G4cerr << "ObjectManager: ERROR - first daughter is null in GDML '"
                       << obj.gdmlFile << "'. Skipping object '" << obj.name << "'." << G4endl;
                continue;
            }

            lvolFromGDML = firstDaughter->GetLogicalVolume();
            if (!lvolFromGDML) {
                G4cerr << "ObjectManager: ERROR - daughter logical volume is null in GDML '"
                       << obj.gdmlFile << "'. Skipping object '" << obj.name << "'." << G4endl;
                continue;
            }

            obj.logicalVolume = lvolFromGDML;
            G4cout << "ObjectManager: loaded GDML object '" << obj.name
                   << "' with LV '" << lvolFromGDML->GetName() << "'" << G4endl;

            // SensitiveDetector for GDML object
            if (obj.isSensitive) {
                G4String sdName = obj.name + "_SD";
                SensitiveDetector* sd = new SensitiveDetector(sdName);
                sd->SetDetectorID(copyNo);
                sdManager->AddNewDetector(sd);
                lvolFromGDML->SetSensitiveDetector(sd);
                // Register in DetectorRegistry
                if (detReg) {
                    detReg->RegisterDetector(sd, sdName, obj.digitizeProps);
                }
                G4cout << "ObjectManager: attached SensitiveDetector '" << sdName
                       << "' (ID=" << copyNo << ") to GDML volume '" << obj.name << "'" << G4endl;
            }

            // Mother volume
            G4LogicalVolume* actualMother = mother;
            if (!obj.mother.empty()) {
                auto it = fIndexMap.find(obj.mother);
                if (it != fIndexMap.end() && fObjects[it->second].logicalVolume) {
                    actualMother = fObjects[it->second].logicalVolume;
                } else {
                    G4cerr << "ObjectManager: Mother '" << obj.mother << "' not found for object '"
                           << obj.name << "'. Placing in World." << G4endl;
                }
            }

            // Place GDML logical volume at YAML-specified position
            G4VPhysicalVolume* physVol = new G4PVPlacement(
                nullptr, obj.pos, lvolFromGDML, obj.name,
                actualMother, false, copyNo++);
            obj.physicalVolume = physVol;

            // Cuts for GDML object
            if (!obj.fCuts.empty()) {
                G4Region* region = new G4Region(obj.name + "_region");
                region->AddRootLogicalVolume(obj.logicalVolume);
                G4ProductionCuts* cuts = new G4ProductionCuts();
                for (const auto& pair : obj.fCuts) {
                    cuts->SetProductionCut(pair.second, pair.first);
                }
                region->SetProductionCuts(cuts);
                G4cout << "ObjectManager: created region for GDML object '" << obj.name << "'" << G4endl;
            }

            // Max step size for GDML object
            if (obj.fMaxStepSize > 0.0) {
                obj.logicalVolume->SetUserLimits(new G4UserLimits(obj.fMaxStepSize));
                G4cout << "ObjectManager: set max step size " << obj.fMaxStepSize/mm
                       << " mm for GDML volume '" << obj.name << "'" << G4endl;
            }

            G4cout << "ObjectManager: placed GDML object '" << obj.name << "' at ("
                   << G4BestUnit(obj.pos.x(),"Length") << ", "
                   << G4BestUnit(obj.pos.y(),"Length") << ", "
                   << G4BestUnit(obj.pos.z(),"Length") << ")" << G4endl;

            continue;  // Skip programmatic solid creation for this object
        }

        //==================================================================
        // Programmatic solid creation (no GDML file)
        //==================================================================
        G4VSolid* solid = nullptr;
        
        G4cout << "ObjectManager: creating object '" << obj.name 
               << "' with shape '" << obj.shape << "'";
        if (!obj.mother.empty()) G4cout << " (mother: " << obj.mother << ")";
        G4cout << G4endl;
        
        if (obj.shape == "box") {
            double sx = 10*mm, sy = 10*mm, sz = 10*mm;
            if (obj.rawSizes.size() >= 3) {
                try {
                    sx = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    sy = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                    sz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[2].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing box sizes: " << e.what() 
                           << ". Using default 10 mm." << G4endl;
                }
            } else if (!obj.rawSizes.empty()) {
                try {
                    sx = sy = sz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing box size: " << e.what() 
                           << ". Using default 10 mm." << G4endl;
                }
            }
            solid = new G4Box(obj.name, sx/2.0, sy/2.0, sz/2.0);
            G4cout << "ObjectManager: created box '" << obj.name << "' with size ("
                   << sx/mm << " mm, " << sy/mm << " mm, " << sz/mm << " mm)" << G4endl;
        } 
        else if (obj.shape == "sphere") {
            double rmin = 0.0, rmax = 10*mm;
            if (obj.rawSizes.size() >= 2) {
                try {
                    rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing sphere radii: " << e.what() 
                           << ". Using defaults (rmin=0, rmax=10 mm)." << G4endl;
                }
            } else if (!obj.rawSizes.empty()) {
                try {
                    rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing sphere radius: " << e.what() 
                           << ". Using default 10 mm." << G4endl;
                }
            }
            solid = new G4Sphere(obj.name, rmin, rmax, 0., 360.*deg, 0., 180.*deg);
            G4cout << "ObjectManager: created sphere '" << obj.name << "' with rmin=" 
                   << rmin/mm << " mm, rmax=" << rmax/mm << " mm" << G4endl;
        } 
        else if (obj.shape == "cylinder") {
            double r = 10*mm, h = 10*mm;
            if (obj.rawSizes.size() >= 2) {
                try {
                    r = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    h = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing cylinder sizes: " << e.what() 
                           << ". Using default 10 mm." << G4endl;
                }
            } else if (!obj.rawSizes.empty()) {
                try {
                    r = h = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing cylinder size: " << e.what() 
                           << ". Using default 10 mm." << G4endl;
                }
            }
            solid = new G4Tubs(obj.name, 0.0, r, h/2.0, 0., 360.*deg);
            G4cout << "ObjectManager: created cylinder '" << obj.name << "' with radius " 
                   << r/mm << " mm, height " << h/mm << " mm" << G4endl;
        }
        else if (obj.shape == "tube") {
            double rmin = 5*mm, rmax = 10*mm, h = 10*mm;
            if (obj.rawSizes.size() >= 3) {
                try {
                    rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                    h = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[2].c_str());
                } catch (const std::exception& e) {
                    G4cerr << "ObjectManager: Error parsing tube sizes: " << e.what() 
                           << ". Using defaults." << G4endl;
                }
            } else {
                G4cerr << "ObjectManager: Not enough sizes for tube, need 3 (rmin, rmax, height). Using defaults." << G4endl;
            }
            solid = new G4Tubs(obj.name, rmin, rmax, h/2.0, 0., 360.*deg);
            G4cout << "ObjectManager: created tube '" << obj.name << "' with rmin=" 
                   << rmin/mm << " mm, rmax=" << rmax/mm << " mm, height=" << h/mm << " mm" << G4endl;
        }
        else if (obj.shape == "cone") {
            double rmin1 = 0.0, rmax1 = 10*mm, rmin2 = 0.0, rmax2 = 10*mm, h = 10*mm;
            if (obj.rawSizes.size() >= 5) {
                try {
                    rmin1 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    rmax1 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                    rmin2 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[2].c_str());
                    rmax2 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[3].c_str());
                    h     = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[4].c_str());
                } catch (...) { G4cerr << "ObjectManager: Error parsing cone sizes." << G4endl; }
            }
            solid = new G4Cons(obj.name, rmin1, rmax1, rmin2, rmax2, h/2.0, 0., 360.*deg);
            G4cout << "ObjectManager: created cone '" << obj.name << "'" << G4endl;
        }
        else if (obj.shape == "torus") {
            double rmin = 0.0, rmax = 10*mm, rtor = 20*mm;
            if (obj.rawSizes.size() >= 3) {
                try {
                    rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                    rtor = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[2].c_str());
                } catch (...) { G4cerr << "ObjectManager: Error parsing torus sizes." << G4endl; }
            }
            solid = new G4Torus(obj.name, rmin, rmax, rtor, 0., 360.*deg);
            G4cout << "ObjectManager: created torus '" << obj.name << "'" << G4endl;
        }
        else if (obj.shape == "para") {
            double dx = 10*mm, dy = 10*mm, dz = 10*mm, alpha = 0, theta = 0, phi = 0;
            if (obj.rawSizes.size() >= 6) {
                try {
                    dx = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[0].c_str());
                    dy = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[1].c_str());
                    dz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[2].c_str());
                    alpha = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[3].c_str());
                    theta = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[4].c_str());
                    phi   = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(obj.rawSizes[5].c_str());
                } catch (...) { G4cerr << "ObjectManager: Error parsing para sizes." << G4endl; }
            }
            solid = new G4Para(obj.name, dx/2.0, dy/2.0, dz/2.0, alpha, theta, phi);
            G4cout << "ObjectManager: created para '" << obj.name << "'" << G4endl;
        }
        else {
            G4cout << "ObjectManager: unknown shape '" << obj.shape << "' for object '" 
                   << obj.name << "'. Skipping." << G4endl;
            continue;
        }
         G4String matName = obj.material.empty() ? "G4_AIR" : obj.material;
         G4Material* mat = G4Material::GetMaterial(matName, false);
        if (!mat) {
            mat = nist->FindOrBuildMaterial(matName);
        }
        if (!mat) {
            G4cerr << "ObjectManager: ERROR - Material '" << matName << "' not found for object '" 
                   << obj.name << "'. Neither custom nor NIST. Using G4_AIR." << G4endl;
            mat = nist->FindOrBuildMaterial("G4_AIR");
        }

        G4LogicalVolume* lvol = new G4LogicalVolume(solid, mat, obj.name + "_LV");
        
        obj.logicalVolume = lvol;

        G4LogicalVolume* actualMother = mother;
        if (!obj.mother.empty()) {
            auto it = fIndexMap.find(obj.mother);
            if (it != fIndexMap.end() && fObjects[it->second].logicalVolume) {
                actualMother = fObjects[it->second].logicalVolume;
            } else {
                G4cerr << "ObjectManager: Mother '" << obj.mother << "' not found for object '"
                       << obj.name << "'. Placing in World." << G4endl;
            }
        }

        // Create child solids (shared LVs — one SD for all replicas)
        for (auto& child : obj.children) {
            if (!child.logicalVolume) {
                // Quick inline solid creation for children
                G4VSolid* childSolid = nullptr;
                if (child.shape == "box") {
                    double sx = 10*mm, sy = 10*mm, sz = 10*mm;
                    if (child.rawSizes.size() >= 3) {
                        try {
                            sx = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(child.rawSizes[0].c_str());
                            sy = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(child.rawSizes[1].c_str());
                            sz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(child.rawSizes[2].c_str());
                        } catch (...) {}
                    }
                    childSolid = new G4Box(child.name, sx/2.0, sy/2.0, sz/2.0);
                }
                if (!childSolid) {
                    G4cerr << "ObjectManager: could not create solid for child '" << child.name << "'" << G4endl;
                    continue;
                }
                G4Material* childMat = G4Material::GetMaterial(child.material, false);
                if (!childMat) childMat = nist->FindOrBuildMaterial(child.material);
                if (!childMat) childMat = nist->FindOrBuildMaterial("G4_AIR");
                child.logicalVolume = new G4LogicalVolume(childSolid, childMat, child.name + "_LV");
            }
        }

        // REPLICA placement
        if (obj.replica.count > 0 && !obj.replica.axis.empty()) {
            char axis = obj.replica.axis[0];
            G4cout << "ObjectManager: placing " << obj.replica.count
                   << " replicas of '" << obj.name << "' (axis=" << obj.replica.axis
                   << ", step=" << obj.replica.step/mm << " mm)" << G4endl;
            for (int i = 0; i < obj.replica.count; i++) {
                double offset = obj.replica.offset + i * obj.replica.step;
                G4ThreeVector replicaPos = obj.pos;
                if (axis == 'x' || axis == 'X') replicaPos.setX(replicaPos.x() + offset);
                else if (axis == 'y' || axis == 'Y') replicaPos.setY(replicaPos.y() + offset);
                else replicaPos.setZ(replicaPos.z() + offset);

                G4VPhysicalVolume* physVol = new G4PVPlacement(
                    nullptr, replicaPos, lvol, obj.name,
                    actualMother, false, i);
                if (i == 0) obj.physicalVolume = physVol;

                // Place children inside each replica
                for (auto& child : obj.children) {
                    if (!child.logicalVolume) continue;
                    new G4PVPlacement(nullptr, child.pos, child.logicalVolume,
                        child.name, lvol, false, i);
                }
            }
        } else {
            // Single placement (no replica)
            G4VPhysicalVolume* physVol = new G4PVPlacement(nullptr, obj.pos, lvol, obj.name, actualMother, false, copyNo++);
            obj.physicalVolume = physVol;

            // Place children in single parent
            for (auto& child : obj.children) {
                if (!child.logicalVolume) continue;
                new G4PVPlacement(nullptr, child.pos, child.logicalVolume, child.name, lvol, false, 0);
            }

            G4cout << "ObjectManager: placed object '" << obj.name << "' at ("
                   << G4BestUnit(obj.pos.x(),"Length") << ", "
                   << G4BestUnit(obj.pos.y(),"Length") << ", "
                   << G4BestUnit(obj.pos.z(),"Length") << ")" << G4endl;
        }

         if (!obj.fCuts.empty()) {
            for (const auto& p : obj.fCuts) {
                G4cout << "  " << p.first << " = " << p.second/mm << " mm" << G4endl;
            }

            G4Region* region = new G4Region(obj.name + "_region");
            region->AddRootLogicalVolume(obj.logicalVolume);

            G4ProductionCuts* cuts = new G4ProductionCuts();
            for (const auto& pair : obj.fCuts) {
                cuts->SetProductionCut(pair.second, pair.first);
            }
            region->SetProductionCuts(cuts);
            G4cout << "ObjectManager: created region for '" << obj.name
                   << "' with cuts: ";
            for (const auto& pair : obj.fCuts) {
                G4cout << pair.first << "=" << pair.second/mm << " mm ";
            }
            G4cout << G4endl;
        }

        if (obj.fMaxStepSize > 0.0) {
            obj.logicalVolume->SetUserLimits(new G4UserLimits(obj.fMaxStepSize));
            G4cout << "ObjectManager: set max step size " << obj.fMaxStepSize/mm
                   << " mm for volume '" << obj.name << "'" << G4endl;
        }

        G4cout << "ObjectManager: placed object '" << obj.name << "' at ("
               << G4BestUnit(obj.pos.x(),"Length") << ", "
               << G4BestUnit(obj.pos.y(),"Length") << ", "
               << G4BestUnit(obj.pos.z(),"Length") << ")" << G4endl;
    }
}

/// @brief Find an object by name (const).
/// @param name Object name.
/// @return Pointer to ObjDesc or nullptr.
const ObjDesc* ObjectManager::FindObject(const std::string& name) const {
    auto it = fIndexMap.find(name);
    if (it != fIndexMap.end()) {
        return &fObjects[it->second];
    }
    return nullptr;
}

/// @brief Check if an object exists by name.
/// @param name Object name.
/// @return true if object is registered.
bool ObjectManager::HasObject(const std::string& name) const {
    return fIndexMap.find(name) != fIndexMap.end();
}

/// @brief Find an object by name (mutable).
/// @param name Object name.
/// @return Pointer to ObjDesc or nullptr.
ObjDesc* ObjectManager::FindObject(const std::string& name) {
    auto it = fIndexMap.find(name);
    if (it != fIndexMap.end()) {
        return &fObjects[it->second];
    }
    return nullptr;
}