//==============================================================================
//
// G4CARE
//
// @file    TreeManager.cc
// @brief   NTuple tree builder from YAML — column lists, filter expressions,
//          string columns, and ROOT NTuple creation.
//
// @details
//   Reads NTUPLE.<name> YAML blocks and builds per-tree column descriptors
//   (ColType enum with optional expression/filter).  Supports string columns
//   and ExprTK filter expressions.
//
//   Configuration keys read:
//     NTUPLE.<name>.columns, NTUPLE.<name>.filter
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
    {"digit_energy_MeV", ColType::DigitEnergy},
    {"coincidence_id", ColType::CoincidenceID},
    {"raw_time_ns", ColType::RawTime},
    {"pileup_size", ColType::PileupSize},
    {"is_pileup", ColType::IsPileup},
    {"afterpulse", ColType::Afterpulse},
    {"quantum_efficiency", ColType::QuantumEfficiency},
    {"noise_energy_MeV", ColType::NoiseEnergy},
    {"hit_edep_MeV", ColType::HitEdep},
    {"strip_number", ColType::StripNumber},
    {"plane_number", ColType::PlaneNumber},
    {"is_x_plane", ColType::IsXPlane},

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
// String/dimensionless columns have no suffix.
static const std::map<ColType, std::string> colTypeToUnit = {
    // NOTE: raw_energy_MeV / raw_time_ns / noise_energy_MeV already carry their unit
    // in the config name, so RawEnergy/RawTime/NoiseEnergy are intentionally absent
    // from this map (avoids double suffix like raw_energy_MeV_MeV).
    // Energy → MeV
    {ColType::KineticEnergy,      "MeV"},
    {ColType::TotalEnergy,        "MeV"},
    {ColType::PreKineticEnergy,   "MeV"},
    {ColType::PostKineticEnergy,  "MeV"},
    {ColType::Edep,               "MeV"},
    {ColType::DeltaE,             "MeV"},
    {ColType::NIEL,               "MeV"},
    {ColType::Mass,               "MeV_c2"},
    {ColType::VertexKineticEnergy,"MeV"},
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
    {ColType::StepGrammage,"g_cm2"},
    {ColType::MeanFreePath,"mm"},
    {ColType::RadiationLength,"mm"},
    {ColType::NuclearInteractionLength,"mm"},

    // Time → ns
    {ColType::GlobalTime,  "ns"},
    {ColType::LocalTime,   "ns"},
    {ColType::ProperTime,  "ns"},
    {ColType::DeltaTime,   "ns"},
    {ColType::AtRestLifeTime,"ns"},
    {ColType::ParticleLifetime,"ns"},

    // Dosimetry
    {ColType::DoseGy, "Gy"},
    {ColType::LET,    "keV_um"},

    // Optics
    {ColType::OpticalWavelength, "nm"},

    // Volume mass
    {ColType::VolumeMass,  "kg"},

    // Density / temperature / pressure
    {ColType::Density,     "g_cm3"},
    {ColType::Temperature, "K"},
    {ColType::Pressure,    "atm"},

    // Angles
    {ColType::ScatteringAngle, "rad"},

    // Cross-sections → mm² (Geant4 internal units)
    {ColType::NeutronTotalXS,  "mm2"},
    {ColType::NeutronCaptureXS,"mm2"},

    // Particles
    {ColType::Charge, "e"},
    {ColType::AtRestRate, "MeV_ns"},
    {ColType::ParticleWidth, "MeV"},
    {ColType::IsotopeMass, "MeV_c2"},
    {ColType::IsotopeLifetime, "ns"},
};

/// @brief Column types that are stored as string columns.
static const std::unordered_set<ColType> stringColumnTypes = {
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

/// @brief Resolve a column name to ColType / string-ness / ROOT branch name.
/// @param colName    Column name from config (e.g. "kinetic_energy").
/// @param type       [out] Resolved ColType.
/// @param isString   [out] true if this is a string column.
/// @param branchName [out] Branch name including unit suffix (e.g. "kinetic_energy_MeV").
/// @return true if the name is a known built-in column.
bool TreeManager::ResolveColumn(const std::string& colName, ColType& type, bool& isString, std::string& branchName) {
    std::string_view sv(colName);
    auto it = nameToColType.find(sv);
    if (it == nameToColType.end()) return false;
    type = it->second;
    isString = (stringColumnTypes.count(type) != 0);
    branchName = colName;
    if (!isString) {
        auto uIt = colTypeToUnit.find(type);
        if (uIt != colTypeToUnit.end()) { branchName += "_"; branchName += uIt->second; }
    }
    return true;
}
