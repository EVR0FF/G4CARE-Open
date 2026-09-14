//==============================================================================
//
// G4CARE
//
// @file    ChemistryAction.cc
// @brief   Geant4-DNA chemistry setup at run start.
//
// @details
//   Configures G4DNAChemistryManager via UI commands at BeginOfRunAction:
//   loads the chemical species list, selects the time-step model
//   (SBS, IRT, IRT_syn), sets environment parameters (temperature,
//   pressure, density, start/end time) and reaction rate constants.
//   Populates ChemSpeciesRegistry in all threads. Uses canonical
//   species names for Geant4-DNA (e_aq instead of eaq).
//
//   This component uses the Geant4-DNA extension developed by the Geant4
//   Collaboration.
//
//   Configuration keys read:
//     CHEMISTRY.ENABLE
//     CHEMISTRY.SPECIES
//     CHEMISTRY.CHEM_LIST
//     CHEMISTRY.TIME_STEP_MODEL
//     CHEMISTRY.TEMPERATURE
//     CHEMISTRY.PRESSURE
//     CHEMISTRY.DENSITY
//     CHEMISTRY.START_TIME
//     CHEMISTRY.END_TIME
//     CHEMISTRY.REACTION
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

#include "ChemistryAction.hh"
#include "ConfigManager.hh"
#include "BeamAnalysis.hh"
#include "G4DNAChemistryManager.hh"
#include "G4UImanager.hh"
#include "G4Threading.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"
#include <map>

namespace {
const std::map<std::string, std::string> kCanonicalSpecies = {
    {"eaq",  "e_aq"},
    {"aq",   "e_aq"},
    {"e_aq", "e_aq"},
    {"OH",   "OH"},
    {"H",    "H"},
    {"H2O2", "H2O2"},
    {"H2",   "H2"},
    {"HO2",  "HO2"},
    {"O2",   "O2"},
    {"O2-",  "O2-"},
    {"O",    "O"},
    {"O-",   "O-"},
    {"H3O",  "H3O"},
    {"OHm",  "OHm"},
    {"H2Op", "H2O^+"}
};

// Normalize molecule names to the canonical Geant4-DNA notation.
std::string CanonicalSpeciesName(const std::string& name) {
    auto it = kCanonicalSpecies.find(name);
    return (it != kCanonicalSpecies.end()) ? it->second : name;
}

} // anonymous namespace

//------------------------------------------------------------------------------
// Initializes Geant4-DNA chemistry at the beginning of a run.
//
// Performs two phases:
// 1. In all threads: loads molecule list from CHEMISTRY.SPECIES
//    (or CHEMISTRY.CHEM_LIST) into ChemSpeciesRegistry.
// 2. Master thread only: applies /chem/... UI commands to configure
//    G4DNAChemistryManager (time-step model, environment parameters,
//    reaction rate constants).
//
// @param run  Pointer to current Geant4 run (unused).
//------------------------------------------------------------------------------
void ChemistryAction::BeginOfRunAction(const G4Run* run) {
    auto* cfg = ConfigManager::Instance();

    if (!cfg->GetBool("CHEMISTRY.ENABLE", false)) {
        if (G4Threading::IsMasterThread())
            G4cout << "ChemistryAction: chemistry is DISABLED in config." << G4endl;
        return;
    }

    auto speciesList = cfg->GetStringVector("CHEMISTRY.SPECIES");
    if (speciesList.empty()) {
        speciesList = cfg->GetStringVector("CHEMISTRY.CHEM_LIST");
    }

    // "All" = use the full set of species supported by G4CARE.
    bool useAllSpecies = (!speciesList.empty() && speciesList[0] == "All");
    if (useAllSpecies) {
        speciesList.clear();
        for (const auto& pair : kCanonicalSpecies) {
            // Exclude duplicates (eaq, aq → e_aq).
            if (pair.first == pair.second) {
                speciesList.push_back(pair.second);
            }
        }
        G4cout << "ChemistryAction: SPECIES=All — using full set of "
               << speciesList.size() << " species" << G4endl;
    }

    if (!speciesList.empty()) {
        auto* beam = BeamAnalysis::Instance();
        auto* chemReg = beam ? beam->GetChemSpeciesRegistry() : nullptr;
        if (chemReg) {
            for (const auto& mol : speciesList) {
                chemReg->Register(CanonicalSpeciesName(mol));
            }
            G4cout << "ChemistryAction: registered " << speciesList.size()
                   << " species in ChemSpeciesRegistry (thread="
                   << G4Threading::G4GetThreadId() << ")" << G4endl;
        }
    }

    if (!G4Threading::IsMasterThread()) return;

    auto* ui = G4UImanager::GetUIpointer();

    if (!speciesList.empty()) {
        for (const auto& mol : speciesList) {
            ui->ApplyCommand("/chem/chemistry/addMolecule " + CanonicalSpeciesName(mol));
        }
        G4cout << "ChemistryAction: loaded " << speciesList.size()
               << " species into G4DNA chemistry manager" << G4endl;
    }

    std::string modelStr = cfg->GetString("CHEMISTRY.TIME_STEP_MODEL", "SBS");
    ui->ApplyCommand("/process/chem/TimeStepModel " + modelStr);
    G4cout << "ChemistryAction: time step model = " << modelStr << G4endl;

    double temperature = cfg->GetValueWithUnits("CHEMISTRY.TEMPERATURE", 300.0 * kelvin);
    ui->ApplyCommand("/chem/diffusion/temperature "
                     + std::to_string(temperature / kelvin) + " kelvin");

    double pressure = cfg->GetValueWithUnits("CHEMISTRY.PRESSURE", 1.0 * atmosphere);
    ui->ApplyCommand("/chem/diffusion/pressure "
                     + std::to_string(pressure / atmosphere) + " atmosphere");

    if (cfg->HasKey("CHEMISTRY.DENSITY")) {
        double density = cfg->GetValueWithUnits("CHEMISTRY.DENSITY", 1.0 * g / cm3);
        ui->ApplyCommand("/chem/diffusion/density "
                         + std::to_string(density / (g / cm3)) + " g/cm3");
    }

    if (cfg->HasKey("CHEMISTRY.START_TIME")) {
        double startTime = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * ps);
        ui->ApplyCommand("/chem/time/start " + std::to_string(startTime / ps) + " ps");
    }

    if (cfg->HasKey("CHEMISTRY.END_TIME")) {
        double endTime = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * ns);
        ui->ApplyCommand("/chem/time/end " + std::to_string(endTime / ns) + " ns");
    }

    auto reactionKeys = cfg->GetSectionKeys("CHEMISTRY.REACTION");
    for (const auto& key : reactionKeys) {
        double rate = cfg->GetValueWithUnits("CHEMISTRY.REACTION." + key, 0.0);
        ui->ApplyCommand("/chem/reaction/modify " + key + " "
                         + std::to_string(rate));
    }

    G4cout << "ChemistryAction: chemistry parameters applied." << G4endl;
}

//------------------------------------------------------------------------------
// Reserved for future use.
//
// May be used to save chemistry state via
// G4DNAChemistryManager::WriteInto().
//
// @param run  Pointer to current Geant4 run (unused).
//------------------------------------------------------------------------------
void ChemistryAction::EndOfRunAction(const G4Run* run) {
    // Optionally: G4DNAChemistryManager::Instance()->WriteInto("chem_state.dat");
}