//==============================================================================
// G4CARE
// @file    ParametricField.hh
// @brief   User-defined electromagnetic field defined by ExprTk mathematical
//          expressions for each E and B component as functions of x, y, z, t,
//          and configurable parameters.
// @details Compiles 6 ExprTk expressions (3 for E, 3 for B) and evaluates them
//   at each field query point.  Supports user-defined parameters passed via
//   a string-to-double map.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef PARAMETRIC_FIELD_HH
#define PARAMETRIC_FIELD_HH

#include "FieldBase.hh"
#include "ExpressionEvaluator.hh"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

/// @brief E/B field from ExprTk expressions with user parameters.
class ParametricField : public FieldBase {
public:
    /// Constructor takes E equations (3) and B equations (3), plus parameters.
    /// If an equation is empty ("0" or ""), the corresponding component is zero.
    ParametricField(const std::string& name,
                    const std::array<std::string, 3>& eEquations,
                    const std::array<std::string, 3>& bEquations,
                    const std::map<std::string, double>& parameters);

    /// Copy constructor for Clone().
    ParametricField(const ParametricField& other);

    ~ParametricField() override = default;

    void GetFieldValue(const G4double point[4], G4double* field) const override;
    
    FieldBase* Clone() const override;

private:
    /// Source data
    std::array<std::string, 3> fEEquations; // Equations for Ex, Ey, Ez
    std::array<std::string, 3> fBEquations; // Equations for Bx, By, Bz
    std::map<std::string, double> fParameters;
    
    /// Variable names for compilation
    std::vector<std::string> fVariableNames;

    /// Compiled expressions (3 for E, 3 for B)
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprEx;
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprEy;
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprEz;
    
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprBx;
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprBy;
    std::shared_ptr<ExpressionEvaluator::CompiledExpression> fExprBz;

    /// Helper for compilation
    void CompileExpressions();
};

#endif
