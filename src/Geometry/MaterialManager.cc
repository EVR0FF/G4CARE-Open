//==============================================================================
//
// G4CARE
//
// @file    MaterialManager.cc
// @brief   Implementation of MaterialManager — custom material builder.
//
// @details
//   Reads the `materials` block from YAML config and constructs
//   G4Material, G4Element, and G4Isotope objects.  Supports elemental
//   composition, isotope mixtures, and optical properties tables.
//
//   Configuration keys read:
//     materials.<name>.name, .density, .temperature, .pressure, .state
//     materials.<name>.elements.<el>.name, .Z, .abundance
//     materials.<name>.elements.<el>.isotopes.<iso>.name, .A, .mass, .abundance
//     materials.<name>.materials.<sub>.name, .abundance
//     materials.<name>.optical (YAML node)
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

#include "MaterialManager.hh"
#include "Config/ConfigManager.hh"
#include "G4NistManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4PhysicalConstants.hh"
#include "G4Isotope.hh"
#include "G4Element.hh"
#include "G4Material.hh"
#include "G4UnitsTable.hh"
#include <numeric> 
#include <fstream>
#include <sstream>

MaterialManager* MaterialManager::fInstance = nullptr;

MaterialManager* MaterialManager::Instance() {
    if (!fInstance) {
        fInstance = new MaterialManager();
    }
    return fInstance;
}

void MaterialManager::DeleteInstance() {
    delete fInstance;
    fInstance = nullptr;
}

MaterialManager::MaterialManager() {}

MaterialManager::~MaterialManager() {}

/// @brief Parse a string with optional Geant4 unit into a double.
/// @param str Input string (e.g. "1.5*cm" or "300*kelvin").
/// @return Value in Geant4 internal units.
static G4double ParseDoubleWithUnit(const std::string& str) {
    if (str.empty()) return 0.0;
    
    // Simple heuristic: find '*' and split number and unit
    size_t starPos = str.find('*');
    std::string numPart = str;
    std::string unitPart = "";
    
    if (starPos != std::string::npos) {
        numPart = str.substr(0, starPos);
        unitPart = str.substr(starPos + 1);
    } else {
        // Try to find the start of unit letters after digits
        size_t i = 0;
        bool digitFound = false;
        for (; i < str.length(); ++i) {
            char c = str[i];
            if (std::isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') {
                digitFound = true;
            } else if (std::isalpha(c) || c == '_') {
                if (digitFound) {
                    numPart = str.substr(0, i);
                    unitPart = str.substr(i);
                    break;
                }
            }
        }
    }

    G4double val = 0.0;
    try {
        val = std::stod(numPart);
    } catch (...) {
        return 0.0;
    }

    if (unitPart.empty()) return val;

    // Use Geant4 unit table for conversion
    if (G4UnitDefinition::IsUnitDefined(unitPart)) {
        G4double unitValue = G4UnitDefinition::GetValueOf(unitPart);
        return val * unitValue;
    } else {
        G4cerr << "MaterialManager: Warning - Unknown unit '" << unitPart << "' in '" << str << "'. Assuming dimensionless." << G4endl;
        return val;
    }
}

/// @brief Build optical properties for a material from YAML node.
/// @param mat Target G4Material.
/// @param opticalNode YAML node containing optical property definitions.
void MaterialManager::BuildOpticalProperties(G4Material* mat, const YAML::Node& opticalNode) {
    if (!mat || !opticalNode.IsDefined() || !opticalNode.IsMap()) return;

    G4MaterialPropertiesTable* mpt = new G4MaterialPropertiesTable();
    bool hasOpticalData = false;

    // List of known Geant4 optical properties
    std::vector<std::string> knownProps = {
        "RINDEX", "ABSLENGTH", "RAYLEIGH", "MIEHG", "COMPONENTS", 
        "FASTCOMPONENT", "SLOWCOMPONENT", "SCINTILLATIONYIELD", 
        "RESOLUTIONSCALE", "YIELDRATIO", "SINGLETTRIPLET", 
        "RISECONSTANT", "DECAYTIME", "FASTTIMECONSTANT", "SLOWTIMECONSTANT"
    };

    for (const auto& prop : knownProps) {
        YAML::Node node = opticalNode[prop];
        if (!node.IsDefined()) continue;

        std::vector<G4double> energies;
        std::vector<G4double> values;
        G4double constValue = -1.0;
        bool isConstant = false;

        // 1. Check for constant (scalar)
        if (node.IsScalar()) {
            std::string valStr = node.as<std::string>();
            constValue = ParseDoubleWithUnit(valStr);
            isConstant = true;
        }
        // 2. Check for table (sequence)
        else if (node.IsSequence()) {
            for (const auto& item : node) {
                if (item.IsSequence() && item.size() >= 2) {
                    // Format: [Energy, Value]
                    std::string eStr = item[0].as<std::string>();
                    std::string vStr = item[1].as<std::string>();
                    
                    energies.push_back(ParseDoubleWithUnit(eStr));
                    values.push_back(ParseDoubleWithUnit(vStr));
                } else if (item.IsMap()) {
                    // Format: {energy: "...", value: "..."}
                    std::string eStr = item["energy"].as<std::string>();
                    std::string vStr = item["value"].as<std::string>();
                    
                    energies.push_back(ParseDoubleWithUnit(eStr));
                    values.push_back(ParseDoubleWithUnit(vStr));
                }
            }
            
            // Sort by energy
            if (!energies.empty()) {
                std::vector<size_t> idx(energies.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j){ return energies[i] < energies[j]; });
                
                std::vector<G4double> sortedE = energies;
                std::vector<G4double> sortedV = values;
                for(size_t i=0; i<energies.size(); ++i) {
                    sortedE[i] = energies[idx[i]];
                    sortedV[i] = values[idx[i]];
                }
                energies = sortedE;
                values = sortedV;
            }
        }
        // 3. Check for file (map with 'file' key)
        else if (node.IsMap() && node["file"]) {
            std::string filename = node["file"].as<std::string>();
            std::string unitE = node["unit_energy"].as<std::string>("");
            std::string unitV = node["unit_value"].as<std::string>("");
            
            std::ifstream file(filename);
            if (!file.is_open()) {
                G4cerr << "MaterialManager: ERROR - Cannot open optical data file: " << filename << G4endl;
                continue;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#') continue;
                
                std::stringstream ss(line);
                std::string col1, col2;
                if (line.find(',') != std::string::npos) {
                    std::getline(ss, col1, ',');
                    std::getline(ss, col2, ',');
                } else {
                    ss >> col1 >> col2;
                }

                G4double eVal = std::stod(col1);
                G4double vVal = std::stod(col2);

                if (col1.find('*') == std::string::npos && !unitE.empty()) {
                    eVal *= ParseDoubleWithUnit("1.0*" + unitE);
                } else {
                    eVal = ParseDoubleWithUnit(col1);
                }

                if (col2.find('*') == std::string::npos && !unitV.empty()) {
                    vVal *= ParseDoubleWithUnit("1.0*" + unitV);
                } else {
                    vVal = ParseDoubleWithUnit(col2);
                }

                energies.push_back(eVal);
                values.push_back(vVal);
            }
            // Sort
            if (!energies.empty()) {
                std::vector<size_t> idx(energies.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j){ return energies[i] < energies[j]; });
                std::vector<G4double> sortedE = energies;
                std::vector<G4double> sortedV = values;
                for(size_t i=0; i<energies.size(); ++i) {
                    sortedE[i] = energies[idx[i]];
                    sortedV[i] = values[idx[i]];
                }
                energies = sortedE;
                values = sortedV;
            }
        }

        // Add property to the table
        if (isConstant) {
            mpt->AddConstProperty(prop, constValue);
            hasOpticalData = true;
            G4cout << "MaterialManager: Added constant property " << prop << " = " << constValue << " to " << mat->GetName() << G4endl;
        } else if (!energies.empty()) {
            mpt->AddProperty(prop, energies, values);
            hasOpticalData = true;
            G4cout << "MaterialManager: Added table property " << prop << " (" << energies.size() << " points) to " << mat->GetName() << G4endl;
        }
    }

    if (hasOpticalData) {
        mat->SetMaterialPropertiesTable(mpt);
    } else {
        delete mpt;
    }
}

/// @brief Build all custom materials from the `materials` config block.
/// @return true on success.
bool MaterialManager::BuildMaterials() {
    auto* cfg = ConfigManager::Instance();
    
    // Check if `materials` block exists
    std::vector<std::string> matIds = cfg->GetSubsections("materials");
    if (matIds.empty()) {
        G4cout << "MaterialManager: No custom 'materials' block found in config. Using NIST only." << G4endl;
        return true;
    }

    G4cout << "MaterialManager: Found " << matIds.size() << " custom materials to build." << G4endl;

    for (const auto& matId : matIds) {
        std::string baseKey = "materials." + matId + ".";
        
        std::string name = cfg->GetString(baseKey + "name", "");
        if (name.empty()) {
            G4cerr << "MaterialManager: ERROR - Material with ID '" << matId << "' has no name. Skipping." << G4endl;
            continue;
        }

        // Check for duplicate name
        if (G4Material::GetMaterial(name, false)) {
            G4cerr << "MaterialManager: WARNING - Material '" << name << "' already exists. Skipping creation." << G4endl;
            continue;
        }

        G4double density = cfg->GetValueWithUnits(baseKey + "density", 0.0);
        if (density <= 0.0) {
            G4cerr << "MaterialManager: ERROR - Material '" << name << "' has invalid density <= 0. Skipping." << G4endl;
            continue;
        }

        G4double temperature = cfg->GetValueWithUnits(baseKey + "temperature", 273.15 * kelvin);
        G4double pressure = cfg->GetValueWithUnits(baseKey + "pressure", 1.0 * atmosphere);
        
        std::string stateStr = cfg->GetString(baseKey + "state", "undefined");
        G4State state = kStateUndefined;
        if (stateStr == "gas") state = kStateGas;
        else if (stateStr == "liquid") state = kStateLiquid;
        else if (stateStr == "solid") state = kStateSolid;

        std::vector<std::string> elemIds = cfg->GetSubsections(baseKey + "elements");
        std::vector<std::string> matIds = cfg->GetSubsections(baseKey + "materials");

        if (elemIds.empty() && matIds.empty()) {
            G4cerr << "MaterialManager: ERROR - Material '" << name << "' has no elements or materials defined. Skipping." << G4endl;
            continue;
        }

        // Collect sub-materials first (if any)
        std::vector<G4Material*> subMaterialList;
        std::vector<G4double> subMaterialFractionList;

        for (const auto& matSubId : matIds) {
            std::string matSubKey = baseKey + "materials." + matSubId + ".";
            std::string subMatName = cfg->GetString(matSubKey + "name", "");
            G4double subAbundance = cfg->GetDouble(matSubKey + "abundance", 0.0);

            if (subMatName.empty() || subAbundance <= 0.0) {
                G4cerr << "MaterialManager: ERROR - Invalid sub-material in '" << name << "'. Skipping." << G4endl;
                continue;
            }

            G4Material* subMat = G4Material::GetMaterial(subMatName, false);
            if (!subMat) {
                G4NistManager* nist = G4NistManager::Instance();
                subMat = nist->FindOrBuildMaterial(subMatName);
            }
            if (!subMat) {
                G4cerr << "MaterialManager: ERROR - Sub-material '" << subMatName << "' not found for '" << name << "'. Skipping." << G4endl;
                continue;
            }

            subMaterialList.push_back(subMat);
            subMaterialFractionList.push_back(subAbundance);
        }

        // Collect elements
        std::vector<G4Element*> elementList;
        std::vector<G4double> fractionList;

        for (const auto& elemId : elemIds) {
            std::string elemKey = baseKey + "elements." + elemId + ".";
            
            std::string elemName = cfg->GetString(elemKey + "name", "");
            G4int Z = cfg->GetInt(elemKey + "Z", 0);
            G4double abundance = cfg->GetDouble(elemKey + "abundance", 0.0);

            if (elemName.empty() || Z <= 0 || abundance <= 0.0) {
                G4cerr << "MaterialManager: ERROR - Invalid element definition in material '" << name << "'. Skipping element." << G4endl;
                continue;
            }

            G4Element* element = nullptr;
            std::vector<std::string> isoIds = cfg->GetSubsections(elemKey + "isotopes");

            if (!isoIds.empty()) {
                // Create element with custom isotopes
                std::string uniqueElemName = name + "_" + elemName + "_Z" + std::to_string(Z);
                
                element = G4Element::GetElement(uniqueElemName, false);
                
                if (!element) {
                    element = new G4Element(elemName, uniqueElemName, Z);
                    
                    for (const auto& isoId : isoIds) {
                        std::string isoKey = elemKey + "isotopes." + isoId + ".";
                        
                        std::string isoNameRaw = cfg->GetString(isoKey + "name", "");
                        G4int A = cfg->GetInt(isoKey + "A", 0);
                        G4double molarMass = cfg->GetValueWithUnits(isoKey + "mass", 0.0);
                        G4double isoAbundance = cfg->GetDouble(isoKey + "abundance", 0.0);

                        if (isoNameRaw.empty() || A <= 0 || molarMass <= 0.0 || isoAbundance <= 0.0) {
                            G4cerr << "MaterialManager: ERROR - Invalid isotope definition in element '" << elemName << "' of material '" << name << "'. Skipping isotope." << G4endl;
                            continue;
                        }

                        std::string uniqueIsoName = name + "_" + elemName + "_" + isoNameRaw;

                        G4Isotope* isotope = new G4Isotope(uniqueIsoName, Z, A, molarMass);
                        element->AddIsotope(isotope, isoAbundance);
                    }
                }
            } else {
                G4NistManager* nist = G4NistManager::Instance();
                element = nist->FindOrBuildElement(Z, false);
                
                if (!element) {
                     G4cerr << "MaterialManager: WARNING - Element Z=" << Z << " not found in NIST. Creating empty placeholder." << G4endl;
                     element = new G4Element(elemName, elemName, Z);
                }
            }

            if (element) {
                elementList.push_back(element);
                fractionList.push_back(abundance);
            }
        }

        if (elementList.empty() && subMaterialList.empty()) {
            G4cerr << "MaterialManager: ERROR - Material '" << name << "' ended up with no valid elements or materials. Skipping." << G4endl;
            continue;
        }

        // Calculate total number of components (elements + sub-materials)
        size_t nComponents = elementList.size() + subMaterialList.size();
        G4Material* material = new G4Material(name, density, nComponents, state, temperature, pressure);
        
        // Add sub-materials
        for (size_t i = 0; i < subMaterialList.size(); ++i) {
            material->AddMaterial(subMaterialList[i], subMaterialFractionList[i]);
        }

        // Add elements
        for (size_t i = 0; i < elementList.size(); ++i) {
            material->AddElement(elementList[i], fractionList[i]);
        }
        // --- Optical properties processing ---
        std::string opticalKey = baseKey + "optical";
        YAML::Node opticalNode = cfg->GetNode(opticalKey);
        
        if (opticalNode.IsDefined() && !opticalNode.IsNull()) {
            BuildOpticalProperties(material, opticalNode);
        }

        G4cout << "MaterialManager: Successfully created material '" << name << "' (" 
               << density/(g/cm3) << " g/cm3, " << elementList.size() << " elements)." << G4endl;
    }

    return true;
    
}