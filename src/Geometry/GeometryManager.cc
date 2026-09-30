//==============================================================================
//
// G4CARE
//
// @file    GeometryManager.cc
// @brief   Implementation of GeometryManager — world, target, SDs.
//
// @details
//   Construct() builds the world volume (GDML scene, programmatic WORLD,
//   or defaults) and delegates OBJECT placement to ObjectManager.
//   ConstructSDandField() registers SensitiveDetectors for target and
//   user objects, creates DigitizerModule, and initializes
//   AdaptiveScoringManager.
//
//   Configuration keys read:
//     GEOMETRY.FILE — GDML full scene (WORLD ignored if set)
//     GEOMETRY.WORLD.file — GDML world only
//     GEOMETRY.WORLD.shape/material/DIMENSIONS — programmatic world
//     GEOMETRY.OBJECTS — objects supplementing the scene
//     SENSITIVE_VOLUMES
//     TARGET_SIZE, TARGET_MATERIAL, TARGET_POS_*, TARGET_IS_SENSITIVE
//     PHYSICS.ADVANCED.WEIGHT_WINDOW.ENABLE
//     SCORING.TARGET_VOLUME_NAME
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-08-05
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "GeometryManager.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "SensitiveDetector.hh"
#include "ObjectManager.hh"
#include "DICOMReader.hh"
#include "DICOMResampler.hh"
#include "HUToMaterialMap.hh"
#include "Rasterizer.hh"
#include "VoxelizedPhantom.hh"
#include "G4GDMLParser.hh"
#include "G4NistManager.hh"
#include "G4Box.hh"
#include "G4Sphere.hh"
#include "G4Tubs.hh"
#include "G4Cons.hh"
#include "G4Torus.hh"
#include "G4Para.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4VSolid.hh"
#include "G4Material.hh"
#include "G4SDManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4UnitsTable.hh"
#include "G4ios.hh"
#include "ConfigManager.hh"
#include "G4GeometrySampler.hh"
#include "G4ImportanceBiasing.hh"
#include "AdaptiveScoringManager.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4RunManager.hh"
#include "G4Threading.hh"
#include "FieldManager.hh"
#include "BeamAnalysis.hh"
#include "DigitizerModule.hh"
#include "G4DigiManager.hh"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>

/// @brief Trim leading and trailing whitespace from a string.
/// @param s Input string.
/// @return Trimmed copy.
std::string GeometryManager::Trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

/// @brief Find a logical volume by name (exact LV, PV→LV, partial match).
/// @param name Volume name to search for.
/// @return Pointer to G4LogicalVolume or nullptr.
G4LogicalVolume* GeometryManager::FindLogicalVolume(const G4String& name) {
    // 1. Exact match by LV name
    G4LogicalVolumeStore* lvStore = G4LogicalVolumeStore::GetInstance();
    if (lvStore) {
        for (auto lv : *lvStore) {
            if (lv && lv->GetName() == name) return lv;
        }
    }

    // 2. Search via physical volumes: find PV with given name,
    //    then return its LV. Needed when user specifies the object
    //    name (PV) rather than LV name (= obj.name + "_LV").
    G4PhysicalVolumeStore* pvStore = G4PhysicalVolumeStore::GetInstance();
    if (pvStore) {
        for (auto pv : *pvStore) {
            if (pv && pv->GetName() == name) {
                G4LogicalVolume* lv = pv->GetLogicalVolume();
                if (lv) {
                    G4cout << "[GeometryManager] Found logical volume '"
                           << lv->GetName() << "' via physical volume '"
                           << name << "'" << G4endl;
                    return lv;
                }
            }
        }
    }

    // 3. Partial match: find LV whose name contains 'name'
    if (lvStore) {
        for (auto lv : *lvStore) {
            if (lv && G4StrUtil::contains(lv->GetName(), name)) {
                G4cout << "[GeometryManager] Found logical volume '"
                       << lv->GetName() << "' via partial match with '"
                       << name << "'" << G4endl;
                return lv;
            }
        }
    }

    G4cout << "[GeometryManager] Logical volume '" << name
           << "' not found (tried: exact LV, PV→LV, partial LV)." << G4endl;
    return nullptr;
}

//==============================================================================
// Helper: create a G4VSolid from shape name and raw dimension strings
//==============================================================================
namespace {
    /// @brief Normalize a dimension string: replace '*' with space
    ///        so G4UIcmdWithADoubleAndUnit can parse "1*m" correctly.
    static std::string NormDim(const std::string& raw) {
        std::string s = raw;
        std::replace(s.begin(), s.end(), '*', ' ');
        return s;
    }

    G4VSolid* CreateSolid(const std::string& name,
                          const std::string& shape,
                          const std::vector<std::string>& dims)
    {
        if (shape == "box") {
            double sx = 1.0*m, sy = 1.0*m, sz = 1.0*m;
            if (dims.size() >= 3) {
                sx = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                sy = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
                sz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[2]).c_str());
            } else if (dims.size() == 1) {
                sx = sy = sz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(dims[0].c_str());
            }
            return new G4Box(name, sx/2.0, sy/2.0, sz/2.0);
        }
        else if (shape == "sphere") {
            double rmin = 0.0, rmax = 1.0*m;
            if (dims.size() >= 2) {
                rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
            } else if (dims.size() == 1) {
                rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
            }
            return new G4Sphere(name, rmin, rmax, 0., 360.*deg, 0., 180.*deg);
        }
        else if (shape == "cylinder" || shape == "tube" || shape == "tubs") {
            double rmin = 0.0, rmax = 1.0*m, h = 1.0*m;
            if (dims.size() >= 3) {
                rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
                h    = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[2]).c_str());
            } else if (dims.size() >= 2) {
                rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                h    = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
            }
            return new G4Tubs(name, rmin, rmax, h/2.0, 0., 360.*deg);
        }
        else if (shape == "cone") {
            double rmin1 = 0.0, rmax1 = 1.0*m, rmin2 = 0.0, rmax2 = 1.0*m, h = 1.0*m;
            if (dims.size() >= 5) {
                rmin1 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                rmax1 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
                rmin2 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[2]).c_str());
                rmax2 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[3]).c_str());
                h     = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[4]).c_str());
            }
            return new G4Cons(name, rmin1, rmax1, rmin2, rmax2, h/2.0, 0., 360.*deg);
        }
        else if (shape == "torus") {
            double rmin = 0.0, rmax = 1.0*m, rtor = 1.0*m;
            if (dims.size() >= 3) {
                rmin = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                rmax = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
                rtor = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[2]).c_str());
            }
            return new G4Torus(name, rmin, rmax, rtor, 0., 360.*deg);
        }
        else if (shape == "para") {
            double dx = 1.0*m, dy = 1.0*m, dz = 1.0*m, alpha = 0, theta = 0, phi = 0;
            if (dims.size() >= 6) {
                dx = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[0]).c_str());
                dy = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[1]).c_str());
                dz = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[2]).c_str());
                alpha = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[3]).c_str());
                theta = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[4]).c_str());
                phi   = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(NormDim(dims[5]).c_str());
            }
            return new G4Para(name, dx/2.0, dy/2.0, dz/2.0, alpha, theta, phi);
        }
        // Default fallback: box
        G4cout << "[GeometryManager] Unknown shape '" << shape
               << "', falling back to box 1×1×1 m³." << G4endl;
        return new G4Box(name, 0.5*m, 0.5*m, 0.5*m);
    }
} // anonymous namespace

GeometryManager::GeometryManager()
    : fParser(),
      fObjectManager(std::make_unique<ObjectManager>()),
      fDetectorRegistry(std::make_unique<DetectorRegistry>()),
      fTargetPV(nullptr),
      fWorldPV(nullptr)
{
    fObjectManager->CreateObjects();
}

GeometryManager::~GeometryManager() = default;

/// @return The DICOM voxel phantom container LV (nullptr if not built).
G4LogicalVolume* GeometryManager::GetDicomPhantomLV() const {
    return fDicomPhantom ? fDicomPhantom->GetContainerLV() : nullptr;
}

/// @brief Build the world volume, target box and place OBJECT objects.
///
/// Priority:
///   1. GEOMETRY.FILE        → GDML scene (WORLD ignored, OBJECTS supplement)
///   2. GEOMETRY.WORLD.file  → GDML world only
///   3. GEOMETRY.WORLD       → programmatic world (shape/material/DIMENSIONS)
///   4. Default              → box, G4_AIR, 1×1×1 m³
///
/// OBJECTS are always placed into the resulting world.
///
/// @return World physical volume pointer.
G4VPhysicalVolume* GeometryManager::Construct() {

    // ---- Weight Window parallel world ----
    bool wwEnabled = ConfigManager::Instance()->GetBool("PHYSICS.ADVANCED.WEIGHT_WINDOW.ENABLE", false);
    if (wwEnabled) {
        auto* wwWorld = new WeightWindowWorld("WeightWindowWorld");
        WeightWindowManager::Instance()->ConfigureParallelWorld(wwWorld);
        RegisterParallelWorld(wwWorld);
    }

    G4NistManager* nist = G4NistManager::Instance();
    G4SDManager* sdManager = G4SDManager::GetSDMpointer();
    auto* cfg = ConfigManager::Instance();

    G4LogicalVolume* logicWorld = nullptr;
    G4VPhysicalVolume* physWorld = nullptr;

    //==================================================================
    // 1. GEOMETRY.FILE — full GDML scene
    //==================================================================
    std::string sceneFile = cfg->GetString("GEOMETRY.FILE", "");
    if (!sceneFile.empty()) {
        G4cout << "[GeometryManager] Loading GDML scene: " << sceneFile << G4endl;
        fParser.Read(sceneFile);
        physWorld = fParser.GetWorldVolume();
        if (!physWorld) {
            G4cerr << "[GeometryManager] ERROR: GDML parser returned null world for: "
                   << sceneFile << G4endl;
            return nullptr;
        }
        logicWorld = physWorld->GetLogicalVolume();
        if (!logicWorld) {
            G4cerr << "[GeometryManager] ERROR: world logical volume is null after GDML read."
                   << G4endl;
            return nullptr;
        }
        fWorldPV = physWorld;
        G4cout << "[GeometryManager] GDML scene loaded. World: '"
               << logicWorld->GetName() << "'" << G4endl;
        G4cout << "[GeometryManager] GEOMETRY.WORLD is ignored (GEOMETRY.FILE takes precedence)."
               << G4endl;

        // Process SENSITIVE_VOLUMES
        std::string sensitiveVolumes = cfg->GetString("SENSITIVE_VOLUMES", "");
        if (!sensitiveVolumes.empty()) {
            auto sensitiveVolumesList = cfg->Split(sensitiveVolumes, ',');
            bool useAll = false;
            for (auto& volNameRaw : sensitiveVolumesList) {
                std::string volName = Trim(volNameRaw);
                if (volName == "*") { useAll = true; break; }
            }
            if (useAll) {
                G4LogicalVolumeStore* lvStore = G4LogicalVolumeStore::GetInstance();
                G4int nSD = 0;
                for (auto* lv : *lvStore) {
                    if (!lv) continue;
                    G4String lvn = lv->GetName();
                    if (lvn.find("World") != std::string::npos || lvn.find("Hall") != std::string::npos) continue;
                    G4String sdName = "All_SD";
                    if (!sdManager->FindSensitiveDetector(sdName, false)) {
                        auto sd = new SensitiveDetector(sdName);
                        sdManager->AddNewDetector(sd);
                    }
                    lv->SetSensitiveDetector(sdManager->FindSensitiveDetector(sdName));
                    ++nSD;
                }
                G4cout << "[GeometryManager] WILDCARD: set SensitiveDetector on " << nSD << " LV(s)" << G4endl;
            } else {
                for (auto& volNameRaw : sensitiveVolumesList) {
                    std::string volName = Trim(volNameRaw);
                    if (volName.empty()) continue;
                    G4LogicalVolume* lv = FindLogicalVolume(volName);
                    if (lv) {
                        G4String sdName = volName + "_SD";
                        if (!sdManager->FindSensitiveDetector(sdName, false)) {
                            auto sd = new SensitiveDetector(sdName);
                            sdManager->AddNewDetector(sd);
                            lv->SetSensitiveDetector(sd);
                        } else {
                            lv->SetSensitiveDetector(sdManager->FindSensitiveDetector(sdName));
                        }
                    } else {
                        G4cout << "[GeometryManager] WARNING: volume '" << volName
                               << "' not found, cannot set SD." << G4endl;
                    }
                }
            }
        }

        // Place supplementary OBJECTS
        fObjectManager->PlaceObjects(logicWorld);
        FieldManager::Instance()->ApplyFields();
        return physWorld;
    }

    //==================================================================
    // 2. GEOMETRY.WORLD — programmatic or GDML world
    //==================================================================
    std::string worldFile = cfg->GetString("GEOMETRY.WORLD.file", "");

    if (!worldFile.empty()) {
        G4cout << "[GeometryManager] Loading world from GDML: " << worldFile << G4endl;
        G4GDMLParser worldParser;
        worldParser.Read(worldFile);
        G4VPhysicalVolume* wPV = worldParser.GetWorldVolume();
        if (!wPV) {
            G4cerr << "[GeometryManager] ERROR: GDML world parser returned null for: "
                   << worldFile << G4endl;
            return nullptr;
        }
        logicWorld = wPV->GetLogicalVolume();
        if (!logicWorld) {
            G4cerr << "[GeometryManager] ERROR: world logical volume is null after GDML world read."
                   << G4endl;
            return nullptr;
        }
        physWorld = wPV;
        fWorldPV = physWorld;
        G4cout << "[GeometryManager] GDML world loaded: '" << logicWorld->GetName() << "'" << G4endl;
    }
    else {
        std::string worldShape = cfg->GetString("GEOMETRY.WORLD.shape", "box");
        std::string worldMatName = cfg->GetString("GEOMETRY.WORLD.material", "G4_AIR");

        std::vector<std::string> worldDims;
        auto dimsVec = cfg->GetStringVector("GEOMETRY.WORLD.DIMENSIONS");
        if (!dimsVec.empty()) {
            worldDims = dimsVec;
        } else {
            worldDims = {"1*m", "1*m", "1*m"};
        }

        G4Material* worldMat = nist->FindOrBuildMaterial(worldMatName);
        if (!worldMat) {
            G4cerr << "[GeometryManager] WARNING: material '" << worldMatName
                   << "' not found. Using G4_AIR." << G4endl;
            worldMat = nist->FindOrBuildMaterial("G4_AIR");
        }

        G4VSolid* solidWorld = CreateSolid("World", worldShape, worldDims);
        logicWorld = new G4LogicalVolume(solidWorld, worldMat, "World");
        physWorld = new G4PVPlacement(nullptr, G4ThreeVector(), logicWorld,
                                      "World", nullptr, false, 0);
        fWorldPV = physWorld;

        G4cout << "[GeometryManager] Programmatic world: shape=" << worldShape
               << ", material=" << worldMatName << ", dimensions=[";
        for (size_t i = 0; i < worldDims.size(); ++i) {
            if (i > 0) G4cout << ", ";
            G4cout << worldDims[i];
        }
        G4cout << "]" << G4endl;
    }

    // ---- Target (legacy) ----
    double targetSize = cfg->GetValueWithUnits("TARGET_SIZE", 0.0);
    if (targetSize > 0.0) {
        std::string targetMatName = cfg->GetString("TARGET_MATERIAL", "G4_WATER");
        G4Material* targetMat = nist->FindOrBuildMaterial(targetMatName);
        if (!targetMat) {
            targetMat = nist->FindOrBuildMaterial("G4_WATER");
        }

        G4Box* solidTarget = new G4Box("Target", targetSize/2, targetSize/2, targetSize/2);
        G4LogicalVolume* logicTarget = new G4LogicalVolume(solidTarget, targetMat, "Target");

        double targetX = cfg->GetValueWithUnits("TARGET_POS_X", 0.0);
        double targetY = cfg->GetValueWithUnits("TARGET_POS_Y", 0.0);
        double targetZ = cfg->GetValueWithUnits("TARGET_POS_Z", 0.0);
        G4ThreeVector targetPos(targetX, targetY, targetZ);

        fTargetPV = new G4PVPlacement(nullptr, targetPos, logicTarget, "Target",
                                      logicWorld, false, 0);

        G4cout << "[GeometryManager] Target: size=" << G4BestUnit(targetSize,"Length")
               << " pos=(" << G4BestUnit(targetPos.x(),"Length") << ","
               << G4BestUnit(targetPos.y(),"Length") << ","
               << G4BestUnit(targetPos.z(),"Length") << ")" << G4endl;
    }

    // ---- Place OBJECTS into the world ----
    fObjectManager->PlaceObjects(logicWorld);

    // ---- DICOM voxel phantom (CT -> HU -> material) ----
    if (cfg->GetBool("GEOMETRY.DICOM.ENABLE", false)) {
        std::string ctDir = cfg->GetString("GEOMETRY.DICOM.CT_DIR", "");
        if (ctDir.empty()) {
            G4cerr << "[GeometryManager] GEOMETRY.DICOM.ENABLE set but CT_DIR is empty." << G4endl;
        } else {
#ifdef G4CARE_HAS_DICOM
            DICOMReader dicom;
            CTSeries ct;
            if (dicom.ReadCTSeries(ctDir, ct)) {
                if (ct.tilted) {
                    if (cfg->GetBool("GEOMETRY.DICOM.STRICT_TILT", false)) {
                        G4cerr << "[GeometryManager] ERROR: gantry tilt " << ct.tiltAngleDeg
                               << " deg (STRICT_TILT=true); skipping phantom." << G4endl;
                        ct.valid = false;
                    } else if (cfg->GetBool("GEOMETRY.DICOM.RESAMPLE_TILT", true)) {
                        CTSeries resampled;
                        if (DICOMResampler::ResampleToAxisAligned(ct, resampled)) {
                            G4cout << "[GeometryManager] Resampled tilted CT to axis-aligned grid: "
                                   << resampled.nx << "x" << resampled.ny << "x" << resampled.nz
                                   << G4endl;
                            ct = std::move(resampled);
                        } else {
                            G4cerr << "[GeometryManager] Resampling failed; building raw grid."
                                   << G4endl;
                        }
                    } else {
                        G4cerr << "[GeometryManager] WARNING: gantry tilt " << ct.tiltAngleDeg
                               << " deg; RESAMPLE_TILT=false (raw axis-aligned grid, inaccurate)."
                               << G4endl;
                    }
                }
                if (ct.valid) {
                    const std::string axesStr = cfg->GetString("GEOMETRY.DICOM.AXES", "LPS");
                    const DicomAxes axes = (axesStr == "RAS") ? DicomAxes::RAS : DicomAxes::LPS;
                    HUToMaterialMap huMap;
                    fDicomPhantom = std::make_unique<VoxelizedPhantom>();
                    if (!fDicomPhantom->Build(ct, huMap, logicWorld, axes)) {
                        G4cerr << "[GeometryManager] Phantom build failed (oblique acquisition?)."
                               << G4endl;
                    }

                    // RTSTRUCT -> organ labels (rasterised onto the CT grid).
                    const std::string rtFile = cfg->GetString("GEOMETRY.DICOM.RTSTRUCT_FILE", "");
                    if (!rtFile.empty() && fDicomPhantom->GetContainerLV()) {
                        RTStruct rt;
                        if (dicom.ReadRTStruct(rtFile, rt)) {
                            std::vector<int> labels;
                            if (DICOMRasterizer::Rasterize(ct, rt, labels)) {
                                std::vector<std::string> names;
                                names.reserve(rt.rois.size());
                                for (const auto& r : rt.rois) names.push_back(r.name);
                                fDicomPhantom->SetOrganNames(names);
                                fDicomPhantom->SetOrganLabelsFromCT(labels);
                                G4cout << "[GeometryManager] " << rt.rois.size()
                                       << " organ ROIs rasterised." << G4endl;
                            }
                        }
                    }

                    // RTPLAN -> treatment beams (logged; auto source wiring).
                    const std::string rpFile = cfg->GetString("GEOMETRY.DICOM.RTPLAN_FILE", "");
                    if (!rpFile.empty()) {
                        RTPlan plan;
                        if (dicom.ReadRTPlan(rpFile, plan)) {
                            for (const auto& b : plan.beams) {
                                if (b.controlPoints.empty()) continue;
                                const auto& cp = b.controlPoints.front();
                                double dir[3];
                                RTPlan::BeamDirection(cp.gantryAngleDeg, cp.couchAngleDeg, dir);
                                G4cout << "[GeometryManager] Beam #" << b.number << " '"
                                       << b.name << "' " << b.radiationType
                                       << " E=" << cp.energyMeV << " MeV gantry="
                                       << cp.gantryAngleDeg << " deg dir=("
                                       << dir[0] << "," << dir[1] << "," << dir[2] << ")"
                                       << G4endl;
                            }
                        }
                    }

                    // RTDOSE -> reference dose grid (logged; comparison).
                    const std::string rdFile = cfg->GetString("GEOMETRY.DICOM.RTDOSE_FILE", "");
                    if (!rdFile.empty()) {
                        RTDose rd;
                        if (dicom.ReadRTDose(rdFile, rd)) {
                            G4cout << "[GeometryManager] RTDOSE grid " << rd.nx << "x"
                                   << rd.ny << "x" << rd.nz << " units=" << rd.doseUnits
                                   << G4endl;
                        }
                    }
                }
            } else {
                G4cerr << "[GeometryManager] Failed to read CT series from: " << ctDir << G4endl;
            }
#else
            G4cerr << "[GeometryManager] DICOM requested but G4CARE built without GDCM." << G4endl;
#endif
        }
    }

    FieldManager::Instance()->ApplyFields();
    return physWorld;
}

/// @brief Register SensitiveDetectors (MT-safe via Geant4 cloning).
///        Creates DigitizerModule after all SDs are registered.
void GeometryManager::ConstructSDandField() {
    G4SDManager* sdManager = G4SDManager::GetSDMpointer();
    auto* detReg = fDetectorRegistry.get();

    bool isMaster = G4Threading::IsMasterThread();

    // TARGET_IS_SENSITIVE
    bool targetIsSensitive = ConfigManager::Instance()->GetBool("TARGET_IS_SENSITIVE", false);
    if (targetIsSensitive && fTargetPV) {
        G4String sdName = "Target_SD";
        G4VSensitiveDetector* sd = sdManager->FindSensitiveDetector(sdName, false);
        if (!sd) {
            if (!isMaster) {
                G4cerr << "[GeometryManager] ConstructSDandField: WARNING - Target_SD not found"
                       << " in worker. Skipping." << G4endl;
            } else {
                sd = new SensitiveDetector(sdName);
                sdManager->AddNewDetector(sd);
            }
        }
        if (sd) {
            int detId = detReg->RegisterDetector(sd, sdName, DetectorProperties());
            static_cast<SensitiveDetector*>(sd)->SetDetectorID(detId);
            fTargetPV->GetLogicalVolume()->SetSensitiveDetector(sd);
        }
    }

    // DICOM phantom voxel sensitive detector (records edep + voxel copy number).
    bool dicomSensitive = ConfigManager::Instance()->GetBool("GEOMETRY.DICOM.IS_SENSITIVE", false);
    if (dicomSensitive && fDicomPhantom && fDicomPhantom->GetVoxelLV()) {
        G4String sdName = "PhantomVoxel_SD";
        G4VSensitiveDetector* sd = sdManager->FindSensitiveDetector(sdName, false);
        if (!sd) {
            if (!isMaster) {
                G4cerr << "[GeometryManager] WARNING: PhantomVoxel_SD not found in worker."
                       << G4endl;
            } else {
                sd = new SensitiveDetector(sdName);
                sdManager->AddNewDetector(sd);
            }
        }
        if (sd) {
            int detId = detReg->RegisterDetector(sd, sdName, DetectorProperties());
            static_cast<SensitiveDetector*>(sd)->SetDetectorID(detId);
            fDicomPhantom->GetVoxelLV()->SetSensitiveDetector(sd);
            G4cout << "[GeometryManager] SD 'PhantomVoxel_SD' attached to phantom voxels (detID="
                   << detId << ")." << G4endl;
        }
    }

    if (fObjectManager) {
        const auto& objects = fObjectManager->GetObjects();
        for (const auto& obj : objects) {
            // Register SD for the object itself
            if (obj.isSensitive && obj.logicalVolume) {
                G4String sdName = obj.name + "_SD";
                G4VSensitiveDetector* sd = sdManager->FindSensitiveDetector(sdName, false);
                if (!sd) {
                    sd = new SensitiveDetector(sdName);
                    sdManager->AddNewDetector(sd);
                }
                if (sd) {
                    int detId = detReg->RegisterDetector(sd, sdName, obj.digitizeProps);
                    static_cast<SensitiveDetector*>(sd)->SetDetectorID(detId);
                    obj.logicalVolume->SetSensitiveDetector(sd);
                    G4cout << "[GeometryManager] SD '" << sdName << "' attached to '"
                           << obj.name << "' (detID=" << detId << ")" << G4endl;
                }
            }
            // Register SDs for children (e.g. TKRDetectorX/Y)
            for (const auto& child : obj.children) {
                if (child.isSensitive && child.logicalVolume) {
                    G4String sdName = child.name + "_SD";
                    G4VSensitiveDetector* sd = sdManager->FindSensitiveDetector(sdName, false);
                    if (!sd) {
                        sd = new SensitiveDetector(sdName);
                        sdManager->AddNewDetector(sd);
                    }
                    if (sd) {
                        int detId = detReg->RegisterDetector(sd, sdName, child.digitizeProps);
                        static_cast<SensitiveDetector*>(sd)->SetDetectorID(detId);
                        child.logicalVolume->SetSensitiveDetector(sd);
                        G4cout << "[GeometryManager] SD '" << sdName << "' attached to child '"
                               << child.name << "' (detID=" << detId << ")" << G4endl;
                    }
                }
            }
        }
    }

    // --- Register DigitizerModule with G4DigiManager (each thread) ---
    {
        static G4ThreadLocal bool sDigitizerRegistered = false;
        if (!sDigitizerRegistered && detReg->GetAll().size() > 0) {
            sDigitizerRegistered = true;
            BeamAnalysis::Instance()->SetDetectorRegistry(detReg);
            auto* eval = BeamAnalysis::Instance()->GetEvaluator();
            G4DigiManager::GetDMpointer()->AddNewModule(new DigitizerModule("DigitizerModule", detReg, eval));
            G4cout << "[GeometryManager] DigitizerModule registered with "
                   << detReg->GetAll().size() << " detectors" << G4endl;
        }
    }

    // ADAPTIVE SCORING INITIALIZATION
    auto* cfg = ConfigManager::Instance();
    if (cfg->HasKey("SCORING.TARGET_VOLUME_NAME")) {
        auto* scoringMgr = AdaptiveScoringManager::Instance();
        scoringMgr->SetGeometryManager(this);
        scoringMgr->LoadConfig("SCORING");
        scoringMgr->ConstructSD();
        G4cout << "[GeometryManager] AdaptiveScoring initialized in ConstructSDandField()"
               << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
    }
}

/// @brief Move the target box to a new position.
/// @param newPos New position for the target volume.
void GeometryManager::MoveTarget(const G4ThreeVector& newPos) {
    if (!fTargetPV) {
        G4cerr << "[GeometryManager] ERROR: Target volume not found, cannot move." << G4endl;
        return;
    }
    
    G4cout << "[GeometryManager] Moving Target from " << fTargetPV->GetTranslation() 
           << " to " << newPos << G4endl;
    
    fTargetPV->SetTranslation(newPos);
    G4RunManager::GetRunManager()->GeometryHasBeenModified();
    
    G4cout << "[GeometryManager] Target moved successfully. Geometry updated." << G4endl;
}

/// @brief Get the current target box position.
/// @return Translation vector of the target PV (or zero if absent).
G4ThreeVector GeometryManager::GetTargetPosition() const {
    if (!fTargetPV) return G4ThreeVector();
    return fTargetPV->GetTranslation();
}

/// @brief Get the world-center position of a volume by name.
/// @param volName Name of the logical or physical volume.
/// @return Center position of the volume (or zero if not found).
G4ThreeVector GeometryManager::GetVolumeCenter(const G4String& volName) const {
    G4LogicalVolumeStore* lvStore = G4LogicalVolumeStore::GetInstance();
    G4LogicalVolume* lv = nullptr;
    if (lvStore) lv = lvStore->GetVolume(volName, false);
    
    G4PhysicalVolumeStore* pvStore = G4PhysicalVolumeStore::GetInstance();
    if (!lv && pvStore) {
        G4VPhysicalVolume* pv = pvStore->GetVolume(volName, false);
        if (pv) return pv->GetTranslation();
    }
    
    if (lv && pvStore) {
        for (auto* pv : *pvStore) {
            if (pv && pv->GetLogicalVolume() == lv) {
                return pv->GetTranslation();
            }
        }
    }
    
    G4cerr << "[GeometryManager] Volume '" << volName << "' not found for center calculation."
           << G4endl;
    return G4ThreeVector(0,0,0);
}