#ifndef CONFIG_MANAGER_HH
#define CONFIG_MANAGER_HH

//==============================================================================
// G4CARE
// @file    ConfigManager.hh
// @brief   Singleton configuration manager (YAML → thread-safe caches)
// @details Loads a YAML configuration file and flattens all scalar, vector,
//   and subsection data into read-only caches for thread-safe access.
//   Supports expression evaluation (ExrpTk), unit-aware value parsing,
//   runtime variable injection (EXPRS↔SOURCE↔GEOMETRY), SCRIPT extraction
//   (:= variable, =function, =expression), and per-thread TLS caching
//   with lazy evaluator initialization.
//   Configuration keys read: ALL (entire YAML file is flattened)
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <yaml-cpp/yaml.h>
#include "globals.hh"
#include "Expression/ExpressionEvaluator.hh"

class ExpressionManager;

class ConfigManager {
public:
    /// @brief Get the singleton instance
    static ConfigManager* Instance();

    /// @brief Delete the singleton instance
    static void DeleteInstance();

    /// @brief Load and flatten a YAML configuration file
    /// @param filename Path to the YAML file
    /// @return true on success
    bool LoadConfig(const std::string& filename);

    /// @brief Get the loaded config file name
    const std::string& GetConfigFileName() const { return fConfigFileName; }

    /// @name Runtime context
    /// @{
    /// @brief Set context variables for expression evaluation (run ID, stage name)
    void SetContextVariables(int runId, const std::string& stageName);

    /// @brief Invalidate thread-local caches (called at begin-of-run)
    void InvalidateThreadCache();
    /// @}

    /// @name Scalar value accessors
    /// @{
    bool                HasKey(const std::string& key) const;
    std::string         GetString(const std::string& key, const std::string& defaultVal = "") const;
    double              GetDouble(const std::string& key, double defaultVal = 0.0) const;
    int                 GetInt(const std::string& key, int defaultVal = 0) const;
    bool                GetBool(const std::string& key, bool defaultVal = false) const;

    /// @brief Get a double value with Geant4 unit parsing
    double              GetValueWithUnits(const std::string& key, double defaultVal = 0.0) const;
    /// @}

    /// @name Vector value accessors
    /// @{
    std::vector<double>      GetDoubleVector(const std::string& key, const std::vector<double>& def = {}) const;
    std::vector<int>         GetIntVector(const std::string& key, const std::vector<int>& def = {}) const;
    std::vector<std::string> GetStringVector(const std::string& key) const;
    /// @}

    /// @name Structural queries (sections / subsections)
    /// @{
    std::vector<std::string> GetSubsections(const std::string& path) const;
    std::map<std::string, std::map<std::string, std::string>>
                             GetSubsectionsMap(const std::string& prefix) const;
    std::vector<std::string> GetSectionKeys(const std::string& sectionPath) const;
    /// @}

    /// @name Expression (SCRIPT) accessors
    /// @{
    std::map<std::string, std::string> GetExpressionDefinitions() const { return fExpressionDefinitions; }
    std::map<std::string, double> GetExpressionVariables() const { return fExpressionVariables; }
    std::map<std::string, std::pair<std::vector<std::string>, std::string>> GetUserFunctions() const { return fUserFunctions; }
    /// @}

    /// @brief Direct YAML node access (only during initialization phase)
    YAML::Node GetNode(const std::string& key) const;

    /// @name Runtime variables (unified namespace EXPRS↔SOURCE↔GEOMETRY)
    /// @{
    static double GetRuntime(const std::string& name);
    static void SetRuntime(const std::string& name, double value);
    static const std::map<std::string, double>& GetAllRuntime();
    static void ClearRuntime();
    /// @}

    /// @name Public utilities (backward compatibility)
    /// @{
    bool IsExpression(const std::string& val) const;
    std::vector<double> GetDoubleVectorWithUnits(const std::string& key, const std::vector<double>& def = {}) const;
    std::vector<std::string> Split(const std::string& str, char delimiter) const;
    std::string Trim(const std::string& str) const;
    /// @}

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /// @name Extraction helpers (called during LoadConfig only)
    /// @{
    /// @brief Extract ALL data from YAML into caches, then clear fRootNode
    void ExtractAllData();

    /// @brief Recursively flatten scalar keys into fFlatScalars
    void FlattenScalars(const YAML::Node& node, const std::string& prefix);

    /// @brief Extract subsection paths into fSubsectionsCache
    void ExtractSubsections(const YAML::Node& node, const std::string& prefix);

    /// @brief Extract vector data into fStringVectors / fDoubleVectors
    void ExtractVectors(const YAML::Node& node, const std::string& prefix);

    /// @brief Extract subsection maps into fSubsectionsMapCache
    void ExtractSubsectionsMap(const YAML::Node& node, const std::string& prefix);

    /// @brief Scan flat scalars for SCRIPT declarations (:= var, = function, = expression)
    void ExtractScripts();
    /// @}

    /// @name Runtime helpers
    /// @{
    bool IsExpressionString(const std::string& val) const;
    double EvaluateExpression(const std::string& exprStr) const;
    double ParseValueWithUnit(const std::string& str, double defaultValue) const;
    /// @}

    /// @name YAML data (preserved for GetNode during initialization)
    /// @{
    YAML::Node fRootNode;
    std::string fConfigFileName;
    /// @}

    /// @name Read-only caches (thread-safe after LoadConfig)
    /// @{
    bool fIsLoaded;
    std::map<std::string, std::string> fFlatScalars;
    std::map<std::string, std::vector<std::string>> fSubsectionsCache;
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> fSubsectionsMapCache;
    std::map<std::string, std::vector<std::string>> fStringVectors;
    std::map<std::string, std::vector<double>> fDoubleVectors;
    std::map<std::string, std::string> fExpressionDefinitions;
    std::map<std::string, double> fExpressionVariables;
    std::map<std::string, std::pair<std::vector<std::string>, std::string>> fUserFunctions;
    /// @}

    /// @name Execution context
    /// @{
    int fCurrentRunID;
    std::string fCurrentStageName;

    /// @brief Weak pointer to ExpressionManager for SCRIPT variable access
    void* fExpressionManager = nullptr;
    /// @}

    /// @brief Per-thread local storage (TLS)
    struct ThreadLocalData {
        bool isCacheValid = false;
        std::unique_ptr<ExpressionEvaluator> evaluator;
        std::map<std::string, std::string> stringCache;
        std::map<std::string, double> doubleCache;
        std::map<std::string, int> intCache;
        std::map<std::string, bool> boolCache;
    };
    static thread_local ThreadLocalData fTLS;
    static ConfigManager* fInstance;
};

#endif // CONFIG_MANAGER_HH