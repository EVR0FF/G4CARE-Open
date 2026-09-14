//==============================================================================
// G4CARE
// @file    ColumnTypes.hh
// @brief   Enumerates all ntuple column types (ColType) and filter variables
//          (FilterVar) used by the analysis framework.
// @details ColType defines the full set of physical quantities that can be
//   written to output NTuples — identifiers, space/time, kinematics, momentum,
//   vertex, processes, step/track data, nuclear properties, detector effects,
//   dosimetry, geometry/materials, sources/detectors, cross sections, activation,
//   radiochemistry, and expressions.  FilterVar is a reduced subset used for
//   event-level filtering expressions.
//
//   Configuration keys read: none (pure enum definition).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef COLUMN_TYPES_HH
#define COLUMN_TYPES_HH

#include <array>
#include <cstddef>

enum class ColType {
    None,
    // ========== 1. Identifiers ==========
    RunID, EventID, TrackID, ParentID,
    ThreadID, StepNumber,
    Weight,

    // ========== 2. Space and time ==========
    PosX, PosY, PosZ,
    GlobalTime, LocalTime, ProperTime,
    PrePosX, PrePosY, PrePosZ,
    PostPosX, PostPosY, PostPosZ,

    // ========== 3. Kinematics and momentum ==========
    PDGCode, Mass, Charge,
    ParticleName,
    ParticleSpin,
    ParticleParity,
    ParticleConjugation,
    ParticleIsospin,
    ParticleIsospin3,
    ParticleGParity,
    ParticleLifetime,
    ParticleWidth,
    ParticleLeptonNumber,
    ParticleBaryonNumber,
    KineticEnergy, TotalEnergy,
    PreKineticEnergy, PostKineticEnergy,
    Px, Py, Pz,
    DirX, DirY, DirZ,
    DirTheta, DirPhi,
    PrePx, PrePy, PrePz,
    PostPx, PostPy, PostPz,
    Pt, Pseudorapidity, Beta,
    ScatteringAngle,
    PolX, PolY, PolZ,

    // ========== 4. Vertex ==========
    VertexX, VertexY, VertexZ,
    VertexPx, VertexPy, VertexPz,
    VertexKineticEnergy, VertexPDGCode,

    // ========== 5. Processes ==========
    ProcessSubType, ProcessType, ProcessID,
    CreatorProcessSubType, CreatorProcessName,
    StepLimitingProcess, CreatorProcessID,
    ProcessName,

    // ========== 6. Step (hits) ==========
    Edep, DeltaE, NIEL, DPA,
    StepLength, DeltaTime,
    NSecondaries,
    StepStatus, Safety,
    IsFirstStepInVolume, IsLastStepInVolume,
    DeltaPositionX, DeltaPositionY, DeltaPositionZ,
    DeltaMomentumX, DeltaMomentumY, DeltaMomentumZ,

    // ========== 7. Track ==========
    TrackLength, TrackStatus,
    MeanFreePath, AtRestRate, AtRestLifeTime,

    // ========== 8. Nuclear properties ==========
    Z, A, Excitation,
    IsomerLevel, RecoilType,

    // ========== 9. Detector effects and dosimetry ==========
    SmearedEdep, VisibleEdep,
    DoseGy, LET, StepGrammage,
    OpticalWavelength, BoundaryStatus,

    // ========== 10. Volume, material, region ==========
    VolumeID, VolumeName,
    CopyNo, VolumeMass,
    MaterialID, MaterialName,
    Density, Temperature, Pressure, State,
    RadiationLength, NuclearInteractionLength,
    Zeff, Aeff, ChemicalFormula,
    RegionID, RegionName,
    TouchablePath,

    // ========== 11. Sources and detectors ==========
    SourceName, DetectorName, DetectorID, DigitType,
    RawEnergy,
    RawTime,
    PileupSize,
    IsPileup,
    Afterpulse,
    QuantumEfficiency,
    NoiseEnergy,

    // ========== 12. Cross sections ==========
    NeutronTotalXS, NeutronCaptureXS, NeutronElasticXS,
    NeutronInelasticXS, NeutronFissionXS, NeutronThermalScatteringXS,
    PhotonTotalXS, PhotonPhotoElectricXS, PhotonComptonXS,
    PhotonConversionXS, PhotonRayleighXS, PhotonNuclearXS, PhotonMuonPairXS,
    ElectronIonisationXS, ElectronBremsstrahlungXS,
    ElectronExcitationXS, ElectronElasticXS,
    PositronIonisationXS, PositronBremsstrahlungXS, PositronAnnihilationXS,
    MuonIonisationXS, MuonBremsstrahlungXS, MuonPairProductionXS, MuonNuclearXS,
    ProtonTotalXS, ProtonElasticXS, ProtonInelasticXS,
    IonIonisationXS, IonInelasticXS, IonElasticXS,

    // ========== 13. Activation ==========

    IsotopePDG,
    IsotopeCount,
    IsotopeMass,
    IsotopeLifetime,

    // ========== 14. Radiochemistry / DNA ==========
    RadicalOH,
    RadicalH,
    RadicalEaq,
    H2O2,
    H2,
    GOH,
    GH,
    GEaq,
    WaterLoss,
    PorosityChange,
    CompressiveStrengthLoss,
    SpeciesID,
    
    // ========== 15. Expressions ==========
    Expression
};

enum class FilterVar : size_t {
    // Common identifiers
    EventID, RunID,
    TrackID, ParentID,
    StepNumber,
    Weight,
    // Space and time
    PosX, PosY, PosZ,
    Time, // global time
    // Kinematics
    PDGCode, Mass, Charge,
    KineticEnergy,
    Px, Py, Pz,
    Pt, Pseudorapidity, Beta,
    ScatteringAngle,
    // Vertex
    VertexX, VertexY, VertexZ,
    VertexKineticEnergy,VertexPDGCode,
    // Processes
    ProcessSubType,
    CreatorProcessSubType, CreatorProcessID,
    // Step
    Edep, DeltaE, NIEL, DPA,
    StepLength,
    NSecondaries,
    StepStatus,
    Safety,
    IsFirstStepInVolume, IsLastStepInVolume,
    DeltaPositionX, DeltaPositionY, DeltaPositionZ,
    DeltaMomentumX, DeltaMomentumY, DeltaMomentumZ,
    PrePx, PrePy, PrePz,
    PostPx, PostPy, PostPz,
    // Track
    TrackLength, TrackStatus,
    MeanFreePath, AtRestRate, AtRestLifeTime,
    // Nuclear
    Z, A, Excitation,
    IsomerLevel, RecoilType,
    // Detector
    SmearedEdep, VisibleEdep,
    DoseGy, LET, StepGrammage,
    OpticalWavelength, BoundaryStatus, DetectorID,
    // Material
    MaterialID, VolumeID,
    Density, Temperature, Pressure, State,
    RadiationLength, NuclearInteractionLength,
    Zeff, Aeff,
    // Particle quantum numbers
    ParticleSpin,
    ParticleParity,
    ParticleConjugation,
    ParticleIsospin,
    ParticleIsospin3,
    ParticleGParity,
    ParticleLifetime,
    ParticleWidth,
    ParticleLeptonNumber,
    ParticleBaryonNumber,
    ParticleName,
    GOH, GH, GEaq, H2O2, H2,
    RadicalOH, RadicalH, RadicalEaq,
    ProcessName,
    VolumeMass,
    Count
};

using FilterVars = std::array<double, static_cast<size_t>(FilterVar::Count)>;

#endif