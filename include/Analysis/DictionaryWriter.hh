//==============================================================================
// G4CARE
// @file    DictionaryWriter.hh
// @brief   Creates and fills lookup-dictionary ntuples (volume, material,
//          process, surface, detector, cross-section, chemical species,
//          property history) using G4AnalysisManager.
// @details DictionaryWriter is separated from TreeManager to isolate the
//   dictionary logic.  It owns pointers to all TypedRegistries and the
//   VolumeMaterialRegistry, and uses CrossSectionCalculator for the
//   cross_section_dict table.
//
//   Configuration keys read: CROSS_SECTION_ENERGIES (via ConfigManager).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DICTIONARY_WRITER_HH
#define DICTIONARY_WRITER_HH

#include "TypedRegistry.hh"
#include "G4AnalysisManager.hh"
#include "VolumeMaterialRegistry.hh"
#include "CrossSectionCalculator.hh"
#include "DetectorRegistry.hh"
#include "SurfaceRegistry.hh"
#include <map>
#include <string>
#include <vector>

class ConfigManager;

/// @brief Creates and populates all dictionary ntuples used for offline
///        data analysis.
class DictionaryWriter {
public:
    /// @param registry           Volume/Material registry.
    /// @param xsCalc             Cross-section calculator (may be nullptr).
    /// @param analysisManager    G4AnalysisManager instance.
    /// @param particleNameReg    Registry for particle names.
    /// @param volumeNameReg      Registry for volume names.
    /// @param materialNameReg    Registry for material names.
    /// @param regionNameReg      Registry for region names.
    /// @param sourceNameReg      Registry for source names.
    /// @param detectorNameReg    Registry for detector names.
    /// @param touchablePathReg   Registry for touchable paths.
    /// @param chemicalFormulaReg Registry for chemical formulas.
    /// @param pdgReg             Registry for PDG codes.
    /// @param processSubTypeReg  Registry for process sub-types.
    /// @param massReg            Registry for particle masses.
    /// @param chargeReg          Registry for particle charges.
    /// @param chemSpeciesReg     Registry for chemical species names.
    /// @param detReg             Detector registry.
    /// @param surfReg            Surface registry.
    DictionaryWriter(VolumeMaterialRegistry& registry,
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
                     TypedRegistry<std::string>* chemSpeciesReg,
                     DetectorRegistry* detReg,
                     SurfaceRegistry* surfReg);

    ~DictionaryWriter() = default;

    /// Creates all dictionary ntuple structures (called in Initialize).
    void CreateDictionaryTrees(ConfigManager* cfg);

    /// Fills all dictionary ntuples with current data (called in BeginRun).
    void FillDictionaryTrees();

    // --- Accessors for dictionary ntuples ---
    int GetDictionaryNtupleId(const std::string& name) const;
    const std::vector<int>& GetDictionaryColumnIds(const std::string& name) const;

    int GetPropertyHistoryNtupleId() const;
    int GetPropertyHistoryColRunId() const;
    int GetPropertyHistoryColPropName() const;
    int GetPropertyHistoryColValue() const;

private:
    VolumeMaterialRegistry& fRegistry;
    CrossSectionCalculator* fXSCalculator;
    G4AnalysisManager* fAnalysisManager;

    TypedRegistry<std::string>* fParticleNameReg;
    TypedRegistry<std::string>* fVolumeNameReg;
    TypedRegistry<std::string>* fMaterialNameReg;
    TypedRegistry<std::string>* fRegionNameReg;
    TypedRegistry<std::string>* fSourceNameReg;
    TypedRegistry<std::string>* fDetectorNameReg;
    TypedRegistry<std::string>* fTouchablePathReg;
    TypedRegistry<std::string>* fChemicalFormulaReg;
    TypedRegistry<int>* fPDGReg;
    TypedRegistry<int>* fProcessSubTypeReg;
    TypedRegistry<double>* fMassReg;
    TypedRegistry<double>* fChargeReg;
    TypedRegistry<std::string>* fChemSpeciesReg;
    DetectorRegistry* fDetectorRegistry;
    SurfaceRegistry* fSurfaceRegistry;

    std::map<std::string, int> fDictionaryNtupleIds;
    std::map<std::string, std::vector<int>> fDictionaryColumnIds;
};

#endif