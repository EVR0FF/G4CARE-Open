//==============================================================================
// G4CARE
// @file    ConfigManager.cc
// @brief   Implementation of the YAML configuration manager
// @details Implements the ConfigManager singleton: YAML file loading,
//   data flattening into thread-safe caches (scalars, vectors,
//   subsections), SCRIPT declaration extraction (:= variable,
//   =function, =expression), runtime context injection, per-thread
//   TLS caching with lazy expression evaluator, unit-aware value
//   parsing via G4UnitDefinition, and ExrpTk-based expression
//   evaluation with ${run_id} variable substitution.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Config/ConfigManager.hh"
#include "G4UnitsTable.hh"
#include "G4SystemOfUnits.hh"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <sstream>

ConfigManager* ConfigManager::fInstance = nullptr;

/// @brief Thread-local runtime variable storage
static thread_local std::map<std::string, double> s_runtimeVars;

/// @brief Get a runtime variable value
/// @param name Variable name
/// @return Value, or 0.0 if not set
double ConfigManager::GetRuntime(const std::string& name) {
    auto it = s_runtimeVars.find(name);
    return (it != s_runtimeVars.end()) ? it->second : 0.0;
}

/// @brief Set a runtime variable value
/// @param name  Variable name
/// @param value Value to assign
void ConfigManager::SetRuntime(const std::string& name, double value) {
    s_runtimeVars[name] = value;
}

/// @brief Get all runtime variables
/// @return Const reference to the runtime variables map
const std::map<std::string, double>& ConfigManager::GetAllRuntime() {
    return s_runtimeVars;
}

/// @brief Clear all runtime variables
void ConfigManager::ClearRuntime() {
    s_runtimeVars.clear();
}

/// @brief Thread-local storage for per-thread caches and evaluator
thread_local ConfigManager::ThreadLocalData ConfigManager::fTLS;

/// @brief Get the singleton instance (creates on first call)
/// @return ConfigManager pointer
ConfigManager* ConfigManager::Instance() {
    if (!fInstance) fInstance = new ConfigManager();
    return fInstance;
}

/// @brief Delete the singleton instance
void ConfigManager::DeleteInstance() {
    delete fInstance;
    fInstance = nullptr;
}

/// @brief Private constructor
ConfigManager::ConfigManager() : fIsLoaded(false), fCurrentRunID(0) {}

/// @brief Private destructor
ConfigManager::~ConfigManager() {}

// ======================================================================
//  Configuration Loading
// ======================================================================

/// @brief Load and flatten a YAML configuration file
///
/// Loads the YAML file, then flattens all data (scalars, vectors,
/// subsections, subsection maps, and SCRIPT declarations) into
/// thread-safe read-only caches.
/// @param filename Path to the YAML file
/// @return true on success
bool ConfigManager::LoadConfig(const std::string& filename) {
    try {
        fRootNode = YAML::LoadFile(filename);
        fConfigFileName = filename;
        fIsLoaded = false;
        fFlatScalars.clear();
        fSubsectionsCache.clear();
        fSubsectionsMapCache.clear();
        fStringVectors.clear();
        fDoubleVectors.clear();

        ExtractAllData();

        fIsLoaded = true;
        G4cout << "ConfigManager: Configuration loaded from " << filename
               << " (" << fFlatScalars.size() << " scalars, "
               << fSubsectionsCache.size() << " subsection paths, "
               << fStringVectors.size() << " string-vectors, "
               << fDoubleVectors.size() << " double-vectors)" << G4endl;

    // YAML tree is preserved for GetNode() during the initialization phase
    return true;
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: Error loading YAML file: " << e.what() << G4endl;
        return false;
    }
}

// ======================================================================
//  Data Extraction from YAML into Thread-Safe Caches
// ======================================================================

/// @brief Extract ALL data from YAML into thread-safe caches
///
/// Calls each extraction step (scalars, subsections, subsection maps,
/// vectors, scripts) in sequence. Errors in individual steps are
/// caught and logged but do not abort the entire extraction.
void ConfigManager::ExtractAllData() {
    try {
        // 1. Flat scalars (recursive traversal)
        FlattenScalars(fRootNode, "");
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: FlattenScalars failed: " << e.what() << G4endl;
    }

    try {
        // 2. Subsections (structural keys)
        ExtractSubsections(fRootNode, "");
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: ExtractSubsections failed: " << e.what() << G4endl;
    }

    try {
        // 3. Subsection maps (for GetSubsectionsMap)
        ExtractSubsectionsMap(fRootNode, "");
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: ExtractSubsectionsMap failed: " << e.what() << G4endl;
    }

    try {
        // 4. Vectors (string and numeric)
        ExtractVectors(fRootNode, "");
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: ExtractVectors failed: " << e.what() << G4endl;
    }

    try {
        // 5. SCRIPT declarations (:=, =(...), =) from all flat scalars
        ExtractScripts();
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: ExtractScripts failed: " << e.what() << G4endl;
    }
}

/// @brief Recursively flatten scalar (key=value) pairs into fFlatScalars
///
/// Walks the YAML tree and stores every scalar leaf with its dot-separated
/// key path. Maps are recursed; sequences of scalars are handled separately
/// in ExtractVectors.
/// @param node   Current YAML node
/// @param prefix Accumulated key prefix (dot-separated)
void ConfigManager::FlattenScalars(const YAML::Node& node, const std::string& prefix) {
    if (node.IsScalar()) {
        std::string key = prefix;
        if (!key.empty() && key[0] == '.') key = key.substr(1);
        fFlatScalars[key] = node.as<std::string>();
    } else if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string childKey = prefix + "." + it->first.as<std::string>();
            auto child = it->second;  // Copy Node — yaml-cpp 0.9.0 does not allow references to iterator sub-objects
            if (child.IsScalar()) {
                std::string k = childKey;
                if (!k.empty() && k[0] == '.') k = k.substr(1);
                fFlatScalars[k] = child.as<std::string>();
            } else if (child.IsMap() || child.IsSequence()) {
                FlattenScalars(child, childKey);
            }
        }
    }
    // Sequences of scalars are handled in ExtractVectors
}

/// @brief Extract subsection paths into fSubsectionsCache
///
/// For each map node, records its scalar children as subsection names.
/// Recurses into sub-maps.
/// @param node   Current YAML node
/// @param prefix Accumulated key prefix
void ConfigManager::ExtractSubsections(const YAML::Node& node, const std::string& prefix) {
    if (!node.IsMap()) return;
    std::vector<std::string> subs;
    for (auto it = node.begin(); it != node.end(); ++it) {
        auto child = it->second;
        if (child.IsMap() || child.IsSequence()) {
            subs.push_back(it->first.as<std::string>());
        }
    }
    if (!subs.empty()) {
        std::string path = prefix;
        if (!path.empty() && path[0] == '.') path = path.substr(1);
        fSubsectionsCache[path] = subs;
    }
    // Recurse into sub-maps
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it->second.IsMap()) {
            std::string childPrefix = prefix.empty() ? it->first.as<std::string>()
                                                     : prefix + "." + it->first.as<std::string>();
            ExtractSubsections(it->second, childPrefix);
        }
    }
}

/// @brief Extract sub-maps into fSubsectionsMapCache
///
/// For each map node, collects scalar children of child maps into
/// nested key-value structures. Recurses into sub-maps.
/// @param node   Current YAML node
/// @param prefix Accumulated key prefix
void ConfigManager::ExtractSubsectionsMap(const YAML::Node& node, const std::string& prefix) {
    if (!node.IsMap()) return;
    std::string currentPath = prefix;
    if (!currentPath.empty() && currentPath[0] == '.') currentPath = currentPath.substr(1);

    std::map<std::string, std::map<std::string, std::string>> subMaps;
    for (auto it = node.begin(); it != node.end(); ++it) {
        auto child = it->second;
        if (child.IsMap()) {
            std::map<std::string, std::string> kv;
            for (auto ci = child.begin(); ci != child.end(); ++ci) {
                if (ci->second.IsScalar()) {
                    kv[ci->first.as<std::string>()] = ci->second.as<std::string>();
                }
            }
            subMaps[it->first.as<std::string>()] = kv;
        }
    }
    if (!subMaps.empty()) {
        fSubsectionsMapCache[currentPath] = subMaps;
    }
    // Recurse
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it->second.IsMap()) {
            std::string childPrefix = prefix.empty() ? it->first.as<std::string>()
                                                     : prefix + "." + it->first.as<std::string>();
            ExtractSubsectionsMap(it->second, childPrefix);
        }
    }
}

/// @brief Scan flat scalars for SCRIPT declarations
///
/// Detects and extracts three types of SCRIPT declarations:
/// - `:= value` → variable/constant
/// - `=(a,b,...) body` → user function
/// - `=expression` → named expression
/// Matched keys are removed from fFlatScalars.
void ConfigManager::ExtractScripts() {
    fExpressionDefinitions.clear();
    fExpressionVariables.clear();
    fUserFunctions.clear();

    std::vector<std::string> keysToRemove;

    for (const auto& [key, rawValue] : fFlatScalars) {
        std::string val = Trim(rawValue);
        if (val.empty()) continue;

        // ── Variable: ":=" prefix ──
        if (val.size() >= 2 && val[0] == ':' && val[1] == '=') {
            std::string numStr = Trim(val.substr(2));
            double dval = 0.0;
            try { dval = std::stod(numStr); }
            catch (...) {
                // Try as expression
                dval = 0.0;
            }
            fExpressionVariables[key] = dval;
            keysToRemove.push_back(key);
            G4cout << "ConfigManager: SCRIPT variable '" << key << "' = " << dval << G4endl;
            continue;
        }

        // ── Function: "=(a,b,c) body" ──
        if (val.size() >= 3 && val[0] == '=' && val[1] == '(') {
            size_t parenClose = val.find(')');
            if (parenClose != std::string::npos) {
                std::string argsStr = val.substr(2, parenClose - 2);
                std::string afterParen = Trim(val.substr(parenClose + 1));
                // Distinguish function from expression:
                // Function: "=(a,b) body" — args are simple identifiers, body is non-empty
                // Expression: "=(edep * 100) / G_OH" — args contain operators, or afterParen is empty
                bool looksLikeFunction = true;
                for (char c : argsStr) {
                    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '?' || c == ':'
                        || c == '>' || c == '<' || c == '=' || c == '!' || c == '&' || c == '|') {
                        looksLikeFunction = false;
                        break;
                    }
                }
                if (looksLikeFunction && !afterParen.empty()) {
                    std::vector<std::string> args;
                    std::stringstream ss(argsStr);
                    std::string arg;
                    while (std::getline(ss, arg, ',')) {
                        std::string a = Trim(arg);
                        if (!a.empty()) args.push_back(a);
                    }
                    fUserFunctions[key] = {args, afterParen};
                    keysToRemove.push_back(key);
                    G4cout << "ConfigManager: SCRIPT function '" << key << "(";
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (i > 0) G4cout << ",";
                        G4cout << args[i];
                    }
                    G4cout << ")'" << G4endl;
                    continue;
                }
            }
        }

        // ── Expression: "=expr" ──
        if (val.size() >= 2 && val[0] == '=') {
            std::string expr = Trim(val.substr(1));
            fExpressionDefinitions[key] = expr;
            keysToRemove.push_back(key);
            G4cout << "ConfigManager: SCRIPT expression '" << key << "' = " << expr << G4endl;
            continue;
        }
    }

    // Remove detected script keys from flat scalars
    for (const auto& k : keysToRemove) {
        fFlatScalars.erase(k);
    }

    G4cout << "ConfigManager: Extracted " << fExpressionVariables.size()
           << " variables, " << fUserFunctions.size()
           << " functions, " << fExpressionDefinitions.size()
           << " expressions from SCRIPT declarations." << G4endl;
}

/// @brief Extract vector data (sequences of scalars) from YAML
///
/// For each sequence node, parses all elements as strings and,
/// if all are parseable as double, also stores them in fDoubleVectors.
/// Recurses into map nodes.
/// @param node   Current YAML node
/// @param prefix Accumulated key prefix
void ConfigManager::ExtractVectors(const YAML::Node& node, const std::string& prefix) {
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string childKey = prefix.empty() ? it->first.as<std::string>()
                                                  : prefix + "." + it->first.as<std::string>();
            auto child = it->second;
            if (child.IsSequence()) {
                // Check the type of the first element
                bool allScalar = true;
                std::vector<std::string> strVec;
                std::vector<double> dblVec;
                for (const auto& item : child) {
                    if (item.IsScalar()) {
                        std::string s = item.as<std::string>();
                        strVec.push_back(s);
                        try { dblVec.push_back(std::stod(s)); }
                        catch (...) { allScalar = false; }
                    } else {
                        allScalar = false;
                        break;
                    }
                }
                if (!strVec.empty()) {
                    fStringVectors[childKey] = strVec;
                }
                if (!dblVec.empty() && allScalar) {
                    fDoubleVectors[childKey] = dblVec;
                }
            }
            // Recurse into map nodes
            if (child.IsMap()) {
                ExtractVectors(child, childKey);
            }
        }
    }
}

// ======================================================================
//  Runtime Context
// ======================================================================

/// @brief Set context variables for expression evaluation
/// @param runId     Current run identifier
/// @param stageName Current stage name
void ConfigManager::SetContextVariables(int runId, const std::string& stageName) {
    fCurrentRunID = runId;
    fCurrentStageName = stageName;
}

/// @brief Invalidate thread-local caches
///
/// Called at begin-of-run to force re-evaluation of cached values
/// for the new run context.
void ConfigManager::InvalidateThreadCache() {
    fTLS.isCacheValid = false;
    fTLS.stringCache.clear();
    fTLS.doubleCache.clear();
    fTLS.intCache.clear();
    fTLS.boolCache.clear();
    if (!fTLS.evaluator) {
        fTLS.evaluator = std::make_unique<ExpressionEvaluator>();
    }
}

// ======================================================================
//  Scalar Value Accessors
// ======================================================================

/// @brief Check if a key exists in any cache
/// @param key Dot-separated configuration key
/// @return true if found in scalars, vectors, or subsections
bool ConfigManager::HasKey(const std::string& key) const {
    if (fFlatScalars.find(key) != fFlatScalars.end()) return true;
    if (fStringVectors.find(key) != fStringVectors.end()) return true;
    if (fDoubleVectors.find(key) != fDoubleVectors.end()) return true;
    if (fSubsectionsCache.find(key) != fSubsectionsCache.end()) return true;
    return false;
}

/// @brief Get a string value (with TLS caching and expression evaluation)
/// @param key        Dot-separated configuration key
/// @param defaultVal Default value if key not found
/// @return String value
std::string ConfigManager::GetString(const std::string& key, const std::string& defaultVal) const {
    // TLS cache check
    if (fTLS.isCacheValid) {
        auto it = fTLS.stringCache.find(key);
        if (it != fTLS.stringCache.end()) return it->second;
    }
    auto it = fFlatScalars.find(key);
    if (it == fFlatScalars.end()) return defaultVal;
    std::string rawValue = it->second;
    std::string result = rawValue;
    if (IsExpressionString(rawValue)) {
        try {
            double val = EvaluateExpression(rawValue);
            result = std::to_string(val);
            result.erase(result.find_last_not_of('0') + 1, std::string::npos);
            if (!result.empty() && result.back() == '.') result.pop_back();
        } catch (...) {}
    }
    fTLS.stringCache[key] = result;
    return result;
}

/// @brief Get a double value (with TLS caching, runtime vars, and expression evaluation)
/// @param key        Dot-separated configuration key
/// @param defaultVal Default value if key not found
/// @return Double value
double ConfigManager::GetDouble(const std::string& key, double defaultVal) const {
    if (fTLS.isCacheValid) {
        auto it = fTLS.doubleCache.find(key);
        if (it != fTLS.doubleCache.end()) return it->second;
    }
    // Check runtime variables first (for EXPRS → GEOMETRY/SOURCE)
    {
        auto rit = s_runtimeVars.find(key);
        if (rit != s_runtimeVars.end()) return rit->second;
    }
    auto it = fFlatScalars.find(key);
    if (it == fFlatScalars.end()) return defaultVal;
    std::string rawValue = it->second;
    double result = defaultVal;
    if (IsExpressionString(rawValue)) {
        result = EvaluateExpression(rawValue);
    } else {
        result = ParseValueWithUnit(rawValue, defaultVal);
    }
    fTLS.doubleCache[key] = result;
    return result;
}

/// @brief Get an integer value (truncated from double)
/// @param key        Dot-separated configuration key
/// @param defaultVal Default value if key not found
/// @return Integer value
int ConfigManager::GetInt(const std::string& key, int defaultVal) const {
    return static_cast<int>(GetDouble(key, static_cast<double>(defaultVal)));
}

/// @brief Get a boolean value
///
/// Accepts case-insensitive "true"/"1"/"yes" for true,
/// "false"/"0"/"no" for false. Falls back to expression evaluation.
/// @param key        Dot-separated configuration key
/// @param defaultVal Default value if key not found
/// @return Boolean value
bool ConfigManager::GetBool(const std::string& key, bool defaultVal) const {
    std::string val = GetString(key, "");
    if (val.empty()) return defaultVal;
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "true" || lower == "1" || lower == "yes") return true;
    if (lower == "false" || lower == "0" || lower == "no") return false;
    if (IsExpressionString(val)) return static_cast<bool>(EvaluateExpression(val));
    return defaultVal;
}

/// @brief Get a value with Geant4 unit parsing
/// @param key        Dot-separated configuration key
/// @param defaultVal Default value if key not found
/// @return Double value (with units parsed)
double ConfigManager::GetValueWithUnits(const std::string& key, double defaultVal) const {
    return GetDouble(key, defaultVal);
}

// ======================================================================
//  Vector Value Accessors
// ======================================================================

/// @brief Get a double vector
///
/// First checks fDoubleVectors, then falls back to parsing fStringVectors
/// with expression evaluation and unit parsing.
/// @param key Dot-separated configuration key
/// @param def Default value if key not found
/// @return Vector of doubles
std::vector<double> ConfigManager::GetDoubleVector(const std::string& key, const std::vector<double>& def) const {
    auto it = fDoubleVectors.find(key);
    if (it != fDoubleVectors.end()) return it->second;
    // May be a string vector that needs parsing
    auto sit = fStringVectors.find(key);
    if (sit != fStringVectors.end()) {
        std::vector<double> result;
        for (const auto& s : sit->second) {
            if (IsExpressionString(s))
                result.push_back(EvaluateExpression(s));
            else
                result.push_back(ParseValueWithUnit(s, 0.0));
        }
        return result;
    }
    return def;
}

/// @brief Get an integer vector (truncated from double vector)
/// @param key Dot-separated configuration key
/// @param def Default value if key not found
/// @return Vector of integers
std::vector<int> ConfigManager::GetIntVector(const std::string& key, const std::vector<int>& def) const {
    auto dVec = GetDoubleVector(key, {});
    if (dVec.empty()) return def;
    std::vector<int> result;
    result.reserve(dVec.size());
    for (double v : dVec) result.push_back(static_cast<int>(v));
    return result;
}

/// @brief Get a string vector
///
/// Returns cached string vector, or splits a single scalar value on commas.
/// @param key Dot-separated configuration key
/// @return Vector of strings (empty if not found)
std::vector<std::string> ConfigManager::GetStringVector(const std::string& key) const {
    auto it = fStringVectors.find(key);
    if (it != fStringVectors.end()) return it->second;
    // May be a single scalar separated by commas
    auto sit = fFlatScalars.find(key);
    if (sit != fFlatScalars.end()) {
        auto parts = Split(sit->second, ',');
        for (auto& p : parts) p = Trim(p);
        return parts;
    }
    return {};
}

// ======================================================================
//  Structural Queries
// ======================================================================

/// @brief Get subsection names for a given path
/// @param path Dot-separated section path
/// @return Vector of subsection names
std::vector<std::string> ConfigManager::GetSubsections(const std::string& path) const {
    auto it = fSubsectionsCache.find(path);
    if (it != fSubsectionsCache.end()) return it->second;
    return {};
}

/// @brief Get a subsection as a nested map
/// @param prefix Dot-separated key prefix
/// @return Nested map: subsection → (key → value)
std::map<std::string, std::map<std::string, std::string>>
ConfigManager::GetSubsectionsMap(const std::string& prefix) const {
    auto it = fSubsectionsMapCache.find(prefix);
    if (it != fSubsectionsMapCache.end()) return it->second;
    return {};
}

/// @brief Get first-level keys for a given section path
///
/// Collects keys from both flat scalars and subsections that are
/// direct children of the given path.
/// @param sectionPath Dot-separated section path
/// @return Vector of first-level key names
std::vector<std::string> ConfigManager::GetSectionKeys(const std::string& sectionPath) const {
    std::vector<std::string> result;
    std::string pfx = sectionPath.empty() ? "" : sectionPath + ".";
    // Scalar keys
    for (const auto& kv : fFlatScalars) {
        if (kv.first.find(pfx) == 0) {
            std::string suffix = kv.first.substr(pfx.size());
            auto dotPos = suffix.find('.');
            std::string key = (dotPos == std::string::npos) ? suffix : suffix.substr(0, dotPos);
            if (std::find(result.begin(), result.end(), key) == result.end())
                result.push_back(key);
        }
    }
    // Subsections
    auto it = fSubsectionsCache.find(sectionPath);
    if (it != fSubsectionsCache.end()) {
        for (const auto& s : it->second) {
            if (std::find(result.begin(), result.end(), s) == result.end())
                result.push_back(s);
        }
    }
    return result;
}

// ======================================================================
//  Runtime Helpers
// ======================================================================

/// @brief Check if a string is an expression (starts with '=' or contains ${...})
/// @param val String to check
/// @return true if it looks like an expression
bool ConfigManager::IsExpressionString(const std::string& val) const {
    if (val.empty()) return false;
    if (val[0] == '=') return true;
    if (val.find("${") != std::string::npos) return true;
    return false;
}

/// @brief Evaluate an expression string with ExrpTk
///
/// Strips the leading '=' if present, substitutes ${run_id}, adds
/// SCRIPT variables from fExpressionVariables, and evaluates using
/// the thread-local ExpressionEvaluator.
/// @param exprStr Expression string
/// @return Evaluated double value
double ConfigManager::EvaluateExpression(const std::string& exprStr) const {
    if (!fTLS.evaluator) fTLS.evaluator = std::make_unique<ExpressionEvaluator>();
    std::string cleanExpr = exprStr;
    if (!cleanExpr.empty() && cleanExpr[0] == '=') cleanExpr = cleanExpr.substr(1);
    std::string processedExpr = cleanExpr;
    std::string runIdStr = std::to_string(fCurrentRunID);
    size_t pos = 0;
    while ((pos = processedExpr.find("${run_id}", pos)) != std::string::npos) {
        processedExpr.replace(pos, 9, runIdStr);
        pos += runIdStr.length();
    }
    std::map<std::string, double> vars;
    vars["run_id"] = static_cast<double>(fCurrentRunID);
    for (const auto& [vname, vval] : fExpressionVariables) {
        vars[vname] = vval;
    }
    static const std::vector<std::map<std::string, double>> emptyVertex;
    try {
        return fTLS.evaluator->EvaluateWithUnit(processedExpr, vars, emptyVertex);
    } catch (const std::exception& e) {
        G4cerr << "ConfigManager: Error evaluating '" << exprStr << "': " << e.what() << G4endl;
        return 0.0;
    }
}

/// @brief Parse a numeric string with an optional Geant4 unit suffix
///
/// Supports both "*unit" syntax and inline unit names (e.g., "1.0*mm", "10 m").
/// Falls back to the raw numeric value if the unit is unknown.
/// @param str          String to parse
/// @param defaultValue Default value on parse failure
/// @return Parsed double value
double ConfigManager::ParseValueWithUnit(const std::string& str, double defaultValue) const {
    if (str.empty()) return defaultValue;
    std::string numPart = str;
    std::string unitPart;
    size_t starPos = str.find('*');
    if (starPos != std::string::npos) {
        numPart = str.substr(0, starPos);
        unitPart = str.substr(starPos + 1);
    } else {
        size_t i = 0;
        bool digitFound = false;
        for (; i < str.length(); ++i) {
            char c = str[i];
            if (std::isdigit(c) || c == '.' || c == '-' || c == '+') {
                digitFound = true;
            } else if (std::isalpha(c) || c == '_') {
                if (digitFound) { numPart = str.substr(0, i); unitPart = str.substr(i); break; }
            }
        }
    }
    double val = defaultValue;
    try { val = std::stod(Trim(numPart)); } catch (...) { return defaultValue; }
    if (unitPart.empty()) return val;
    unitPart = Trim(unitPart);
    if (G4UnitDefinition::IsUnitDefined(unitPart))
        return val * G4UnitDefinition::GetValueOf(unitPart);
    G4cerr << "ConfigManager: Unknown unit '" << unitPart << "' in '" << str << "'\n";
    return val;
}

/// @brief Trim leading and trailing whitespace from a string
/// @param str Input string
/// @return Trimmed string
std::string ConfigManager::Trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

/// @brief Split a string by a delimiter
/// @param str       Input string
/// @param delimiter Delimiter character
/// @return Vector of substrings
std::vector<std::string> ConfigManager::Split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) tokens.push_back(token);
    return tokens;
}

// ======================================================================
//  Direct YAML Access (Initialization Phase Only)
// ======================================================================

/// @brief Get a YAML node by dot-separated key
///
/// Supports dot notation: "A.B.C" → fRootNode["A"]["B"]["C"].
/// Only valid during the initialization phase (before caches are built).
/// @param key Dot-separated key path
/// @return YAML node (undefined if key not found)
YAML::Node ConfigManager::GetNode(const std::string& key) const {
    if (!fRootNode.IsDefined()) return YAML::Node();
    auto parts = Split(key, '.');
    YAML::Node current = fRootNode;
    for (const auto& part : parts) {
        if (!current.IsMap()) return YAML::Node();
        current = current[part];
        if (!current.IsDefined()) return YAML::Node();
    }
    return current;
}

// ======================================================================
//  Public Utility Methods (Backward Compatibility)
// ======================================================================

/// @brief Check if a string is an expression (backward-compatible alias)
/// @param val String to check
/// @return true if it looks like an expression
bool ConfigManager::IsExpression(const std::string& val) const {
    return IsExpressionString(val);
}

/// @brief Get a double vector with unit parsing from a string vector
/// @param key Dot-separated configuration key
/// @param def Default value if key not found
/// @return Vector of doubles with units parsed
std::vector<double> ConfigManager::GetDoubleVectorWithUnits(const std::string& key, const std::vector<double>& def) const {
    auto it = fStringVectors.find(key);
    if (it == fStringVectors.end()) {
        auto dit = fDoubleVectors.find(key);
        if (dit != fDoubleVectors.end()) return dit->second;
        return def;
    }
    std::vector<double> result;
    for (const auto& s : it->second) {
        result.push_back(ParseValueWithUnit(s, 0.0));
    }
    return result;
}