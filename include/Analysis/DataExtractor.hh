//==============================================================================
// G4CARE
// @file    DataExtractor.hh
// @brief   Central data extraction class that dispatches ColType / FilterVar
//          queries to specialized extractors (kinematics, step, track, process,
//          particle, material, geometry, cross-section, detector effects,
//          chemistry).
// @details DataExtractor owns one instance of each domain-specific extractor
//   and provides GetDouble / GetString / BuildFilterVars methods.  It is
//   constructed with pointers to shared registries (VolumeMaterialRegistry,
//   TypedRegistries, CrossSectionCalculator) and a thread-local ThreadCache.
//
//   Configuration keys read: none (registry-driven).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DATA_EXTRACTOR_HH
#define DATA_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "KinematicsExtractor.hh"
#include "StepExtractor.hh"
#include "TrackExtractor.hh"
#include "ProcessExtractor.hh"
#include "ParticleExtractor.hh"
#include "MaterialExtractor.hh"
#include "GeometryExtractor.hh"
#include "CrossSectionExtractor.hh"
#include "DetectorEffectsExtractor.hh"
#include "ChemistryExtractor.hh"
#include "TypedRegistry.hh"
#include "VolumeMaterialRegistry.hh"
#include "ChemSpeciesRegistry.hh"

struct ThreadCache;

class DataExtractor {
public:
    DataExtractor(ThreadCache& cache,
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
                  TypedRegistry<std::string>* fChemSpeciesReg);

    [[nodiscard]] double GetDouble(ColType type, const UnifiedSource& src, int eventId) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;
    void BuildFilterVars(FilterVars& vars, const UnifiedSource& src, int eventId, int volId, int matId, const std::vector<FilterVar>* neededVars = nullptr) const;

    /// Access to ChemistryExtractor for accumulation and reset
    ChemistryExtractor& GetChemistryExtractor() { return fChemistry; }
    const ChemistryExtractor& GetChemistryExtractor() const { return fChemistry; }

private:

    void _BuildFilterVarsFull(FilterVars& vars, const UnifiedSource& src, int eventId, int volId, int matId) const;
    void _FillSingleVar(FilterVars& vars, FilterVar fv, const UnifiedSource& src, int eventId, int volId, int matId) const;

    ThreadCache& fCache;
    CrossSectionCalculator* fXSCalculator;
    VolumeMaterialRegistry* fRegistry;

    KinematicsExtractor fKinematics;
    StepExtractor fStep;
    TrackExtractor fTrack;
    ProcessExtractor fProcess;
    ParticleExtractor fParticle;
    MaterialExtractor fMaterial;
    GeometryExtractor fGeometry;
    CrossSectionExtractor fCrossSection;
    DetectorEffectsExtractor fDetectorEffects;
    ChemistryExtractor fChemistry;

    TypedRegistry<std::string>* fParticleNameReg;
    TypedRegistry<std::string>* fVolumeNameReg;
    TypedRegistry<std::string>* fMaterialNameReg;
    TypedRegistry<std::string>* fRegionNameReg;
    TypedRegistry<std::string>* fSourceNameReg;
    TypedRegistry<std::string>* fDetectorNameReg;
    TypedRegistry<std::string>* fTouchablePathReg;
    TypedRegistry<std::string>* fChemicalFormulaReg;
    TypedRegistry<int>* fPDGReg;
    TypedRegistry<double>* fMassReg;
    TypedRegistry<double>* fChargeReg;
    TypedRegistry<std::string>* fChemSpeciesReg;
};

#endif // DATA_EXTRACTOR_HH