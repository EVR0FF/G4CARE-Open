//==============================================================================
//
// G4CARE
//
// @file    TreeManager.cc
// @brief   NTuple tree builder from YAML — column lists, filter expressions,
//          dictionary support, and ROOT NTuple creation.
//
// @details
//   Reads NTUPLE.<name> YAML blocks and builds per-tree column descriptors
//   (ColType enum with optional expression/filter).  Supports dictionary
//   columns (integer indices into typed registries with separate dictionary
//   trees), string columns, and ExprTK filter expressions.
//
//   Configuration keys read:
//     NTUPLE.<name>.columns, NTUPLE.<name>.filter, ENABLE_DICTIONARY
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "TreeManager.hh"
#include "ConfigManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4Threading.hh"
#include "G4RunManager.hh"
#include "G4Run.hh"
#include "ColumnTypes.hh"
#include "G4ProcessTable.hh"
#include "G4ProcessVector.hh"
#include "ExpressionEvaluator.hh"
#include "DetectorRegistry.hh"
#include "ProcessUtils.hh"
#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include "FilterVarRegistry.hh"

/// @brief Mapping from column name strings (as used in NTUPLE.<name>.columns) to ColType enum.
///
/// Covers all recognised column types: identifiers, position/time, kinematics,
/// polarisation, vertex, process, step, track, nuclear properties, detector
/// effects, volume/material/region, source/detector, cross-sections, and chemistry.
static const std::unordered_map<std::string_view, ColType> nameToColType = {
    // Identifiers
    {"run_id", ColType::RunID},
    {"event_id", ColType::EventID},
    {"track_id", ColType::TrackID},
    {"parent_id", ColType::ParentID},
    {"thread_id", ColType::ThreadID},
    {"step_number", ColType::StepNumber},
    {"weight", ColType::Weight},

    // Position and time
    {"x", ColType::PosX}, {"pos_x", ColType::PosX},
    {"y", ColType::PosY}, {"pos_y", ColType::PosY},
    {"z", ColType::PosZ}, {"pos_z", ColType::PosZ},
    {"pre_x", ColType::PrePosX}, {"pre_pos_x", ColType::PrePosX},
    {"pre_y", ColType::PrePosY}, {"pre_pos_y", ColType::PrePosY},
    {"pre_z", ColType::PrePosZ}, {"pre_pos_z", ColType::PrePosZ},
    {"post_x", ColType::PostPosX}, {"post_pos_x", ColType::PostPosX},
    {"post_y", ColType::PostPosY}, {"post_pos_y", ColType::PostPosY},
    {"post_z", ColType::PostPosZ}, {"post_pos_z", ColType::PostPosZ},
    {"global_time", ColType::GlobalTime},
    {"local_time", ColType::LocalTime},
    {"proper_time", ColType::ProperTime},

    // Kinematics and momentum
    {"pdg_code", ColType::PDGCode}, {"particle", ColType::PDGCode},
    {"mass", ColType::Mass},
    {"charge", ColType::Charge},
    {"particle_spin", ColType::ParticleSpin},
    {"particle_name", ColType::ParticleName},
    {"kinetic_energy", ColType::KineticEnergy},
    {"total_energy", ColType::TotalEnergy},
    {"pre_kinetic_energy", ColType::PreKineticEnergy},
    {"post_kinetic_energy", ColType::PostKineticEnergy},
    {"px", ColType::Px}, {"mom_x", ColType::Px},
    {"py", ColType::Py}, {"mom_y", ColType::Py},
    {"pz", ColType::Pz}, {"mom_z", ColType::Pz},
    {"pre_px", ColType::PrePx},
    {"pre_py", ColType::PrePy},
    {"pre_pz", ColType::PrePz},
    {"post_px", ColType::PostPx},
    {"post_py", ColType::PostPy},
    {"post_pz", ColType::PostPz},
    {"dir_x", ColType::DirX},
    {"dir_y", ColType::DirY},
    {"dir_z", ColType::DirZ},
    {"theta", ColType::DirTheta},
    {"phi", ColType::DirPhi},
    {"pt", ColType::Pt},
    {"eta", ColType::Pseudorapidity},
    {"beta", ColType::Beta},
    {"scattering_angle", ColType::ScatteringAngle},

    // Polarisation
    {"pol_x", ColType::PolX},
    {"pol_y", ColType::PolY},
    {"pol_z", ColType::PolZ},

    // Vertex
    {"vertex_x", ColType::VertexX},
    {"vertex_y", ColType::VertexY},
    {"vertex_z", ColType::VertexZ},
    {"vertex_px", ColType::VertexPx},
    {"vertex_py", ColType::VertexPy},
    {"vertex_pz", ColType::VertexPz},
    {"vertex_energy", ColType::VertexKineticEnergy},
    {"vertex_pdg", ColType::VertexPDGCode},

    // Processes
    {"process_subtype", ColType::ProcessSubType},
    {"process_type", ColType::ProcessType},
    {"process_id", ColType::ProcessID},
    {"creator_process_subtype", ColType::CreatorProcessSubType},
    {"creator_process_name", ColType::CreatorProcessName},
    {"process_name", ColType::ProcessName},
    {"step_limiting_process", ColType::StepLimitingProcess},

    // Step
    {"edep", ColType::Edep}, {"energy_deposit", ColType::Edep},
    {"delta_e", ColType::DeltaE},
    {"niel", ColType::NIEL},
    {"dpa", ColType::DPA},
    {"step_length", ColType::StepLength},
    {"delta_time", ColType::DeltaTime},
    {"n_secondaries", ColType::NSecondaries},
    {"step_status", ColType::StepStatus},
    {"safety", ColType::Safety},
    {"is_first_step_in_volume", ColType::IsFirstStepInVolume},
    {"is_last_step_in_volume", ColType::IsLastStepInVolume},
    {"delta_position_x", ColType::DeltaPositionX},
    {"delta_position_y", ColType::DeltaPositionY},
    {"delta_position_z", ColType::DeltaPositionZ},
    {"delta_momentum_x", ColType::DeltaMomentumX},
    {"delta_momentum_y", ColType::DeltaMomentumY},
    {"delta_momentum_z", ColType::DeltaMomentumZ},

    // Track
    {"track_length", ColType::TrackLength},
    {"track_status", ColType::TrackStatus},
    {"mean_free_path", ColType::MeanFreePath},
    {"at_rest_rate", ColType::AtRestRate},
    {"at_rest_lifetime", ColType::AtRestLifeTime},

    // Nuclear properties
    {"Z", ColType::Z}, {"atomic_number", ColType::Z},
    {"A", ColType::A}, {"atomic_mass", ColType::A},
    {"excitation", ColType::Excitation},
    {"isomer_level", ColType::IsomerLevel},
    {"recoil_type", ColType::RecoilType},

    // Detector effects
    {"smeared_edep", ColType::SmearedEdep},
    {"visible_edep", ColType::VisibleEdep},
    {"dose_gy", ColType::DoseGy},
    {"let", ColType::LET},
    {"step_grammage", ColType::StepGrammage},
    {"wavelength", ColType::OpticalWavelength},
    {"boundary_status", ColType::BoundaryStatus},

    // Volume, material, region
    {"volume_id", ColType::VolumeID},
    {"volume_name", ColType::VolumeName},
    {"volume_mass", ColType::VolumeMass},
    {"copy_no", ColType::CopyNo},
    {"material_id", ColType::MaterialID},
    {"material_name", ColType::MaterialName},
    {"density", ColType::Density},
    {"temperature", ColType::Temperature},
    {"pressure", ColType::Pressure},
    {"state", ColType::State},
    {"radiation_length", ColType::RadiationLength},
    {"nuclear_interaction_length", ColType::NuclearInteractionLength},
    {"zeff", ColType::Zeff},
    {"aeff", ColType::Aeff},
    {"chemical_formula", ColType::ChemicalFormula},
    {"region_id", ColType::RegionID},
    {"region_name", ColType::RegionName},
    {"touchable_path", ColType::TouchablePath},

    // Sources and detectors
    {"source_name", ColType::SourceName},
    {"detector_name", ColType::DetectorName},
    {"detector_id", ColType::DetectorID},
    {"digit_type", ColType::DigitType},
    {"energy", ColType::Edep}, {"time", ColType::GlobalTime},
    {"raw_energy_MeV", ColType::RawEnergy},
    {"raw_time_ns", ColType::RawTime},
    {"pileup_size", ColType::PileupSize},
    {"is_pileup", ColType::IsPileup},
    {"afterpulse", ColType::Afterpulse},
    {"quantum_efficiency", ColType::QuantumEfficiency},
    {"noise_energy_MeV", ColType::NoiseEnergy},

    // Cross-sections
    {"NeutronCaptureXS", ColType::NeutronCaptureXS},
    {"NeutronElasticXS", ColType::NeutronElasticXS},
    {"NeutronInelasticXS", ColType::NeutronInelasticXS},
    {"NeutronFissionXS", ColType::NeutronFissionXS},
    {"NeutronTotalXS", ColType::NeutronTotalXS},
    {"NeutronThermalScatteringXS", ColType::NeutronThermalScatteringXS},
    {"PhotonTotalXS", ColType::PhotonTotalXS},
    {"PhotonPhotoElectricXS", ColType::PhotonPhotoElectricXS},
    {"PhotonComptonXS", ColType::PhotonComptonXS},
    {"PhotonConversionXS", ColType::PhotonConversionXS},
    {"PhotonRayleighXS", ColType::PhotonRayleighXS},
    {"PhotonNuclearXS", ColType::PhotonNuclearXS},
    {"PhotonMuonPairXS", ColType::PhotonMuonPairXS},
    {"ElectronIonisationXS", ColType::ElectronIonisationXS},
    {"ElectronBremsstrahlungXS", ColType::ElectronBremsstrahlungXS},
    {"ElectronExcitationXS", ColType::ElectronExcitationXS},
    {"ElectronElasticXS", ColType::ElectronElasticXS},
    {"PositronIonisationXS", ColType::PositronIonisationXS},
    {"PositronBremsstrahlungXS", ColType::PositronBremsstrahlungXS},
    {"PositronAnnihilationXS", ColType::PositronAnnihilationXS},
    {"MuonIonisationXS", ColType::MuonIonisationXS},
    {"MuonBremsstrahlungXS", ColType::MuonBremsstrahlungXS},
    {"MuonPairProductionXS", ColType::MuonPairProductionXS},
    {"MuonNuclearXS", ColType::MuonNuclearXS},
    {"IonIonisationXS", ColType::IonIonisationXS},
    {"IonInelasticXS", ColType::IonInelasticXS},
    {"IonElasticXS", ColType::IonElasticXS},

    // Chemistry
    {"species_id", ColType::SpeciesID}
};

// ── Physical quantity units ──
// Appended as suffixes to ROOT branch names.
// String/dictionary/dimensionless columns have no suffix.
static const std::map<ColType, std::string> colTypeToUnit = {
    // Energy → MeV
    {ColType::KineticEnergy,      "MeV"},
    {ColType::TotalEnergy,        "MeV"},
    {ColType::PreKineticEnergy,   "MeV"},
    {ColType::PostKineticEnergy,  "MeV"},
    {ColType::Edep,               "MeV"},
    {ColType::DeltaE,             "MeV"},
    {ColType::NIEL,               "MeV"},
    {ColType::Mass,               "MeV/c2"},
    {ColType::VertexKineticEnergy,"MeV"},
    {ColType::RawEnergy,          "MeV"},
    {ColType::NoiseEnergy,        "MeV"},
    {ColType::SmearedEdep,        "MeV"},
    {ColType::VisibleEdep,        "MeV"},

    // Momentum → MeV/c
    {ColType::Px, "MeV"}, {ColType::Py, "MeV"}, {ColType::Pz, "MeV"},
    {ColType::PrePx,"MeV"}, {ColType::PrePy,"MeV"}, {ColType::PrePz,"MeV"},
    {ColType::PostPx,"MeV"},{ColType::PostPy,"MeV"},{ColType::PostPz,"MeV"},
    {ColType::DeltaMomentumX,"MeV"},{ColType::DeltaMomentumY,"MeV"},{ColType::DeltaMomentumZ,"MeV"},
    {ColType::Pt, "MeV"},
    {ColType::VertexPx,"MeV"}, {ColType::VertexPy,"MeV"}, {ColType::VertexPz,"MeV"},

    // Length / position → mm
    {ColType::PosX, "mm"}, {ColType::PosY, "mm"}, {ColType::PosZ, "mm"},
    {ColType::PrePosX,"mm"}, {ColType::PrePosY,"mm"}, {ColType::PrePosZ,"mm"},
    {ColType::PostPosX,"mm"},{ColType::PostPosY,"mm"},{ColType::PostPosZ,"mm"},
    {ColType::DeltaPositionX,"mm"},{ColType::DeltaPositionY,"mm"},{ColType::DeltaPositionZ,"mm"},
    {ColType::VertexX,"mm"}, {ColType::VertexY,"mm"}, {ColType::VertexZ,"mm"},
    {ColType::StepLength, "mm"},
    {ColType::TrackLength,"mm"},
    {ColType::Safety,     "mm"},
    {ColType::StepGrammage,"g/cm2"},
    {ColType::MeanFreePath,"mm"},
    {ColType::RadiationLength,"mm"},
    {ColType::NuclearInteractionLength,"mm"},

    // Time → ns
    {ColType::GlobalTime,  "ns"},
    {ColType::LocalTime,   "ns"},
    {ColType::ProperTime,  "ns"},
    {ColType::DeltaTime,   "ns"},
    {ColType::RawTime,     "ns"},
    {ColType::AtRestLifeTime,"ns"},
    {ColType::ParticleLifetime,"ns"},

    // Dosimetry
    {ColType::DoseGy, "Gy"},
    {ColType::LET,    "keV/um"},

    // Optics
    {ColType::OpticalWavelength, "nm"},

    // Volume mass
    {ColType::VolumeMass,  "kg"},

    // Density / temperature / pressure
    {ColType::Density,     "g/cm3"},
    {ColType::Temperature, "K"},
    {ColType::Pressure,    "atm"},

    // Angles
    {ColType::ScatteringAngle, "rad"},

    // Cross-sections → mm² (Geant4 internal units)
    {ColType::NeutronTotalXS,  "mm2"},
    {ColType::NeutronCaptureXS,"mm2"},

    // Particles
    {ColType::Charge, "e"},
    {ColType::AtRestRate, "MeV/ns"},
    {ColType::ParticleWidth, "MeV"},
    {ColType::IsotopeMass, "MeV/c2"},
    {ColType::IsotopeLifetime, "ns"},
};

/// @brief Column types that are stored as dictionary (int index) references.
static const std::unordered_set<ColType> dictionaryTypes = {
    ColType::VolumeName,
    ColType::MaterialName,
    ColType::RegionName,
    ColType::SourceName,
    ColType::DetectorName,
    ColType::TouchablePath,
    ColType::ChemicalFormula,
    ColType::SpeciesID,
    ColType::ParticleName,
    ColType::ProcessName,
    ColType::CreatorProcessName,
};

/// @brief Mapping from dictionary ColType to the corresponding registry tree name.
static const std::unordered_map<ColType, std::string> typeToDictName = {
    {ColType::VolumeName, "volume_dict"},
    {ColType::MaterialName, "material_dict"},
    {ColType::RegionName, "region_dict"},
    {ColType::SourceName, "source_dict"},
    {ColType::DetectorName, "detector_dict"},
    {ColType::TouchablePath, "touchable_dict"},
    {ColType::ChemicalFormula, "chemical_formula_dict"},
    {ColType::SpeciesID, "chemical_species_dict"},
    {ColType::ParticleName, "particle_dict"},
    {ColType::ProcessName, "process_dict"},
    {ColType::CreatorProcessName, "process_dict"},
};

TreeManager::TreeManager(VolumeMaterialRegistry& registry,
                         ExpressionEvaluator& evaluator,
                         CrossSectionCalculator* xsCalc,
                         G4AnalysisManager* analysisManager,
                         TypedRegistry<std::string>* particleNameReg,
                         TypedRegistry<std::string>* volumeNameReg,
                         TypedRegistry<std::string>* materialNameReg,
                         TypedRegistry<std::string>* regionNameReg,
                         TypedRegistry<std::string>* sourceNameReg,
                         TypedRegistry<std::string>* detectorNameReg,
                         TypedRegistry<std::string>* touchablePathReg,
                         TypedRegistry<std::string>* chemicalFormulaReg,
                         TypedRegistry<int>* pdgReg,
                         TypedRegistry<int>* processSubTypeReg,
                         TypedRegistry<double>* massReg,
                         TypedRegistry<double>* chargeReg,
                         DetectorRegistry* detReg, SurfaceRegistry* surfReg, TypedRegistry<std::string>* chemSpeciesReg)
    : fRegistry(registry),
      fEvaluator(evaluator),
      fXSCalculator(xsCalc),
      fAnalysisManager(analysisManager),
      fParticleNameReg(particleNameReg),
      fVolumeNameReg(volumeNameReg),
      fMaterialNameReg(materialNameReg),
      fRegionNameReg(regionNameReg),
      fSourceNameReg(sourceNameReg),
      fDetectorNameReg(detectorNameReg),
      fTouchablePathReg(touchablePathReg),
      fChemicalFormulaReg(chemicalFormulaReg),
      fPDGReg(pdgReg),
      fProcessSubTypeReg(processSubTypeReg),
      fMassReg(massReg),
      fChargeReg(chargeReg),
      fDetectorRegistry(detReg), fSurfaceRegistry(surfReg) {}

/// @brief Build an NTuple tree from the NTUPLE.<treeName> YAML config block.
///
/// Reads NTUPLE.<treeName>.columns (list of column names) and optionally
/// NTUPLE.<treeName>.filter (scalar string or {volumes, condition} map).
/// For each column:
/// - If the column name matches a registered ExpressionManager expression,
///   the column type is set to Expression with a symbolic reference.
/// - If the name matches a known ColType in nameToColType, the column is
///   configured as a built-in type.  Dictionary columns may be stored as
///   integer indices (ENABLE_DICTIONARY=true) or direct strings (=false).
/// - If the name is an ExprTK expression (ConfigManager::IsExpression),
///   it is compiled via PrecompileForFilter and added as an expression column.
///
/// Filter parsing supports three formats:
/// 1. Scalar string: `filter: "edep > 0"` (legacy).
/// 2. Map: `filter: {volumes: [...], condition: "edep > 0"}` (new).
/// 3. Key-value under NTUPLE.<treeName>.filter (backward compat).
///
/// Unit suffixes are automatically appended to numeric columns (e.g. `edep_MeV`).
/// A `run_id` column is always appended if not explicitly requested.
///
/// @param treeName NTuple tree name (e.g. "g4care", "secondary", "activation").
/// @param cfg      ConfigManager instance providing YAML access.
void TreeManager::CreateTreeFromConfig(const std::string& treeName, ConfigManager* cfg) {
    std::string base = "NTUPLE." + treeName + ".";
    std::vector<std::string> colNames = cfg->GetStringVector(base + "columns");
    if (colNames.empty()) {
        G4cerr << "TreeManager: No columns defined for tree '" << treeName << "'" << G4endl;
        return;
    }

    // Read ENABLE_DICTIONARY from global key (default true)
    bool enableDict = cfg->GetBool("ENABLE_DICTIONARY", true);
    G4cout << "TreeManager: ENABLE_DICTIONARY=" << (enableDict ? "true" : "false")
           << " for tree '" << treeName << "'" << G4endl;

    TreeInfo ti;
    ti.ntupleId = fAnalysisManager->CreateNtuple(treeName, treeName);
    G4cout << "TreeManager::CreateTreeFromConfig: tree='" << treeName
           << "' ntupleId=" << ti.ntupleId << G4endl;

    const std::vector<std::string>& filterVarNames = GetAllFilterVarNames();
    const std::vector<FilterVar>& filterVarIndices = GetAllFilterVarIndices();

    std::unordered_set<FilterVar> usedVarSet;
    for (const auto& colName : colNames) {
        ColumnInfo col;
        col.name = colName;
        std::string_view sv(colName);

        // Check for ExpressionManager expressions (any name, resolved via ConfigManager)
        auto exprDefs = cfg->GetExpressionDefinitions();
        if (exprDefs.find(colName) != exprDefs.end()) {
            col.type = ColType::Expression;
            col.expressionRef = colName;
            col.name = colName;
        } else if (nameToColType.find(sv) != nameToColType.end()) {
            auto it = nameToColType.find(sv);
            col.type = it->second;
            if (dictionaryTypes.count(col.type)) {
                col.dictionaryName = typeToDictName.at(col.type);
                if (enableDict) {
                    // Dictionaries enabled — store int (registry ID) + separate dictionary tree
                    col.isDictionary = true;
                } else {
                    // Dictionaries disabled — store raw string
                    col.isSColumn = true;
                }
            }
        } else if (cfg->IsExpression(base + colName)) {
            col.type = ColType::Expression;
            std::string exprStr = cfg->GetString(base + colName);
            col.expr = fEvaluator.PrecompileForFilter(exprStr, filterVarIndices, filterVarNames);
            if (col.expr) {
                auto exprVars = fEvaluator.GetUsedVariables(col.expr.get());
                usedVarSet.insert(exprVars.begin(), exprVars.end());
            } else {
                G4cerr << "TreeManager: Failed to compile expression '" << exprStr
                       << "' for column '" << colName << "'" << G4endl;
                continue;
            }
        } else {
            G4cerr << "TreeManager: Unknown column '" << colName
                   << "' in tree '" << treeName << "', skipping." << G4endl;
            continue;
        }

        // Build column name with unit suffix for ROOT
        std::string rootColName(colName);
        if (!col.isDictionary && !col.isSColumn && col.type != ColType::Expression) {
            auto uIt = colTypeToUnit.find(col.type);
            if (uIt != colTypeToUnit.end()) {
                rootColName += "_";
                rootColName += uIt->second;
            }
        }

        if (col.isDictionary) {
            // Dictionary column — int (registry ID)
            col.ntupleColumnId = fAnalysisManager->CreateNtupleIColumn(ti.ntupleId, rootColName);
        } else if (col.isSColumn) {
            // String column (dictionaries disabled)
            col.ntupleColumnId = fAnalysisManager->CreateNtupleSColumn(ti.ntupleId, rootColName);
        } else {
            col.ntupleColumnId = fAnalysisManager->CreateNtupleDColumn(ti.ntupleId, rootColName);
        }
        ti.allColumns.push_back(col);
        ti.activeColumns.push_back(ti.allColumns.size() - 1);
    }

    // ── Filter parsing: scalar (string) or map {volumes, condition} ──
    auto filterNode = cfg->GetNode("NTUPLE." + treeName + ".filter");
    if (filterNode.IsDefined()) {
        if (filterNode.IsScalar()) {
            // Legacy format: filter: "edep > 0"
            std::string filterExpr = filterNode.as<std::string>();
            auto compiledFilter = fEvaluator.PrecompileForFilter(filterExpr, filterVarIndices, filterVarNames);
            if (compiledFilter) {
                ti.filter = compiledFilter;
                ti.filterVarIndices = fEvaluator.GetUsedVariables(compiledFilter.get());
                usedVarSet.insert(ti.filterVarIndices.begin(), ti.filterVarIndices.end());
            } else {
                G4cerr << "TreeManager: Failed to compile filter '" << filterExpr << "'" << G4endl;
            }
        } else if (filterNode.IsMap()) {
            // New format: filter: {volumes: [...], condition: "edep > 0"}
            if (filterNode["volumes"].IsSequence()) {
                for (const auto& v : filterNode["volumes"]) {
                    ti.filterVolumes.push_back(v.as<std::string>());
                }
            }
            if (filterNode["condition"].IsScalar()) {
                std::string condStr = filterNode["condition"].as<std::string>();
                auto condCompiled = fEvaluator.PrecompileForFilter(condStr, filterVarIndices, filterVarNames);
                if (condCompiled) {
                    ti.filterCondition = condCompiled;
                    auto condVars = fEvaluator.GetUsedVariables(condCompiled.get());
                    usedVarSet.insert(condVars.begin(), condVars.end());
                } else {
                    G4cerr << "TreeManager: Failed to compile filter condition '" << condStr << "'" << G4endl;
                }
            }
            // Backward compat: if old filter expression still used
            if (!ti.filterCondition && !ti.filter) {
                std::string filterExpr = cfg->GetString(base + "filter", "");
                if (!filterExpr.empty()) {
                    auto compiledFilter = fEvaluator.PrecompileForFilter(filterExpr, filterVarIndices, filterVarNames);
                    if (compiledFilter) {
                        ti.filter = compiledFilter;
                        ti.filterVarIndices = fEvaluator.GetUsedVariables(compiledFilter.get());
                        usedVarSet.insert(ti.filterVarIndices.begin(), ti.filterVarIndices.end());
                    }
                }
            }
        }
    } else {
        // Backward compat: old string filter
        std::string filterExpr = cfg->GetString(base + "filter", "");
        if (!filterExpr.empty()) {
            auto compiledFilter = fEvaluator.PrecompileForFilter(filterExpr, filterVarIndices, filterVarNames);
            if (compiledFilter) {
                ti.filter = compiledFilter;
                ti.filterVarIndices = fEvaluator.GetUsedVariables(compiledFilter.get());
                usedVarSet.insert(ti.filterVarIndices.begin(), ti.filterVarIndices.end());
            } else {
                G4cerr << "TreeManager: Failed to compile filter '" << filterExpr << "'" << G4endl;
            }
        }
    }

    ti.expressionVarIndices.assign(usedVarSet.begin(), usedVarSet.end());
    ti.allNeededVarIndices = ti.expressionVarIndices;

    bool hasRunId = false;
    for (const auto& col : ti.allColumns) {
        if (col.name == "run_id") { hasRunId = true; break; }
    }
    if (!hasRunId) {
        ColumnInfo runIdCol;
        runIdCol.name = "run_id";
        runIdCol.type = ColType::RunID;
        runIdCol.ntupleColumnId = fAnalysisManager->CreateNtupleDColumn(ti.ntupleId, "run_id");
        ti.allColumns.push_back(runIdCol);
        ti.activeColumns.push_back(ti.allColumns.size() - 1);
    }
    fAnalysisManager->FinishNtuple(ti.ntupleId);
    fTrees[treeName] = std::move(ti);
}

/// @brief Compile a filter expression for an already-created tree info.
///
/// @param tree       TreeInfo to update (filter pointer and var list).
/// @param filterExpr ExprTK filter expression string.
/// @param varNames   Variable names for the ExprTK symbol table.
void TreeManager::CompileFilter(TreeInfo& tree, const std::string& filterExpr,
                                const std::vector<std::string>& varNames) {
    if (filterExpr.empty()) {
        tree.filter.reset();
        tree.filterVarIndices.clear();
        return;
    }

    const std::vector<FilterVar>& filterVarIndices = GetAllFilterVarIndices();
    const std::vector<std::string>& filterVarNames = GetAllFilterVarNames();

    auto compiled = fEvaluator.PrecompileForFilter(filterExpr, filterVarIndices, filterVarNames);
    if (compiled) {
        tree.filter = compiled;
        tree.filterVarIndices = fEvaluator.GetUsedVariables(compiled.get());
    } else {
        G4cerr << "TreeManager: Failed to compile filter '" << filterExpr << "'" << G4endl;
    }
}

/// @brief Look up an NTuple tree by name (mutable).
/// @param name Tree name (e.g. "g4care").
/// @return     Pointer to TreeInfo, or nullptr.
TreeManager::TreeInfo* TreeManager::GetTree(const std::string& name) {
    auto it = fTrees.find(name);
    if (it != fTrees.end()) return &it->second;
    return nullptr;
}

/// @brief Look up an NTuple tree by name (const).
/// @param name Tree name.
/// @return     Pointer to TreeInfo, or nullptr.
const TreeManager::TreeInfo* TreeManager::GetTree(const std::string& name) const {
    auto it = fTrees.find(name);
    if (it != fTrees.end()) return &it->second;
    return nullptr;
}

/// @brief Resolve a dictionary integer ID to a human-readable string.
///
/// @param dictName Registry tree name (e.g. "volume_dict", "material_dict").
/// @param id       Integer ID from the corresponding typed registry.
/// @return         Human-readable string, or empty string if not found.
std::string TreeManager::GetDictString(const std::string& dictName, int id) const {
    if (dictName == "volume_dict" && fVolumeNameReg) {
        return fVolumeNameReg->GetValue(id);
    }
    if (dictName == "material_dict" && fMaterialNameReg) {
        return fMaterialNameReg->GetValue(id);
    }
    if (dictName == "source_dict" && fSourceNameReg) {
        return fSourceNameReg->GetValue(id);
    }
    if (dictName == "detector_dict" && fDetectorNameReg) {
        return fDetectorNameReg->GetValue(id);
    }
    if (dictName == "touchable_dict" && fTouchablePathReg) {
        return fTouchablePathReg->GetValue(id);
    }
    if (dictName == "chemical_formula_dict" && fChemicalFormulaReg) {
        return fChemicalFormulaReg->GetValue(id);
    }
    if (dictName == "chemical_species_dict" && fChemSpeciesReg) {
        return fChemSpeciesReg->GetValue(id);
    }
    if (dictName == "particle_dict" && fParticleNameReg) {
        return fParticleNameReg->GetValue(id);
    }
    return "";
}