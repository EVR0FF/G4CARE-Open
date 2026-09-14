#ifndef TREE_MANAGER_HH
#define TREE_MANAGER_HH

#include "ColumnTypes.hh"
#include "G4AnalysisManager.hh"
#include "ExpressionEvaluator.hh"
#include "VolumeMaterialRegistry.hh"
#include "CrossSectionCalculator.hh"
#include "TypedRegistry.hh"
#include "DetectorRegistry.hh"
#include "SurfaceRegistry.hh"
#include <map>
#include <string>
#include <vector>
#include <memory>

//==============================================================================
// G4CARE
// @file    TreeManager.hh
// @brief   Manages the creation and configuration of output NTuples from
//          NTUPLE.* YAML blocks, including column definitions, filtering
//          expressions, and dictionary lookups.
// @details TreeManager parses NTUPLE configuration sections, compiles
//   optional ExprTk filter expressions, resolves ColType and dictionary
//   column references, builds compile-time filter-variable index lists,
//   and provides per-tree ntuple IDs and column metadata.  It delegates
//   dictionary ntuple creation to DictionaryWriter via SetDictionaryWriter.
//
//   Configuration keys read: NTUPLES.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

class ConfigManager;
class DictionaryWriter;

/// @brief Builds and manages output NTuple trees from configuration.
class TreeManager {
public:
    struct ColumnInfo {
        std::string name;
        ColType type = ColType::None;
        int ntupleColumnId = -1;
        bool isDictionary = false;
        bool isSColumn = false;  // true when ENABLE_DICTIONARY=false, filled as string
        std::string dictionaryName;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> expr;
        std::string expressionRef;  // non-empty for EXPRESSIONS.* columns
    };

    struct TreeInfo {
        int ntupleId = -1;
        std::vector<ColumnInfo> allColumns;
        std::vector<size_t> activeColumns;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> filter;
        std::vector<FilterVar> filterVarIndices;
        std::vector<FilterVar> expressionVarIndices;
        std::vector<FilterVar> allNeededVarIndices;
        // Combined filter (volumes + condition)
        std::vector<std::string> filterVolumes;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> filterCondition;
    };

    TreeManager(VolumeMaterialRegistry& registry,
                ExpressionEvaluator& evaluator,
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
                DetectorRegistry* detReg, SurfaceRegistry* surfReg, TypedRegistry<std::string>* chemSpeciesReg);

    ~TreeManager() = default;

    /// Set an external DictionaryWriter (owned by BeamAnalysis).
    void SetDictionaryWriter(DictionaryWriter* writer) { fDictionaryWriter = writer; }
    DictionaryWriter* GetDictionaryWriter() const { return fDictionaryWriter; }

    void CreateTreeFromConfig(const std::string& treeName, ConfigManager* cfg);

    /// For SColumn: converts a registry ID to its string (used when
    /// ENABLE_DICTIONARY=false).
    std::string GetDictString(const std::string& dictName, int id) const;

    void CompileFilter(TreeInfo& tree, const std::string& filterExpr,
                       const std::vector<std::string>& varNames);

    TreeInfo* GetTree(const std::string& name);
    const TreeInfo* GetTree(const std::string& name) const;

private:
    VolumeMaterialRegistry& fRegistry;
    ExpressionEvaluator& fEvaluator;
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
    DetectorRegistry* fDetectorRegistry;
    SurfaceRegistry* fSurfaceRegistry;
    TypedRegistry<std::string>* fChemSpeciesReg;
    
    DictionaryWriter* fDictionaryWriter = nullptr;

    std::map<std::string, TreeInfo> fTrees;
};

#endif