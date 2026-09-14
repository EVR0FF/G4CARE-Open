//==============================================================================
// G4CARE
// @file    FilterVarRegistry.hh
// @brief   Provides a compile-time name-to-enum map for FilterVar, allowing
//          filtering expression strings to be resolved to FilterVar indices.
// @details Inline functions GetNameToFilterVar(), GetAllFilterVarNames(), and
//   GetAllFilterVarIndices() provide the full bidirectional mapping between
//   human-readable variable names (e.g. "edep", "niel", "kinetic_energy")
//   and the FilterVar enum used in expression evaluation.
//
//   Configuration keys read: none (static registry).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef FILTER_VAR_REGISTRY_HH
#define FILTER_VAR_REGISTRY_HH

#include "ColumnTypes.hh"
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

/// @brief Returns the name-to-FilterVar lookup map.
inline const std::unordered_map<std::string_view, FilterVar>& GetNameToFilterVar() {
    static const std::unordered_map<std::string_view, FilterVar> map = {
        {"event_id", FilterVar::EventID}, {"run_id", FilterVar::RunID},
        {"track_id", FilterVar::TrackID}, {"parent_id", FilterVar::ParentID},
        {"step_number", FilterVar::StepNumber}, {"weight", FilterVar::Weight},
        {"x", FilterVar::PosX}, {"pos_x", FilterVar::PosX},
        {"y", FilterVar::PosY}, {"pos_y", FilterVar::PosY},
        {"z", FilterVar::PosZ}, {"pos_z", FilterVar::PosZ},
        {"global_time", FilterVar::Time},
        {"pdg_code", FilterVar::PDGCode},
        {"energy", FilterVar::KineticEnergy},
        {"px", FilterVar::Px}, {"py", FilterVar::Py}, {"pz", FilterVar::Pz},
        {"mass", FilterVar::Mass}, {"charge", FilterVar::Charge},
        {"edep", FilterVar::Edep}, {"delta_e", FilterVar::DeltaE},
        {"niel", FilterVar::NIEL}, {"dpa", FilterVar::DPA},
        {"kinetic_energy", FilterVar::KineticEnergy},
        {"volume_id", FilterVar::VolumeID}, {"material_id", FilterVar::MaterialID},
        {"process_subtype", FilterVar::ProcessSubType},
        {"step_status", FilterVar::StepStatus}, {"step_length", FilterVar::StepLength},
        {"n_secondaries", FilterVar::NSecondaries},
        {"Z", FilterVar::Z}, {"A", FilterVar::A}, {"excitation", FilterVar::Excitation},
        {"track_length", FilterVar::TrackLength}, {"track_status", FilterVar::TrackStatus},
        {"mean_free_path", FilterVar::MeanFreePath},
        {"at_rest_rate", FilterVar::AtRestRate}, {"at_rest_lifetime", FilterVar::AtRestLifeTime},
        {"smeared_edep", FilterVar::SmearedEdep}, {"visible_edep", FilterVar::VisibleEdep},
        {"dose_gy", FilterVar::DoseGy}, {"let", FilterVar::LET},
        {"step_grammage", FilterVar::StepGrammage},
        {"wavelength", FilterVar::OpticalWavelength},
        {"boundary_status", FilterVar::BoundaryStatus},
        {"safety", FilterVar::Safety},
        {"is_first_step_in_volume", FilterVar::IsFirstStepInVolume},
        {"is_last_step_in_volume", FilterVar::IsLastStepInVolume},
        {"delta_position_x", FilterVar::DeltaPositionX},
        {"delta_position_y", FilterVar::DeltaPositionY},
        {"delta_position_z", FilterVar::DeltaPositionZ},
        {"delta_momentum_x", FilterVar::DeltaMomentumX},
        {"delta_momentum_y", FilterVar::DeltaMomentumY},
        {"delta_momentum_z", FilterVar::DeltaMomentumZ},
        {"vertex_x", FilterVar::VertexX}, {"vertex_y", FilterVar::VertexY},
        {"vertex_z", FilterVar::VertexZ}, {"vertex_energy", FilterVar::VertexKineticEnergy},
        {"pt", FilterVar::Pt}, {"eta", FilterVar::Pseudorapidity}, {"beta", FilterVar::Beta},
        {"density", FilterVar::Density}, {"temperature", FilterVar::Temperature},
        {"pressure", FilterVar::Pressure}, {"state", FilterVar::State},
        {"rad_length", FilterVar::RadiationLength},
        {"nuc_int_length", FilterVar::NuclearInteractionLength},
        {"zeff", FilterVar::Zeff}, {"aeff", FilterVar::Aeff},
        {"isomer_level", FilterVar::IsomerLevel}, {"recoil_type", FilterVar::RecoilType},
        {"particle_spin", FilterVar::ParticleSpin},
        {"particle_parity", FilterVar::ParticleParity},
        {"particle_conjugation", FilterVar::ParticleConjugation},
        {"particle_isospin", FilterVar::ParticleIsospin},
        {"particle_isospin3", FilterVar::ParticleIsospin3},
        {"particle_gparity", FilterVar::ParticleGParity},
        {"particle_lifetime", FilterVar::ParticleLifetime},
        {"particle_width", FilterVar::ParticleWidth},
        {"particle_lepton_number", FilterVar::ParticleLeptonNumber},
        {"particle_baryon_number", FilterVar::ParticleBaryonNumber},
        {"particle_name", FilterVar::ParticleName},
        {"process_name", FilterVar::ProcessName},
        {"volume_mass", FilterVar::VolumeMass},
        // — Chemistry G-values —
        {"g_oh", FilterVar::GOH}, {"g_h", FilterVar::GH},
        {"g_eaq", FilterVar::GEaq}, {"h2o2", FilterVar::H2O2}, {"h2", FilterVar::H2},
        {"radical_oh", FilterVar::RadicalOH}, {"radical_h", FilterVar::RadicalH},
        {"radical_eaq", FilterVar::RadicalEaq},
    };
    return map;
}

/// @brief Returns all filter variable names as strings.
inline const std::vector<std::string>& GetAllFilterVarNames() {
    static std::vector<std::string> names = []{
        std::vector<std::string> n; n.reserve(GetNameToFilterVar().size());
        for (const auto& pair : GetNameToFilterVar()) n.push_back(std::string(pair.first));
        return n;
    }(); return names;
}

/// @brief Returns all FilterVar enum values in the same order as the names.
inline const std::vector<FilterVar>& GetAllFilterVarIndices() {
    static std::vector<FilterVar> indices = []{
        std::vector<FilterVar> idx; idx.reserve(GetNameToFilterVar().size());
        for (const auto& pair : GetNameToFilterVar()) idx.push_back(pair.second);
        return idx;
    }(); return indices;
}

#endif