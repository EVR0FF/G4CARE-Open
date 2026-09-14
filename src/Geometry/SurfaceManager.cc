//==============================================================================
//
// G4CARE
//
// @file    SurfaceManager.cc
// @brief   Optical surface builder from YAML configuration.
//
// @details
//   Reads the `surfaces` configuration block and constructs
//   G4OpticalSurface, G4LogicalSkinSurface, and G4LogicalBorderSurface
//   objects. Supports all Geant4 optical models (glisur, unified, LUT,
//   DAVIS, dichroic), finishes, surface types, and material properties
//   tables (REFLECTIVITY, EFFICIENCY, TRANSMITTANCE, etc.) loaded from
//   inline YAML data or external files.
//
//   Configuration keys read:
//     surfaces.<id>.name
//     surfaces.<id>.type
//     surfaces.<id>.volume
//     surfaces.<id>.phys_volume1
//     surfaces.<id>.phys_volume2
//     surfaces.<id>.properties (YAML node)
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

#include "SurfaceManager.hh"
#include "ConfigManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4PhysicalConstants.hh"
#include "G4UnitsTable.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4ios.hh"
#include "G4OpticalSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4LogicalBorderSurface.hh"
#include "G4MaterialPropertiesTable.hh"

#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <unordered_map>

//------------------------------------------------------------------------------
// Parses a string with optional unit (e.g. "1.5*eV") to a double in Geant4
// internal units.
//------------------------------------------------------------------------------
static G4double ParseDoubleWithUnit(const std::string& str) {
    if (str.empty()) return 0.0;

    std::string s = str;
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return 0.0;
    size_t last = s.find_last_not_of(" \t\n\r");
    s = s.substr(first, last - first + 1);

    size_t starPos = s.find('*');
    std::string numPart = s;
    std::string unitPart = "";

    if (starPos != std::string::npos) {
        numPart = s.substr(0, starPos);
        unitPart = s.substr(starPos + 1);
    } else {
        size_t i = 0;
        bool digitFound = false;
        for (; i < s.length(); ++i) {
            char c = s[i];
            if (std::isdigit(c)) {
                digitFound = true;
            } else if (c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') {
                if (!digitFound && c != '-' && c != '+') break;
            } else if (std::isalpha(c) || c == '_') {
                if (digitFound) {
                    numPart = s.substr(0, i);
                    unitPart = s.substr(i);
                    break;
                } else {
                    break;
                }
            } else if (c == ' ' || c == '\t') {
                if (digitFound) {
                    size_t j = i;
                    while (j < s.length() && (s[j] == ' ' || s[j] == '\t')) j++;
                    if (j < s.length() && (std::isalpha(s[j]) || s[j] == '_')) {
                        numPart = s.substr(0, i);
                        unitPart = s.substr(j);
                        break;
                    }
                }
            } else {
                break;
            }
        }
    }

    G4double val = 0.0;
    try {
        val = std::stod(numPart);
    } catch (...) {
        G4cerr << "SurfaceManager: Warning - Invalid number format in '" << str << "'. Returning 0." << G4endl;
        return 0.0;
    }

    if (unitPart.empty()) return val;

    size_t uFirst = unitPart.find_first_not_of(" \t");
    if (uFirst != std::string::npos) unitPart = unitPart.substr(uFirst);

    if (G4UnitDefinition::IsUnitDefined(unitPart)) {
        return val * G4UnitDefinition::GetValueOf(unitPart);
    } else {
        G4cerr << "SurfaceManager: Warning - Unknown unit '" << unitPart << "' in '" << str << "'. Assuming dimensionless." << G4endl;
        return val;
    }
}

//------------------------------------------------------------------------------
// Reads a YAML property table (list of [energy, value] pairs or {file: path})
// into energy and value vectors.
//------------------------------------------------------------------------------
static bool ReadPropertyTable(const YAML::Node& node, std::vector<G4double>& energies, std::vector<G4double>& values) {
    if (!node.IsDefined()) return false;

    if (node.IsScalar()) {
        return false;
    }

    if (node.IsSequence()) {
        for (const auto& item : node) {
            if (item.IsSequence() && item.size() >= 2) {
                try {
                    std::string eStr = item[0].as<std::string>();
                    std::string vStr = item[1].as<std::string>();
                    energies.push_back(ParseDoubleWithUnit(eStr));
                    values.push_back(ParseDoubleWithUnit(vStr));
                } catch (const std::exception& e) {
                    G4cerr << "SurfaceManager: Error parsing table entry: " << e.what() << G4endl;
                }
            }
        }
    } else if (node.IsMap() && node["file"]) {
        std::string filename = node["file"].as<std::string>();
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            G4cerr << "SurfaceManager: ERROR - Cannot open file " << filename << G4endl;
            return false;
        }

        std::string line;
        int lineNum = 0;
        while (std::getline(file, line)) {
            lineNum++;
            if (line.empty() || line[0] == '#') continue;
            
            std::stringstream ss(line);
            std::string c1, c2;
            if (line.find(',') != std::string::npos) {
                std::getline(ss, c1, ',');
                std::getline(ss, c2, ',');
            } else {
                ss >> c1 >> c2;
            }

            if (c1.empty() || c2.empty()) {
                G4cerr << "SurfaceManager: Warning - Empty column in file " << filename << " at line " << lineNum << ". Skipping." << G4endl;
                continue;
            }

            try {
                G4double eVal = ParseDoubleWithUnit(c1);
                G4double vVal = ParseDoubleWithUnit(c2);
                
                energies.push_back(eVal);
                values.push_back(vVal);
            } catch (const std::exception& e) {
                G4cerr << "SurfaceManager: Error parsing line " << lineNum << " in " << filename << ": " << e.what() << G4endl;
            }
        }
    }
    
    if (!energies.empty()) {
        std::vector<size_t> idx(energies.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j){ return energies[i] < energies[j]; });
        
        std::vector<G4double> sE(energies.size()), sV(values.size());
        for(size_t i=0; i<energies.size(); ++i) {
            sE[i] = energies[idx[i]];
            sV[i] = values[idx[i]];
        }
        energies = sE;
        values = sV;
    }
    return !energies.empty();
}

SurfaceManager* SurfaceManager::fInstance = nullptr;

//------------------------------------------------------------------------------
// @brief Returns the singleton instance.
//------------------------------------------------------------------------------
SurfaceManager* SurfaceManager::Instance() {
    if (!fInstance) fInstance = new SurfaceManager();
    return fInstance;
}

//------------------------------------------------------------------------------
// @brief Deletes the singleton instance.
//------------------------------------------------------------------------------
void SurfaceManager::DeleteInstance() { 
    delete fInstance; 
    fInstance = nullptr; 
}

SurfaceManager::SurfaceManager() {}
SurfaceManager::~SurfaceManager() {}

//------------------------------------------------------------------------------
// @brief Creates a G4OpticalSurface from a YAML properties node.
//
// Reads model, finish, type, sigma_alpha, polish, and material properties
// table entries. Automatically loads LUT/DAVIS/dichroic data files when
// the corresponding model is selected.
//
// @param name       Surface name.
// @param propsNode  YAML node containing surface properties.
// @return           Pointer to the created G4OpticalSurface, or nullptr.
//------------------------------------------------------------------------------
G4OpticalSurface* SurfaceManager::CreateOpticalSurface(const std::string& name, const YAML::Node& propsNode) {
    if (!propsNode.IsMap()) return nullptr;

    G4OpticalSurface* surface = new G4OpticalSurface(name);
    bool hasAnyProperty = false;

    // Model
    G4OpticalSurfaceModel model = glisur;
    if (propsNode["model"]) {
        std::string val = propsNode["model"].as<std::string>();
        if (val == "glisur") model = glisur;
        else if (val == "unified") model = unified;
        else if (val == "LUT") model = LUT;
        else if (val == "DAVIS") model = DAVIS;
        else if (val == "dichroic") model = dichroic;
        else G4cerr << "SurfaceManager: Warning - Unknown model '" << val << "' for '" << name << "'. Using default." << G4endl;
    }
    surface->SetModel(model);
    hasAnyProperty = true;

    // Finish
    G4OpticalSurfaceFinish finish = polished;
    if (propsNode["finish"]) {
        std::string val = propsNode["finish"].as<std::string>();
        if (val == "polished") finish = polished;
        else if (val == "ground") finish = ground;
        else if (val == "polishedfrontpainted") finish = polishedfrontpainted;
        else if (val == "polishedbackpainted") finish = polishedbackpainted;
        else if (val == "groundfrontpainted") finish = groundfrontpainted;
        else if (val == "groundbackpainted") finish = groundbackpainted;
        else if (val == "polishedlumirrorair") finish = polishedlumirrorair;
        else if (val == "polishedlumirrorglue") finish = polishedlumirrorglue;
        else if (val == "polishedair") finish = polishedair;
        else if (val == "polishedteflonair") finish = polishedteflonair;
        else if (val == "polishedtioair") finish = polishedtioair;
        else if (val == "polishedtyvekair") finish = polishedtyvekair;
        else if (val == "polishedvm2000air") finish = polishedvm2000air;
        else if (val == "polishedvm2000glue") finish = polishedvm2000glue;
        else if (val == "etchedlumirrorair") finish = etchedlumirrorair;
        else if (val == "etchedlumirrorglue") finish = etchedlumirrorglue;
        else if (val == "etchedair") finish = etchedair;
        else if (val == "etchedteflonair") finish = etchedteflonair;
        else if (val == "etchedtioair") finish = etchedtioair;
        else if (val == "etchedtyvekair") finish = etchedtyvekair;
        else if (val == "etchedvm2000air") finish = etchedvm2000air;
        else if (val == "etchedvm2000glue") finish = etchedvm2000glue;
        else if (val == "groundlumirrorair") finish = groundlumirrorair;
        else if (val == "groundlumirrorglue") finish = groundlumirrorglue;
        else if (val == "groundair") finish = groundair;
        else if (val == "groundteflonair") finish = groundteflonair;
        else if (val == "groundtioair") finish = groundtioair;
        else if (val == "groundtyvekair") finish = groundtyvekair;
        else if (val == "groundvm2000air") finish = groundvm2000air;
        else if (val == "groundvm2000glue") finish = groundvm2000glue;
        else if (val == "Rough_LUT") finish = Rough_LUT;
        else if (val == "RoughTeflon_LUT") finish = RoughTeflon_LUT;
        else if (val == "RoughESR_LUT") finish = RoughESR_LUT;
        else if (val == "RoughESRGrease_LUT") finish = RoughESRGrease_LUT;
        else if (val == "Polished_LUT") finish = Polished_LUT;
        else if (val == "PolishedTeflon_LUT") finish = PolishedTeflon_LUT;
        else if (val == "PolishedESR_LUT") finish = PolishedESR_LUT;
        else if (val == "PolishedESRGrease_LUT") finish = PolishedESRGrease_LUT;
        else if (val == "Detector_LUT") finish = Detector_LUT;
        else G4cerr << "SurfaceManager: Warning - Unknown finish '" << val << "' for '" << name << "'. Using default." << G4endl;
    }
    surface->SetFinish(finish);
    hasAnyProperty = true;

    // Surface type
    G4SurfaceType type = dielectric_dielectric;
    if (propsNode["type"]) {
        std::string val = propsNode["type"].as<std::string>();
        if (val == "dielectric_metal" || val == "metal") type = dielectric_metal;
        else if (val == "dielectric_dielectric") type = dielectric_dielectric;
        else if (val == "dielectric_LUT") type = dielectric_LUT;
        else if (val == "dielectric_DAVIS" || val == "dielectric_LUTDAVIS") type = dielectric_LUTDAVIS;
        else G4cerr << "SurfaceManager: Warning - Unknown type '" << val << "' for '" << name << "'. Using default." << G4endl;
    }
    surface->SetType(type);
    hasAnyProperty = true;

    // Model parameters
    if (propsNode["sigma_alpha"]) {
        surface->SetSigmaAlpha(ParseDoubleWithUnit(propsNode["sigma_alpha"].as<std::string>()));
        hasAnyProperty = true;
    }
    
    if (propsNode["polish"]) {
        surface->SetPolish(ParseDoubleWithUnit(propsNode["polish"].as<std::string>()));
        hasAnyProperty = true;
    }

    // Auto-load data files for LUT/DAVIS/Dichroic models
    if (model == LUT) {
        const char* dataDir = std::getenv("G4REALSURFACEDATA");
        if (!dataDir) {
            G4cerr << "SurfaceManager: ERROR - G4REALSURFACEDATA not set! Cannot load LUT data for '" << name << "'." << G4endl;
        } else {
            surface->ReadLUTFile();
            G4cout << "SurfaceManager: Loaded LUT data for surface '" << name << "'." << G4endl;
        }
    }
    else if (model == DAVIS) {
        const char* dataDir = std::getenv("G4REALSURFACEDATA");
        if (!dataDir) {
            G4cerr << "SurfaceManager: ERROR - G4REALSURFACEDATA not set! Cannot load DAVIS data for '" << name << "'." << G4endl;
        } else {
            surface->ReadLUTDAVISFile();
            G4cout << "SurfaceManager: Loaded DAVIS data for surface '" << name << "'." << G4endl;
        }
    }
    else if (model == dichroic) {
        const char* ledata = std::getenv("G4LEDATA");
        std::string expectedFile = ledata ? std::string(ledata) + "/OpticalSurface/" + name + ".dichroic" : name + ".dichroic";
        std::ifstream test(expectedFile);
        if (test.good()) {
            surface->ReadDichroicFile();
            G4cout << "SurfaceManager: Loaded dichroic file for surface '" << name << "'." << G4endl;
        } else {
            G4cerr << "SurfaceManager: WARNING - Dichroic file '" << expectedFile 
                   << "' not found. Surface '" << name << "' will not have dichroic properties." << G4endl;
        }
    }

    // Material properties table
    G4MaterialPropertiesTable* mpt = new G4MaterialPropertiesTable();
    bool hasTableData = false;

    std::vector<std::string> tableProps = {
        "REFLECTIVITY", "EFFICIENCY", "TRANSMITTANCE", 
        "SPECULARLOBECONSTANT", "SPECULARSPIKECONSTANT", "BACKSCATTERCONSTANT"
    };

    for (const auto& prop : tableProps) {
        YAML::Node node = propsNode[prop];
        if (!node.IsDefined()) continue;

        std::vector<G4double> E, V;
        if (ReadPropertyTable(node, E, V)) {
            mpt->AddProperty(prop, E, V);
            hasTableData = true;
        } else if (node.IsScalar()) {
            mpt->AddConstProperty(prop, ParseDoubleWithUnit(node.as<std::string>()));
            hasTableData = true;
        }
    }

    if (hasTableData) {
        surface->SetMaterialPropertiesTable(mpt);
        G4cout << "SurfaceManager: Optical surface '" << name << "' created with MPT." << G4endl;
    } else {
        delete mpt;
        if (!hasAnyProperty) {
            G4cerr << "SurfaceManager: WARNING - Optical surface '" << name << "' has NO properties!" << G4endl;
        } else {
            G4cout << "SurfaceManager: Optical surface '" << name << "' created with geometry params only." << G4endl;
        }
    }

    return surface;
}

//------------------------------------------------------------------------------
// @brief Builds all optical surfaces from the `surfaces` configuration block.
//
// Reads surface definitions via ConfigManager, creates G4OpticalSurface
// objects, and attaches them as G4LogicalSkinSurface or
// G4LogicalBorderSurface.
//
// @return true on success, false on error.
//------------------------------------------------------------------------------
bool SurfaceManager::BuildSurfaces() {
    G4LogicalBorderSurface::CleanSurfaceTable();
    G4LogicalSkinSurface::CleanSurfaceTable();
    G4cout << "SurfaceManager: Cleaned existing surface tables." << G4endl;

    auto* cfg = ConfigManager::Instance();
    std::vector<std::string> surfIds = cfg->GetSubsections("surfaces");
    
    if (surfIds.empty()) {
        G4cout << "SurfaceManager: No 'surfaces' block found." << G4endl;
        return true;
    }

    G4cout << "SurfaceManager: Building " << surfIds.size() << " optical surfaces..." << G4endl;

    std::unordered_map<std::string, G4LogicalVolume*> logVolCache;
    std::unordered_map<std::string, G4VPhysicalVolume*> physVolCache;

    auto* logStore = G4LogicalVolumeStore::GetInstance();
    auto* physStore = G4PhysicalVolumeStore::GetInstance();

    if (logStore) {
        for (auto* lv : *logStore) {
            if (lv) logVolCache[lv->GetName()] = lv;
        }
    }
    if (physStore) {
        for (auto* pv : *physStore) {
            if (pv) {
                std::string name = pv->GetName();
                if (physVolCache.find(name) != physVolCache.end()) {
                    G4cout << "SurfaceManager: Warning - Duplicate physical volume name '" << name << "' found." << G4endl;
                }
                physVolCache[name] = pv;
            }
        }
    }

    int createdCount = 0;

    for (const auto& id : surfIds) {
        std::string baseKey = "surfaces." + id + ".";
        std::string name = cfg->GetString(baseKey + "name", "");
        std::string type = cfg->GetString(baseKey + "type", "border");
        
        if (name.empty()) name = "Surf_" + id;

        YAML::Node propsNode = cfg->GetNode(baseKey + "properties");
        if (!propsNode.IsDefined()) {
            G4cerr << "SurfaceManager: ERROR - No properties for surface '" << name << "'. Skipping." << G4endl;
            continue;
        }

        G4OpticalSurface* optSurface = CreateOpticalSurface(name, propsNode);
        if (!optSurface) continue;

        if (type == "skin") {
            std::string volName = cfg->GetString(baseKey + "volume", "");
            G4LogicalVolume* logVol = nullptr;
            
            auto itLog = logVolCache.find(volName);
            if (itLog != logVolCache.end()) {
                logVol = itLog->second;
            }

            if (logVol) {
                new G4LogicalSkinSurface(name + "_skin", logVol, optSurface);
                G4cout << "SurfaceManager: [OK] Created SkinSurface '" << name << "' on LogicalVolume '" << volName << "'." << G4endl;
                createdCount++;
            } else {
                G4cerr << "SurfaceManager: [FAIL] LogicalVolume '" << volName << "' not found for SkinSurface '" << name << "'" << G4endl;
                delete optSurface;
            }

        } else {
            std::string vol1Name = cfg->GetString(baseKey + "phys_volume1", "");
            std::string vol2Name = cfg->GetString(baseKey + "phys_volume2", "");
            
            if (vol1Name.empty() || vol2Name.empty()) {
                 G4cerr << "SurfaceManager: [FAIL] BorderSurface '" << name << "' requires 'phys_volume1' and 'phys_volume2'." << G4endl;
                 delete optSurface;
                 continue;
            }

            G4VPhysicalVolume* physVol1 = nullptr;
            G4VPhysicalVolume* physVol2 = nullptr;

            auto it1 = physVolCache.find(vol1Name);
            if (it1 != physVolCache.end()) physVol1 = it1->second;

            auto it2 = physVolCache.find(vol2Name);
            if (it2 != physVolCache.end()) physVol2 = it2->second;

            if (physVol1 && physVol2) {
                new G4LogicalBorderSurface(name + "_border", physVol1, physVol2, optSurface);
                G4cout << "SurfaceManager: [OK] Created BorderSurface '" << name << "' between '" 
                       << vol1Name << "' and '" << vol2Name << "'" << G4endl;
                createdCount++;
            } else {
                G4cerr << "SurfaceManager: [FAIL] Physical volumes not found for BorderSurface '" << name << "': ";
                if (!physVol1) G4cerr << "Missing '" << vol1Name << "' ";
                if (!physVol2) G4cerr << "Missing '" << vol2Name << "'";
                G4cerr << G4endl;
                delete optSurface;
            }
        }
    }

    G4cout << "SurfaceManager: Successfully created " << createdCount << " out of " << surfIds.size() << " surfaces." << G4endl;
    return true;
}