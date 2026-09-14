//==============================================================================
//
// G4CARE
//
// @file    ExpressionEvaluator.cc
// @brief   ExprTK-based expression evaluation engine with chemistry data
//          support, data table I/O, and Geant4 unit parsing.
//
// @details
//   Core expression evaluator for the G4CARE framework.  Provides:
//   - Single-expression evaluation with Geant4 unit support.
//     via EvaluateWithUnit().
//   - Precompilation of expressions (Precompile()) for fast repeated
//     evaluation in source generators and scoring scripts.
//   - Data table loading and registration (CSV, ROOT, IAEA) for
//     data-driven particle sources and activation models.
//   - Thread-safe compiled-expression cache with LRU eviction.
//   - ExprTK script execution with full variable context including
//     FilterVars and chemistry vectors (Execute()).
//   - Chemistry support: RegisterChemVectors(), SetGlobalSpeciesRegistry(),
//     SpeciesNameFunc for per-species naming in ExprTK scripts.
//   - Built-in output functions: g4cout (inline with Geant4 stream),
//     sprintf for formatted string construction.
//   - Built-in random distribution functions (Poisson, Binomial,
//     Exponential, Gauss, Uniform, Landau, etc.) and ion property
//     lookup functions (mass, charge, spin, lifetime).
//   - NIST/Geant4 unit constants registered in the ExprTK symbol table
//     (MeV, mm, gray, joule, etc.).
//
//   Configuration keys read: none directly (receives pre-parsed expressions).
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

#include "ExpressionEvaluator.hh"
#include "ConfigManager.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"
#include "G4UnitsTable.hh"
#include "G4IonTable.hh"
#include "G4NuclideTable.hh"
#include "G4NistManager.hh"
#include "G4NuclearLevelData.hh"
#include "G4LevelManager.hh"
#include "RDFReader.hh"
#include "IAEAReader.hh"
#include "ColumnTypes.hh"
// ChemSpeciesRegistry — forward-declared in .hh (SpeciesNameFunc uses pointer)

#include <iostream>
#include <regex>
#include <cmath>
#include <iomanip>

// #define exprtk_disable_rtl_io — REMOVED: enable print/println for user configs
#include "exprtk.hpp"

G4ThreadLocal std::unique_ptr<ExpressionEvaluator::ThreadLocalData> ExpressionEvaluator::fgTLS;
std::map<std::string, int> ExpressionEvaluator::fDataAliases;
G4ThreadLocal bool ExpressionEvaluator::fgAbortEventFlag = false;
G4ThreadLocal bool ExpressionEvaluator::fgKillTrackFlag = false;
THREAD_LOCAL std::unordered_map<std::string, std::shared_ptr<ExpressionEvaluator::CompiledExpression>> ExpressionEvaluator::fCache;
thread_local ExpressionEvaluator::OutputFormat ExpressionEvaluator::s_outFmt;

// Thread-local species_name function instance (shared by Precompile and SetGlobalSpeciesRegistry)
static thread_local ExpressionEvaluator::SpeciesNameFunc s_speciesName;

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

ExpressionEvaluator::PrecompiledExpr::PrecompiledExpr(const std::string& raw,
                                                       const std::vector<std::string>& varNames,
                                                       ExpressionEvaluator* eval)
    : unitMultiplier(1.0)
{
    if (raw.empty()) return;
    std::string trimmed = raw;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return;

    std::string mathExpr = trimmed;
    std::string unit;

    if (trimmed.size() >= 4 && trimmed[0] == '$' && trimmed[1] == '$') {
        size_t pos2 = trimmed.find("$$", 2);
        if (pos2 != std::string::npos) {
            mathExpr = trimmed.substr(2, pos2 - 2);
            unit = trimmed.substr(pos2 + 2);
            unit.erase(0, unit.find_first_not_of(" \t"));
        }
    } else {
        size_t lastSpace = trimmed.find_last_of(" \t");
        if (lastSpace != std::string::npos) {
            std::string possibleUnit = trimmed.substr(lastSpace + 1);
            if (G4UnitDefinition::IsUnitDefined(possibleUnit.c_str())) {
                unit = possibleUnit;
                mathExpr = trimmed.substr(0, lastSpace);
            }
        }
    }

    if (!unit.empty() && G4UnitDefinition::IsUnitDefined(unit.c_str())) {
        unitMultiplier = G4UnitDefinition::GetValueOf(unit.c_str());
    }

    expr = mathExpr;
    compiled = eval->Precompile(mathExpr, varNames);
    if (!unit.empty()) {
    unitMultiplier = G4UnitDefinition::GetValueOf(unit.c_str());
    
    } else { }
   
}

/// @brief Compile an ExprTK expression with named variables, registering
///        built-in functions, constants, Geant4 units, and chemistry vectors.
///
/// @param expr_str       Raw expression string.
/// @param var_names      Variable names to pre-register in the symbol table.
/// @param extraFunctions Additional user functions (from ExprsManager).
/// @param extraConstants Additional user constants (from ExprsManager).
/// @return               Shared pointer to CompiledExpression, or nullptr.
///
/// The compiled expression is cached in fCache for reuse.  Function
/// and constant references are thread-local static to avoid dangling
/// references after return.
std::shared_ptr<ExpressionEvaluator::CompiledExpression>
ExpressionEvaluator::Precompile(const std::string& expr_str,
                                 const std::vector<std::string>& var_names,
                                 const std::map<std::string, exprtk::igeneric_function<double>*>& extraFunctions,
                                 const std::map<std::string, double>& extraConstants)
{
    
    std::string trimmed = expr_str;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return nullptr;

    if (trimmed.find("$$") != std::string::npos) {
    return nullptr;
    }

    auto it = fCache.find(trimmed);
    if (it != fCache.end()) return it->second;

    auto compiled = std::make_unique<CompiledExpression>();
    exprtk::parser<double> parser;
    const auto& aliases = fDataAliases;
    for (const auto& pair : aliases) {
        compiled->symbol_table.add_constant(pair.first, static_cast<double>(pair.second));
    }
    
    // Static long-lived function instances to avoid dangling references
    static thread_local RandPoissonFunc s_randPoisson;
    static thread_local RandBinomialFunc s_randBinomial;
    static thread_local RandExponentialFunc s_randExponential;
    static thread_local RandGaussFunc s_randGauss;
    static thread_local RandUniformFunc s_randUniform;
    static thread_local RandBreitWignerFunc s_randBreitWigner;
    static thread_local RandBreitWignerCutFunc s_randBreitWignerCut;
    static thread_local RandLandauFunc s_randLandau;
    static thread_local RandGammaFunc s_randGamma;
    static thread_local RandChiSquareFunc s_randChiSquare;
    static thread_local RandStudentTFunc s_randStudentT;
    static thread_local RandLognormalFunc s_randLognormal;
    static thread_local RandRayleighFunc s_randRayleigh;
    static thread_local IonMassFunc s_ionMass;
    static thread_local IonChargeFunc s_ionCharge;
    static thread_local IonSpinFunc s_ionSpin;
    static thread_local IonMagMomentFunc s_ionMagMoment;
    static thread_local IonLifetimeFunc s_ionLifetime;
    static thread_local IonExcitationEnergyFunc s_ionExcitationEnergy;
    static thread_local OutputFunc s_outputFunc;
    static thread_local ErrorFunc s_errorFunc;
    static thread_local AbortEventFunc s_abortEvent;
    static thread_local KillTrackFunc s_killTrack;
    static thread_local DataRowsFunc s_dataRows;
    static thread_local DataCellFunc s_dataCell;

    compiled->symbol_table.add_function("rand_poisson", s_randPoisson);
    compiled->symbol_table.add_function("rand_binomial", s_randBinomial);
    compiled->symbol_table.add_function("rand_exponential", s_randExponential);
    compiled->symbol_table.add_function("rand_gauss", s_randGauss);
    compiled->symbol_table.add_function("rand_uniform", s_randUniform);
    compiled->symbol_table.add_function("rand_breitwigner", s_randBreitWigner);
    compiled->symbol_table.add_function("rand_breitwigner_cut", s_randBreitWignerCut);
    compiled->symbol_table.add_function("rand_landau", s_randLandau);
    compiled->symbol_table.add_function("rand_gamma", s_randGamma);
    compiled->symbol_table.add_function("rand_chisquare", s_randChiSquare);
    compiled->symbol_table.add_function("rand_student_t", s_randStudentT);
    compiled->symbol_table.add_function("rand_lognormal", s_randLognormal);
    compiled->symbol_table.add_function("rand_rayleigh", s_randRayleigh);
    static thread_local ExpressionEvaluator::Pamela2009AcceptFunc s_pamelaAccept;
    compiled->symbol_table.add_function("pamela_accept", s_pamelaAccept);

    compiled->symbol_table.add_function("ion_mass", s_ionMass);
    compiled->symbol_table.add_function("ion_charge", s_ionCharge);
    compiled->symbol_table.add_function("ion_spin", s_ionSpin);
    compiled->symbol_table.add_function("ion_magnetic_moment", s_ionMagMoment);
    compiled->symbol_table.add_function("ion_lifetime", s_ionLifetime);
    compiled->symbol_table.add_function("ion_excitation_energy", s_ionExcitationEnergy);
  
    compiled->symbol_table.add_function("data_rows", s_dataRows);
    compiled->symbol_table.add_function("data_cell", s_dataCell);

    compiled->symbol_table.add_function("g4cout", s_outputFunc);
    compiled->symbol_table.add_function("g4cerr", s_errorFunc);
    compiled->symbol_table.add_function("abort_event", s_abortEvent);
    compiled->symbol_table.add_function("kill_track", s_killTrack);

    // ── species_name ──
    compiled->symbol_table.add_function("species_name", s_speciesName);

    // ── format + println/print (io::package) ──
    static thread_local FormatFunc s_formatFunc;
    compiled->symbol_table.add_function("format", s_formatFunc);


    // Extra functions from ExpressionManager (user inline functions)
    for (const auto& [fname, fptr] : extraFunctions) {
        compiled->symbol_table.add_function(fname, *fptr);
    }

    // Extra constants from ExpressionManager (user variables + выражений)
    for (const auto& [cname, cval] : extraConstants) {
        compiled->symbol_table.add_constant(cname, cval);
    }

    compiled->symbol_table.add_constant("Avogadro", CLHEP::Avogadro);

    compiled->symbol_table.add_constant("c_light", CLHEP::c_light);
    compiled->symbol_table.add_constant("c_squared", CLHEP::c_squared);

    compiled->symbol_table.add_constant("h_Planck", CLHEP::h_Planck);
    compiled->symbol_table.add_constant("hbar_Planck", CLHEP::hbar_Planck);
    compiled->symbol_table.add_constant("hbarc", CLHEP::hbarc);
    compiled->symbol_table.add_constant("hbarc_squared", CLHEP::hbarc_squared);

    compiled->symbol_table.add_constant("electron_charge", CLHEP::electron_charge);
    compiled->symbol_table.add_constant("e_squared", CLHEP::e_squared);

    compiled->symbol_table.add_constant("electron_mass_c2", CLHEP::electron_mass_c2);
    compiled->symbol_table.add_constant("proton_mass_c2", CLHEP::proton_mass_c2);
    compiled->symbol_table.add_constant("neutron_mass_c2", CLHEP::neutron_mass_c2);
    compiled->symbol_table.add_constant("amu_c2", CLHEP::amu_c2);
    compiled->symbol_table.add_constant("amu", CLHEP::amu);

    compiled->symbol_table.add_constant("mu0", CLHEP::mu0);
    compiled->symbol_table.add_constant("epsilon0", CLHEP::epsilon0);

    compiled->symbol_table.add_constant("elm_coupling", CLHEP::elm_coupling);
    compiled->symbol_table.add_constant("fine_structure_const", CLHEP::fine_structure_const);
    compiled->symbol_table.add_constant("classic_electr_radius", CLHEP::classic_electr_radius);
    compiled->symbol_table.add_constant("electron_Compton_length", CLHEP::electron_Compton_length);
    compiled->symbol_table.add_constant("Bohr_radius", CLHEP::Bohr_radius);

    compiled->symbol_table.add_constant("alpha_rcl2", CLHEP::alpha_rcl2);
    compiled->symbol_table.add_constant("twopi_mc2_rcl2", CLHEP::twopi_mc2_rcl2);

    compiled->symbol_table.add_constant("Bohr_magneton", CLHEP::Bohr_magneton);
    compiled->symbol_table.add_constant("nuclear_magneton", CLHEP::nuclear_magneton);

    compiled->symbol_table.add_constant("k_Boltzmann", CLHEP::k_Boltzmann);

    compiled->symbol_table.add_constant("STP_Temperature", CLHEP::STP_Temperature);
    compiled->symbol_table.add_constant("STP_Pressure", CLHEP::STP_Pressure);
    compiled->symbol_table.add_constant("kGasThreshold", CLHEP::kGasThreshold);

    compiled->symbol_table.add_constant("universe_mean_density", CLHEP::universe_mean_density);

    // ── Derived SI conversion constants ──
    compiled->symbol_table.add_constant("joule", CLHEP::joule);  // MeV per Joule
    compiled->symbol_table.add_constant("gray",  CLHEP::gray);   // 1 Gy in CLHEP

    for (const auto& name : var_names) {
        auto ptr = std::make_unique<double>(0.0);
        compiled->symbol_table.add_variable(name, *ptr);
        compiled->varPtrs[name] = std::move(ptr);
    }

    // ── Chemistry vectors (registered at compile-time, filled at runtime) ──
    // reserve(50) ensures no reallocation when RegisterChemVectors resizes
    // Pre-allocate 50 elements so RegisterChemVectors::resize() never reallocates
    compiled->chemNc.reserve(50); compiled->chemNc.resize(50, 0.0);
    compiled->chemNt.reserve(50); compiled->chemNt.resize(50, 0.0);
    compiled->chemGc.reserve(50); compiled->chemGc.resize(50, 0.0);
    compiled->chemGt.reserve(50); compiled->chemGt.resize(50, 0.0);
    compiled->chemCharge.reserve(50); compiled->chemCharge.resize(50, 0.0);
    compiled->symbol_table.add_vector("Nc",     compiled->chemNc);
    compiled->symbol_table.add_vector("Nt",     compiled->chemNt);
    compiled->symbol_table.add_vector("Gc",     compiled->chemGc);
    compiled->symbol_table.add_vector("Gt",     compiled->chemGt);
    compiled->symbol_table.add_vector("charge", compiled->chemCharge);

    // Register CLHEP units BEFORE symbol_table registration (ExprTK requires this order)
    // Skip single-char units only (n=nano, k=kilo, m=meter, s=second...)
    // to avoid variable name conflicts while keeping cm, mm, nm, ps, ns, etc.
    auto& unitsTable = G4UnitDefinition::GetUnitsTable();
    for (G4UnitsCategory* category : unitsTable) {
        G4UnitsContainer& units = category->GetUnitsList();
        for (G4UnitDefinition* unit : units) {
            if (unit->GetSymbol().length() < 2) continue;
            compiled->symbol_table.add_constant(unit->GetSymbol(), unit->GetValue());
        }
    }

    compiled->expr.register_symbol_table(compiled->symbol_table);

    parser.settings().enable_all_control_structures();
    if (!parser.compile(trimmed, compiled->expr)) {
        G4cerr << "ExpressionEvaluator: failed to compile expression: " << trimmed << G4endl;
        for (std::size_t i = 0; i < parser.error_count(); ++i) {
            exprtk::parser_error::type error = parser.get_error(i);
            G4cerr << "  Error " << i << ": " << error.diagnostic << G4endl;
        }
        return nullptr;
    }

    auto shared = std::shared_ptr<CompiledExpression>(compiled.release());
    fCache[trimmed] = shared;
    return shared;
}

/// @brief Compile an ExprTK expression using FilterVar index-based storage.
///
/// Unlike Precompile(), this method maps FilterVar indices to compact
/// storage slots for high-performance per-step evaluation.  Only
/// FilterVars actually used in the expression are allocated (regex scan).
///
/// @param expr_str       Raw expression string.
/// @param var_indices    FilterVar enum values for each named variable.
/// @param var_names      Variable display names (matched in expression).
/// @param extraConstants Additional user constants.
/// @param extraFunctions Additional user functions.
/// @param extraVarNames  Additional mutable variable names.
/// @return               Shared pointer to CompiledExpression, or nullptr.
std::shared_ptr<ExpressionEvaluator::CompiledExpression>
ExpressionEvaluator::PrecompileForFilter(const std::string& expr_str,
                                         const std::vector<FilterVar>& var_indices,
                                         const std::vector<std::string>& var_names,
                                         const std::map<std::string, double>& extraConstants,
                                         const std::map<std::string, exprtk::igeneric_function<double>*>& extraFunctions,
                                         const std::vector<std::string>& extraVarNames)
{
    std::string trimmed = expr_str;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return nullptr;

    // Static long-lived instances so references survive after tmpEval destroyed
    static thread_local RandPoissonFunc s_randPoisson;
    static thread_local RandBinomialFunc s_randBinomial;
    static thread_local RandExponentialFunc s_randExponential;
    static thread_local RandGaussFunc s_randGauss;
    static thread_local RandUniformFunc s_randUniform;
    static thread_local RandBreitWignerFunc s_randBreitWigner;
    static thread_local RandBreitWignerCutFunc s_randBreitWignerCut;
    static thread_local RandLandauFunc s_randLandau;
    static thread_local RandGammaFunc s_randGamma;
    static thread_local RandChiSquareFunc s_randChiSquare;
    static thread_local RandStudentTFunc s_randStudentT;
    static thread_local RandLognormalFunc s_randLognormal;
    static thread_local RandRayleighFunc s_randRayleigh;
    static thread_local IonMassFunc s_ionMass;
    static thread_local IonChargeFunc s_ionCharge;
    static thread_local IonSpinFunc s_ionSpin;
    static thread_local IonMagMomentFunc s_ionMagMoment;
    static thread_local IonLifetimeFunc s_ionLifetime;
    static thread_local IonExcitationEnergyFunc s_ionExcitationEnergy;
    static thread_local OutputFunc s_outputFunc;
    static thread_local ErrorFunc s_errorFunc;
    static thread_local AbortEventFunc s_abortEventFunc;
    static thread_local KillTrackFunc s_killTrackFunc;
    static thread_local DataRowsFunc s_dataRows;
    static thread_local DataCellFunc s_dataCell;

    auto compiled = std::make_unique<CompiledExpression>();
    exprtk::parser<double> parser;

    const auto& aliases = fDataAliases;
    for (const auto& pair : aliases) {
        compiled->symbol_table.add_constant(pair.first, static_cast<double>(pair.second));
    }

    compiled->symbol_table.add_function("rand_poisson", s_randPoisson);
    compiled->symbol_table.add_function("rand_binomial", s_randBinomial);
    compiled->symbol_table.add_function("rand_exponential", s_randExponential);
    compiled->symbol_table.add_function("rand_gauss", s_randGauss);
    compiled->symbol_table.add_function("rand_uniform", s_randUniform);
    compiled->symbol_table.add_function("rand_breitwigner", s_randBreitWigner);
    compiled->symbol_table.add_function("rand_breitwigner_cut", s_randBreitWignerCut);
    compiled->symbol_table.add_function("rand_landau", s_randLandau);
    compiled->symbol_table.add_function("rand_gamma", s_randGamma);
    compiled->symbol_table.add_function("rand_chisquare", s_randChiSquare);
    compiled->symbol_table.add_function("rand_student_t", s_randStudentT);
    compiled->symbol_table.add_function("rand_lognormal", s_randLognormal);
    compiled->symbol_table.add_function("rand_rayleigh", s_randRayleigh);

    compiled->symbol_table.add_function("ion_mass", s_ionMass);
    compiled->symbol_table.add_function("ion_charge", s_ionCharge);
    compiled->symbol_table.add_function("ion_spin", s_ionSpin);
    compiled->symbol_table.add_function("ion_magnetic_moment", s_ionMagMoment);
    compiled->symbol_table.add_function("ion_lifetime", s_ionLifetime);
    compiled->symbol_table.add_function("ion_excitation_energy", s_ionExcitationEnergy);

    compiled->symbol_table.add_function("data_rows", s_dataRows);
    compiled->symbol_table.add_function("data_cell", s_dataCell);

    compiled->symbol_table.add_function("g4cout", s_outputFunc);
    compiled->symbol_table.add_function("g4cerr", s_errorFunc);
    compiled->symbol_table.add_function("abort_event", s_abortEventFunc);
    compiled->symbol_table.add_function("kill_track", s_killTrackFunc);

    // ── format — thread-local output precision for println/print ──
    static thread_local FormatFunc s_formatFunc2;
    compiled->symbol_table.add_function("format", s_formatFunc2);


    // Extra functions from ExpressionManager (user inline functions)
    for (const auto& [fname, fptr] : extraFunctions) {
        compiled->symbol_table.add_function(fname, *fptr);
    }

    // Extra constants from ExpressionManager (user variables — := constants)
    for (const auto& [cname, cval] : extraConstants) {
        compiled->symbol_table.add_constant(cname, cval);
    }

    // Extra variable names from ExpressionManager (SCRIPT expressions — mutable, updated at runtime)
    for (const auto& vname : extraVarNames) {
        auto ptr = std::make_unique<double>(0.0);
        compiled->symbol_table.add_variable(vname, *ptr);
        compiled->varPtrs[vname] = std::move(ptr);
    }

    // Физические константы (полный список из Precompile)
    compiled->symbol_table.add_constant("Avogadro", CLHEP::Avogadro);
    compiled->symbol_table.add_constant("c_light", CLHEP::c_light);
    compiled->symbol_table.add_constant("c_squared", CLHEP::c_squared);
    compiled->symbol_table.add_constant("h_Planck", CLHEP::h_Planck);
    compiled->symbol_table.add_constant("hbar_Planck", CLHEP::hbar_Planck);
    compiled->symbol_table.add_constant("hbarc", CLHEP::hbarc);
    compiled->symbol_table.add_constant("hbarc_squared", CLHEP::hbarc_squared);
    compiled->symbol_table.add_constant("electron_charge", CLHEP::electron_charge);
    compiled->symbol_table.add_constant("e_squared", CLHEP::e_squared);
    compiled->symbol_table.add_constant("electron_mass_c2", CLHEP::electron_mass_c2);
    compiled->symbol_table.add_constant("proton_mass_c2", CLHEP::proton_mass_c2);
    compiled->symbol_table.add_constant("neutron_mass_c2", CLHEP::neutron_mass_c2);
    compiled->symbol_table.add_constant("amu_c2", CLHEP::amu_c2);
    compiled->symbol_table.add_constant("amu", CLHEP::amu);
    compiled->symbol_table.add_constant("mu0", CLHEP::mu0);
    compiled->symbol_table.add_constant("epsilon0", CLHEP::epsilon0);
    compiled->symbol_table.add_constant("elm_coupling", CLHEP::elm_coupling);
    compiled->symbol_table.add_constant("fine_structure_const", CLHEP::fine_structure_const);
    compiled->symbol_table.add_constant("classic_electr_radius", CLHEP::classic_electr_radius);
    compiled->symbol_table.add_constant("electron_Compton_length", CLHEP::electron_Compton_length);
    compiled->symbol_table.add_constant("Bohr_radius", CLHEP::Bohr_radius);
    compiled->symbol_table.add_constant("alpha_rcl2", CLHEP::alpha_rcl2);
    compiled->symbol_table.add_constant("twopi_mc2_rcl2", CLHEP::twopi_mc2_rcl2);
    compiled->symbol_table.add_constant("Bohr_magneton", CLHEP::Bohr_magneton);
    compiled->symbol_table.add_constant("nuclear_magneton", CLHEP::nuclear_magneton);
    compiled->symbol_table.add_constant("k_Boltzmann", CLHEP::k_Boltzmann);
    compiled->symbol_table.add_constant("STP_Temperature", CLHEP::STP_Temperature);
    compiled->symbol_table.add_constant("STP_Pressure", CLHEP::STP_Pressure);
    compiled->symbol_table.add_constant("kGasThreshold", CLHEP::kGasThreshold);
    compiled->symbol_table.add_constant("universe_mean_density", CLHEP::universe_mean_density);

    // Pre-register only FilterVar that are actually used in the expression
    size_t nVars = var_names.size();
    std::vector<size_t> usedIndices;
    usedIndices.reserve(nVars);
    {
        auto isWordChar = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        for (size_t i = 0; i < nVars; ++i) {
            const std::string& vname = var_names[i];
            size_t pos = 0;
            while (pos < trimmed.size()) {
                pos = trimmed.find(vname, pos);
                if (pos == std::string::npos) break;
                bool leftOk = (pos == 0) || !isWordChar(trimmed[pos - 1]);
                bool rightOk = (pos + vname.size() >= trimmed.size())
                                   || !isWordChar(trimmed[pos + vname.size()]);
                if (leftOk && rightOk) {
                    usedIndices.push_back(i);
                    break;
                }
                ++pos;
            }
        }
    }

    size_t nUsed = usedIndices.size();
    compiled->storage.resize(nUsed, 0.0);
    compiled->var_mapping.clear();
    compiled->var_mapping.reserve(nUsed);
    for (size_t j = 0; j < nUsed; ++j) {
        size_t i = usedIndices[j];
        compiled->symbol_table.add_variable(var_names[i], compiled->storage[j]);
        compiled->var_mapping.emplace_back(var_indices[i], j);
    }

    compiled->expr.register_symbol_table(compiled->symbol_table);

    if (!parser.compile(trimmed, compiled->expr)) {
        G4cerr << "ExpressionEvaluator: failed to compile expression: " << trimmed << G4endl;
        for (std::size_t i = 0; i < parser.error_count(); ++i) {
            exprtk::parser_error::type error = parser.get_error(i);
            G4cerr << "  Error " << i << ": " << error.diagnostic << G4endl;
        }
        return nullptr;
    }

    auto shared = std::shared_ptr<CompiledExpression>(compiled.release());
    return shared;
}

/// @brief Register a DATA-block alias for use in ExprTK expressions.
/// @param name  Alias name (e.g. "xenon_table").
/// @param index Index into the thread-local reader list.
void ExpressionEvaluator::RegisterDataAlias(const std::string& name, int index) {
    fDataAliases[name] = index;
}

/// @brief Look up a previously registered DATA alias.
/// @param name Alias name.
/// @return Reader index, or -1 if not found.
int ExpressionEvaluator::GetDataAlias(const std::string& name) {
    auto it = fDataAliases.find(name);
    return (it != fDataAliases.end()) ? it->second : -1;
}

double ExpressionEvaluator::Execute(CompiledExpression* compiled,
                                     const std::map<std::string, double>& values) {
    if (!compiled) return 0.0;
    for (const auto& [name, val] : values) {
        auto it = compiled->varPtrs.find(name);
        if (it != compiled->varPtrs.end()) {
            *(it->second) = val;
        }
    }
    return compiled->expr.value();
}

double ExpressionEvaluator::Execute(CompiledExpression* compiled, const FilterVars& vars) const {
    if (!compiled) return 0.0;

    for (const auto& [fv, idx] : compiled->var_mapping) {
        compiled->storage[idx] = vars[static_cast<size_t>(fv)];
    }

    return compiled->expr.value();
}

double* ExpressionEvaluator::GetVariablePtr(CompiledExpression* compiled, const std::string& name) {
    if (!compiled) return nullptr;
    auto it = compiled->varPtrs.find(name);
    return (it != compiled->varPtrs.end()) ? it->second.get() : nullptr;
}

double ExpressionEvaluator::VertexAttrFunc::operator()(const double& idx) {
    if (!fVertexResultsPtr || !(*fVertexResultsPtr)) {
        G4cerr << "ExpressionEvaluator: vertex results pointer is null" << G4endl;
        return 0.0;
    }
    const auto& vertexResults = **fVertexResultsPtr;
    int i = static_cast<int>(std::floor(idx + 0.5));
    if (i < 0 || i >= static_cast<int>(vertexResults.size())) {
        G4cerr << "ExpressionEvaluator: vertex index " << i << " out of range (0.." << vertexResults.size()-1 << ")" << G4endl;
        return 0.0;
    }
    const auto& vdata = vertexResults[i];
    auto it = vdata.find(fAttr);
    if (it == vdata.end()) {
        G4cerr << "ExpressionEvaluator: vertex attribute '" << fAttr << "' not found for index " << i << G4endl;
        return 0.0;
    }
    return it->second;
}

double ExpressionEvaluator::RandPoissonFunc::operator()(const double& mean) {
    if (mean <= 0.0) {
        G4cerr << "ExpressionEvaluator: Poisson mean must be positive, got " << mean << G4endl;
        return 0.0;
    }
    return CLHEP::RandPoissonQ::shoot(G4Random::getTheEngine(), mean);
}

double ExpressionEvaluator::RandBinomialFunc::operator()(const double& n, const double& p) {
    if (n < 0 || p < 0.0 || p > 1.0) {
        G4cerr << "ExpressionEvaluator: Binomial n=" << n << ", p=" << p << " invalid" << G4endl;
        return 0.0;
    }
    return CLHEP::RandBinomial::shoot(G4Random::getTheEngine(), static_cast<long>(n), p);
}

double ExpressionEvaluator::RandExponentialFunc::operator()(const double& lambda) {
    if (lambda <= 0.0) {
        G4cerr << "ExpressionEvaluator: Exponential rate lambda must be positive, got " << lambda << G4endl;
        return 0.0;
    }
    return CLHEP::RandExponential::shoot(G4Random::getTheEngine(), 1.0/lambda);
}

double ExpressionEvaluator::RandGaussFunc::operator()(const double& mean, const double& sigma) {
    if (sigma < 0.0) {
        G4cerr << "ExpressionEvaluator: Gauss sigma must be non-negative, got " << sigma << G4endl;
        return mean;
    }
    if (sigma == 0.0) return mean;
    return CLHEP::RandGauss::shoot(G4Random::getTheEngine(), mean, sigma);
}

double ExpressionEvaluator::RandUniformFunc::operator()(const double& min, const double& max) {
    if (min >= max) {
        G4cerr << "ExpressionEvaluator: Uniform min (" << min << ") >= max (" << max << ")" << G4endl;
        return min;
    }
    return CLHEP::RandFlat::shoot(G4Random::getTheEngine(), min, max);
}

double ExpressionEvaluator::RandBreitWignerFunc::operator()(const double& mean, const double& gamma) {
    if (gamma <= 0.0) {
        G4cerr << "ExpressionEvaluator: Breit-Wigner gamma must be positive, got " << gamma << G4endl;
        return mean;
    }
    return CLHEP::RandBreitWigner::shoot(G4Random::getTheEngine(), mean, gamma);
}

double ExpressionEvaluator::RandBreitWignerCutFunc::operator()(const double& mean, const double& gamma, const double& cut) {
    if (gamma <= 0.0 || cut <= 0.0) {
        G4cerr << "ExpressionEvaluator: Breit-Wigner gamma and cut must be positive" << G4endl;
        return mean;
    }
    return CLHEP::RandBreitWigner::shoot(G4Random::getTheEngine(), mean, gamma, cut);
}

double ExpressionEvaluator::RandLandauFunc::operator()() {
    return CLHEP::RandLandau::shoot(G4Random::getTheEngine());
}

double ExpressionEvaluator::RandGammaFunc::operator()(const double& k, const double& lambda) {
    if (k <= 0.0 || lambda <= 0.0) {
        G4cerr << "ExpressionEvaluator: Gamma parameters must be positive: k=" << k << ", lambda=" << lambda << G4endl;
        return 0.0;
    }
    return CLHEP::RandGamma::shoot(G4Random::getTheEngine(), k, lambda);
}

double ExpressionEvaluator::RandChiSquareFunc::operator()(const double& df) {
    if (df <= 0.0) {
        G4cerr << "ExpressionEvaluator: ChiSquare degrees of freedom must be positive, got " << df << G4endl;
        return 0.0;
    }
    return CLHEP::RandChiSquare::shoot(G4Random::getTheEngine(), df);
}

double ExpressionEvaluator::RandStudentTFunc::operator()(const double& n) {
    if (n <= 0.0) {
        G4cerr << "ExpressionEvaluator: StudentT n must be positive, got " << n << G4endl;
        return 0.0;
    }
    return CLHEP::RandStudentT::shoot(G4Random::getTheEngine(), n);
}

double ExpressionEvaluator::RandLognormalFunc::operator()(const double& zeta, const double& sigma) {
    if (sigma <= 0.0) {
        G4cerr << "ExpressionEvaluator: lognormal sigma must be positive, got " << sigma << G4endl;
        return 0.0;
    }
    double gauss = CLHEP::RandGaussQ::shoot(G4Random::getTheEngine(), zeta, sigma);
    return std::exp(gauss);
}

double ExpressionEvaluator::RandRayleighFunc::operator()(const double& sigma) {
    if (sigma <= 0.0) {
        G4cerr << "ExpressionEvaluator: rayleigh sigma must be positive, got " << sigma << G4endl;
        return 0.0;
    }
    double u = CLHEP::RandFlat::shoot(G4Random::getTheEngine(), 0.0, 1.0);
    return sigma * std::sqrt(-2.0 * std::log(u));
}

ExpressionEvaluator::IonMassFunc::IonMassFunc() : exprtk::ifunction<double>(2) {}

double ExpressionEvaluator::IonMassFunc::operator()(const double& Z, const double& A) {
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));
    if (Z_int <= 0 || A_int <= 0) {
        G4cerr << "ExpressionEvaluator: ion_mass called with invalid Z/A: " << Z_int << "/" << A_int << G4endl;
        return 0.0;
    }
    G4ParticleDefinition* ion = G4IonTable::GetIonTable()->GetIon(Z_int, A_int, 0.0);
    if (!ion) {
        G4cerr << "ExpressionEvaluator: ion not found for Z=" << Z_int << " A=" << A_int << G4endl;
        return 0.0;
    }
    return ion->GetPDGMass();
}

ExpressionEvaluator::IonChargeFunc::IonChargeFunc() : exprtk::ifunction<double>(2) {}

double ExpressionEvaluator::IonChargeFunc::operator()(const double& Z, const double& A) {
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));
    if (Z_int <= 0 || A_int <= 0) return 0.0;
    G4ParticleDefinition* ion = G4IonTable::GetIonTable()->GetIon(Z_int, A_int, 0.0);
    if (!ion) return 0.0;
    return ion->GetPDGCharge();
}

ExpressionEvaluator::IonSpinFunc::IonSpinFunc() : exprtk::ifunction<double>(2) {}

double ExpressionEvaluator::IonSpinFunc::operator()(const double& Z, const double& A) {
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));
    if (Z_int <= 0 || A_int <= 0) return 0.0;
    G4ParticleDefinition* ion = G4IonTable::GetIonTable()->GetIon(Z_int, A_int, 0.0);
    if (!ion) return 0.0;
    return ion->GetPDGSpin();
}

ExpressionEvaluator::IonMagMomentFunc::IonMagMomentFunc() : exprtk::ifunction<double>(2) {}

double ExpressionEvaluator::IonMagMomentFunc::operator()(const double& Z, const double& A) {
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));
    if (Z_int <= 0 || A_int <= 0) return 0.0;
    G4ParticleDefinition* ion = G4IonTable::GetIonTable()->GetIon(Z_int, A_int, 0.0);
    if (!ion) return 0.0;
    return ion->GetPDGMagneticMoment();
}

ExpressionEvaluator::IonLifetimeFunc::IonLifetimeFunc(): exprtk::ifunction<double>(3) {}

double ExpressionEvaluator::IonLifetimeFunc::operator()(
    const double& Z, const double& A, const double& excitation)
{
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));

    if (Z_int <= 0 || A_int <= 0 || excitation < 0.0) {
        G4cerr << "ExpressionEvaluator: ion_lifetime called with invalid parameters: "
               << "Z=" << Z_int << ", A=" << A_int << ", excitation=" << excitation << G4endl;
        return 0.0;
    }

    const G4LevelManager* lvlMan = G4NuclearLevelData::GetInstance()
        ->GetLevelManager(Z_int, A_int);

    if (!lvlMan) {
        G4cerr << "ExpressionEvaluator: No level manager found for Z=" << Z_int
               << " A=" << A_int << G4endl;
        return 0.0;
    }

    std::size_t idx = lvlMan->NearestLevelIndex(excitation);

    if (idx >= lvlMan->NumberOfTransitions()) {
        G4cerr << "ExpressionEvaluator: Internal error: NearestLevelIndex returned invalid index "
               << idx << G4endl;
        return 0.0;
    }

    G4double lifeTime = lvlMan->LifeTime(idx);

    return lifeTime;
}

ExpressionEvaluator::IonExcitationEnergyFunc::IonExcitationEnergyFunc()
    : exprtk::ifunction<double>(3)
{}

double ExpressionEvaluator::IonExcitationEnergyFunc::operator()(
    const double& Z, const double& A, const double& level_index)
{
    G4int Z_int = static_cast<G4int>(std::round(Z));
    G4int A_int = static_cast<G4int>(std::round(A));
    G4int lvl = static_cast<G4int>(std::round(level_index));

    if (Z_int <= 0 || A_int <= 0 || lvl < 0) {
        G4cerr << "ExpressionEvaluator: ion_excitation_energy called with invalid parameters: "
               << "Z=" << Z_int << ", A=" << A_int << ", level=" << lvl << G4endl;
        return 0.0;
    }

    const G4LevelManager* lvlMan = G4NuclearLevelData::GetInstance()
        ->GetLevelManager(Z_int, A_int);

    if (!lvlMan) {
        G4cerr << "ExpressionEvaluator: No level manager found for Z=" << Z_int
               << " A=" << A_int << G4endl;
        return 0.0;
    }

    if (lvl >= static_cast<G4int>(lvlMan->NumberOfTransitions())) {
        G4cerr << "ExpressionEvaluator: Level index " << lvl << " out of range (0.."
               << lvlMan->NumberOfTransitions()-1 << ") for Z=" << Z_int << " A=" << A_int << G4endl;
        return 0.0;
    }

    G4double energy = lvlMan->LevelEnergy(lvl);

    return energy;
}

int ExpressionEvaluator::LoadROOTFile(const std::string& spec) {
    if (!fgTLS) fgTLS = std::make_unique<ThreadLocalData>();

    std::string filename = spec;
    std::string treename = "tree";
    size_t colon = spec.find(':');
    if (colon != std::string::npos) {
        filename = spec.substr(0, colon);
        treename = spec.substr(colon + 1);
    }

    auto it = fgTLS->fileIndexMap.find(spec);
    if (it != fgTLS->fileIndexMap.end()) return it->second;

    auto reader = std::make_shared<RDFReader>();
    if (!reader->LoadROOTFile({filename}, treename)) {
        G4cerr << "ExpressionEvaluator: Failed to load ROOT file " << spec << G4endl;
        return -1;
    }

    int idx = fgTLS->readers.size();
    fgTLS->readers.push_back(reader);
    fgTLS->fileIndexMap[spec] = idx;
    return idx;
}

int ExpressionEvaluator::LoadCSVFile(const std::string& filename, bool hasHeader, char delimiter, int skip) {
    if (!fgTLS) fgTLS = std::make_unique<ThreadLocalData>();

    auto it = fgTLS->fileIndexMap.find(filename);
    if (it != fgTLS->fileIndexMap.end()) return it->second;

    auto reader = std::make_shared<RDFReader>();
    if (!reader->LoadCSVFile(filename, hasHeader, delimiter, skip)) {
        G4cerr << "ExpressionEvaluator: Failed to load CSV file " << filename << G4endl;
        return -1;
    }

    int idx = fgTLS->readers.size();
    fgTLS->readers.push_back(reader);
    fgTLS->fileIndexMap[filename] = idx;
    return idx;
}

int ExpressionEvaluator::LoadIAEAFile(const std::string& filename) {
    if (!fgTLS) fgTLS = std::make_unique<ThreadLocalData>();

    auto it = fgTLS->fileIndexMap.find(filename);
    if (it != fgTLS->fileIndexMap.end()) return it->second;

    auto reader = std::make_shared<IAEAReader>();
    if (!reader->Load(filename)) {
        G4cerr << "ExpressionEvaluator: Failed to load IAEA file " << filename << G4endl;
        return -1;
    }

    int idx = fgTLS->readers.size();
    fgTLS->readers.push_back(reader);
    fgTLS->fileIndexMap[filename] = idx;
    return idx;
}

std::shared_ptr<const DataReader> ExpressionEvaluator::GetReader(int idx) {
    if (!fgTLS || idx < 0 || idx >= (int)fgTLS->readers.size()) return nullptr;
    return fgTLS->readers[idx];
}

/// @brief Evaluate a single expression string, optionally followed by a Geant4 unit.
///
/// @param input         Raw expression (e.g. "sqrt(x*x + y*y) mm").
/// @param variables     Name → value map for ExprTK context.
/// @param vertexResults Per-vertex data for vertex().attr() calls (unused).
/// @return              Numeric result in Geant4 internal units.
///
/// Tries G4UIcmdWithADoubleAndUnit first for plain numbers with units.
/// Falls back to ExprTK compilation via Precompile().
double ExpressionEvaluator::EvaluateWithUnit(const std::string& input,
                                             const std::map<std::string, double>& variables,
                                             const std::vector<std::map<std::string, double>>& vertexResults)
{
    if (input.empty()) return 0.0;

    std::string trimmed = input;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return 0.0;

    G4cout << "ExpressionEvaluator::EvaluateWithUnit - Input: '" << input << "'" 
           << " | Trimmed: '" << trimmed << "'" << G4endl;

    // Support ExprTK-style "expr*UNIT" format (e.g. "27*cm", "rand_uniform(0,10)*MeV")
    size_t starPos = trimmed.find('*');
    if (starPos != std::string::npos && starPos > 0) {
        std::string numPart = Trim(trimmed.substr(0, starPos));
        std::string unitPart = Trim(trimmed.substr(starPos + 1));
        if (G4UnitDefinition::IsUnitDefined(unitPart.c_str())) {
            // Try fast path: parse as plain number
            try { double numVal = std::stod(numPart); return numVal * G4UnitDefinition::GetValueOf(unitPart.c_str()); }
            catch (const std::exception&) {}
            // Fallback: compile via ExprTK (unit constants like MeV/mm/cm are registered)
            auto compiled = Precompile(numPart, {});
            double val = compiled ? compiled->expr.value() : 0.0;
            return val * G4UnitDefinition::GetValueOf(unitPart.c_str());
        }
    }
    // Legacy "NUMBER UNIT" format via Geant4 parser
    try {
        double val = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(trimmed.c_str());
        G4cout << "ExpressionEvaluator: parsed as simple number with unit: " << val << G4endl;
        return val;
    } catch (const std::exception& e) {
        G4cout << "ExpressionEvaluator: not a simple number, parsing as expression..." << G4endl;
    }

    std::string exprPart = trimmed;
    std::string unitPart;

    size_t unitDelim = exprPart.find_last_of(" \t");
    if (unitDelim != std::string::npos) {
        std::string possibleUnit = exprPart.substr(unitDelim + 1);
   
        possibleUnit = Trim(possibleUnit);
        G4cout << "ExpressionEvaluator::EvaluateWithUnit - Possible unit: '" 
               << possibleUnit << "'" << G4endl;

        if (G4UnitDefinition::IsUnitDefined(possibleUnit.c_str())) {
            unitPart = possibleUnit;
            exprPart = exprPart.substr(0, unitDelim);
            if (!exprPart.empty() && exprPart.back() == ' ') {
                exprPart.pop_back();
            }
            exprPart = Trim(exprPart);
            G4cout << "ExpressionEvaluator::EvaluateWithUnit - Unit recognized: '" 
                   << unitPart << "' | Multiplier: " 
                   << G4UnitDefinition::GetValueOf(unitPart.c_str()) 
                   << " | exprPart after trim: '" << exprPart << "'" << G4endl;
        } else {
            G4cout << "ExpressionEvaluator::EvaluateWithUnit - Unit NOT recognized: '" 
                   << possibleUnit << "'" << G4endl;
        }
    }

    std::vector<std::string> varNames;
    for (const auto& v : variables) varNames.push_back(v.first);
    
    auto compiled = Precompile(exprPart, varNames);
    double val = compiled ? Execute(compiled.get(), variables) : 0.0;

    if (!unitPart.empty()) {
        val *= G4UnitDefinition::GetValueOf(unitPart.c_str());
        G4cout << "EvaluateWithUnit - Final value: " << val 
               << " (expr=" << val / G4UnitDefinition::GetValueOf(unitPart.c_str())
               << " * unit=" << G4UnitDefinition::GetValueOf(unitPart.c_str()) << ")" << G4endl;
    } else {
        G4cout << "ExpressionEvaluator::EvaluateWithUnit - Final value (no unit): " 
               << val << G4endl;
    }
    return val;
}

/// @brief Check if a string is an ExprTK expression (delegates to ConfigManager).
bool ExpressionEvaluator::IsExpression(const std::string& str) {
    return ConfigManager::Instance()->IsExpression(str);
}

//------------------------------Evaluator--------------------------------------//

/// @brief Constructor — empty; compilation happens lazily via Precompile().
ExpressionEvaluator::ExpressionEvaluator() {}

ExpressionEvaluator::~ExpressionEvaluator() = default;

const std::vector<FilterVar>& ExpressionEvaluator::GetUsedVariables(const CompiledExpression* compiled) const {
    static thread_local std::vector<FilterVar> empty;
    static thread_local std::vector<FilterVar> result;
    if (!compiled) return empty;
    result.clear();
    result.reserve(compiled->var_mapping.size());
    for (const auto& p : compiled->var_mapping) {
        result.push_back(p.first);
    }
    return result;
}

double ExpressionEvaluator::DataRowsFunc::operator()(const double& idx) {
    int i = static_cast<int>(std::round(idx));
    auto reader = ExpressionEvaluator::GetReader(i);
    if (!reader) {
        G4cerr << "DataRowsFunc: reader not found for idx=" << i << G4endl;
        return 0.0;
    }
    return static_cast<double>(reader->GetNumberOfRows());
}

double ExpressionEvaluator::DataCellFunc::operator()(const double& idx, const double& row, const double& col) {
    
    int i = static_cast<int>(std::round(idx));
    size_t r = static_cast<size_t>(std::floor(row + 0.5));
    size_t c = static_cast<size_t>(std::floor(col + 0.5));
    auto reader = ExpressionEvaluator::GetReader(i);
    if (!reader) return 0.0;
    auto opt = reader->GetCell(r, c);
    if (!opt.has_value()) {
        G4cerr << "DataCellFunc: invalid cell (" << r << "," << c << ") in file idx=" << i << G4endl;
        return 0.0;
    }
    double val = opt.value();
    return val; 
}

// ── Output / Error / Control functions ──

double ExpressionEvaluator::OutputFunc::operator()(const std::size_t& /*ps_index*/,
                                                    exprtk::igeneric_function<double>::parameter_list_t parameters) {
    typedef exprtk::igeneric_function<double>::generic_type GT;
    auto oldFlags = std::cout.flags();
    auto oldPrec  = std::cout.precision();
    if (s_outFmt.scientific)
        std::cout << std::scientific;
    else
        std::cout << std::fixed;
    std::cout.precision(s_outFmt.precision);

    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].type == GT::e_string) {
            std::cout << std::string(static_cast<const char*>(parameters[i].data), parameters[i].size);
        } else {
            std::cout << " " << GT::scalar_view(parameters[i])();
        }
    }
    std::cout << std::endl;

    std::cout.flags(oldFlags);
    std::cout.precision(oldPrec);
    return 0.0;
}

double ExpressionEvaluator::ErrorFunc::operator()(const std::size_t& /*ps_index*/,
                                                   exprtk::igeneric_function<double>::parameter_list_t parameters) {
    typedef exprtk::igeneric_function<double>::generic_type GT;
    auto oldFlags = std::cerr.flags();
    auto oldPrec  = std::cerr.precision();
    if (s_outFmt.scientific)
        std::cerr << std::scientific;
    else
        std::cerr << std::fixed;
    std::cerr.precision(s_outFmt.precision);

    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].type == GT::e_string) {
            std::cerr << std::string(static_cast<const char*>(parameters[i].data), parameters[i].size);
        } else {
            std::cerr << " " << GT::scalar_view(parameters[i])();
        }
    }
    std::cerr << std::endl;

    std::cerr.flags(oldFlags);
    std::cerr.precision(oldPrec);
    return 0.0;
}

double ExpressionEvaluator::AbortEventFunc::operator()() {
    fgAbortEventFlag = true;
    return 1.0;
}

double ExpressionEvaluator::KillTrackFunc::operator()() {
    fgKillTrackFlag = true;
    return 1.0;
}

// ── species_name implementation ──
ExpressionEvaluator::SpeciesNameFunc::SpeciesNameFunc()
    : igeneric_function<double>("T", igeneric_function<double>::e_rtrn_string) {}

double ExpressionEvaluator::SpeciesNameFunc::operator()(
    std::string& result,
    exprtk::igeneric_function<double>::parameter_list_t parameters)
{
    result.clear();
    if (!fRegistry || parameters.empty()) return 0.0;

    typedef typename exprtk::igeneric_function<double>::generic_type generic_type;
    if (parameters[0].type != generic_type::e_scalar) {
        result = "?";
        return 0.0;
    }

    int idx = static_cast<int>(std::round(*static_cast<double*>(parameters[0].data)));
    if (idx < 0 || idx >= static_cast<int>(fRegistry->Size())) {
        result = "?";
        return 0.0;
    }

    result = std::string(fRegistry->GetValue(idx));
    return 0.0;
}

// ── RegisterChemVectors ──
/// @brief Resize chemistry vector storage in a compiled expression to nSpecies.
///
/// Vectors were pre-allocated with reserve(50) in Precompile().
/// Called from RunAction to prepare per-species arrays (Nc, Nt, Gc, Gt)
/// before ExprTK script execution.
///
/// @param compiled Target compiled expression.
/// @param nSpecies Number of chemical species.
/// @param reg      Unused (API compatibility).
void ExpressionEvaluator::RegisterChemVectors(
    CompiledExpression* compiled,
    size_t nSpecies,
    const ChemSpeciesRegistry* /*reg*/)
{
    // Vectors registered in Precompile with reserve(50) — resize is safe (no realloc)
    // Data filling is done via .assign() in RunAction.cc (Bug 1 fix preserves buffer)
    if (!compiled || nSpecies == 0) return;
    compiled->chemNc.resize(nSpecies);
    compiled->chemNt.resize(nSpecies);
    compiled->chemGc.resize(nSpecies);
    compiled->chemGt.resize(nSpecies);
    compiled->chemCharge.resize(nSpecies);
}

// ── SetGlobalSpeciesRegistry ──
/// @brief Store a thread-local pointer to the ChemSpeciesRegistry for
///        the SpeciesNameFunc callback.
/// @param reg Pointer to the registry (nullptr clears).
void ExpressionEvaluator::SetGlobalSpeciesRegistry(const ChemSpeciesRegistry* reg) {
    s_speciesName.SetRegistry(reg);
}

// ── Pamela2009 proton spectrum acceptance sampling (0 args: generates E internally) ──
double ExpressionEvaluator::Pamela2009AcceptFunc::operator()() {
    const int maxIter = 1000;
    for (int iter = 0; iter < maxIter; ++iter) {
        double T_MeV = 10.0 + 99990.0 * G4UniformRand();  // 10 MeV – 100 GeV
        double T = T_MeV * 0.001;  // GeV
        double phi = 0.379, tr = 0.938;
        double A = (T + phi) * (T + phi + 2.0 * tr);
        double flux = 1.9 * std::pow(A, -1.39) * T * (T + 2.0 * tr)
                      / ((1.0 + 0.4866 * std::pow(A, -1.255)) * (T + phi) * (T + phi + 2.0 * tr));
        if (G4UniformRand() * 0.268 < flux) return T_MeV;  // accepted, return energy in MeV
    }
    return 100.0;  // fallback
}

/// @brief Release all thread-local data, alias maps, and compiled expression cache.
void ExpressionEvaluator::ClearAll() {
    fgTLS.reset();
    fDataAliases.clear();
    fCache.clear();
}

// ── Format / Print / Println — replacement for rtl::io::package ──

double ExpressionEvaluator::FormatFunc::operator()(
    const std::size_t& ps_index,
    exprtk::igeneric_function<double>::parameter_list_t parameters)
{
    if (ps_index == 0) {
        // format(N) — set precision
        double n = *static_cast<double*>(parameters[0].data);
        s_outFmt.precision = std::max(1, std::min(17, static_cast<int>(std::round(n))));
    } else {
        // format('short'|'long'|'fixed'|'sci')
        std::string& s = *static_cast<std::string*>(parameters[0].data);
        if (s == "short" || s == "default") {
            s_outFmt.precision = 6; s_outFmt.scientific = true;
        } else if (s == "long") {
            s_outFmt.precision = 15; s_outFmt.scientific = true;
        } else if (s == "fixed") {
            s_outFmt.scientific = false;
        } else if (s == "sci" || s == "scientific") {
            s_outFmt.scientific = true;
        }
    }
    return 0.0;
}
