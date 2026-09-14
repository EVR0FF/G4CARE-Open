#ifndef EXPRESSION_MANAGER_HH
#define EXPRESSION_MANAGER_HH

//==============================================================================
// G4CARE
// @file    ExpressionManager.hh
// @brief   Manager for loading, compiling, and evaluating expressions
// @details Central expression management system for G4CARE. Loads
//   expression definitions and user-defined functions from the YAML
//   configuration, compiles them with ExprTk via ExpressionEvaluator,
//   resolves dependencies via topological sort, and evaluates them
//   at different simulation levels (Step, EventEnd, RunEnd).
//   Supports user-defined inline functions with automatic expansion
//   during expression registration (symbolic substitution).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ExpressionEvaluator.hh"
#include "ColumnTypes.hh"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <unordered_set>

class ConfigManager;

class ExpressionManager {
public:
    /// @brief Evaluation level for expressions
    enum class Level { Step, EventEnd, RunEnd };

    /// @brief Default constructor
    ExpressionManager();

    /// @brief Destructor
    ~ExpressionManager() = default;

    /// @brief Load expression definitions from configuration
    ///
    /// Parses EXPRESSIONS and DEFINE sections, compiles each expression,
    /// resolves dependencies via topological sort.
    /// @param cfg Pointer to ConfigManager
    /// @return true if all expressions loaded and compiled successfully
    bool LoadFromConfig(ConfigManager* cfg);

    /// @brief Load user-defined functions from configuration
    ///
    /// Compiles user functions defined in the EXPRTK.FUNCTIONS section.
    /// These functions are later inline-expanded during expression
    /// registration.
    /// @param cfg Pointer to ConfigManager
    /// @return true if all functions compiled successfully
    bool LoadUserFunctions(ConfigManager* cfg);

    /// @brief Manually register an expression
    /// @param name  Expression name
    /// @param expr  Expression body
    /// @param level Evaluation level (default: Step)
    /// @return true if compilation succeeded
    bool Register(const std::string& name, const std::string& expr, Level level = Level::Step);

    /// @brief Evaluate all expressions at a given level
    ///
    /// Evaluates expressions in dependency order, updating variable
    /// pointers and computing results for dependent expressions.
    /// @param level     Evaluation level
    /// @param stepVars  Current step filter variables
    /// @param extraVars Additional variables to set
    /// @return Map of expression name → evaluated value
    const std::map<std::string, double>& EvaluateAll(Level level,
        const FilterVars& stepVars, const std::map<std::string, double>& extraVars = {});

    /// @brief Get the last evaluated value of an expression
    /// @param name       Expression name
    /// @param defaultVal Default value if not found
    /// @return Last evaluated value or default
    double GetValue(const std::string& name, double defaultVal = 0.0) const;

    /// @brief Check if an expression is registered
    /// @param name Expression name
    /// @return true if the expression exists
    bool HasExpression(const std::string& name) const;

    /// @brief Get the last evaluation results map
    /// @return Const reference to the last results map
    const std::map<std::string, double>& GetLastResults() const { return fLastResults; }

    /// @brief Add a geometry property variable BEFORE LoadFromConfig
    ///
    /// Used to inject variables like target_mass that are computed
    /// after geometry construction but before expression loading.
    /// @param name  Variable name
    /// @param value Variable value
    void AddVariable(const std::string& name, double value) { fExpressionVariables[name] = value; }

private:
    /// @brief Internal representation of a compiled expression
    struct ExprEntry {
        std::string name;                                               ///< Expression name
        std::string originalExpr;                                       ///< Original expression string
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> compiled; ///< Compiled expression
        Level level = Level::Step;                                      ///< Evaluation level
        double lastValue = 0.0;                                         ///< Cached last evaluation result
        std::vector<std::string> dependencies;                          ///< Names of dependent expressions
    };

    /// @brief Build topological evaluation order from dependencies
    /// @param order Output: ordered expression names
    /// @return true if a valid order was found
    bool BuildEvaluationOrder(std::vector<std::string>& order);

    /// @brief Compute inter-expression dependencies
    void ComputeDependencies();

    /// @brief Map of all registered expressions
    std::map<std::string, ExprEntry> fExpressions;

    /// @brief Map of last evaluation results
    std::map<std::string, double> fLastResults;

    /// @brief User-defined expression variables (injected before loading)
    std::map<std::string, double> fExpressionVariables;

    /// @brief Compiled user-defined functions
    std::map<std::string, std::shared_ptr<ExpressionEvaluator::CompiledExpression>> fUserFuncs;
};

#endif