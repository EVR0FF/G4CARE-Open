//==============================================================================
//
// G4CARE
//
// @file    StageManager.cc
// @brief   Multi-stage simulation manager.
//
// @details
//   Manages sequential execution of simulation stages with per-stage
//   geometry, physics/chemistry macros, and event counts.
//
//   Configuration keys read:
//     STAGES.<name>.EVENTS
//     STAGES.<name>.GEOMETRY_FILE
//     STAGES.<name>.PHYSICS_MACRO
//     STAGES.<name>.CHEMISTRY_MACRO
//     STAGES.<name>.SAVE_CHEM_STATE
//     STAGES.<name>.LOAD_CHEM_STATE
//     ANALYSIS.TRACKED_PROPERTIES
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

#include "StageManager.hh"
#include "G4UImanager.hh"
#include "G4DNAChemistryManager.hh"
#include "G4GeometryManager.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4SolidStore.hh"
#include "G4AnalysisManager.hh"
#include "BeamAnalysis.hh"
#include "RunAction.hh"
#include "GeometryManager.hh"
#include "G4Element.hh"
#include "G4Isotope.hh"
#include "G4ios.hh"
#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4OpticalSurface.hh"
#include "G4VSensitiveDetector.hh"
#include "G4SDManager.hh"
#include "DetectorRegistry.hh"
#include "SurfaceRegistry.hh"
#include "SourceManager.hh" 
#include <sstream>

/// @brief Constructor.
/// @param runMgr  Pointer to the Geant4 run manager.
StageManager::StageManager(G4RunManager* runMgr) : fRunManager(runMgr) {}

/// @brief Adds a simulation stage.
/// @param geom                Geometry (nullptr = keep current).
/// @param nEvents             Number of events.
/// @param physicsMacro        Physics macro path.
/// @param chemistryMacro      Chemistry macro path.
/// @param saveChemistryState  Save chem state after stage.
/// @param loadChemistryState  Load chem state before stage.
void StageManager::AddStage(G4VUserDetectorConstruction* geom, int nEvents,
                            const std::string& physicsMacro,
                            const std::string& chemistryMacro,
                            bool saveChemistryState,
                            bool loadChemistryState) {
    fStages.push_back({geom, nEvents, physicsMacro, chemistryMacro,
                       saveChemistryState, loadChemistryState});
}

/// @brief Loads stage config from YAML. Reads STAGES.* and ANALYSIS.TRACKED_PROPERTIES.
/// @param configFileName  Path to YAML config file.
void StageManager::LoadConfiguration(const std::string& configFileName) {
    auto* cfg = ConfigManager::Instance();
    auto stageKeys = cfg->GetSubsections("STAGES");
    for (const auto& key : stageKeys) {
        Stage sc;
        std::string base = "STAGES." + key + ".";
        sc.events         = static_cast<int>(cfg->GetDouble(base + "EVENTS", 100));
        std::string geoFile = cfg->GetString(base + "GEOMETRY_FILE", "");
        sc.physicsMacro   = cfg->GetString(base + "PHYSICS_MACRO", "");
        sc.chemistryMacro = cfg->GetString(base + "CHEMISTRY_MACRO", "");
        sc.saveChemState  = cfg->GetBool(base + "SAVE_CHEM_STATE", false);
        sc.loadChemState  = cfg->GetBool(base + "LOAD_CHEM_STATE", false);

        if (!geoFile.empty()) {
            auto* newGeom = new GeometryManager();
            newGeom->SetGeometryFile(geoFile);
            sc.geometry = newGeom;
        } else {
            sc.geometry = nullptr;
        }

        AddStage(sc.geometry, sc.events, sc.physicsMacro, sc.chemistryMacro,
                 sc.saveChemState, sc.loadChemState);
    }

    std::vector<std::string> tracked = cfg->GetStringVector("ANALYSIS.TRACKED_PROPERTIES");
    fPropertyTracker.Initialize(tracked);
}

/// @brief Sets up property history tracking NTuple columns.
/// @param histNtupleId  History NTuple ID.
/// @param colRunId      Column index for run ID.
/// @param colPropName   Column index for property name.
/// @param colValue      Column index for property value.
void StageManager::InitializePropertyTracker(int histNtupleId, int colRunId,
                                              int colPropName, int colValue) {
    fPropertyTracker.SetHistoryNtupleId(histNtupleId, colRunId, colPropName, colValue);
}

/// @brief Runs all stages sequentially. Falls back to root-level config if no STAGES defined.
void StageManager::RunAll() {
    G4UImanager* ui = G4UImanager::GetUIpointer();
    auto* cfg = ConfigManager::Instance();

    // If no STAGES section is defined, create a single stage using
    // root-level parameters (EVENTS, PHYSICS_MACRO, etc.).
    if (fStages.empty()) {
        int nEvents = static_cast<int>(cfg->GetDouble("EVENTS", 1000));
        std::string physMacro = cfg->GetString("PHYSICS_MACRO", "");
        std::string chemMacro = cfg->GetString("CHEMISTRY_MACRO", "");
        bool saveChem = cfg->GetBool("SAVE_CHEM_STATE", false);
        bool loadChem = cfg->GetBool("LOAD_CHEM_STATE", false);
        AddStage(nullptr, nEvents, physMacro, chemMacro, saveChem, loadChem);
    }

    for (size_t i = 0; i < fStages.size(); ++i) {
        auto& stage = fStages[i];
        G4cout << "=== Stage " << i << " (" << stage.events << " events) ===" << G4endl;

        if (!stage.physicsMacro.empty())
            ui->ApplyCommand("/control/execute " + stage.physicsMacro);
        if (!stage.chemistryMacro.empty())
            ui->ApplyCommand("/control/execute " + stage.chemistryMacro);

        if (stage.geometry) {
            G4GeometryManager::GetInstance()->OpenGeometry();

            fRunManager->SetUserInitialization(stage.geometry);
            fRunManager->ReinitializeGeometry(true);  // cleans up old geometry
            fRunManager->Initialize();
            fRunManager->GeometryHasBeenModified();
        }

        // Pass chemistry state flags before beam start.
        if (fRunAction) {
            fRunAction->SetChemFlags(stage.saveChemState, stage.loadChemState);
        }

        fRunManager->BeamOn(stage.events);

        if (stage.saveChemState) {
            G4DNAChemistryManager::Instance()->WriteInto(
                "chem_stage_" + std::to_string(i) + ".dat");
            G4cout << "StageManager: Chemistry state saved." << G4endl;
        }

        std::map<std::string, std::string> currentValues;
        fPropertyTracker.Update(static_cast<int>(i), currentValues);
        for (const auto& key : fPropertyTracker.GetTrackedProperties()) {
            currentValues[key] = GetCurrentPropertyValue(key);
        }
        fPropertyTracker.Update(static_cast<int>(i), currentValues);
    }
}

/// @brief Returns current value of a physical, surface, or detector property.
/// @param propertyName  "objectName:propertyName" format.
/// @return              Property value string, or empty if not found.
std::string StageManager::GetCurrentPropertyValue(const std::string& propertyName) const {
    size_t colon = propertyName.find(':');
    if (colon == std::string::npos) return "";

    std::string objName = propertyName.substr(0, colon);
    std::string propName = propertyName.substr(colon + 1);

    // -------- 1. Physical volume and material properties --------
    auto* pvStore = G4PhysicalVolumeStore::GetInstance();
    G4VPhysicalVolume* pv = nullptr;
    for (auto* vol : *pvStore) {
        if (vol->GetName() == objName) { pv = vol; break; }
    }

    if (pv) {
        G4LogicalVolume* lv = pv->GetLogicalVolume();
        if (lv) {
            const G4Material* mat = lv->GetMaterial();
            std::ostringstream oss;

            if (propName == "position") {
                G4ThreeVector pos = pv->GetTranslation();
                oss << pos.x()/CLHEP::mm << " " << pos.y()/CLHEP::mm << " " << pos.z()/CLHEP::mm << " mm";
                return oss.str();
            }

            if (mat) {
                if (propName == "material_name") return mat->GetName();
                if (propName == "density") {
                    oss << mat->GetDensity()/(CLHEP::g/CLHEP::cm3) << " g/cm3";
                    return oss.str();
                }
                if (propName == "temperature") {
                    oss << mat->GetTemperature()/CLHEP::kelvin << " K";
                    return oss.str();
                }
                if (propName == "pressure") {
                    oss << mat->GetPressure()/CLHEP::atmosphere << " atm";
                    return oss.str();
                }
                if (propName == "state") {
                    switch (mat->GetState()) {
                        case kStateSolid: return "solid";
                        case kStateLiquid: return "liquid";
                        case kStateGas: return "gas";
                        default: return "undefined";
                    }
                }
                if (propName == "radiation_length") {
                    oss << mat->GetRadlen()/CLHEP::cm << " cm";
                    return oss.str();
                }
                if (propName == "nuclear_interaction_length") {
                    oss << mat->GetNuclearInterLength()/CLHEP::cm << " cm";
                    return oss.str();
                }
                if (propName == "Z_eff") {
                    const G4ElementVector* elements = mat->GetElementVector();
                    const G4double* fractions = mat->GetFractionVector();
                    double zeff = 0.0;
                    for (size_t i = 0; i < mat->GetNumberOfElements(); ++i)
                        zeff += fractions[i] * (*elements)[i]->GetZ();
                    oss << zeff;
                    return oss.str();
                }
                if (propName == "A_eff") {
                    const G4ElementVector* elements = mat->GetElementVector();
                    const G4double* fractions = mat->GetFractionVector();
                    double aeff = 0.0;
                    for (size_t i = 0; i < mat->GetNumberOfElements(); ++i)
                        aeff += fractions[i] * (*elements)[i]->GetA()/(CLHEP::g/CLHEP::mole);
                    oss << aeff << " g/mole";
                    return oss.str();
                }
                if (propName == "chemical_formula") return mat->GetChemicalFormula();
                if (propName == "isotope_composition") {
                    const G4ElementVector* elements = mat->GetElementVector();
                    const G4double* fractions = mat->GetFractionVector();
                    for (size_t i = 0; i < mat->GetNumberOfElements(); ++i) {
                        const G4Element* el = (*elements)[i];
                        G4IsotopeVector* isotopes = el->GetIsotopeVector();
                        G4double* abundances = el->GetRelativeAbundanceVector();
                        if (isotopes && isotopes->size() > 0) {
                            for (size_t j = 0; j < isotopes->size(); ++j) {
                                if (i > 0 || j > 0) oss << ",";
                                oss << (*isotopes)[j]->GetName() << ":" << fractions[i] * abundances[j];
                            }
                        } else {
                            if (i > 0) oss << ",";
                            oss << el->GetName() << ":" << fractions[i];
                        }
                    }
                    return oss.str();
                }

                // Optical properties
                G4MaterialPropertiesTable* mpt = mat->GetMaterialPropertiesTable();
                if (mpt) {
                    if (propName == "rindex") { oss << mpt->GetConstProperty("RINDEX"); return oss.str(); }
                    if (propName == "absorption_length") { oss << mpt->GetConstProperty("ABSLENGTH")/CLHEP::mm << " mm"; return oss.str(); }
                    if (propName == "rayleigh_length") { oss << mpt->GetConstProperty("RAYLEIGH")/CLHEP::mm << " mm"; return oss.str(); }
                    if (propName == "scintillation_yield") { oss << mpt->GetConstProperty("SCINTILLATIONYIELD") << " photons/MeV"; return oss.str(); }
                    if (propName == "fast_time_constant") { oss << mpt->GetConstProperty("FASTTIMECONSTANT")/CLHEP::ns << " ns"; return oss.str(); }
                    if (propName == "slow_time_constant") { oss << mpt->GetConstProperty("SLOWTIMECONSTANT")/CLHEP::ns << " ns"; return oss.str(); }
                    if (propName == "yield_ratio") { oss << mpt->GetConstProperty("YIELDRATIO"); return oss.str(); }
                    if (propName == "resolution_scale") { oss << mpt->GetConstProperty("RESOLUTIONSCALE"); return oss.str(); }
                    if (propName == "wls_component") { oss << mpt->GetConstProperty("WLSCOMPONENT"); return oss.str(); }
                    if (propName == "wls_time_constant") { oss << mpt->GetConstProperty("WLSTIMECONSTANT")/CLHEP::ns << " ns"; return oss.str(); }
                }
            }
        }
    }

    // -------- 2. Surface properties --------
    auto* surfaceReg = BeamAnalysis::Instance()->GetSurfaceRegistry();
    if (surfaceReg) {
        int surfID = surfaceReg->GetSurfaceID(objName);
        if (surfID >= 0) {
            const SurfaceProperties& surf = surfaceReg->GetProperties(surfID);
            std::ostringstream oss;
            if (propName == "reflectivity") { oss << surf.reflectivity; return oss.str(); }
            if (propName == "efficiency")    { oss << surf.efficiency; return oss.str(); }
            if (propName == "sigma_alpha")   { oss << surf.sigma_alpha; return oss.str(); }
            if (propName == "surface_model") {
                switch (surf.model) {
                    case unified:            return "unified";
                    case glisur:             return "glisur";
                    case LUT:                return "LUT";
                    case DAVIS:              return "DAVIS";
                    case dichroic:           return "dichroic";
                    default:                 return "unknown";
                }
            }
            if (propName == "surface_finish") {
                switch (surf.finish) {
                    case polished:            return "polished";
                    case polishedfrontpainted: return "polishedfrontpainted";
                    case polishedbackpainted:  return "polishedbackpainted";
                    case ground:              return "ground";
                    case groundfrontpainted:   return "groundfrontpainted";
                    case groundbackpainted:    return "groundbackpainted";
                    default:                  return "unknown";
                }
            }
            if (propName == "surface_type") {
                switch (surf.type) {
                    case dielectric_metal:      return "dielectric_metal";
                    case dielectric_dielectric: return "dielectric_dielectric";
                    default:                    return "unknown";
                }
            }
        }
    }

    // -------- 3. Detector properties --------
    auto* detReg = BeamAnalysis::Instance()->GetDetectorRegistry();
    if (detReg) {
        int detID = detReg->GetDetectorID(objName);
        if (detID >= 0) {
            const DetectorProperties& det = detReg->GetProperties(detID);
            std::ostringstream oss;
            if (propName == "dead_time") { oss << det.deadTime_ns << " ns"; return oss.str(); }
            if (propName == "time_resolution") { oss << det.timeResFWHM_ns << " ns"; return oss.str(); }
            if (propName == "energy_resolution") { oss << det.energyResFWHM; return oss.str(); }
            if (propName == "quantum_efficiency") { oss << det.quantumEfficiency; return oss.str(); }
            if (propName == "gain") { oss << det.gain; return oss.str(); }
            if (propName == "pileup_model") { return det.pileupModel; }
            if (propName == "afterpulse_probability") { oss << det.afterpulseProbability; return oss.str(); }
            if (propName == "threshold") { oss << det.threshold_MeV << " MeV"; return oss.str(); }
            if (propName == "noise_level") { oss << det.noiseLevel_MeV << " MeV"; return oss.str(); }
            if (propName == "signal_rise_time") { oss << det.signalRiseTime_ns << " ns"; return oss.str(); }
            if (propName == "signal_fall_time") { oss << det.signalFallTime_ns << " ns"; return oss.str(); }
            if (propName == "signal_model") { return det.signalModel; }
            if (propName == "time_smear_model") { return det.timeSmearModel; }
            if (propName == "energy_smear_model") { return det.energySmearModel; }
        }
    }
    return "";
}