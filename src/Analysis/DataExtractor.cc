//==============================================================================
//
// G4CARE
//
// @file    DataExtractor.cc
// @brief   Extracts FilterVar values from Geant4 step, track, and vertex data.
//
// @details
//   Implements the main data extraction dispatcher used by BeamAnalysis::Fill().
//   Provides three access levels:
//   1. GetDouble() — retrieves a single numeric value by ColType enum,
//      delegating to specialised sub-extractors (KinematicsExtractor,
//      StepExtractor, TrackExtractor, ProcessExtractor, ParticleExtractor,
//      MaterialExtractor, GeometryExtractor, CrossSectionExtractor,
//      ChemistryExtractor, DetectorEffectsExtractor).
//   2. GetString() — retrieves a string value (volume name, material name,
//      particle name, process name).
//   3. BuildFilterVars() — populates a FilterVars array for a given
//      UnifiedSource, either fully (all variables) or selectively (only
//      the variables needed by the current NTuple filter/expression columns).
//
//   Sub-extractors are constructed once and reused for all GetDouble calls
//   within a single step/track.
//
//   Configuration keys read: none (uses registries populated by BeamAnalysis).
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

#include "DataExtractor.hh"
#include "BeamAnalysis.hh"
#include "G4RunManager.hh"
#include "G4Event.hh"
#include "ConfigManager.hh"
#include "GeometryExtractor.hh"
#include "CrossSectionExtractor.hh"
#include "KinematicsExtractor.hh"
#include "StepExtractor.hh"
#include "DetectorEffectsExtractor.hh"
#include "ProcessExtractor.hh"
#include "ProcessUtils.hh"
#include "ParticleExtractor.hh"

/// @brief Construct the DataExtractor with references to all helper sub-extractors.
///
/// Sub-extractors (KinematicsExtractor, StepExtractor, TrackExtractor,
/// ProcessExtractor, ParticleExtractor, MaterialExtractor, GeometryExtractor,
/// CrossSectionExtractor, ChemistryExtractor, DetectorEffectsExtractor) are
/// value members initialised in-place.  The constructor only stores external
/// pointers and references.
///
/// @param cache             Thread-local cache for run/event IDs.
/// @param xsCalc            Cross-section calculator (owned by BeamAnalysis).
/// @param registry          Volume-material registry for material/lookup.
/// @param particleNameReg   Typed registry for particle name strings.
/// @param volumeNameReg     Typed registry for volume name strings.
/// @param materialNameReg   Typed registry for material name strings.
/// @param regionNameReg     Typed registry for region name strings.
/// @param sourceNameReg     Typed registry for source name strings.
/// @param detectorNameReg   Typed registry for detector name strings.
/// @param touchablePathReg  Typed registry for touchable path strings.
/// @param chemicalFormulaReg Typed registry for chemical formula strings.
/// @param pdgReg            Typed registry for PDG code → int mapping.
/// @param massReg           Typed registry for particle mass values.
/// @param chargeReg         Typed registry for particle charge values.
/// @param chemSpeciesReg    Typed registry for chemical species names.
DataExtractor::DataExtractor(ThreadCache& cache,
                             CrossSectionCalculator* xsCalc,
                             VolumeMaterialRegistry* registry,
                             TypedRegistry<std::string>* particleNameReg,
                             TypedRegistry<std::string>* volumeNameReg,
                             TypedRegistry<std::string>* materialNameReg,
                             TypedRegistry<std::string>* regionNameReg,
                             TypedRegistry<std::string>* sourceNameReg,
                             TypedRegistry<std::string>* detectorNameReg,
                             TypedRegistry<std::string>* touchablePathReg,
                             TypedRegistry<std::string>* chemicalFormulaReg,
                             TypedRegistry<int>* pdgReg,
                             TypedRegistry<double>* massReg,
                             TypedRegistry<double>* chargeReg,
                             TypedRegistry<std::string>* chemSpeciesReg)
    : fCache(cache),
      fXSCalculator(xsCalc),
      fRegistry(registry),
      fKinematics(),
      fStep(),
      fTrack(),
      fProcess(),
      fParticle(particleNameReg, pdgReg, massReg, chargeReg),
      fMaterial(registry, materialNameReg, chemicalFormulaReg),
      fGeometry(registry, volumeNameReg, regionNameReg, touchablePathReg),
      fCrossSection(xsCalc),
      fChemistry(chemSpeciesReg),
      fChemSpeciesReg(chemSpeciesReg),
      fDetectorEffects()
{
}

/// @brief Retrieve a single numeric value from the given source by ColType.
///
/// Dispatches to the appropriate sub-extractor based on the column type
/// category.  The switch statement groups ColType values by functional
/// domain: identifiers, kinematics, step properties, detector effects,
/// track properties, processes, particle properties, material properties,
/// geometry/volume information, cross-sections, chemistry (G-values),
/// and digit-specific values.
///
/// @param type    Column type enum (see ColumnTypes.hh).
/// @param src     Unified source wrapping G4Step, G4Track, or G4PrimaryVertex.
/// @param eventId Current event ID (-1 to auto-detect from G4RunManager).
/// @return        Numeric value in Geant4 internal units.
double DataExtractor::GetDouble(ColType type, const UnifiedSource& src, int eventId) const {
    switch (type) {
        // ========== General identifiers ==========
        case ColType::RunID:
            return static_cast<double>(fCache.currentRunId);
        case ColType::EventID: {
            const G4Event* evt = G4RunManager::GetRunManager()->GetCurrentEvent();
            return static_cast<double>(evt ? evt->GetEventID() : -1);
        }
        case ColType::TrackID:
            return static_cast<double>(src.GetTrackID());
        case ColType::ParentID:
            return static_cast<double>(src.GetParentID());
        case ColType::ThreadID:
            return static_cast<double>(G4Threading::G4GetThreadId());
        case ColType::Weight:
            return src.GetWeight();
        case ColType::Afterpulse:
            if (const Digit* dig = src.GetDigit())
                return dig->HasAfterpulse() ? 1.0 : 0.0;
            return 0.0;

        // ========== Position (from UnifiedSource) ==========
        case ColType::PosX: return src.GetPosition().x();
        case ColType::PosY: return src.GetPosition().y();
        case ColType::PosZ: return src.GetPosition().z();

        // ========== Kinematics ==========
        case ColType::KineticEnergy:
        case ColType::TotalEnergy:
        case ColType::Px: case ColType::Py: case ColType::Pz:
        case ColType::DirX: case ColType::DirY: case ColType::DirZ:
        case ColType::DirTheta: case ColType::DirPhi:
        case ColType::PrePx: case ColType::PrePy: case ColType::PrePz:
        case ColType::PostPx: case ColType::PostPy: case ColType::PostPz:
        case ColType::Pt:
        case ColType::Pseudorapidity:
        case ColType::Beta:
        case ColType::ScatteringAngle:
        case ColType::PolX: case ColType::PolY: case ColType::PolZ:
            return fKinematics.Get(type, src);

        // ========== Step (basic geometric and energy parameters) ==========
        case ColType::Edep:
        case ColType::DeltaE:
        case ColType::NIEL:
        case ColType::DPA:
        case ColType::StepLength:
        case ColType::DeltaTime:
        case ColType::NSecondaries:
        case ColType::StepStatus:
        case ColType::Safety:
        case ColType::IsFirstStepInVolume:
        case ColType::IsLastStepInVolume:
        case ColType::DeltaPositionX: case ColType::DeltaPositionY: case ColType::DeltaPositionZ:
        case ColType::DeltaMomentumX: case ColType::DeltaMomentumY: case ColType::DeltaMomentumZ:
        case ColType::BoundaryStatus:
        case ColType::StepNumber:
            return fStep.Get(type, src, eventId, -1, -1);

        // ========== Detector effects ==========
        case ColType::SmearedEdep:
        case ColType::VisibleEdep:
        case ColType::DoseGy:
        case ColType::LET:
        case ColType::StepGrammage:
        case ColType::OpticalWavelength:
            return fDetectorEffects.Get(type, src);

        // ========== Track (secondary only) ==========
        case ColType::TrackLength:
        case ColType::TrackStatus:
        case ColType::VertexX: case ColType::VertexY: case ColType::VertexZ:
        case ColType::VertexKineticEnergy:
        case ColType::VertexPDGCode:
        case ColType::MeanFreePath:
        case ColType::AtRestRate:
        case ColType::AtRestLifeTime:
            return fTrack.Get(type, src);

        // ========== Processes ==========
        case ColType::ProcessSubType:
        case ColType::ProcessType:
        case ColType::ProcessID:
        case ColType::CreatorProcessSubType:
        case ColType::CreatorProcessID:
        case ColType::StepLimitingProcess:
            return fProcess.Get(type, src);

        // ========== Particle properties ==========
        case ColType::PDGCode:
        case ColType::ParticleName:
        case ColType::ParticleSpin:
        case ColType::ParticleParity:
        case ColType::ParticleConjugation:
        case ColType::ParticleIsospin:
        case ColType::ParticleIsospin3:
        case ColType::ParticleGParity:
        case ColType::ParticleLifetime:
        case ColType::ParticleWidth:
        case ColType::ParticleLeptonNumber:
        case ColType::ParticleBaryonNumber:
        case ColType::Mass:
        case ColType::Charge:
        case ColType::Z:
        case ColType::A:
        case ColType::Excitation:
        case ColType::IsomerLevel:
        case ColType::RecoilType:
            return fParticle.Get(type, src);

        // ========== Material ==========
        case ColType::MaterialID:
        case ColType::MaterialName:
        case ColType::Density:
        case ColType::Temperature:
        case ColType::Pressure:
        case ColType::State:
        case ColType::RadiationLength:
        case ColType::NuclearInteractionLength:
        case ColType::Zeff:
        case ColType::Aeff:
        case ColType::ChemicalFormula: {
            auto* mat = src.GetMaterial();
            int matId = mat ? fRegistry->GetMaterialID(mat) : -1;
            return fMaterial.Get(type, src, matId);
        }
        case ColType::SpeciesID: {
            std::string name = src.GetChemicalSpeciesName();
            if (!name.empty() && fChemSpeciesReg) {
                return static_cast<double>(fChemSpeciesReg->GetID(name));
            }
            return 0.0;
        }

        // ========== Geometry ==========
        case ColType::VolumeID:
        case ColType::VolumeName:
        case ColType::VolumeMass: {
            auto* lv = src.GetLogicalVolume();
            if (lv && lv->GetSolid()) {
                auto* mat = lv->GetMaterial();
                double dens = mat ? mat->GetDensity() / (CLHEP::g/CLHEP::cm3) : 1.0;
                double vol_cm3 = lv->GetSolid()->GetCubicVolume() / CLHEP::cm3;
                return dens * vol_cm3 / 1000.0;  // kg
            }
            return 0.0;
        }
        case ColType::CopyNo:
        case ColType::RegionID:
        case ColType::RegionName:
        case ColType::TouchablePath: {
            auto* lv = src.GetLogicalVolume();
            int volId = lv ? fRegistry->GetVolumeID(lv) : -1;
            return fGeometry.Get(type, src, volId);
        }

        // ========== Cross-sections ==========
        case ColType::NeutronCaptureXS:
        case ColType::NeutronElasticXS:
        case ColType::NeutronInelasticXS:
        case ColType::NeutronFissionXS:
        case ColType::NeutronTotalXS:
        case ColType::NeutronThermalScatteringXS:
        case ColType::PhotonTotalXS:
        case ColType::PhotonPhotoElectricXS:
        case ColType::PhotonComptonXS:
        case ColType::PhotonConversionXS:
        case ColType::PhotonRayleighXS:
        case ColType::PhotonNuclearXS:
        case ColType::PhotonMuonPairXS:
        case ColType::ElectronIonisationXS:
        case ColType::ElectronBremsstrahlungXS:
        case ColType::ElectronExcitationXS:
        case ColType::ElectronElasticXS:
        case ColType::PositronIonisationXS:
        case ColType::PositronBremsstrahlungXS:
        case ColType::PositronAnnihilationXS:
        case ColType::MuonIonisationXS:
        case ColType::MuonBremsstrahlungXS:
        case ColType::MuonPairProductionXS:
        case ColType::MuonNuclearXS:
        case ColType::ProtonTotalXS:
        case ColType::ProtonElasticXS:
        case ColType::ProtonInelasticXS:
        case ColType::IonIonisationXS:
        case ColType::IonInelasticXS:
        case ColType::IonElasticXS:
            return fCrossSection.Get(type, src);

        // ========== Chemistry (G-values, yields) ==========
        case ColType::RadicalOH:
        case ColType::RadicalH:
        case ColType::RadicalEaq:
        case ColType::H2O2:
        case ColType::H2:
        case ColType::GOH:
        case ColType::GH:
        case ColType::GEaq:
        case ColType::WaterLoss:
        case ColType::PorosityChange:
        case ColType::CompressiveStrengthLoss:
            return fChemistry.Get(type, src);

        // ========== Digit-specific fields ==========
        if (src.GetType() == UnifiedSource::Type::Digit) {
            const Digit* dig = src.GetDigit();
            if (dig) {
                switch (type) {
                    case ColType::RawEnergy: return dig->GetRawEnergy() / MeV;
                    case ColType::RawTime: return dig->GetRawTime() / ns;
                    case ColType::PileupSize: return static_cast<double>(dig->GetPileupSize());
                    case ColType::IsPileup: return dig->IsPileup() ? 1.0 : 0.0;
                    case ColType::Afterpulse: return dig->HasAfterpulse() ? 1.0 : 0.0;
                    case ColType::QuantumEfficiency: return dig->GetQuantumEfficiency();
                    case ColType::NoiseEnergy: return dig->GetNoiseEnergy() / MeV;
                    case ColType::DigitType: return static_cast<double>(dig->GetDigitType());
                    case ColType::DetectorID: return static_cast<double>(dig->GetDetectorID());
                    default: break;
                }
            }
        }

        default:
            return 0.0;
    }
}

/// @brief Retrieve a string value from the given source by ColType.
///
/// Dispatches to geometry (volume/region/touchable-path names),
/// material (name, chemical formula), particle (name), and process
/// (name, creator name, step-limiting process) extractors.
///
/// @param type  Column type enum.
/// @param src   Unified source.
/// @return      Human-readable string, or empty string if not applicable.
std::string DataExtractor::GetString(ColType type, const UnifiedSource& src) const {
    switch (type) {
        // Geometry
        case ColType::VolumeName:
        case ColType::RegionName:
        case ColType::TouchablePath:
            return fGeometry.GetString(type, src);

        // Material
        case ColType::MaterialName:
        case ColType::ChemicalFormula:
            return fMaterial.GetString(type, src);

        // Particle
        case ColType::ParticleName:
            return fParticle.GetString(type, src);

        // Process
        case ColType::ProcessName:
        case ColType::CreatorProcessName:
        case ColType::StepLimitingProcess:
            return fProcess.GetString(type, src);

        default:
            return "";
    }
}

/// @brief Populate the FilterVars array with ALL possible variables from the source.
///
/// This is the fast path used when no variable-whitelist is supplied.
/// It fills every FilterVar slot in a single pass, grouped by category:
/// identifiers, kinematics, quantum numbers, track IDs, vertex parameters,
/// step properties, detector effects, ion properties (Z/A/excitation),
/// chemistry G-values, and volume mass.
///
/// @param vars     FilterVars array to fill (size = FilterVar::NUM_VARS).
/// @param src      Unified source (step, track, or vertex).
/// @param eventId  Current event ID.
/// @param volId    Pre-computed volume ID (from registry cache).
/// @param matId    Pre-computed material ID (from registry cache).
void DataExtractor::_BuildFilterVarsFull(FilterVars& vars, const UnifiedSource& src,
                                         int eventId, int volId, int matId) const {
    // General identifiers
    vars[static_cast<size_t>(FilterVar::EventID)] = (src.GetType() == UnifiedSource::Type::Hit)
        ? static_cast<double>(eventId) : []() -> double {
            const G4Event* evt = G4RunManager::GetRunManager()->GetCurrentEvent();
            return static_cast<double>(evt ? evt->GetEventID() : -1);
        }();
    vars[static_cast<size_t>(FilterVar::RunID)] = static_cast<double>(fCache.currentRunId);
    vars[static_cast<size_t>(FilterVar::Weight)] = src.GetWeight();

    // Kinematics
    vars[static_cast<size_t>(FilterVar::PDGCode)] = fParticle.Get(ColType::PDGCode, src);
    vars[static_cast<size_t>(FilterVar::KineticEnergy)] = fKinematics.Get(ColType::KineticEnergy, src);
    vars[static_cast<size_t>(FilterVar::Mass)] = fParticle.Get(ColType::Mass, src);
    vars[static_cast<size_t>(FilterVar::Charge)] = fParticle.Get(ColType::Charge, src);
    vars[static_cast<size_t>(FilterVar::PosX)] = fKinematics.Get(ColType::PosX, src);
    vars[static_cast<size_t>(FilterVar::PosY)] = fKinematics.Get(ColType::PosY, src);
    vars[static_cast<size_t>(FilterVar::PosZ)] = fKinematics.Get(ColType::PosZ, src);
    vars[static_cast<size_t>(FilterVar::Time)] = fKinematics.Get(ColType::GlobalTime, src);
    vars[static_cast<size_t>(FilterVar::Px)] = fKinematics.Get(ColType::Px, src);
    vars[static_cast<size_t>(FilterVar::Py)] = fKinematics.Get(ColType::Py, src);
    vars[static_cast<size_t>(FilterVar::Pz)] = fKinematics.Get(ColType::Pz, src);
    vars[static_cast<size_t>(FilterVar::Pt)] = fKinematics.Get(ColType::Pt, src);
    vars[static_cast<size_t>(FilterVar::Pseudorapidity)] = fKinematics.Get(ColType::Pseudorapidity, src);
    vars[static_cast<size_t>(FilterVar::Beta)] = fKinematics.Get(ColType::Beta, src);
    vars[static_cast<size_t>(FilterVar::ScatteringAngle)] = fKinematics.Get(ColType::ScatteringAngle, src);

    // Particle quantum numbers
    vars[static_cast<size_t>(FilterVar::ParticleSpin)] = fParticle.Get(ColType::ParticleSpin, src);
    vars[static_cast<size_t>(FilterVar::ParticleParity)] = fParticle.Get(ColType::ParticleParity, src);
    vars[static_cast<size_t>(FilterVar::ParticleConjugation)] = fParticle.Get(ColType::ParticleConjugation, src);
    vars[static_cast<size_t>(FilterVar::ParticleIsospin)] = fParticle.Get(ColType::ParticleIsospin, src);
    vars[static_cast<size_t>(FilterVar::ParticleIsospin3)] = fParticle.Get(ColType::ParticleIsospin3, src);
    vars[static_cast<size_t>(FilterVar::ParticleGParity)] = fParticle.Get(ColType::ParticleGParity, src);
    vars[static_cast<size_t>(FilterVar::ParticleLifetime)] = fParticle.Get(ColType::ParticleLifetime, src);
    vars[static_cast<size_t>(FilterVar::ParticleWidth)] = fParticle.Get(ColType::ParticleWidth, src);
    vars[static_cast<size_t>(FilterVar::ParticleLeptonNumber)] = fParticle.Get(ColType::ParticleLeptonNumber, src);
    vars[static_cast<size_t>(FilterVar::ParticleBaryonNumber)] = fParticle.Get(ColType::ParticleBaryonNumber, src);

    // Track identifiers (only for non-primary sources)
    if (src.GetType() != UnifiedSource::Type::Primary) {
        vars[static_cast<size_t>(FilterVar::TrackID)] = static_cast<double>(src.GetTrackID());
        vars[static_cast<size_t>(FilterVar::ParentID)] = static_cast<double>(src.GetParentID());
        vars[static_cast<size_t>(FilterVar::VolumeID)] = static_cast<double>(volId);
        auto* proc = src.GetCreatorProcess();
        vars[static_cast<size_t>(FilterVar::ProcessSubType)] = proc ? static_cast<double>(proc->GetProcessSubType()) : 0.0;

        // Vertex parameters
        vars[static_cast<size_t>(FilterVar::VertexX)] = fTrack.Get(ColType::VertexX, src);
        vars[static_cast<size_t>(FilterVar::VertexY)] = fTrack.Get(ColType::VertexY, src);
        vars[static_cast<size_t>(FilterVar::VertexZ)] = fTrack.Get(ColType::VertexZ, src);
        vars[static_cast<size_t>(FilterVar::VertexKineticEnergy)] = fTrack.Get(ColType::VertexKineticEnergy, src);
        vars[static_cast<size_t>(FilterVar::VertexPDGCode)] = fTrack.Get(ColType::VertexPDGCode, src);
        vars[static_cast<size_t>(FilterVar::TrackLength)] = fTrack.Get(ColType::TrackLength, src);
        vars[static_cast<size_t>(FilterVar::TrackStatus)] = fTrack.Get(ColType::TrackStatus, src);
        vars[static_cast<size_t>(FilterVar::MeanFreePath)] = fTrack.Get(ColType::MeanFreePath, src);
        vars[static_cast<size_t>(FilterVar::AtRestRate)] = fTrack.Get(ColType::AtRestRate, src);
        vars[static_cast<size_t>(FilterVar::AtRestLifeTime)] = fTrack.Get(ColType::AtRestLifeTime, src);
    } else {
        // Default values for primary particles
        vars[static_cast<size_t>(FilterVar::TrackID)] = 0.0;
        vars[static_cast<size_t>(FilterVar::ParentID)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VolumeID)] = -1.0;
        vars[static_cast<size_t>(FilterVar::ProcessSubType)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VertexX)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VertexY)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VertexZ)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VertexKineticEnergy)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VertexPDGCode)] = 0.0;
        vars[static_cast<size_t>(FilterVar::TrackLength)] = 0.0;
        vars[static_cast<size_t>(FilterVar::TrackStatus)] = 0.0;
        vars[static_cast<size_t>(FilterVar::MeanFreePath)] = 0.0;
        vars[static_cast<size_t>(FilterVar::AtRestRate)] = 0.0;
        vars[static_cast<size_t>(FilterVar::AtRestLifeTime)] = 0.0;
    }

    // Step properties (only for Hit sources)
    if (src.GetType() == UnifiedSource::Type::Hit) {
        vars[static_cast<size_t>(FilterVar::Edep)] = fStep.Get(ColType::Edep, src, eventId, volId, matId);
        vars[static_cast<size_t>(FilterVar::DeltaE)] = fStep.Get(ColType::DeltaE, src, eventId, volId, matId);
        vars[static_cast<size_t>(FilterVar::KineticEnergy)] = fKinematics.Get(ColType::KineticEnergy, src);
        vars[static_cast<size_t>(FilterVar::MaterialID)] = static_cast<double>(matId);
        vars[static_cast<size_t>(FilterVar::StepLength)] = fStep.Get(ColType::StepLength, src, eventId, volId, matId);
        vars[static_cast<size_t>(FilterVar::NSecondaries)] = fStep.Get(ColType::NSecondaries, src, eventId, volId, matId);
        vars[static_cast<size_t>(FilterVar::StepStatus)] = fStep.Get(ColType::StepStatus, src, eventId, volId, matId);
        vars[static_cast<size_t>(FilterVar::PrePx)] = fKinematics.Get(ColType::PrePx, src);
        vars[static_cast<size_t>(FilterVar::PrePy)] = fKinematics.Get(ColType::PrePy, src);
        vars[static_cast<size_t>(FilterVar::PrePz)] = fKinematics.Get(ColType::PrePz, src);
        vars[static_cast<size_t>(FilterVar::PostPx)] = fKinematics.Get(ColType::PostPx, src);
        vars[static_cast<size_t>(FilterVar::PostPy)] = fKinematics.Get(ColType::PostPy, src);
        vars[static_cast<size_t>(FilterVar::PostPz)] = fKinematics.Get(ColType::PostPz, src);
        vars[static_cast<size_t>(FilterVar::StepNumber)] = fStep.Get(ColType::StepNumber, src, eventId, volId, matId);

        // Detector effects
        vars[static_cast<size_t>(FilterVar::SmearedEdep)] = fDetectorEffects.Get(ColType::SmearedEdep, src);
        vars[static_cast<size_t>(FilterVar::VisibleEdep)] = fDetectorEffects.Get(ColType::VisibleEdep, src);
        vars[static_cast<size_t>(FilterVar::DoseGy)] = fDetectorEffects.Get(ColType::DoseGy, src);
        vars[static_cast<size_t>(FilterVar::LET)] = fDetectorEffects.Get(ColType::LET, src);
        vars[static_cast<size_t>(FilterVar::StepGrammage)] = fDetectorEffects.Get(ColType::StepGrammage, src);
        vars[static_cast<size_t>(FilterVar::BoundaryStatus)] = fDetectorEffects.Get(ColType::BoundaryStatus, src);
        vars[static_cast<size_t>(FilterVar::OpticalWavelength)] = fDetectorEffects.Get(ColType::OpticalWavelength, src);
        // VolumeMass: compute from logical volume solid + material density
        {
            auto* lv = src.GetLogicalVolume();
            if (lv && lv->GetSolid()) {
                auto* mat = lv->GetMaterial();
                double dens = mat ? mat->GetDensity()/(CLHEP::g/CLHEP::cm3) : 1.0;
                double vol = lv->GetSolid()->GetCubicVolume()/CLHEP::cm3;
                vars[static_cast<size_t>(FilterVar::VolumeMass)] = dens * vol / 1000.0;
            } else vars[static_cast<size_t>(FilterVar::VolumeMass)] = 0.0;
        }
    } else {
        // Default values for non-Hit sources
        vars[static_cast<size_t>(FilterVar::Edep)] = 0.0;
        vars[static_cast<size_t>(FilterVar::DeltaE)] = 0.0;
        vars[static_cast<size_t>(FilterVar::KineticEnergy)] = 0.0;
        vars[static_cast<size_t>(FilterVar::MaterialID)] = -1.0;
        vars[static_cast<size_t>(FilterVar::StepLength)] = 0.0;
        vars[static_cast<size_t>(FilterVar::NSecondaries)] = 0.0;
        vars[static_cast<size_t>(FilterVar::StepStatus)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PrePx)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PrePy)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PrePz)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PostPx)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PostPy)] = 0.0;
        vars[static_cast<size_t>(FilterVar::PostPz)] = 0.0;
        vars[static_cast<size_t>(FilterVar::StepNumber)] = 0.0;
        vars[static_cast<size_t>(FilterVar::SmearedEdep)] = 0.0;
        vars[static_cast<size_t>(FilterVar::VisibleEdep)] = 0.0;
        vars[static_cast<size_t>(FilterVar::DoseGy)] = 0.0;
        vars[static_cast<size_t>(FilterVar::LET)] = 0.0;
        vars[static_cast<size_t>(FilterVar::StepGrammage)] = 0.0;
        vars[static_cast<size_t>(FilterVar::BoundaryStatus)] = 0.0;
        vars[static_cast<size_t>(FilterVar::OpticalWavelength)] = 0.0;
    }

    // Z, A, Excitation for ions
    auto* def = src.GetParticleDefinition();
    if (def && def->IsGeneralIon()) {
        vars[static_cast<size_t>(FilterVar::Z)] = static_cast<double>(def->GetAtomicNumber());
        vars[static_cast<size_t>(FilterVar::A)] = static_cast<double>(def->GetAtomicMass());
        if (auto* ion = dynamic_cast<const G4Ions*>(def))
            vars[static_cast<size_t>(FilterVar::Excitation)] = ion->GetExcitationEnergy() / MeV;
        } else {
        vars[static_cast<size_t>(FilterVar::Z)] = 0.0;
        vars[static_cast<size_t>(FilterVar::A)] = 0.0;
        vars[static_cast<size_t>(FilterVar::Excitation)] = 0.0;
    }
    // Chemistry G-values
    vars[static_cast<size_t>(FilterVar::GOH)] = fChemistry.Get(ColType::GOH, src);
    vars[static_cast<size_t>(FilterVar::GH)] = fChemistry.Get(ColType::GH, src);
    vars[static_cast<size_t>(FilterVar::GEaq)] = fChemistry.Get(ColType::GEaq, src);
    vars[static_cast<size_t>(FilterVar::H2O2)] = fChemistry.Get(ColType::H2O2, src);
    vars[static_cast<size_t>(FilterVar::H2)] = fChemistry.Get(ColType::H2, src);
    vars[static_cast<size_t>(FilterVar::RadicalOH)] = fChemistry.Get(ColType::RadicalOH, src);
    vars[static_cast<size_t>(FilterVar::RadicalH)] = fChemistry.Get(ColType::RadicalH, src);
    vars[static_cast<size_t>(FilterVar::RadicalEaq)] = fChemistry.Get(ColType::RadicalEaq, src);
}

/// @brief Fill a single FilterVar slot from the source.
///
/// Used when a variable whitelist (neededVars) is supplied by the caller.
/// Only the requested FilterVar is extracted, minimising overhead for
/// NTuple trees that use a small subset of the available variables.
///
/// @param vars     FilterVars array (modified in-place).
/// @param fv       Which FilterVar to fill.
/// @param src      Unified source.
/// @param eventId  Current event ID.
/// @param volId    Pre-computed volume ID.
/// @param matId    Pre-computed material ID.
void DataExtractor::_FillSingleVar(FilterVars& vars, FilterVar fv, const UnifiedSource& src,
                                    int eventId, int volId, int matId) const {
    size_t idx = static_cast<size_t>(fv);
    if (idx >= vars.size()) return;

    switch (fv) {
        // General identifiers
        case FilterVar::EventID:
            vars[idx] = (src.GetType() == UnifiedSource::Type::Hit)
                ? static_cast<double>(eventId) : []() -> double {
                    const G4Event* evt = G4RunManager::GetRunManager()->GetCurrentEvent();
                    return static_cast<double>(evt ? evt->GetEventID() : -1);
                }();
            break;
        case FilterVar::RunID: vars[idx] = static_cast<double>(fCache.currentRunId); break;
        case FilterVar::TrackID: vars[idx] = static_cast<double>(src.GetTrackID()); break;
        case FilterVar::ParentID: vars[idx] = static_cast<double>(src.GetParentID()); break;
        case FilterVar::StepNumber: vars[idx] = static_cast<double>(src.GetStepNumber()); break;
        case FilterVar::Weight: vars[idx] = src.GetWeight(); break;

        // Kinematics
        case FilterVar::PDGCode: vars[idx] = fParticle.Get(ColType::PDGCode, src); break;
        case FilterVar::Mass: vars[idx] = fParticle.Get(ColType::Mass, src); break;
        case FilterVar::Charge: vars[idx] = fParticle.Get(ColType::Charge, src); break;
        case FilterVar::KineticEnergy: vars[idx] = fKinematics.Get(ColType::KineticEnergy, src); break;
        case FilterVar::Px: vars[idx] = fKinematics.Get(ColType::Px, src); break;
        case FilterVar::Py: vars[idx] = fKinematics.Get(ColType::Py, src); break;
        case FilterVar::Pz: vars[idx] = fKinematics.Get(ColType::Pz, src); break;
        case FilterVar::Pt: vars[idx] = fKinematics.Get(ColType::Pt, src); break;
        case FilterVar::Pseudorapidity: vars[idx] = fKinematics.Get(ColType::Pseudorapidity, src); break;
        case FilterVar::Beta: vars[idx] = fKinematics.Get(ColType::Beta, src); break;
        case FilterVar::ScatteringAngle: vars[idx] = fKinematics.Get(ColType::ScatteringAngle, src); break;

        // Position and time
        case FilterVar::PosX: vars[idx] = fKinematics.Get(ColType::PosX, src); break;
        case FilterVar::PosY: vars[idx] = fKinematics.Get(ColType::PosY, src); break;
        case FilterVar::PosZ: vars[idx] = fKinematics.Get(ColType::PosZ, src); break;
        case FilterVar::Time: vars[idx] = fKinematics.Get(ColType::GlobalTime, src); break;

        // Geometry
        case FilterVar::VolumeID: vars[idx] = static_cast<double>(volId); break;
        case FilterVar::MaterialID: vars[idx] = static_cast<double>(matId); break;

        // Processes
        case FilterVar::ProcessSubType: {
            auto* proc = src.GetCreatorProcess();
            vars[idx] = proc ? static_cast<double>(proc->GetProcessSubType()) : 0.0;
            break;
        }
        case FilterVar::CreatorProcessSubType: {
            auto* proc = src.GetCreatorProcess();
            vars[idx] = proc ? static_cast<double>(proc->GetProcessSubType()) : 0.0;
            break;
        }
        case FilterVar::CreatorProcessID: {
            auto* proc = src.GetCreatorProcess();
            vars[idx] = proc ? static_cast<double>(ProcessUtils::PackProcessID(*proc)) : 0.0;
            break;
        }

        // Step
        case FilterVar::Edep: vars[idx] = fStep.Get(ColType::Edep, src, eventId, volId, matId); break;
        case FilterVar::DeltaE: vars[idx] = fStep.Get(ColType::DeltaE, src, eventId, volId, matId); break;
        case FilterVar::NIEL: vars[idx] = fStep.Get(ColType::NIEL, src, eventId, volId, matId); break;
        case FilterVar::DPA: vars[idx] = fStep.Get(ColType::DPA, src, eventId, volId, matId); break;
        case FilterVar::StepLength: vars[idx] = fStep.Get(ColType::StepLength, src, eventId, volId, matId); break;
        case FilterVar::NSecondaries: vars[idx] = fStep.Get(ColType::NSecondaries, src, eventId, volId, matId); break;
        case FilterVar::StepStatus: vars[idx] = fStep.Get(ColType::StepStatus, src, eventId, volId, matId); break;
        case FilterVar::Safety: vars[idx] = fStep.Get(ColType::Safety, src, eventId, volId, matId); break;
        case FilterVar::IsFirstStepInVolume: vars[idx] = src.IsFirstStepInVolume() ? 1.0 : 0.0; break;
        case FilterVar::IsLastStepInVolume: vars[idx] = src.IsLastStepInVolume() ? 1.0 : 0.0; break;
        case FilterVar::DeltaPositionX: vars[idx] = fStep.Get(ColType::DeltaPositionX, src, eventId, volId, matId); break;
        case FilterVar::DeltaPositionY: vars[idx] = fStep.Get(ColType::DeltaPositionY, src, eventId, volId, matId); break;
        case FilterVar::DeltaPositionZ: vars[idx] = fStep.Get(ColType::DeltaPositionZ, src, eventId, volId, matId); break;
        case FilterVar::DeltaMomentumX: vars[idx] = fStep.Get(ColType::DeltaMomentumX, src, eventId, volId, matId); break;
        case FilterVar::DeltaMomentumY: vars[idx] = fStep.Get(ColType::DeltaMomentumY, src, eventId, volId, matId); break;
        case FilterVar::DeltaMomentumZ: vars[idx] = fStep.Get(ColType::DeltaMomentumZ, src, eventId, volId, matId); break;
        case FilterVar::PrePx: vars[idx] = fStep.Get(ColType::PrePx, src, eventId, volId, matId); break;
        case FilterVar::PrePy: vars[idx] = fStep.Get(ColType::PrePy, src, eventId, volId, matId); break;
        case FilterVar::PrePz: vars[idx] = fStep.Get(ColType::PrePz, src, eventId, volId, matId); break;
        case FilterVar::PostPx: vars[idx] = fStep.Get(ColType::PostPx, src, eventId, volId, matId); break;
        case FilterVar::PostPy: vars[idx] = fStep.Get(ColType::PostPy, src, eventId, volId, matId); break;
        case FilterVar::PostPz: vars[idx] = fStep.Get(ColType::PostPz, src, eventId, volId, matId); break;

        // Track
        case FilterVar::TrackLength: vars[idx] = fTrack.Get(ColType::TrackLength, src); break;
        case FilterVar::TrackStatus: vars[idx] = fTrack.Get(ColType::TrackStatus, src); break;
        case FilterVar::MeanFreePath: vars[idx] = fTrack.Get(ColType::MeanFreePath, src); break;
        case FilterVar::AtRestRate: vars[idx] = fTrack.Get(ColType::AtRestRate, src); break;
        case FilterVar::AtRestLifeTime: vars[idx] = fTrack.Get(ColType::AtRestLifeTime, src); break;
        case FilterVar::VertexX: vars[idx] = fTrack.Get(ColType::VertexX, src); break;
        case FilterVar::VertexY: vars[idx] = fTrack.Get(ColType::VertexY, src); break;
        case FilterVar::VertexZ: vars[idx] = fTrack.Get(ColType::VertexZ, src); break;
        case FilterVar::VertexKineticEnergy: vars[idx] = fTrack.Get(ColType::VertexKineticEnergy, src); break;
        case FilterVar::VertexPDGCode: vars[idx] = fTrack.Get(ColType::VertexPDGCode, src); break;

        // Nuclear properties
        case FilterVar::Z: vars[idx] = fParticle.Get(ColType::Z, src); break;
        case FilterVar::A: vars[idx] = fParticle.Get(ColType::A, src); break;
        case FilterVar::Excitation: vars[idx] = fParticle.Get(ColType::Excitation, src); break;
        case FilterVar::IsomerLevel: vars[idx] = fParticle.Get(ColType::IsomerLevel, src); break;
        case FilterVar::RecoilType: vars[idx] = fParticle.Get(ColType::RecoilType, src); break;

        // Particle quantum numbers
        case FilterVar::ParticleSpin: vars[idx] = fParticle.Get(ColType::ParticleSpin, src); break;
        case FilterVar::ParticleParity: vars[idx] = fParticle.Get(ColType::ParticleParity, src); break;
        case FilterVar::ParticleConjugation: vars[idx] = fParticle.Get(ColType::ParticleConjugation, src); break;
        case FilterVar::ParticleIsospin: vars[idx] = fParticle.Get(ColType::ParticleIsospin, src); break;
        case FilterVar::ParticleIsospin3: vars[idx] = fParticle.Get(ColType::ParticleIsospin3, src); break;
        case FilterVar::ParticleGParity: vars[idx] = fParticle.Get(ColType::ParticleGParity, src); break;
        case FilterVar::ParticleLifetime: vars[idx] = fParticle.Get(ColType::ParticleLifetime, src); break;
        case FilterVar::ParticleWidth: vars[idx] = fParticle.Get(ColType::ParticleWidth, src); break;
        case FilterVar::ParticleLeptonNumber: vars[idx] = fParticle.Get(ColType::ParticleLeptonNumber, src); break;
        case FilterVar::ParticleBaryonNumber: vars[idx] = fParticle.Get(ColType::ParticleBaryonNumber, src); break;

        // Detector effects
        case FilterVar::SmearedEdep: vars[idx] = fDetectorEffects.Get(ColType::SmearedEdep, src); break;
        case FilterVar::VisibleEdep: vars[idx] = fDetectorEffects.Get(ColType::VisibleEdep, src); break;
        case FilterVar::DoseGy: vars[idx] = fDetectorEffects.Get(ColType::DoseGy, src); break;
        case FilterVar::LET: vars[idx] = fDetectorEffects.Get(ColType::LET, src); break;
        case FilterVar::StepGrammage: vars[idx] = fDetectorEffects.Get(ColType::StepGrammage, src); break;
        case FilterVar::OpticalWavelength: vars[idx] = fDetectorEffects.Get(ColType::OpticalWavelength, src); break;
        case FilterVar::BoundaryStatus: vars[idx] = fDetectorEffects.Get(ColType::BoundaryStatus, src); break;
        // VolumeMass: volume mass in kg
        case FilterVar::VolumeMass: {
            auto* lv = src.GetLogicalVolume();
            if (lv && lv->GetSolid()) {
                auto* mat = lv->GetMaterial();
                double dens = mat ? mat->GetDensity() / (CLHEP::g/CLHEP::cm3) : 1.0;
                double vol_cm3 = lv->GetSolid()->GetCubicVolume() / CLHEP::cm3;
                vars[idx] = dens * vol_cm3 / 1000.0;
            } else { vars[idx] = 0.0; }
            break;
        }
        // Material properties (by ID)
        case FilterVar::Density: vars[idx] = fMaterial.Get(ColType::Density, src, matId); break;
        case FilterVar::Temperature: vars[idx] = fMaterial.Get(ColType::Temperature, src, matId); break;
        case FilterVar::Pressure: vars[idx] = fMaterial.Get(ColType::Pressure, src, matId); break;
        case FilterVar::State: vars[idx] = fMaterial.Get(ColType::State, src, matId); break;
        case FilterVar::RadiationLength: vars[idx] = fMaterial.Get(ColType::RadiationLength, src, matId); break;
        case FilterVar::NuclearInteractionLength: vars[idx] = fMaterial.Get(ColType::NuclearInteractionLength, src, matId); break;
        case FilterVar::Zeff: vars[idx] = fMaterial.Get(ColType::Zeff, src, matId); break;
        case FilterVar::Aeff: vars[idx] = fMaterial.Get(ColType::Aeff, src, matId); break;
                // Chemistry G-values
        case FilterVar::GOH: vars[idx] = fChemistry.Get(ColType::GOH, src); break;
        case FilterVar::GH: vars[idx] = fChemistry.Get(ColType::GH, src); break;
        case FilterVar::GEaq: vars[idx] = fChemistry.Get(ColType::GEaq, src); break;
        case FilterVar::H2O2: vars[idx] = fChemistry.Get(ColType::H2O2, src); break;
        case FilterVar::H2: vars[idx] = fChemistry.Get(ColType::H2, src); break;
        case FilterVar::RadicalOH: vars[idx] = fChemistry.Get(ColType::RadicalOH, src); break;
        case FilterVar::RadicalH: vars[idx] = fChemistry.Get(ColType::RadicalH, src); break;
        case FilterVar::RadicalEaq: vars[idx] = fChemistry.Get(ColType::RadicalEaq, src); break;
        default: vars[idx] = 0.0; break;

    }
}

/// @brief Populate a FilterVars array from a UnifiedSource.
///
/// Two modes:
/// 1. **Full mode** (neededVars == nullptr): calls _BuildFilterVarsFull()
///    which fills every FilterVar slot in one pass.  Used when no
///    variable whitelist is available (e.g. all columns are expression-based).
/// 2. **Selective mode** (neededVars != nullptr): iterates over the
///    supplied whitelist and calls _FillSingleVar() for each, minimising
///    extraction overhead.
///
/// @param vars       FilterVars array to fill (size = FilterVar::NUM_VARS).
/// @param src        Unified source (step, track, or vertex).
/// @param eventId    Current event ID.
/// @param volId      Pre-computed volume ID (from registry cache).
/// @param matId      Pre-computed material ID (from registry cache).
/// @param neededVars If non-null, only these variables are extracted.
void DataExtractor::BuildFilterVars(FilterVars& vars, const UnifiedSource& src,
                                     int eventId, int volId, int matId,
                                     const std::vector<FilterVar>* neededVars) const {
    if (!neededVars) {
        _BuildFilterVarsFull(vars, src, eventId, volId, matId);
    } else {
        for (FilterVar fv : *neededVars) {
            _FillSingleVar(vars, fv, src, eventId, volId, matId);
        }
    }
}