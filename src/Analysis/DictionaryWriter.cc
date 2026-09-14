//==============================================================================
// G4CARE
// @file    DictionaryWriter.cc
// @brief   Creates and fills Geant4 analysis ntuples that serve as lookup
//          dictionaries for volumes, materials, processes, surfaces, isotopes,
//          cross-sections, detectors, and chemical species.
// @details DictionaryWriter builds a set of G4Analysis ntuples (volume_dict,
//   material_dict, process_dict, surface_dict, composition_dict, cross_section_dict,
//   detector_dict, chemical_species_dict, property_history) during the
//   initialization phase and populates them with data extracted from the Geant4
//   state and user configuration.  Registry-backed dictionaries (particle, volume
//   name, material name, etc.) are created and filled via TypedRegistry.
//
//   Configuration keys read: CROSS_SECTION_ENERGIES.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "DictionaryWriter.hh"
#include "ConfigManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4MaterialTable.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4ProcessTable.hh"
#include "G4ProcessVector.hh"
#include "G4VProcess.hh"
#include "ProcessUtils.hh"
#include "G4ParticleTable.hh"
#include <algorithm>

/// @brief Constructs the DictionaryWriter with all required registries and
///        helper objects.
/// @param registry Volume-to-material lookup registry.
/// @param xsCalc Cross-section calculator for populating the cross_section_dict.
/// @param analysisManager G4AnalysisManager instance for ntuple creation.
/// @param particleNameReg   TypedRegistry for particle names.
/// @param volumeNameReg     TypedRegistry for logical volume names.
/// @param materialNameReg   TypedRegistry for material names.
/// @param regionNameReg     TypedRegistry for region names.
/// @param sourceNameReg     TypedRegistry for source names.
/// @param detectorNameReg   TypedRegistry for detector names.
/// @param touchablePathReg  TypedRegistry for touchable paths.
/// @param chemicalFormulaReg TypedRegistry for chemical formulas.
/// @param pdgReg            TypedRegistry for PDG codes.
/// @param processSubTypeReg TypedRegistry for process sub-types.
/// @param massReg           TypedRegistry for particle masses.
/// @param chargeReg         TypedRegistry for particle charges.
/// @param chemSpeciesReg    TypedRegistry for chemical species names.
/// @param detReg            Detector registry with properties.
/// @param surfReg           Surface registry with properties.
DictionaryWriter::DictionaryWriter(VolumeMaterialRegistry& registry,
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
                                   SurfaceRegistry* surfReg)
    : fRegistry(registry),
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
      fChemSpeciesReg(chemSpeciesReg),
      fDetectorRegistry(detReg),
      fSurfaceRegistry(surfReg) {}

/// @brief Creates all dictionary ntuple structures (volume_dict, material_dict,
///        process_dict, surface_dict, composition_dict, cross_section_dict,
///        detector_dict, chemical_species_dict, property_history, and
///        registry-backed dictionaries).
/// @param cfg Configuration manager for reading CROSS_SECTION_ENERGIES.
void DictionaryWriter::CreateDictionaryTrees(ConfigManager* cfg) {
    // G4cout << "[CreateDictTrees] ENTER isMaster=" << G4Threading::IsMasterThread() << " fAnalysisManager=" << fAnalysisManager << " threadID=" << G4Threading::G4GetThreadId() << G4endl;
    // Volume dictionary
    int volNtuple = fAnalysisManager->CreateNtuple("volume_dict", "Volume Dictionary");
    // G4cout << "[CreateDictTrees] volume_dict ntupleId=" << volNtuple << G4endl;
    int volIdCol   = fAnalysisManager->CreateNtupleIColumn(volNtuple, "vol_id");
    int volNameCol = fAnalysisManager->CreateNtupleSColumn(volNtuple, "vol_name");
    int volMatCol  = fAnalysisManager->CreateNtupleIColumn(volNtuple, "material_id");
    int volParentCol = fAnalysisManager->CreateNtupleIColumn(volNtuple, "parent_vol_id");
    fAnalysisManager->FinishNtuple(volNtuple);
    fDictionaryNtupleIds["volume_dict"] = volNtuple;
    fDictionaryColumnIds["volume_dict"] = {volIdCol, volNameCol, volMatCol, volParentCol};

    // Process dictionary
    int procNtuple = fAnalysisManager->CreateNtuple("process_dict", "Process Dictionary");
    // G4cout << "[CreateDictTrees] process_dict ntupleId=" << procNtuple << G4endl;
    int procPackedCol = fAnalysisManager->CreateNtupleIColumn(procNtuple, "packed_id");
    int procNameCol   = fAnalysisManager->CreateNtupleSColumn(procNtuple, "process_name");
    int procTypeCol   = fAnalysisManager->CreateNtupleIColumn(procNtuple, "process_type");
    int procSubCol    = fAnalysisManager->CreateNtupleIColumn(procNtuple, "process_subtype");
    fAnalysisManager->FinishNtuple(procNtuple);
    fDictionaryNtupleIds["process_dict"] = procNtuple;
    fDictionaryColumnIds["process_dict"] = {procPackedCol, procNameCol, procTypeCol, procSubCol};

    // Material dictionary
    int matNtuple = fAnalysisManager->CreateNtuple("material_dict", "Material Dictionary");
    // G4cout << "[CreateDictTrees] material_dict ntupleId=" << matNtuple << G4endl;
    int matIdCol   = fAnalysisManager->CreateNtupleIColumn(matNtuple, "mat_id");
    int matNameCol = fAnalysisManager->CreateNtupleSColumn(matNtuple, "mat_name");
    int densCol    = fAnalysisManager->CreateNtupleDColumn(matNtuple, "density");
    int tempCol    = fAnalysisManager->CreateNtupleDColumn(matNtuple, "temperature");
    int pressCol   = fAnalysisManager->CreateNtupleDColumn(matNtuple, "pressure");
    int stateCol   = fAnalysisManager->CreateNtupleIColumn(matNtuple, "state");
    int radLenCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "radiation_length");
    int nucIntCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "nuclear_interaction_length");
    int zeffCol    = fAnalysisManager->CreateNtupleDColumn(matNtuple, "Z_eff");
    int aeffCol    = fAnalysisManager->CreateNtupleDColumn(matNtuple, "A_eff");
    int formulaCol = fAnalysisManager->CreateNtupleSColumn(matNtuple, "chemical_formula");
    int rindexCol   = fAnalysisManager->CreateNtupleDColumn(matNtuple, "rindex");
    int scintYieldCol = fAnalysisManager->CreateNtupleDColumn(matNtuple, "scintillation_yield");
    int fastTimeCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "fast_time_constant");
    int slowTimeCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "slow_time_constant");
    int yieldRatioCol = fAnalysisManager->CreateNtupleDColumn(matNtuple, "yield_ratio");
    int resScaleCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "resolution_scale");
    int absLenCol    = fAnalysisManager->CreateNtupleDColumn(matNtuple, "absorption_length");
    int wlsCompCol   = fAnalysisManager->CreateNtupleDColumn(matNtuple, "wls_component");
    int wlsTimeCol   = fAnalysisManager->CreateNtupleDColumn(matNtuple, "wls_time_constant");
    int rayleighCol  = fAnalysisManager->CreateNtupleDColumn(matNtuple, "rayleigh_length");
    
    fAnalysisManager->FinishNtuple(matNtuple);
    fDictionaryNtupleIds["material_dict"] = matNtuple;
    fDictionaryColumnIds["material_dict"] = {
        matIdCol, matNameCol, densCol, tempCol, pressCol,
        stateCol, radLenCol, nucIntCol, zeffCol, aeffCol, formulaCol,
        rindexCol, scintYieldCol, fastTimeCol, slowTimeCol, yieldRatioCol,
        resScaleCol, absLenCol, wlsCompCol, wlsTimeCol, rayleighCol
    };

    // Surface dictionary
    int surfNtuple = fAnalysisManager->CreateNtuple("surface_dict", "Surface Dictionary");
    int surfIdCol   = fAnalysisManager->CreateNtupleIColumn(surfNtuple, "surface_id");
    int surfNameCol = fAnalysisManager->CreateNtupleSColumn(surfNtuple, "surface_name");
    int surfTypeCol = fAnalysisManager->CreateNtupleIColumn(surfNtuple, "surface_type");
    int surfFinishCol = fAnalysisManager->CreateNtupleIColumn(surfNtuple, "surface_finish");
    int reflectivityCol = fAnalysisManager->CreateNtupleDColumn(surfNtuple, "reflectivity");
    int efficiencyCol   = fAnalysisManager->CreateNtupleDColumn(surfNtuple, "efficiency");
    int sigmaAlphaCol   = fAnalysisManager->CreateNtupleDColumn(surfNtuple, "sigma_alpha");
    fAnalysisManager->FinishNtuple(surfNtuple);
    fDictionaryNtupleIds["surface_dict"] = surfNtuple;
    fDictionaryColumnIds["surface_dict"] = {surfIdCol, surfNameCol, surfTypeCol, surfFinishCol,
                                            reflectivityCol, efficiencyCol, sigmaAlphaCol};
                                            
    // Composition dictionary (isotopic composition)
    int compNtuple = fAnalysisManager->CreateNtuple("composition_dict", "Material Composition Dictionary");
    int cMatId = fAnalysisManager->CreateNtupleIColumn(compNtuple, "material_id");
    int cElemZ = fAnalysisManager->CreateNtupleIColumn(compNtuple, "element_Z");
    int cIsoA  = fAnalysisManager->CreateNtupleIColumn(compNtuple, "isotope_A");
    int cMassF = fAnalysisManager->CreateNtupleDColumn(compNtuple, "mass_fraction");
    int cAtAb  = fAnalysisManager->CreateNtupleDColumn(compNtuple, "atomic_abundance");
    fAnalysisManager->FinishNtuple(compNtuple);
    fDictionaryNtupleIds["composition_dict"] = compNtuple;
    fDictionaryColumnIds["composition_dict"] = {cMatId, cElemZ, cIsoA, cMassF, cAtAb};

    // Detector dictionary (31 columns — must match FillDictionaryTrees exactly)
    int detNtuple = fAnalysisManager->CreateNtuple("detector_dict", "Detector Dictionary");
    int detIdCol      = fAnalysisManager->CreateNtupleIColumn(detNtuple, "detector_id");              // 0
    int detNameCol    = fAnalysisManager->CreateNtupleSColumn(detNtuple, "detector_name");            // 1
    int deadTimeCol   = fAnalysisManager->CreateNtupleDColumn(detNtuple, "dead_time_ns");             // 2
    int deadTimeMCol  = fAnalysisManager->CreateNtupleSColumn(detNtuple, "dead_time_model");          // 3
    int timeResCol    = fAnalysisManager->CreateNtupleDColumn(detNtuple, "time_resolution_fwhm_ns");  // 4
    int energyResCol  = fAnalysisManager->CreateNtupleDColumn(detNtuple, "energy_resolution_fwhm");   // 5
    int energyRefCol  = fAnalysisManager->CreateNtupleDColumn(detNtuple, "energy_resolution_ref_energy_MeV"); // 6
    int qeCol         = fAnalysisManager->CreateNtupleDColumn(detNtuple, "quantum_efficiency");       // 7
    int gainCol       = fAnalysisManager->CreateNtupleDColumn(detNtuple, "gain");                     // 8
    int pileupCol     = fAnalysisManager->CreateNtupleSColumn(detNtuple, "pileup_model");             // 9
    int afterpulseCol = fAnalysisManager->CreateNtupleDColumn(detNtuple, "afterpulse_probability");   // 10
    int riseTimeCol   = fAnalysisManager->CreateNtupleDColumn(detNtuple, "signal_rise_time_ns");      // 11
    int fallTimeCol   = fAnalysisManager->CreateNtupleDColumn(detNtuple, "signal_fall_time_ns");      // 12
    int signalMCol    = fAnalysisManager->CreateNtupleSColumn(detNtuple, "signal_model");             // 13
    int pileupWinCol  = fAnalysisManager->CreateNtupleDColumn(detNtuple, "pileup_window_ns");         // 14
    int pileupBufCol  = fAnalysisManager->CreateNtupleIColumn(detNtuple, "pileup_buffer_size");       // 15
    int threshCol     = fAnalysisManager->CreateNtupleDColumn(detNtuple, "threshold_MeV");            // 16
    int noiseCol      = fAnalysisManager->CreateNtupleDColumn(detNtuple, "noise_level_MeV");          // 17
    int timeSmearMCol = fAnalysisManager->CreateNtupleSColumn(detNtuple, "time_smear_model");         // 18
    int energySmearMCol = fAnalysisManager->CreateNtupleSColumn(detNtuple, "energy_smear_model");     // 19
    int useTExprCol   = fAnalysisManager->CreateNtupleIColumn(detNtuple, "use_time_smear_expression");// 20
    int tExprCol      = fAnalysisManager->CreateNtupleSColumn(detNtuple, "time_smear_expression");    // 21
    int useEExprCol   = fAnalysisManager->CreateNtupleIColumn(detNtuple, "use_energy_smear_expression");// 22
    int eExprCol      = fAnalysisManager->CreateNtupleSColumn(detNtuple, "energy_smear_expression");  // 23
    int useSigExprCol = fAnalysisManager->CreateNtupleIColumn(detNtuple, "use_signal_time_expression");// 24
    int sigExprCol    = fAnalysisManager->CreateNtupleSColumn(detNtuple, "signal_time_expression");   // 25
    int usePileExprCol= fAnalysisManager->CreateNtupleIColumn(detNtuple, "use_pileup_time_expression");// 26
    int pileExprCol   = fAnalysisManager->CreateNtupleSColumn(detNtuple, "pileup_time_expression");   // 27
    int darkRateCol   = fAnalysisManager->CreateNtupleDColumn(detNtuple, "dark_count_rate_kHz");      // 28
    int crosstalkCol  = fAnalysisManager->CreateNtupleDColumn(detNtuple, "cross_talk_probability");   // 29
    int recoveryCol   = fAnalysisManager->CreateNtupleDColumn(detNtuple, "recovery_time_ns");         // 30
    fAnalysisManager->FinishNtuple(detNtuple);
    fDictionaryNtupleIds["detector_dict"] = detNtuple;
    fDictionaryColumnIds["detector_dict"] = {
        detIdCol, detNameCol, deadTimeCol, deadTimeMCol, timeResCol, energyResCol, energyRefCol,
        qeCol, gainCol, pileupCol, afterpulseCol,
        riseTimeCol, fallTimeCol, signalMCol, pileupWinCol, pileupBufCol,
        threshCol, noiseCol, timeSmearMCol, energySmearMCol,
        useTExprCol, tExprCol, useEExprCol, eExprCol,
        useSigExprCol, sigExprCol, usePileExprCol, pileExprCol,
        darkRateCol, crosstalkCol, recoveryCol
    };

    // Cross-section dictionary
    std::vector<double> energies = cfg->GetDoubleVectorWithUnits("CROSS_SECTION_ENERGIES", {});
    if (!energies.empty()) {
        int xsNtuple = fAnalysisManager->CreateNtuple("cross_section_dict", "Cross Section Dictionary");
        int matIdCol   = fAnalysisManager->CreateNtupleIColumn(xsNtuple, "material_id");
        int energyCol  = fAnalysisManager->CreateNtupleDColumn(xsNtuple, "energy_MeV");
        int nTotalCol  = fAnalysisManager->CreateNtupleDColumn(xsNtuple, "neutron_total");
        int nCapCol    = fAnalysisManager->CreateNtupleDColumn(xsNtuple, "neutron_capture");
        int pTotalCol  = fAnalysisManager->CreateNtupleDColumn(xsNtuple, "proton_total");
        int phTotalCol = fAnalysisManager->CreateNtupleDColumn(xsNtuple, "photon_total");
        fAnalysisManager->FinishNtuple(xsNtuple);
        fDictionaryNtupleIds["cross_section_dict"] = xsNtuple;
        fDictionaryColumnIds["cross_section_dict"] = {matIdCol, energyCol, nTotalCol, nCapCol, pTotalCol, phTotalCol};
    }

    // Chemical species dictionary
    int chemNtuple = fAnalysisManager->CreateNtuple("chemical_species_dict", "Chemical Species Dictionary");
    int chemIdCol   = fAnalysisManager->CreateNtupleIColumn(chemNtuple, "species_id");
    int chemNameCol = fAnalysisManager->CreateNtupleSColumn(chemNtuple, "species_name");
    fAnalysisManager->FinishNtuple(chemNtuple);
    fDictionaryNtupleIds["chemical_species_dict"] = chemNtuple;
    fDictionaryColumnIds["chemical_species_dict"] = {chemIdCol, chemNameCol};

    int histNtuple = fAnalysisManager->CreateNtuple("property_history", "Property History");
    int colRunId   = fAnalysisManager->CreateNtupleIColumn(histNtuple, "run_id");
    int colProp    = fAnalysisManager->CreateNtupleSColumn(histNtuple, "property_name");
    int colVal     = fAnalysisManager->CreateNtupleSColumn(histNtuple, "value");
    fAnalysisManager->FinishNtuple(histNtuple);
    fDictionaryNtupleIds["property_history"] = histNtuple;
    fDictionaryColumnIds["property_history"] = {colRunId, colProp, colVal};

    // --- Registry dictionaries: create Ntuple structures BEFORE OpenFile ---
    if (fParticleNameReg)    fDictionaryNtupleIds["particle_dict"]             = fParticleNameReg->CreateDictionary(fAnalysisManager, "particle_dict");
    if (fVolumeNameReg)      fDictionaryNtupleIds["volume_name_dict"]          = fVolumeNameReg->CreateDictionary(fAnalysisManager, "volume_name_dict");
    if (fMaterialNameReg)    fDictionaryNtupleIds["material_name_dict"]        = fMaterialNameReg->CreateDictionary(fAnalysisManager, "material_name_dict");
    if (fRegionNameReg)      fDictionaryNtupleIds["region_dict"]               = fRegionNameReg->CreateDictionary(fAnalysisManager, "region_dict");
    if (fSourceNameReg)      fDictionaryNtupleIds["source_dict"]               = fSourceNameReg->CreateDictionary(fAnalysisManager, "source_dict");
    if (fDetectorNameReg)    fDictionaryNtupleIds["detector_name_dict"]        = fDetectorNameReg->CreateDictionary(fAnalysisManager, "detector_name_dict");
    if (fTouchablePathReg)   fDictionaryNtupleIds["touchable_dict"]            = fTouchablePathReg->CreateDictionary(fAnalysisManager, "touchable_dict");
    if (fChemicalFormulaReg) fDictionaryNtupleIds["chemical_formula_dict"]     = fChemicalFormulaReg->CreateDictionary(fAnalysisManager, "chemical_formula_dict");
    if (fPDGReg)             fDictionaryNtupleIds["pdg_dict"]                  = fPDGReg->CreateDictionary(fAnalysisManager, "pdg_dict");
    if (fProcessSubTypeReg)  fDictionaryNtupleIds["process_subtype_dict"]      = fProcessSubTypeReg->CreateDictionary(fAnalysisManager, "process_subtype_dict");
    if (fMassReg)            fDictionaryNtupleIds["mass_dict"]                 = fMassReg->CreateDictionary(fAnalysisManager, "mass_dict");
    if (fChargeReg)          fDictionaryNtupleIds["charge_dict"]               = fChargeReg->CreateDictionary(fAnalysisManager, "charge_dict");
    if (fChemSpeciesReg)     fDictionaryNtupleIds["chemical_species_name_dict"] = fChemSpeciesReg->CreateDictionary(fAnalysisManager, "chemical_species_name_dict");
}

/// @brief Fills all dictionary ntuples with current Geant4 state data
///        (logical volumes, materials, processes, isotope composition,
///        cross-section tables, detectors, and registry-backed dictionaries).
void DictionaryWriter::FillDictionaryTrees() {
    // G4cout << "[FillDictionaryTrees] ENTER - isMaster=" << G4Threading::IsMasterThread() << " fAnalysisManager=" << fAnalysisManager << " threadID=" << G4Threading::G4GetThreadId() << G4endl;

    auto* lvStore = G4LogicalVolumeStore::GetInstance();
    if (lvStore) {
        int ntupleId = fDictionaryNtupleIds["volume_dict"];
        // G4cout << "[FillDict] volume_dict ntupleId=" << ntupleId << G4endl;
        const auto& cols = fDictionaryColumnIds["volume_dict"];
        for (auto* lv : *lvStore) {
            int volId = fRegistry.GetVolumeID(lv);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[0], volId);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[1], lv->GetName());
            int matId = fRegistry.GetMaterialID(lv->GetMaterial());
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[2], matId);
            int parentId = -1;
            auto* parent = fRegistry.GetParentLogicalVolume(lv);
            if (parent) parentId = fRegistry.GetVolumeID(parent);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[3], parentId);
            fAnalysisManager->AddNtupleRow(ntupleId);
        }
    }

    const G4MaterialTable* matTable = G4Material::GetMaterialTable();
    if (matTable) {
        int ntupleId = fDictionaryNtupleIds["material_dict"];
        // G4cout << "[FillDict] material_dict ntupleId=" << ntupleId << G4endl;
        const auto& cols = fDictionaryColumnIds["material_dict"];
        for (size_t i = 0; i < matTable->size(); ++i) {
            G4Material* mat = (*matTable)[i];
            int matId = static_cast<int>(i);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[0], matId);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[1], mat->GetName());
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[2], mat->GetDensity() / (g/cm3));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[3], mat->GetTemperature() / kelvin);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[4], mat->GetPressure() / atmosphere);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[5], static_cast<int>(mat->GetState()));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[6], mat->GetRadlen() / cm);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[7], mat->GetNuclearInterLength() / cm);
            // Effective Z for compound materials
            G4double effZ = 0.0;
            const auto* elements = mat->GetElementVector();
            const auto fractions = mat->GetFractionVector();
            for (size_t j = 0; j < mat->GetNumberOfElements(); ++j) {
                effZ += fractions[j] * (*elements)[j]->GetZ();
            }
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[8], effZ);
            // Effective A for compound materials
            G4double effA = 0.0;
            for (size_t j = 0; j < mat->GetNumberOfElements(); ++j) {
                effA += fractions[j] * (*elements)[j]->GetA() / (g/mole);
            }
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[9], effA);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[10], mat->GetChemicalFormula());
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[11], fRegistry.GetMaterialRIndex(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[12], fRegistry.GetMaterialScintillationYield(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[13], fRegistry.GetMaterialFastTimeConstant(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[14], fRegistry.GetMaterialSlowTimeConstant(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[15], fRegistry.GetMaterialYieldRatio(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[16], fRegistry.GetMaterialResolutionScale(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[17], fRegistry.GetMaterialAbsorptionLength(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[18], fRegistry.GetMaterialWLSComponent(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[19], fRegistry.GetMaterialWLSTimeConstant(matId));
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[20], fRegistry.GetMaterialRayleighLength(matId));
            fAnalysisManager->AddNtupleRow(ntupleId);
        }
    }

    auto* processTable = G4ProcessTable::GetProcessTable();
    if (processTable) {
        std::unique_ptr<G4ProcessVector> processList(processTable->FindProcesses());
        if (processList) {
            int ntupleId = fDictionaryNtupleIds["process_dict"];
            // G4cout << "[FillDict] process_dict ntupleId=" << ntupleId << G4endl;
            const auto& cols = fDictionaryColumnIds["process_dict"];
            for (G4int i = 0; i < processList->entries(); ++i) {
                G4VProcess* proc = (*processList)[i];
                if (!proc) continue;

                uint32_t packed = ProcessUtils::PackProcessID(*proc);

                fAnalysisManager->FillNtupleIColumn(ntupleId, cols[0], static_cast<int>(packed));
                fAnalysisManager->FillNtupleSColumn(ntupleId, cols[1], proc->GetProcessName());
                fAnalysisManager->FillNtupleIColumn(ntupleId, cols[2], static_cast<int>(proc->GetProcessType()));
                fAnalysisManager->FillNtupleIColumn(ntupleId, cols[3], proc->GetProcessSubType());
                fAnalysisManager->AddNtupleRow(ntupleId);
            }
        }
    }

    if (matTable) {
        int compNtupleId = fDictionaryNtupleIds["composition_dict"];
        const auto& compCols = fDictionaryColumnIds["composition_dict"];
        for (size_t i = 0; i < matTable->size(); ++i) {
            G4Material* mat = (*matTable)[i];
            const G4ElementVector* elements = mat->GetElementVector();
            const G4double* massFractions = mat->GetFractionVector();

            for (size_t elIdx = 0; elIdx < mat->GetNumberOfElements(); ++elIdx) {
                const G4Element* el = (*elements)[elIdx];
                G4IsotopeVector* isotopes = el->GetIsotopeVector();
                G4double* abundances = el->GetRelativeAbundanceVector();

                if (isotopes && isotopes->size() > 0) {
                    for (size_t isoIdx = 0; isoIdx < isotopes->size(); ++isoIdx) {
                        G4Isotope* iso = (*isotopes)[isoIdx];
                        fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[0], static_cast<int>(i));
                        fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[1], iso->GetZ());
                        fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[2], iso->GetN());
                        fAnalysisManager->FillNtupleDColumn(compNtupleId, compCols[3], massFractions[elIdx]);
                        fAnalysisManager->FillNtupleDColumn(compNtupleId, compCols[4], abundances[isoIdx]);
                        fAnalysisManager->AddNtupleRow(compNtupleId);
                    }
                } else {
                    fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[0], static_cast<int>(i));
                    fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[1], el->GetZ());
                    fAnalysisManager->FillNtupleIColumn(compNtupleId, compCols[2], 0);
                    fAnalysisManager->FillNtupleDColumn(compNtupleId, compCols[3], massFractions[elIdx]);
                    fAnalysisManager->FillNtupleDColumn(compNtupleId, compCols[4], 1.0);
                    fAnalysisManager->AddNtupleRow(compNtupleId);
                }
            }
        }
    }

    auto itXs = fDictionaryNtupleIds.find("cross_section_dict");
    if (itXs != fDictionaryNtupleIds.end() && fXSCalculator) {
        auto* cfg = ConfigManager::Instance();
        std::vector<double> energies = cfg->GetDoubleVectorWithUnits("CROSS_SECTION_ENERGIES", {});
        if (energies.empty()) return;

        int ntupleId = itXs->second;
        const auto& cols = fDictionaryColumnIds["cross_section_dict"];
        const G4MaterialTable* matTable2 = G4Material::GetMaterialTable();
        if (!matTable2) return;

        for (size_t i = 0; i < matTable2->size(); ++i) {
            G4Material* mat = (*matTable2)[i];
            for (double e_MeV : energies) {
                double e = e_MeV * MeV;
                double xs_n_total   = fXSCalculator->GetNeutronTotalXSForMaterial(mat, e);
                double xs_n_capture = fXSCalculator->GetNeutronCaptureXSForMaterial(mat, e);
                double xs_p_total   = fXSCalculator->GetProtonTotalXSForMaterial(mat, e);
                double xs_ph_total  = fXSCalculator->GetPhotonTotalXSForMaterial(mat, e);
                fAnalysisManager->FillNtupleIColumn(ntupleId, cols[0], static_cast<int>(i));
                fAnalysisManager->FillNtupleDColumn(ntupleId, cols[1], e_MeV);
                fAnalysisManager->FillNtupleDColumn(ntupleId, cols[2], xs_n_total);
                fAnalysisManager->FillNtupleDColumn(ntupleId, cols[3], xs_n_capture);
                fAnalysisManager->FillNtupleDColumn(ntupleId, cols[4], xs_p_total);
                fAnalysisManager->FillNtupleDColumn(ntupleId, cols[5], xs_ph_total);
                fAnalysisManager->AddNtupleRow(ntupleId);
            }
        }
    }

    if (fParticleNameReg)    fParticleNameReg->FillDictionary(fAnalysisManager,    fDictionaryNtupleIds["particle_dict"]);
    if (fVolumeNameReg)      fVolumeNameReg->FillDictionary(fAnalysisManager,      fDictionaryNtupleIds["volume_name_dict"]);
    if (fMaterialNameReg)    fMaterialNameReg->FillDictionary(fAnalysisManager,    fDictionaryNtupleIds["material_name_dict"]);
    if (fRegionNameReg)      fRegionNameReg->FillDictionary(fAnalysisManager,      fDictionaryNtupleIds["region_dict"]);
    if (fSourceNameReg)      fSourceNameReg->FillDictionary(fAnalysisManager,      fDictionaryNtupleIds["source_dict"]);
    if (fDetectorNameReg)    fDetectorNameReg->FillDictionary(fAnalysisManager,    fDictionaryNtupleIds["detector_name_dict"]);
    if (fTouchablePathReg)   fTouchablePathReg->FillDictionary(fAnalysisManager,   fDictionaryNtupleIds["touchable_dict"]);
    if (fChemicalFormulaReg) fChemicalFormulaReg->FillDictionary(fAnalysisManager, fDictionaryNtupleIds["chemical_formula_dict"]);
    if (fPDGReg)             fPDGReg->FillDictionary(fAnalysisManager,             fDictionaryNtupleIds["pdg_dict"]);
    if (fProcessSubTypeReg)  fProcessSubTypeReg->FillDictionary(fAnalysisManager,  fDictionaryNtupleIds["process_subtype_dict"]);
    if (fMassReg)            fMassReg->FillDictionary(fAnalysisManager,            fDictionaryNtupleIds["mass_dict"]);
    if (fChargeReg)          fChargeReg->FillDictionary(fAnalysisManager,          fDictionaryNtupleIds["charge_dict"]);
    if (fChemSpeciesReg)     fChemSpeciesReg->FillDictionary(fAnalysisManager,     fDictionaryNtupleIds["chemical_species_name_dict"]);
    if (fDetectorRegistry) {
        int ntupleId = fDictionaryNtupleIds["detector_dict"];
        const auto& cols = fDictionaryColumnIds["detector_dict"];
        for (const auto& entry : fDetectorRegistry->GetAll()) {
            G4VSensitiveDetector* sd = entry.first;
            const DetectorProperties& props = entry.second;
            int detId = fDetectorRegistry->GetDetectorID(sd);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[0], detId);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[1], sd->GetName());
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[2], props.deadTime_ns);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[3], props.deadTimeModel);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[4], props.timeResFWHM_ns);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[5], props.energyResFWHM);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[6], props.energyResRefEnergy_MeV);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[7], props.quantumEfficiency);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[8], props.gain);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[9], props.pileupModel);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[10], props.afterpulseProbability);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[11], props.signalRiseTime_ns);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[12], props.signalFallTime_ns);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[13], props.signalModel);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[14], props.pileupWindow_ns);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[15], props.pileupBufferSize);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[16], props.threshold_MeV);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[17], props.noiseLevel_MeV);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[18], props.timeSmearModel);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[19], props.energySmearModel);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[20], props.useTimeSmearExpression ? 1 : 0);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[21], props.timeSmearExpression);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[22], props.useEnergySmearExpression ? 1 : 0);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[23], props.energySmearExpression);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[24], props.useSignalTimeExpression ? 1 : 0);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[25], props.signalTimeExpression);
            fAnalysisManager->FillNtupleIColumn(ntupleId, cols[26], props.usePileupTimeExpression ? 1 : 0);
            fAnalysisManager->FillNtupleSColumn(ntupleId, cols[27], props.pileupTimeExpression);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[28], props.darkCountRate_kHz);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[29], props.crossTalkProbability);
            fAnalysisManager->FillNtupleDColumn(ntupleId, cols[30], props.recoveryTime_ns);
            fAnalysisManager->AddNtupleRow(ntupleId);
        }
    }
}

/// @brief Returns the ntuple ID for a given dictionary by name.
/// @param name Dictionary name (e.g. "volume_dict", "material_dict").
/// @return Ntuple ID, or -1 if not found.
int DictionaryWriter::GetDictionaryNtupleId(const std::string& name) const {
    auto it = fDictionaryNtupleIds.find(name);
    if (it != fDictionaryNtupleIds.end()) return it->second;
    return -1;
}

/// @brief Returns the column ID vector for a given dictionary by name.
/// @param name Dictionary name.
/// @return Reference to column ID vector; empty vector if not found.
const std::vector<int>& DictionaryWriter::GetDictionaryColumnIds(const std::string& name) const {
    static std::vector<int> empty;
    auto it = fDictionaryColumnIds.find(name);
    if (it != fDictionaryColumnIds.end()) return it->second;
    return empty;
}

/// @brief Returns the ntuple ID of the property_history dictionary.
/// @return Ntuple ID, or -1 if not found.
int DictionaryWriter::GetPropertyHistoryNtupleId() const {
    auto it = fDictionaryNtupleIds.find("property_history");
    return (it != fDictionaryNtupleIds.end()) ? it->second : -1;
}

/// @brief Returns the column ID of the "run_id" column in property_history.
/// @return Column ID, or -1 if not found.
int DictionaryWriter::GetPropertyHistoryColRunId() const {
    auto it = fDictionaryColumnIds.find("property_history");
    return (it != fDictionaryColumnIds.end() && it->second.size() > 0) ? it->second[0] : -1;
}

/// @brief Returns the column ID of the "property_name" column in property_history.
/// @return Column ID, or -1 if not found.
int DictionaryWriter::GetPropertyHistoryColPropName() const {
    auto it = fDictionaryColumnIds.find("property_history");
    return (it != fDictionaryColumnIds.end() && it->second.size() > 1) ? it->second[1] : -1;
}

/// @brief Returns the column ID of the "value" column in property_history.
/// @return Column ID, or -1 if not found.
int DictionaryWriter::GetPropertyHistoryColValue() const {
    auto it = fDictionaryColumnIds.find("property_history");
    return (it != fDictionaryColumnIds.end() && it->second.size() > 2) ? it->second[2] : -1;
}