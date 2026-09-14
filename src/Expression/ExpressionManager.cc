//==============================================================================
// G4CARE
// @file    ExpressionManager.cc
// @brief   Implementation of expression loading, compilation, and evaluation
// @details Implements the ExpressionManager class: loading expressions and
//   user-defined functions from YAML configuration, compiling them
//   with ExprTk via ExpressionEvaluator, resolving dependencies via
//   topological sort (Kahn's algorithm), and evaluating them at
//   different simulation levels (Step, EventEnd, RunEnd).
//   User-defined inline functions are expanded symbolically during
//   Register() by substituting argument placeholders in the function
//   body with the call-site arguments. A cyclic dependency fallback
//   preserves registration order when topological sort detects a cycle.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ExpressionManager.hh"
#include "ConfigManager.hh"
#include "G4ios.hh"
#include "FilterVarRegistry.hh"

#include <algorithm>
#include <queue>
#include <cctype>

/// @brief Default constructor
ExpressionManager::ExpressionManager() = default;

/// @brief Thread-local storage for user function metadata
///
/// Maps function name to (argument names, original body) for later
/// inline expansion during Register().
static thread_local std::map<std::string, std::pair<std::vector<std::string>, std::string>> s_funcMeta;

/// @brief ExprTk generic function wrapper for user-defined inline functions
///
/// Wraps a compiled expression body so it can be registered as an
/// ExprTk igeneric_function. Arguments are forwarded via setArg()
/// which writes into the compiled expression's variable pointers.
struct UserInlineFunction : public exprtk::igeneric_function<double> {
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> compiled;
    std::vector<std::string> argNames;

    /// @brief Constructor
    /// @param name Function name for ExprTk registration
    UserInlineFunction(const std::string& name)
        : exprtk::igeneric_function<double>(name, exprtk::igeneric_function<double>::e_rtrn_scalar) {}

    /// @brief Set an argument value into the compiled expression's variable pointer
    /// @param idx Argument index
    /// @param val Value to assign
    void setArg(int idx, double val) {
        if (idx >= 0 && idx < static_cast<int>(argNames.size()) && compiled) {
            auto it = compiled->varPtrs.find(argNames[idx]);
            if (it != compiled->varPtrs.end()) *(it->second) = val;
        }
    }

    /// @brief Evaluate the function with given parameters
    /// @param parameters ExprTk parameter list
    /// @return Evaluated function result
    double operator()(parameter_list_t parameters) override {
        int n = static_cast<int>(parameters.size());
        for (int i = 0; i < n; ++i) {
            auto& p = parameters[i];
            double val = (p.type == exprtk::type_store<double>::e_scalar)
                         ? *static_cast<double*>(p.data) : 0.0;
            setArg(i, val);
        }
        return compiled ? compiled->expr.value() : 0.0;
    }
};

/// @brief Check if an expression string references a dependency name as a whole word
///
/// Matches only whole-word occurrences to avoid false matches
/// (e.g., "edep" should not match "edep_total").
/// @param exprStr Expression string to search
/// @param depName Dependency name to look for
/// @return true if the dependency is referenced in the expression
static bool RefersTo(const std::string& exprStr, const std::string& depName) {
    auto isWordChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    size_t pos = 0;
    while (pos < exprStr.size()) {
        pos = exprStr.find(depName, pos);
        if (pos == std::string::npos) return false;
        bool leftOk = (pos == 0) || !isWordChar(exprStr[pos - 1]);
        bool rightOk = (pos + depName.size() >= exprStr.size()) || !isWordChar(exprStr[pos + depName.size()]);
        if (leftOk && rightOk) return true;
        ++pos;
    }
    return false;
}

/// @brief Pre-compute expression dependencies with clean name mapping
///
/// Strips level prefixes (run_end., event_end.) from expression names
/// and builds a dependency graph: for each expression, finds which
/// other expressions it references.
/// @param defs         Map of name → expression string
/// @param deps         Output: dependency graph
/// @param cleanNameMap Output: original name → clean name mapping
static void ComputeDepsPre(
    const std::map<std::string, std::string>& defs,
    std::map<std::string, std::vector<std::string>>& deps,
    std::map<std::string, std::string>& cleanNameMap)
{
    deps.clear(); cleanNameMap.clear();
    for (const auto& [name, expr] : defs) {
        std::string cn = name;
        size_t dp = name.rfind('.');
        if (dp != std::string::npos && dp < name.size()-1) {
            std::string p = name.substr(0, dp);
            if (p == "run_end" || p == "RUN_END" || p == "event_end" || p == "EVENT_END")
                cn = name.substr(dp+1);
        }
        cleanNameMap[name] = cn;
    }
    for (const auto& [name, expr] : defs) {
        std::vector<std::string> d;
        for (const auto& [other, otherExpr] : defs) {
            if (other == name) continue;
            if (RefersTo(expr, cleanNameMap[other]))
                d.push_back(other);
        }
        deps[name] = d;
    }
}

/// @brief Topological sort using Kahn's algorithm
///
/// Returns expressions in dependency order. If a cycle is detected,
/// falls back to definition order.
/// @param defs Expression definitions map
/// @param deps Dependency graph
/// @return Sorted expression names
static std::vector<std::string> TopoSort(
    const std::map<std::string, std::string>& defs,
    const std::map<std::string, std::vector<std::string>>& deps)
{
    std::map<std::string, int> inDeg;
    for (const auto& [n, _] : defs) inDeg[n] = 0;
    for (const auto& [n, dlist] : deps)
        for (const auto& d : dlist) inDeg[n]++;
    std::queue<std::string> q;
    for (const auto& [n, deg] : inDeg) if (deg == 0) q.push(n);
    std::vector<std::string> order;
    while (!q.empty()) {
        std::string cur = q.front(); q.pop(); order.push_back(cur);
        for (const auto& [n, dlist] : deps)
            for (const auto& d : dlist)
                if (d == cur) { inDeg[n]--; if (inDeg[n] == 0) q.push(n); }
    }
    if (order.size() != defs.size()) {
        order.clear();
        for (const auto& [n, _] : defs) order.push_back(n);
    }
    return order;
}

/// @brief Load expression definitions from configuration
///
/// Parses EXPRESSIONS and DEFINE sections from YAML config,
/// compiles each expression via ExpressionEvaluator, resolves
/// dependencies, and validates that no circular dependencies exist.
/// @param cfg Pointer to ConfigManager
/// @return true if all expressions were loaded and compiled
bool ExpressionManager::LoadFromConfig(ConfigManager* cfg) {
    if (!cfg) return false;
    LoadUserFunctions(cfg);
    auto defs = cfg->GetExpressionDefinitions();
    fExpressions.clear(); fLastResults.clear();
    auto vars = cfg->GetExpressionVariables();
    for (const auto& [vname, vval] : vars) {
        fExpressionVariables[vname] = vval;
        G4cout << "ExpressionManager: user variable '" << vname << "' = " << vval << G4endl;
    }
    if (defs.empty()) {
        G4cout << "ExpressionManager: No expressions found in config." << G4endl;
        return fUserFuncs.size() > 0;
    }
    std::map<std::string, std::vector<std::string>> preDeps;
    std::map<std::string, std::string> cleanMap;
    ComputeDepsPre(defs, preDeps, cleanMap);
    auto order = TopoSort(defs, preDeps);
    for (const auto& name : order) {
        const std::string& ex = defs.at(name);
        Level level = Level::Step;
        if (!Register(cleanMap[name], ex, level)) {
            G4cerr << "ExpressionManager: Failed to compile '" << name << "'" << G4endl;
            return false;
        }
    }
    ComputeDependencies();
    std::vector<std::string> evalOrder;
    if (!BuildEvaluationOrder(evalOrder)) {
        G4cerr << "ExpressionManager: Circular dependency!" << G4endl;
        return false;
    }
    G4cout << "ExpressionManager: Loaded " << fExpressions.size() << " expressions." << G4endl;
    return true;
}

/// @brief Register (compile) an expression with inline function expansion
///
/// Takes the expression string, inline-expands any user-defined
/// function calls by substituting argument placeholders with call-site
/// arguments, then compiles the resulting expression with ExprTk.
/// @param name  Expression name
/// @param expr  Raw expression string
/// @param level Evaluation level
/// @return true if compilation succeeded
bool ExpressionManager::Register(const std::string& name, const std::string& expr, Level level) {
    if (name.empty() || expr.empty()) return false;
    std::string expandedExpr = expr;

    // Inline-expand user functions: substitute f(a,b) → (body with a,b replaced)
    for (const auto& [fname, compiled] : fUserFuncs) {
        if (!compiled) continue;
        auto metaIt = s_funcMeta.find(fname);
        if (metaIt == s_funcMeta.end()) continue;
        const std::vector<std::string>& argNames = metaIt->second.first;
        if (argNames.empty()) continue;
        const std::string& body = metaIt->second.second;

        std::string searchPrefix = fname + "(";
        size_t pos = 0;
        while ((pos = expandedExpr.find(searchPrefix, pos)) != std::string::npos) {
            size_t open = pos + searchPrefix.size() - 1;
            size_t close = open; int depth = 1;
            while (depth > 0 && close + 1 < expandedExpr.size()) {
                close++;
                if (expandedExpr[close] == '(') depth++;
                else if (expandedExpr[close] == ')') depth--;
            }
            if (depth != 0) break;
            std::string argsStr = expandedExpr.substr(open + 1, close - open - 1);
            std::vector<std::string> argVals;
            {
                size_t start = 0; int nest = 0;
                for (size_t i = 0; i <= argsStr.size(); ++i) {
                    if (i == argsStr.size() || (argsStr[i] == ',' && nest == 0)) {
                        std::string a = argsStr.substr(start, i - start);
                        a.erase(0, a.find_first_not_of(" \t"));
                        a.erase(a.find_last_not_of(" \t") + 1);
                        argVals.push_back(a); start = i + 1;
                    } else if (argsStr[i] == '(') nest++;
                    else if (argsStr[i] == ')') nest--;
                }
            }
            if (argVals.size() != argNames.size()) {
                G4cerr << "ExpressionManager: function '" << fname << "' expects "
                       << argNames.size() << " args, got " << argVals.size() << G4endl;
                break;
            }
            std::string replacement = body;
            for (size_t i = 0; i < argNames.size(); ++i) {
                const std::string& pat = argNames[i];
                size_t rpos = 0;
                while ((rpos = replacement.find(pat, rpos)) != std::string::npos) {
                    bool leftOk = (rpos == 0) || (!std::isalnum(replacement[rpos-1]) && replacement[rpos-1] != '_');
                    bool rightOk = (rpos + pat.size() >= replacement.size()) ||
                        (!std::isalnum(replacement[rpos+pat.size()]) && replacement[rpos+pat.size()] != '_');
                    if (leftOk && rightOk) {
                        replacement.replace(rpos, pat.size(), "(" + argVals[i] + ")");
                        rpos += 2 + argVals[i].size();
                    } else rpos++;
                }
            }
            expandedExpr.replace(pos, close - pos + 1, "(" + replacement + ")");
        }
    }

    // List of already-registered expressions to expose as variables
    std::vector<std::string> extraVarNames;
    for (const auto& [pn, pe] : fExpressions)
        if (pe.level == level) extraVarNames.push_back(pn);

    ExpressionEvaluator tmpEval;
    std::map<std::string, exprtk::igeneric_function<double>*> noFuncs;
    auto res = tmpEval.PrecompileForFilter(expandedExpr,
        GetAllFilterVarIndices(), GetAllFilterVarNames(),
        fExpressionVariables, noFuncs, extraVarNames);
    if (!res) {
        G4cerr << "ExpressionManager: Failed to compile '" << name << "': " << expr << G4endl;
        return false;
    }
    ExprEntry e;
    e.name = name; e.originalExpr = expr; e.compiled = res; e.level = level;
    fExpressions[name] = std::move(e);
    G4cout << "ExpressionManager: Registered '" << name << "' (level=" << (int)level << ")" << G4endl;
    return true;
}

/// @brief Compute inter-expression dependencies at the same level
///
/// For each expression, identifies which other same-level expressions
/// it references, enabling topological evaluation order.
void ExpressionManager::ComputeDependencies() {
    for (auto& [name, entry] : fExpressions) {
        entry.dependencies.clear();
        for (const auto& [on, oe] : fExpressions) {
            if (on == name || oe.level != entry.level) continue;
            if (RefersTo(entry.originalExpr, on))
                entry.dependencies.push_back(on);
        }
    }
}

/// @brief Build topological evaluation order (Kahn's algorithm)
///
/// If a cycle is detected, falls back to registration order.
/// @param order Output: ordered expression names
/// @return true (always succeeds; cycle fallback is always valid)
bool ExpressionManager::BuildEvaluationOrder(std::vector<std::string>& order) {
    std::map<std::string, int> inDeg;
    for (const auto& [n, _] : fExpressions) inDeg[n] = 0;
    for (const auto& [n, e] : fExpressions)
        for (const auto& d : e.dependencies) inDeg[n]++;
    std::queue<std::string> q;
    for (const auto& [n, d] : inDeg) if (d == 0) q.push(n);
    order.clear(); order.reserve(fExpressions.size());
    while (!q.empty()) {
        std::string cur = q.front(); q.pop(); order.push_back(cur);
        for (auto& [n, e] : fExpressions) {
            auto it = std::find(e.dependencies.begin(), e.dependencies.end(), cur);
            if (it != e.dependencies.end()) { inDeg[n]--; if (inDeg[n] == 0) q.push(n); }
        }
    }
    if (order.size() != fExpressions.size()) {
        G4cerr << "ExpressionManager: Circular dependency! order=" << order.size()
               << " expected=" << fExpressions.size() << G4endl;
        order.clear();
        for (const auto& [n, _] : fExpressions) order.push_back(n);
    }
    return true;
}

/// @brief Evaluate all expressions at a given simulation level
///
/// Evaluates expressions in dependency order. For each expression:
/// copies filter variables into the compiled expression storage,
/// applies extra variables, updates dependency result pointers,
/// then evaluates and caches the result.
/// @param level     Evaluation level (Step, EventEnd, RunEnd)
/// @param stepVars  Current step filter variable values
/// @param extraVars Additional name→value pairs to inject
/// @return Const reference to the result map (name → value)
const std::map<std::string, double>& ExpressionManager::EvaluateAll(
    Level level, const FilterVars& stepVars, const std::map<std::string, double>& extraVars)
{
    fLastResults.clear();
    std::vector<std::string> order;
    if (!BuildEvaluationOrder(order)) return fLastResults;
    for (const auto& name : order) {
        auto it = fExpressions.find(name);
        if (it == fExpressions.end()) continue;
        const auto& e = it->second;
        if (e.level != level || !e.compiled) continue;

        for (const auto& [fv, idx] : e.compiled->var_mapping)
            e.compiled->storage[idx] = stepVars[static_cast<size_t>(fv)];

        for (const auto& [vn, vv] : extraVars)
            for (size_t j = 0; j < e.compiled->var_mapping.size(); ++j)
                if (static_cast<size_t>(e.compiled->var_mapping[j].first) < GetAllFilterVarNames().size())
                    if (GetAllFilterVarNames()[static_cast<size_t>(e.compiled->var_mapping[j].first)] == vn)
                        e.compiled->storage[j] = vv;

        for (const auto& dep : e.dependencies) {
            auto di = fLastResults.find(dep);
            if (di != fLastResults.end()) {
                double* ptr = ExpressionEvaluator::GetVariablePtr(e.compiled.get(), dep);
                if (ptr) *ptr = di->second;
            }
        }

        double val = e.compiled->expr.value();
        fLastResults[name] = val;
        const_cast<ExprEntry&>(e).lastValue = val;
    }
    return fLastResults;
}

/// @brief Get the last evaluated value of a named expression
/// @param name Expression name
/// @param def  Default value if not found
/// @return Last evaluated value, or default
double ExpressionManager::GetValue(const std::string& name, double def) const {
    auto it = fLastResults.find(name); if (it != fLastResults.end()) return it->second;
    auto ei = fExpressions.find(name); if (ei != fExpressions.end()) return ei->second.lastValue;
    return def;
}

/// @brief Check if an expression name is registered
/// @param name Expression name
/// @return true if registered
bool ExpressionManager::HasExpression(const std::string& name) const { return fExpressions.count(name); }

/// @brief Load and compile user-defined functions from configuration
///
/// Reads EXPRTK.FUNCTIONS section, compiles each function body with
/// ExprTk, and stores the compiled form in fUserFuncs. User functions
/// are later inline-expanded during Register().
/// @param cfg Pointer to ConfigManager
/// @return true if all functions compiled successfully
bool ExpressionManager::LoadUserFunctions(ConfigManager* cfg) {
    if (!cfg) return false;
    auto funcs = cfg->GetUserFunctions();
    if (funcs.empty()) return true;
    fUserFuncs.clear();

    for (const auto& [n, ab] : funcs)
        ::s_funcMeta[n] = {ab.first, ab.second};

    /// @brief Split comma-separated arguments ignoring nested parentheses
    auto SplitArgs = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> r;
        size_t start = 0; int depth = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || (s[i] == ',' && depth == 0)) {
                std::string a = s.substr(start, i - start);
                a.erase(0, a.find_first_not_of(" \t"));
                a.erase(a.find_last_not_of(" \t") + 1);
                if (!a.empty()) r.push_back(a); start = i + 1;
            } else if (s[i] == '(') depth++; else if (s[i] == ')') depth--;
        }
        return r;
    };

    /// @brief Replace a whole-word occurrence in a string
    auto ReplaceWord = [](std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            bool leftOk = (pos == 0) || (!std::isalnum(str[pos-1]) && str[pos-1] != '_');
            bool rightOk = (pos + from.size() >= str.size()) ||
                (!std::isalnum(str[pos+from.size()]) && str[pos+from.size()] != '_');
            if (leftOk && rightOk) { str.replace(pos, from.size(), to); pos += to.size(); }
            else pos++;
        }
    };

    for (const auto& [name, ab] : funcs) {
        const auto& args = ab.first; const auto& body = ab.second;
        G4cout << "ExpressionManager: compiling function '" << name << "(";
        for (size_t i = 0; i < args.size(); ++i) { if(i) G4cout << ","; G4cout << args[i]; }
        G4cout << ")' ..." << G4endl;

        std::string expandedBody = body;
        for (const auto& [fn, meta] : ::s_funcMeta) {
            if (fn == name) continue;
            const std::string& fb = meta.second;
            const std::vector<std::string>& fa = meta.first;
            std::string sp = fn + "(";
            size_t pos = 0;
            while ((pos = expandedBody.find(sp, pos)) != std::string::npos) {
                size_t open = pos + sp.size() - 1, close = open; int d = 1;
                while (d > 0 && close + 1 < expandedBody.size()) {
                    close++; if (expandedBody[close] == '(') d++; else if (expandedBody[close] == ')') d--;
                }
                if (d != 0) break;
                std::string as = expandedBody.substr(open + 1, close - open - 1);
                auto av = SplitArgs(as);
                if (av.size() == fa.size()) {
                    std::string repl = fb;
                    for (size_t ii = 0; ii < fa.size(); ++ii)
                        ReplaceWord(repl, fa[ii], "(" + av[ii] + ")");
                    expandedBody.replace(pos, close - pos + 1, "(" + repl + ")");
                } else ++pos;
            }
        }
        ExpressionEvaluator tmp;
        auto c = tmp.Precompile(expandedBody, args);
        if (!c) { G4cerr << "ExpressionManager: Failed to compile function '" << name << "'" << G4endl; return false; }
        fUserFuncs[name] = c;
    }
    G4cout << "ExpressionManager: Loaded " << funcs.size() << " user functions." << G4endl;
    return true;
}