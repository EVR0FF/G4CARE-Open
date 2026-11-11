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
#include "G4PVParameterised.hh"
#include "G4VPVParameterisation.hh"
#include "G4RotationMatrix.hh"
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
#include <cmath>

namespace {
    bool IsNumber(const std::string& s) {
        std::regex number_regex(R"(^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$)");
        return std::regex_match(s, number_regex);
    }

    /// Build a G4RotationMatrix from Euler angles in degrees (X then Y then Z),
    /// matching GEOMETRY.OVERRIDES.*.ROTATION convention. Returns nullptr when
    /// the rotation is identity (no matrix needed).
    G4RotationMatrix* BuildRotation(const G4ThreeVector& rotDeg) {
        if (rotDeg.x() == 0.0 && rotDeg.y() == 0.0 && rotDeg.z() == 0.0) {
            return nullptr;
        }
        G4RotationMatrix* rm = new G4RotationMatrix();
        rm->rotateX(rotDeg.x() * CLHEP::deg);
        rm->rotateY(rotDeg.y() * CLHEP::deg);
        rm->rotateZ(rotDeg.z() * CLHEP::deg);
        return rm;
    }

    // ── Array parameterisation (REPLICA.mode = linear | grid | polar). ──
    // Geant4 requires a parameterised volume to be the ONLY daughter of a
    // non-world mother. So the array is wrapped in a dedicated air "container"
    // box placed in the real mother; copies are computed relative to the
    // container centre. One G4PVParameterised + one parameterisation object
    // replaces N individual G4PVPlacement objects (one LV, one PV, transforms
    // computed on the fly) — far smaller memory footprint and faster
    // voxelisation for large detector arrays.
    struct ArrayBasis { G4ThreeVector ax, u, v; };

    ArrayBasis ArrayBasisFor(char a) {
        if (a == 'x' || a == 'X') return { G4ThreeVector(1,0,0), G4ThreeVector(0,1,0), G4ThreeVector(0,0,1) };
        if (a == 'y' || a == 'Y') return { G4ThreeVector(0,1,0), G4ThreeVector(1,0,0), G4ThreeVector(0,0,1) };
        return { G4ThreeVector(0,0,1), G4ThreeVector(1,0,0), G4ThreeVector(0,1,0) };
    }

    class ArrayParameterisation : public G4VPVParameterisation {
    public:
        ArrayParameterisation(const ReplicaDesc& r, const G4ThreeVector& center,
                              G4RotationMatrix* baseRot)
            : fR(r), fCenter(center), fBaseRot(baseRot),
              fBasis(ArrayBasisFor(r.axis.empty() ? 'z' : r.axis[0])) {}

        ~ArrayParameterisation() override { delete fBaseRot; }

        void ComputeTransformation(const G4int copyNo, G4VPhysicalVolume* pv) const override {
            G4ThreeVector delta;
            G4RotationMatrix* rm = nullptr;

            if (fR.mode == "grid") {
                const int nx = std::max(1, fR.nx), ny = std::max(1, fR.ny), nz = std::max(1, fR.nz);
                const int nxy = nx * ny;
                const int k = copyNo / nxy;
                const int rem = copyNo % nxy;
                const int j = rem / nx;
                const int i = rem % nx;
                delta = (i * fR.dx) * fBasis.u + (j * fR.dy) * fBasis.v + (k * fR.dz) * fBasis.ax;
                if (fBaseRot) rm = new G4RotationMatrix(*fBaseRot);
            } else if (fR.mode == "polar") {
                // Evenly spaced over totalAngle (count copies, no duplicate at 360°).
                const double stepAng = (fR.count > 1) ? fR.totalAngle / fR.count : 0.0;
                const double angRad = (fR.startAngle + copyNo * stepAng) * CLHEP::deg;
                delta = (fR.radius * std::cos(angRad)) * fBasis.u
                      + (fR.radius * std::sin(angRad)) * fBasis.v;
                if (fR.rotate) {
                    CLHEP::HepRotation rp;
                    rp.rotate(angRad, fBasis.ax);                     // rp = R(axis, ang)
                    const CLHEP::HepRotation base = fBaseRot ? *fBaseRot : CLHEP::HepRotation();
                    rm = new G4RotationMatrix(rp * base);            // R(axis, ang) * R0
                } else if (fBaseRot) {
                    rm = new G4RotationMatrix(*fBaseRot);
                }
            } else { // linear
                delta = (fR.offset + copyNo * fR.step) * fBasis.ax;
                if (fBaseRot) rm = new G4RotationMatrix(*fBaseRot);
            }

            pv->SetTranslation(delta - fCenter);
            pv->SetRotation(rm); // the physical volume takes ownership
        }

    private:
        ReplicaDesc fR;
        G4ThreeVector fCenter;
        G4RotationMatrix* fBaseRot;
        ArrayBasis fBasis;
    };

    // Bounding box (array frame, before obj.rot) of all copies plus the solid's
    // half-extent; drives the container mother volume size.
    struct ArrayExtent { G4ThreeVector center, half; };

    ArrayExtent ArrayExtentFor(const ReplicaDesc& r, const G4ThreeVector& solidHalf) {
        const ArrayBasis b = ArrayBasisFor(r.axis.empty() ? 'z' : r.axis[0]);
        G4ThreeVector lo(0,0,0), hi(0,0,0);
        if (r.mode == "grid") {
            const int nx = std::max(1, r.nx), ny = std::max(1, r.ny), nz = std::max(1, r.nz);
            const G4ThreeVector corner = ((nx-1)*r.dx)*b.u + ((ny-1)*r.dy)*b.v + ((nz-1)*r.dz)*b.ax;
            lo = G4ThreeVector(std::min(0.0, corner.x()), std::min(0.0, corner.y()), std::min(0.0, corner.z()));
            hi = G4ThreeVector(std::max(0.0, corner.x()), std::max(0.0, corner.y()), std::max(0.0, corner.z()));
        } else if (r.mode == "polar") {
            const G4ThreeVector rr = std::abs(r.radius) * (b.u + b.v);
            lo = G4ThreeVector(-std::abs(rr.x()), -std::abs(rr.y()), -std::abs(rr.z()));
            hi = G4ThreeVector( std::abs(rr.x()),  std::abs(rr.y()),  std::abs(rr.z()));
        } else { // linear
            const double e = r.offset + (std::max(1, r.count) - 1) * r.step;
            const G4ThreeVector c = e * b.ax;
            lo = G4ThreeVector(std::min(0.0, c.x()), std::min(0.0, c.y()), std::min(0.0, c.z()));
            hi = G4ThreeVector(std::max(0.0, c.x()), std::max(0.0, c.y()), std::max(0.0, c.z()));
        }
        ArrayExtent out;
        out.center = (lo + hi) * 0.5;
        out.half = (hi - lo) * 0.5;
        // Per-axis solid half-extent. Using the bounding-sphere radius here
        // (the previous approach) made the air container span the solid's
        // diagonal in *every* axis, so a wide/tall solid (e.g. a silicon
        // tracker layer) produced an oversized container that overlapped
        // neighbouring volumes (e.g. the calorimeter) and silently replaced
        // their material with G4_AIR during navigation. The axis-aligned
        // half-extent is the correct container size for the non-rotated case.
        out.half += solidHalf;
        return out;
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
                        continue;
                    }
                }
                // Plain number without a unit → default to millimetres. This is
                // required because G4UIcommand::ConvertToDimensionedDouble reads an
                // uninitialised unit buffer for unit-less strings.
                result.push_back(tokens[i] + " mm");
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

        // Mother volume name (empty = World)
        currentDesc.mother = cfg->GetString(baseKey + "mother", "");

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
        if (cfg->GetNode(baseKey + "DIGITIZE").IsDefined()) {
            currentDesc.digitizeProps.deadTime_ns = cfg->GetValueWithUnits(digitizeBase + "dead_time_ns", 0.0) / ns;
            currentDesc.digitizeProps.deadTimeModel = cfg->GetString(digitizeBase + "dead_time_model", "nonparalyzable");
            currentDesc.digitizeProps.timeResFWHM_ns = cfg->GetValueWithUnits(digitizeBase + "time_res_fwhm_ns", 0.0) / ns;
            currentDesc.digitizeProps.energyResFWHM = cfg->GetDouble(digitizeBase + "energy_res_fwhm", 0.0);
            currentDesc.digitizeProps.energyResRefEnergy_MeV = cfg->GetDouble(digitizeBase + "energy_res_ref_energy_MeV", 1.0);
            currentDesc.digitizeProps.intrinsicResFWHM_MeV = cfg->GetValueWithUnits(digitizeBase + "intrinsic_res_fwhm_MeV", 0.0) / MeV;
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
            currentDesc.digitizeProps.useEfficiencyExpression = cfg->GetBool(digitizeBase + "use_efficiency_expression", false);
            currentDesc.digitizeProps.efficiencyExpression = cfg->GetString(digitizeBase + "efficiency_expression", "");
            currentDesc.digitizeProps.useGainExpression = cfg->GetBool(digitizeBase + "use_gain_expression", false);
            currentDesc.digitizeProps.gainExpression = cfg->GetString(digitizeBase + "gain_expression", "");
            currentDesc.digitizeProps.useNoiseExpression = cfg->GetBool(digitizeBase + "use_noise_expression", false);
            currentDesc.digitizeProps.noiseExpression = cfg->GetString(digitizeBase + "noise_expression", "");
            currentDesc.digitizeProps.useThresholdExpression = cfg->GetBool(digitizeBase + "use_threshold_expression", false);
            currentDesc.digitizeProps.thresholdExpression = cfg->GetString(digitizeBase + "threshold_expression", "");
            currentDesc.digitizeProps.useUpperThresholdExpression = cfg->GetBool(digitizeBase + "use_upper_threshold_expression", false);
            currentDesc.digitizeProps.upperThresholdExpression = cfg->GetString(digitizeBase + "upper_threshold_expression", "");
            // Additional effects
            currentDesc.digitizeProps.darkCountRate_kHz = cfg->GetDouble(digitizeBase + "dark_count_rate_kHz", 0.0);
            currentDesc.digitizeProps.crossTalkProbability = cfg->GetDouble(digitizeBase + "cross_talk_probability", 0.0);
            currentDesc.digitizeProps.recoveryTime_ns = cfg->GetValueWithUnits(digitizeBase + "recovery_time_ns", 0.0) / ns;
            currentDesc.digitizeProps.coincidenceWindow_ns = cfg->GetValueWithUnits(digitizeBase + "coincidence_window_ns", 0.0) / ns;
            currentDesc.digitizeProps.spatialResFWHM_mm = cfg->GetValueWithUnits(digitizeBase + "spatial_res_fwhm_mm", 0.0) / mm;
            currentDesc.digitizeProps.efficiencyMode = cfg->GetString(digitizeBase + "efficiency_mode", "constant");
            currentDesc.digitizeProps.efficiencyCenter_MeV = cfg->GetDouble(digitizeBase + "efficiency_center_MeV", 0.511);
            currentDesc.digitizeProps.efficiencyFWHM_MeV = cfg->GetDouble(digitizeBase + "efficiency_fwhm_MeV", 0.0);
            currentDesc.digitizeProps.readoutGroup = cfg->GetInt(digitizeBase + "readout_group", -1);
            currentDesc.digitizeProps.upperThreshold_MeV = cfg->GetValueWithUnits(digitizeBase + "upper_threshold", 0.0) / MeV;
            currentDesc.digitizeProps.energyFrame_MeV = cfg->GetValueWithUnits(digitizeBase + "energy_frame_MeV", 0.0) / MeV;
            currentDesc.digitizeProps.doiResolution_mm = cfg->GetValueWithUnits(digitizeBase + "doi_resolution_mm", 0.0) / mm;
            currentDesc.digitizeProps.coincidenceMultiplicity = cfg->GetInt(digitizeBase + "coincidence_multiplicity", 2);
            currentDesc.digitizeProps.efficiencyFile = cfg->GetString(digitizeBase + "efficiency_file", "");
            if (!currentDesc.digitizeProps.efficiencyFile.empty()) {
                std::ifstream ef(currentDesc.digitizeProps.efficiencyFile);
                std::string line;
                while (std::getline(ef, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    std::istringstream ls(line);
                    double e, p; if (ls >> e >> p) currentDesc.digitizeProps.efficiencyCurve.emplace_back(e, p);
                }
                std::sort(currentDesc.digitizeProps.efficiencyCurve.begin(), currentDesc.digitizeProps.efficiencyCurve.end());
            }
            // Detector type
            currentDesc.digitizeProps.calorimeterMode = cfg->GetBool(digitizeBase + "calorimeter_mode", false);
            currentDesc.digitizeProps.digitType = cfg->GetInt(digitizeBase + "digit_type", -1);
            // Configurable singles pipeline (ordered step names; empty = default order)
            currentDesc.digitizeProps.pipeline = cfg->GetStringVector(digitizeBase + "pipeline");
            // GATE-parity enhancements
            currentDesc.digitizeProps.crossTalkFraction = cfg->GetDouble(digitizeBase + "cross_talk_fraction", 0.0);
            currentDesc.digitizeProps.crossTalkNeighbors = cfg->GetIntVector(digitizeBase + "cross_talk_neighbors", {1});
            currentDesc.digitizeProps.darkCountEnergy_MeV = cfg->GetValueWithUnits(digitizeBase + "dark_count_energy_MeV", 1.0) / MeV;
            currentDesc.digitizeProps.coincidenceMinSectorDiff = cfg->GetInt(digitizeBase + "coincidence_min_sector_diff", 0);
            currentDesc.digitizeProps.coincidenceMultiplesPolicy = cfg->GetString(digitizeBase + "coincidence_multiples_policy", "reject");
            currentDesc.digitizeProps.readoutPolicy = cfg->GetString(digitizeBase + "readout_policy", "energy_winner");
            // Light-yield energy resolution model (GATE intrinsic resolution)
            currentDesc.digitizeProps.lightYield = cfg->GetDouble(digitizeBase + "light_yield", 0.0);
            currentDesc.digitizeProps.transferEfficiency = cfg->GetDouble(digitizeBase + "transfer_efficiency", 1.0);
            currentDesc.digitizeProps.intrinsicResFWHM_frac = cfg->GetDouble(digitizeBase + "intrinsic_res_fwhm", 0.0);
            currentDesc.digitizeProps.intrinsicResRefEnergy_MeV = cfg->GetDouble(digitizeBase + "intrinsic_res_ref_energy_MeV", 1.0);
            // Dead-time buffer + confined spatial resolution
            currentDesc.digitizeProps.deadTimeBufferSize = cfg->GetInt(digitizeBase + "dead_time_buffer_size", 0);
            currentDesc.digitizeProps.spatialConfine = cfg->GetBool(digitizeBase + "spatial_confine", false);
            currentDesc.digitizeProps.spatialHalfX_mm = cfg->GetValueWithUnits(digitizeBase + "spatial_half_x_mm", 0.0) / mm;
            currentDesc.digitizeProps.spatialHalfY_mm = cfg->GetValueWithUnits(digitizeBase + "spatial_half_y_mm", 0.0) / mm;
            currentDesc.digitizeProps.spatialHalfZ_mm = cfg->GetValueWithUnits(digitizeBase + "spatial_half_z_mm", 0.0) / mm;
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

        // Rotation: ROTATION vector [x, y, z] Euler angles in degrees
        auto rotDblVec = cfg->GetDoubleVector(baseKey + "ROTATION");
        if (rotDblVec.size() >= 3) {
            currentDesc.rot.setX(rotDblVec[0]);
            currentDesc.rot.setY(rotDblVec[1]);
            currentDesc.rot.setZ(rotDblVec[2]);
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

        // REPLICA configuration (linear / grid / polar).
        std::string replicaBase = baseKey + "REPLICA.";
        if (cfg->HasKey(replicaBase + "count") || cfg->HasKey(replicaBase + "nx")
            || cfg->HasKey(replicaBase + "radius")) {
            currentDesc.replica.mode = cfg->GetString(replicaBase + "mode", "linear");
            currentDesc.replica.axis = cfg->GetString(replicaBase + "axis", "z");
            currentDesc.replica.count = cfg->GetInt(replicaBase + "count", 0);
            currentDesc.replica.step = cfg->GetValueWithUnits(replicaBase + "step", 0.0);
            currentDesc.replica.offset = cfg->GetValueWithUnits(replicaBase + "offset", 0.0);
            currentDesc.replica.nx = cfg->GetInt(replicaBase + "nx", 0);
            currentDesc.replica.ny = cfg->GetInt(replicaBase + "ny", 0);
            currentDesc.replica.nz = cfg->GetInt(replicaBase + "nz", 0);
            currentDesc.replica.dx = cfg->GetValueWithUnits(replicaBase + "dx", 0.0);
            currentDesc.replica.dy = cfg->GetValueWithUnits(replicaBase + "dy", 0.0);
            currentDesc.replica.dz = cfg->GetValueWithUnits(replicaBase + "dz", 0.0);
            currentDesc.replica.radius = cfg->GetValueWithUnits(replicaBase + "radius", 0.0);
            currentDesc.replica.startAngle = cfg->GetDouble(replicaBase + "startAngle", 0.0);
            currentDesc.replica.totalAngle = cfg->GetDouble(replicaBase + "totalAngle", 360.0);
            currentDesc.replica.rotate = cfg->GetBool(replicaBase + "rotate", false);
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

            auto childRot = cfg->GetDoubleVector(childKey + "ROTATION");
            if (childRot.size() >= 3) {
                childDesc.rot.setX(childRot[0]);
                childDesc.rot.setY(childRot[1]);
                childDesc.rot.setZ(childRot[2]);
            }

            // DIGITIZE for child
            std::string childDigi = childKey + "DIGITIZE.";
            if (cfg->GetNode(childKey + "DIGITIZE").IsDefined()) {
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
               << ", replica=" << (currentDesc.replica.count > 0
                                    || currentDesc.replica.nx > 0
                                    || currentDesc.replica.radius > 0.0 ? "yes" : "no")
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
            G4RotationMatrix* rotMatrix = BuildRotation(obj.rot);
            G4VPhysicalVolume* physVol = new G4PVPlacement(
                rotMatrix, obj.pos, lvolFromGDML, obj.name,
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
        
        obj.solid = solid;
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

        // ── Placement: array (REPLICA) or single ──
        // Arrays are built as a single G4PVParameterised volume (one LV, one PV,
        // N copies computed by ArrayParameterisation) instead of a loop of
        // G4PVPlacement objects — much smaller memory footprint and faster
        // voxelisation for large detector arrays.
        const ReplicaDesc& rep = obj.replica;
        int nCopies = 0;
        const bool isGrid = (rep.mode == "grid");
        const bool hasArray = !rep.axis.empty()
            && (isGrid ? (rep.nx > 0 && rep.ny > 0) : rep.count > 0);
        if (hasArray) {
            if (isGrid) {
                const int nx = std::max(1, rep.nx), ny = std::max(1, rep.ny), nz = std::max(1, rep.nz);
                nCopies = nx * ny * nz;
            } else {
                nCopies = rep.count; // linear + polar
            }
        }

        // Place children once in the shared LV — they appear in every array copy
        // automatically (all copies share the same logical volume).
        for (auto& child : obj.children) {
            if (!child.logicalVolume) continue;
            G4RotationMatrix* childRot = BuildRotation(child.rot);
            new G4PVPlacement(childRot, child.pos, child.logicalVolume,
                              child.name, lvol, false, 0);
        }

        if (nCopies > 0) {
            // Solid half-extent (drives the container size).
            G4ThreeVector solidHalf(1 * mm, 1 * mm, 1 * mm);
            if (obj.solid) {
                G4ThreeVector pMin, pMax;
                obj.solid->BoundingLimits(pMin, pMax);
                solidHalf = (pMax - pMin) * 0.5;
            }
            const ArrayExtent ext = ArrayExtentFor(rep, solidHalf);

            // Dedicated air container mother (must be non-world and have the
            // parameterised volume as its only daughter).
            auto* contSolid = new G4Box(obj.name + "_ArrayContainer",
                                        ext.half.x(), ext.half.y(), ext.half.z());
            G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
            auto* contLV = new G4LogicalVolume(contSolid, air, obj.name + "_ArrayContainer_LV");
            const G4ThreeVector contPos = obj.pos + ext.center;
            new G4PVPlacement(nullptr, contPos, contLV, obj.name + "_ArrayContainer",
                              actualMother, false, copyNo++);

            G4RotationMatrix* baseRot = BuildRotation(obj.rot);
            auto* param = new ArrayParameterisation(rep, ext.center, baseRot);
            auto* physVol = new G4PVParameterised(obj.name, lvol, contLV,
                                                 kUndefined, nCopies, param);
            obj.physicalVolume = physVol;
            G4cout << "ObjectManager: placed array '" << obj.name << "' (mode=" << rep.mode
                   << ", copies=" << nCopies << ", axis=" << rep.axis << ") in container '"
                   << obj.name << "_ArrayContainer'" << G4endl;
        } else {
            G4RotationMatrix* rotMatrix = BuildRotation(obj.rot);
            G4VPhysicalVolume* physVol = new G4PVPlacement(rotMatrix, obj.pos, lvol, obj.name, actualMother, false, copyNo++);
            obj.physicalVolume = physVol;
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