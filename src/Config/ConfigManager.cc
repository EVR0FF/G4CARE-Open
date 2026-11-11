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
#include <cstdlib>
#include <set>

// ======================================================================
//  Modular configuration: INCLUDE resolution and deep merge (file-local)
// ======================================================================
namespace {

/// @brief Extract the directory part of a path.
std::string DirName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

/// @brief Join a base directory and a (possibly relative) path.
std::string JoinPath(const std::string& dir, const std::string& rel) {
    if (rel.empty()) return dir;
    if (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':')) return rel; // absolute
    if (dir.empty() || dir == ".") return rel;
    return dir + "/" + rel;
}

/// @brief Canonicalize a path (used for cycle detection).
std::string CanonicalPath(const std::string& path) {
    char* resolved = realpath(path.c_str(), nullptr);
    if (resolved) {
        std::string s(resolved);
        std::free(resolved);
        return s;
    }
    return path;
}

/// @brief Deep-merge @p source into @p target (source wins on conflict).
/// Maps are merged recursively; scalars and sequences are replaced.
void DeepMerge(YAML::Node target, const YAML::Node source) {
    if (!source.IsMap() || !target.IsMap()) return;
    for (auto it = source.begin(); it != source.end(); ++it) {
        const std::string key = it->first.as<std::string>();
        const YAML::Node srcChild = it->second;
        YAML::Node existing = target[key];
        if (existing && existing.IsMap() && srcChild.IsMap()) {
            DeepMerge(existing, srcChild);
        } else {
            target[key] = srcChild;
        }
    }
}

/// @brief Recursively resolve INCLUDE directives and deep-merge included files.
YAML::Node ResolveIncludes(const std::string& filename, std::set<std::string>& active) {
    const std::string absPath = CanonicalPath(filename);
    if (active.count(absPath)) {
        G4cerr << "ConfigManager: circular INCLUDE detected for '" << filename
               << "'; skipping this include." << G4endl;
        return YAML::Node(YAML::NodeType::Map);
    }

    YAML::Node root = YAML::LoadFile(filename);
    active.insert(absPath);

    const YAML::Node& constRoot = root;
    const YAML::Node includeNode = constRoot["INCLUDE"];

    if (!includeNode) {
        active.erase(absPath);
        return root;
    }

    std::vector<std::string> includes;
    if (includeNode.IsScalar()) {
        includes.push_back(includeNode.as<std::string>());
    } else if (includeNode.IsSequence()) {
        for (const auto& item : includeNode) {
            if (item.IsScalar()) includes.push_back(item.as<std::string>());
        }
    }

    const std::string dir = DirName(absPath);
    YAML::Node merged(YAML::NodeType::Map);
    for (const auto& inc : includes) {
        const std::string incPath = JoinPath(dir, inc);
        YAML::Node incRoot = ResolveIncludes(incPath, active);
        DeepMerge(merged, incRoot);
    }

    // The including file's own keys take precedence over included files.
    root.remove("INCLUDE");
    DeepMerge(merged, root);

    active.erase(absPath);
    return merged;
}

} // anonymous namespace

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

/// @brief Load a YAML config file and resolve INCLUDE directives (deep merge).
/// @param filename Path to the YAML file (may contain INCLUDE blocks).
/// @return Merged YAML root node.
YAML::Node ConfigManager::LoadConfigFile(const std::string& filename) const {
    std::set<std::string> active;
    return ResolveIncludes(filename, active);
}

/// @brief Load and flatten a YAML configuration file
///
/// Loads the YAML file, then flattens all data (scalars, vectors,
/// subsections, subsection maps, and SCRIPT declarations) into
/// thread-safe read-only caches.
/// @param filename Path to the YAML file
/// @return true on success
bool ConfigManager::LoadConfig(const std::string& filename) {
    try {
        fRootNode = LoadConfigFile(filename);
        fConfigFileName = filename;
        fIsLoaded = false;
        fFlatScalars.clear();
        fSubsectionsCache.clear();
        fSubsectionsMapCache.clear();
        fStringVectors.clear();
        fDoubleVectors.clear();

        ExtractAllData();

        // Parse OUTPUT now while fRootNode is valid (before any lazy access).
        {
            YAML::Node out = fRootNode["OUTPUT"];
            if (out && out.IsMap()) {
                if (out["file"] && out["file"].IsScalar())
                    fOutputSpec.file = out["file"].as<std::string>();
                if (out["format"] && out["format"].IsScalar())
                    fOutputSpec.format = out["format"].as<std::string>();
                if (out["compression"] && out["compression"].IsMap()) {
                    const auto& c = out["compression"];
                    if (c["algorithm"] && c["algorithm"].IsScalar())
                        fOutputSpec.compressionAlgorithm = c["algorithm"].as<std::string>();
                    if (c["level"] && c["level"].IsScalar())
                        fOutputSpec.compressionLevel = c["level"].as<int>();
                }
                if (out["cluster_size_mb"] && out["cluster_size_mb"].IsScalar())
                    fOutputSpec.clusterSizeMb = out["cluster_size_mb"].as<int>();
                if (out["page_size_kb"] && out["page_size_kb"].IsScalar())
                    fOutputSpec.pageSizeKb = out["page_size_kb"].as<int>();
                if (out["streams"] && out["streams"].IsSequence()) {
                    for (const auto& s : out["streams"]) {
                        if (!s || !s.IsMap()) continue;
                        OutputStreamSpec os;
                        if (s["name"] && s["name"].IsScalar()) os.name = s["name"].as<std::string>();
                        if (s["level"] && s["level"].IsScalar()) os.level = s["level"].as<std::string>();
                        if (s["filter"] && s["filter"].IsScalar()) {
                            os.filter = s["filter"].as<std::string>();
                        } else if (s["filter"] && s["filter"].IsMap()) {
                            if (s["filter"]["volumes"] && s["filter"]["volumes"].IsSequence())
                                for (const auto& v : s["filter"]["volumes"])
                                    if (v && v.IsScalar()) os.filterVolumes.push_back(v.as<std::string>());
                            if (s["filter"]["condition"] && s["filter"]["condition"].IsScalar())
                                os.filterCondition = s["filter"]["condition"].as<std::string>();
                        }
                        if (s["columns"] && s["columns"].IsSequence())
                            for (const auto& c : s["columns"])
                                if (c && c.IsScalar()) os.columns.push_back(c.as<std::string>());
                        if (!os.name.empty()) fOutputSpec.streams.push_back(std::move(os));
                    }
                }
            }
        }
        ExpressionEvaluator::SetGlobalConstants(fExpressionVariables);
        for (const auto& [fname, fab] : fUserFunctions) {
            ExpressionEvaluator::RegisterGlobalFunction(fname, fab.first, fab.second);
        }

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

    try {
        // 6. Dedicated SCRIPT section (variables / functions / scripts)
        ParseScriptSection();
    } catch (const YAML::Exception& e) {
        G4cerr << "ConfigManager: ParseScriptSection failed: " << e.what() << G4endl;
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
        // The dedicated SCRIPT section is handled by ParseScriptSection (leaf
        // names); skip its flattened keys to avoid duplicate dot-path entries.
        if (key == "SCRIPT" || key.rfind("SCRIPT.", 0) == 0) continue;
        std::string val = Trim(rawValue);
        if (val.empty()) continue;

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

    G4cout << "ConfigManager: Extracted " << fExpressionDefinitions.size()
           << " expressions from flat declarations." << G4endl;
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
    // Check runtime variables first (for SCRIPT.scripts → GEOMETRY/SOURCE)
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

/// @brief Parse the OUTPUT section into a structured spec.
/// @return OutputSpec with file/format and stream definitions.
OutputSpec ConfigManager::GetOutputSpec() const {
    return fOutputSpec;
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

/// @brief Parse the dedicated SCRIPT section into the same caches (leaf names).
///
/// Reads SCRIPT.variables / SCRIPT.functions from the YAML tree and stores
/// them under their leaf names (no section prefix), complementing the flat
/// := / =( / = declarations handled by ExtractScripts.  SCRIPT.scripts is
/// read by ScriptsManager directly from the YAML file.
void ConfigManager::ParseScriptSection() {
    if (!fRootNode["SCRIPT"] || !fRootNode["SCRIPT"].IsMap()) return;
    const YAML::Node script = fRootNode["SCRIPT"];

    // Variables: leaf name -> constant (numeric) or expression definition.
    if (script["variables"] && script["variables"].IsMap()) {
        for (const auto& kv : script["variables"]) {
            const std::string name = kv.first.as<std::string>();
            std::string val = Trim(kv.second.as<std::string>());
            if (val.empty()) continue;

            if (val.size() >= 2 && val[0] == ':' && val[1] == '=') {
                // ":=" -> constant variable (alias of the flat syntax).
                val = Trim(val.substr(2));
                double d = 0.0;
                try { d = std::stod(val); } catch (...) { d = 0.0; }
                fExpressionVariables[name] = d;
                G4cout << "ConfigManager: SCRIPT variable '" << name << "' = " << d << G4endl;
            } else if (val[0] == '=') {
                // "=expr" -> expression definition.
                fExpressionDefinitions[name] = Trim(val.substr(1));
                G4cout << "ConfigManager: SCRIPT expression '" << name << "' = " << Trim(val.substr(1)) << G4endl;
            } else {
                // Plain value: number -> constant, else -> expression definition.
                bool numeric = false;
                double d = 0.0;
                try {
                    std::size_t idx = 0;
                    d = std::stod(val, &idx);
                    numeric = (idx == val.size());
                } catch (...) { numeric = false; }
                if (numeric) {
                    fExpressionVariables[name] = d;
                    G4cout << "ConfigManager: SCRIPT variable '" << name << "' = " << d << G4endl;
                } else {
                    fExpressionDefinitions[name] = val;
                    G4cout << "ConfigManager: SCRIPT expression '" << name << "' = " << val << G4endl;
                }
            }
        }
    }

    // Functions: leaf name -> "=(args) body".
    if (script["functions"] && script["functions"].IsMap()) {
        for (const auto& kv : script["functions"]) {
            const std::string name = kv.first.as<std::string>();
            std::string val = Trim(kv.second.as<std::string>());
            if (val.size() >= 2 && val[0] == ':' && val[1] == '=') val = Trim(val.substr(2));
            if (val.size() < 3 || val[0] != '=' || val[1] != '(') {
                G4cerr << "ConfigManager: SCRIPT function '" << name << "' must be '=(args) body'." << G4endl;
                continue;
            }
            const std::size_t parenClose = val.find(')');
            if (parenClose == std::string::npos) continue;
            const std::string argsStr = Trim(val.substr(2, parenClose - 2));
            const std::string body = Trim(val.substr(parenClose + 1));
            std::vector<std::string> args;
            std::stringstream ss(argsStr);
            std::string a;
            while (std::getline(ss, a, ',')) {
                a = Trim(a);
                if (!a.empty()) args.push_back(a);
            }
            fUserFunctions[name] = {args, body};
            G4cout << "ConfigManager: SCRIPT function '" << name << "'(" << argsStr << ")" << G4endl;
        }
    }
}
