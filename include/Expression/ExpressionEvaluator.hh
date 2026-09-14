//==============================================================================
//
// G4CARE
//
// @file    ExpressionEvaluator.hh
// @brief   ExprTK-based expression evaluation engine.
//
// @details
//   Provides single-expression evaluation, precompilation (Precompile,
//   PrecompileForFilter), data table registration (CSV/ROOT/IAEA),
//   thread-safe cache, chemistry vector support, built-in random
//   distributions (Poisson, Gauss, Landau, etc.), ion property lookups,
//   and output functions (g4cout, g4cerr, abort_event, kill_track).
//
//   Configuration keys read: none (operates on pre-parsed expressions).
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

#ifndef EXPRESSIONEVALUATOR_HH
#define EXPRESSIONEVALUATOR_HH

#include "G4Types.hh"
#include "exprtk.hpp"
#include "Randomize.hh"
#include "DataReader.hh"
#include "ColumnTypes.hh"
#include "ChemSpeciesRegistry.hh"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <cstdio>


#ifdef G4MULTITHREADED
#include "G4Threading.hh"
#define THREAD_LOCAL G4ThreadLocal
#else
#define THREAD_LOCAL
#endif

class ExpressionEvaluator {
public:
    ExpressionEvaluator();
    ~ExpressionEvaluator();

    double EvaluateWithUnit(const std::string& input,
                            const std::map<std::string, double>& vars,
                            const std::vector<std::map<std::string, double>>& vertexResults);

    static bool IsExpression(const std::string& str);
    static int LoadCSVFile(const std::string& filename,
                       bool hasHeader = true,
                       char delimiter = ',',
                       int skip = 0);
    static std::shared_ptr<const DataReader> GetReader(int idx);
    static int LoadROOTFile(const std::string& spec);
    static void RegisterDataAlias(const std::string& name, int index);
    static int GetDataAlias(const std::string& name);
    static int LoadIAEAFile(const std::string& filename);
    static void ClearAll();

struct VertexAttrFunc : public exprtk::ifunction<double> {
    const std::vector<std::map<std::string, double>>** fVertexResultsPtr; std::string fAttr; VertexAttrFunc(const std::vector<std::map<std::string, double>>** ptr, const std::string& attr) 
    : exprtk::ifunction<double>(1), fVertexResultsPtr(ptr), fAttr(attr) {}
    double operator()(const double& idx) override;
};

struct CompiledExpression {
    exprtk::symbol_table<double> symbol_table;
    exprtk::expression<double> expr;

    std::map<std::string, double> varStorage;

    std::map<std::string, std::unique_ptr<double>> varPtrs;

    const std::vector<std::map<std::string, double>>* currentVertexResults = nullptr;

    std::unique_ptr<VertexAttrFunc> func_x, func_y, func_z, func_time, func_weight;

    std::vector<std::pair<FilterVar, size_t>> var_mapping;
    std::vector<double> storage;

    // ── Chemistry vectors (registered via add_vector in RegisterChemVectors) ──
    std::vector<double> chemNc, chemNt, chemGc, chemGt, chemCharge;

    ~CompiledExpression() {}
};

static double* GetVariablePtr(CompiledExpression* compiled, const std::string& name);

struct PrecompiledExpr {
        std::string expr;
        std::shared_ptr<CompiledExpression> compiled;
        double unitMultiplier = 1.0;

        PrecompiledExpr() = default;
        PrecompiledExpr(const std::string& raw, const std::vector<std::string>& varNames, ExpressionEvaluator* eval);
    };

std::shared_ptr<CompiledExpression> Precompile(const std::string& expr_str, const std::vector<std::string>& var_names,
    const std::map<std::string, exprtk::igeneric_function<double>*>& extraFunctions = {},
    const std::map<std::string, double>& extraConstants = {});
std::shared_ptr<CompiledExpression> PrecompileForFilter(const std::string& expr_str, const std::vector<FilterVar>& var_indices,
 const std::vector<std::string>& var_names,
 const std::map<std::string, double>& extraConstants = {},
 const std::map<std::string, exprtk::igeneric_function<double>*>& extraFunctions = {},
 const std::vector<std::string>& extraVarNames = {});

const std::vector<FilterVar>& GetUsedVariables(const CompiledExpression* compiled) const;

double Execute(CompiledExpression* compiled, const std::map<std::string, double>& values);

double Execute(CompiledExpression* compiled, const FilterVars& vars) const;

// ── Chemistry: register per-species vectors in the compiled expression ──
void RegisterChemVectors(CompiledExpression* compiled, size_t nSpecies,
                         const ChemSpeciesRegistry* reg);

// Установить глобальный реестр для species_name (thread-local)
static void SetGlobalSpeciesRegistry(const ChemSpeciesRegistry* reg);

// ── species_name(index) → string ──
// Public: используется static thread_local переменной в .cc
struct SpeciesNameFunc : public exprtk::igeneric_function<double> {
    SpeciesNameFunc();
    double operator()(std::string& result,
                      exprtk::igeneric_function<double>::parameter_list_t parameters) override;
    void SetRegistry(const ChemSpeciesRegistry* reg) { fRegistry = reg; }
private:
    const ChemSpeciesRegistry* fRegistry = nullptr;
};

private:

struct ThreadLocalData {
        std::vector<std::shared_ptr<const DataReader>> readers;
        std::map<std::string, int> fileIndexMap;
    };
    
static std::map<std::string, int> fDataAliases;

static G4ThreadLocal std::unique_ptr<ThreadLocalData> fgTLS;

struct RandPoissonFunc : public exprtk::ifunction<double> {
    RandPoissonFunc() : exprtk::ifunction<double>(1) {}
    double operator()(const double& mean) override;
};

struct RandBinomialFunc : public exprtk::ifunction<double> {
    RandBinomialFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& n, const double& p) override;
};

struct RandExponentialFunc : public exprtk::ifunction<double> {
    RandExponentialFunc() : exprtk::ifunction<double>(1) {}
    double operator()(const double& lambda) override;
};

struct RandGaussFunc : public exprtk::ifunction<double> {
    RandGaussFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& mean, const double& sigma) override;
};

struct RandUniformFunc : public exprtk::ifunction<double> {
    RandUniformFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& min, const double& max) override;
};

struct RandBreitWignerFunc : public exprtk::ifunction<double> {
    RandBreitWignerFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& mean, const double& gamma) override;
};

struct RandBreitWignerCutFunc : public exprtk::ifunction<double> {
    RandBreitWignerCutFunc() : exprtk::ifunction<double>(3) {}
    double operator()(const double& mean, const double& gamma, const double& cut) override;
};

struct RandLandauFunc : public exprtk::ifunction<double> {
    RandLandauFunc() : exprtk::ifunction<double>(0) {}
    double operator()() override;
};

struct RandGammaFunc : public exprtk::ifunction<double> {
    RandGammaFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& k, const double& lambda) override;
};

struct RandChiSquareFunc : public exprtk::ifunction<double> {
    RandChiSquareFunc() : exprtk::ifunction<double>(1) {}
    double operator()(const double& df) override;
};

struct RandStudentTFunc : public exprtk::ifunction<double> {
    RandStudentTFunc() : exprtk::ifunction<double>(1) {}
    double operator()(const double& n) override;
};

struct RandLognormalFunc : public exprtk::ifunction<double> {
    RandLognormalFunc() : exprtk::ifunction<double>(2) {}
    double operator()(const double& zeta, const double& sigma) override;
};

struct RandRayleighFunc : public exprtk::ifunction<double> {
    RandRayleighFunc() : exprtk::ifunction<double>(1) {}
    double operator()(const double& sigma) override;
};

struct IonMassFunc : public exprtk::ifunction<double> {
    IonMassFunc();
    double operator()(const double& Z, const double& A) override;
};

struct IonChargeFunc : public exprtk::ifunction<double> {
    IonChargeFunc();
    double operator()(const double& Z, const double& A) override;
};

struct IonSpinFunc : public exprtk::ifunction<double> {
    IonSpinFunc();
    double operator()(const double& Z, const double& A) override;
};

struct IonMagMomentFunc : public exprtk::ifunction<double> {
    IonMagMomentFunc();
    double operator()(const double& Z, const double& A) override;
};

struct IonLifetimeFunc : public exprtk::ifunction<double> {
    IonLifetimeFunc();
    double operator()(const double& Z, const double& A, const double& excitation) override;
};

struct IonExcitationEnergyFunc : public exprtk::ifunction<double> {
    IonExcitationEnergyFunc();
    double operator()(const double& Z, const double& A, const double& level) override;
};

struct Pamela2009AcceptFunc : public exprtk::ifunction<double> {
    Pamela2009AcceptFunc() : exprtk::ifunction<double>(0) {}
    double operator()() override;
};

struct DataCache { std::map<std::string, std::shared_ptr<DataReader>> readers;};

struct DataRowsFunc : public exprtk::ifunction<double> {
    DataRowsFunc() : exprtk::ifunction<double>(1) {}
    virtual ~DataRowsFunc() = default;
    double operator()(const double& idx) override;
};

struct DataCellFunc : public exprtk::ifunction<double> {
    DataCellFunc() : exprtk::ifunction<double>(3) {}
    virtual ~DataCellFunc() = default;
    double operator()(const double& idx, const double& row, const double& col) override;
};

    RandPoissonFunc fRandPoisson;
    RandBinomialFunc fRandBinomial;
    RandExponentialFunc fRandExponential;
    RandGaussFunc fRandGauss;
    RandUniformFunc fRandUniform;
    RandBreitWignerFunc fRandBreitWigner;
    RandBreitWignerCutFunc fRandBreitWignerCut;
    RandLandauFunc fRandLandau;
    RandGammaFunc fRandGamma;
    RandChiSquareFunc fRandChiSquare;
    RandStudentTFunc fRandStudentT;
    RandLognormalFunc fRandLognormal;
    RandRayleighFunc fRandRayleigh;
    IonMassFunc fIonMass;
    IonChargeFunc fIonCharge;
    IonSpinFunc fIonSpin;
    IonMagMomentFunc fIonMagMoment;
    IonLifetimeFunc fIonLifetime;
    IonExcitationEnergyFunc fIonExcitationEnergy;
    struct OutputFunc : public exprtk::igeneric_function<double> {
        OutputFunc() : igeneric_function<double>(generate_prefix_args("S")) {}
        double operator()(const std::size_t& ps_index,
                          exprtk::igeneric_function<double>::parameter_list_t parameters) override;
    };

    struct ErrorFunc : public exprtk::igeneric_function<double> {
        ErrorFunc() : igeneric_function<double>(generate_prefix_args("S")) {}
        double operator()(const std::size_t& ps_index,
                          exprtk::igeneric_function<double>::parameter_list_t parameters) override;
    };

    struct AbortEventFunc : public exprtk::ifunction<double> {
        AbortEventFunc() : exprtk::ifunction<double>(0) {}
        double operator()() override;
    };

    struct KillTrackFunc : public exprtk::ifunction<double> {
        KillTrackFunc() : exprtk::ifunction<double>(0) {}
        double operator()() override;
    };

    OutputFunc fOutputFunc;
    ErrorFunc fErrorFunc;
    AbortEventFunc fAbortEvent;
    KillTrackFunc fKillTrack;

    SpeciesNameFunc fSpeciesName;

    DataRowsFunc fDataRows;
    DataCellFunc fDataCell;

    /// @brief Thread-local abort-event flag (set by abort_event() ExprTK function).
    static G4ThreadLocal bool fgAbortEventFlag;
    /// @brief Thread-local kill-track flag (set by kill_track() ExprTK function).
    static G4ThreadLocal bool fgKillTrackFlag;

    static THREAD_LOCAL std::unordered_map<std::string, std::shared_ptr<CompiledExpression>> fCache;

public:
    static bool GetAbortEventFlag() { return fgAbortEventFlag; }
    static void ClearAbortEventFlag() { fgAbortEventFlag = false; }
    static bool GetKillTrackFlag() { return fgKillTrackFlag; }
    static void ClearKillTrackFlag() { fgKillTrackFlag = false; }

    // ── Thread-local output formatting ──
    struct OutputFormat { int precision = 6; bool scientific = true; };
    static thread_local OutputFormat s_outFmt;

    // ── format(N) sets std::cout precision for io::package println/print ──
    // io::package caches format at compile time, so format(N) must be called
    // BEFORE println/print in the same script.  Works because they share
    // std::cout and format(N) modifies std::cout beforehand.
    struct FormatFunc : public exprtk::igeneric_function<double> {
        FormatFunc() : igeneric_function<double>("T|S") {}
        double operator()(const std::size_t& ps_index, parameter_list_t parameters) override;
    };
};

#endif