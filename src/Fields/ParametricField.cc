//==============================================================================
// G4CARE
// @file    ParametricField.cc
// @brief   Implementation of a user-defined E/B field from ExprTk expressions.
// @details Compiles 6 ExprTk expressions (3 for E, 3 for B) in the constructor
//   and evaluates them at each field query point.  Clone() provides a
//   thread-local copy with its own compiled expressions for MT mode.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ParametricField.hh"
#include "G4SystemOfUnits.hh"

/// @brief Constructs the field, compiles expressions, and sets the field type.
/// @param name       Field name.
/// @param eEquations Array of 3 ExprTk strings for Ex, Ey, Ez.
/// @param bEquations Array of 3 ExprTk strings for Bx, By, Bz.
/// @param parameters Map of parameter name → value.
ParametricField::ParametricField(const std::string& name,
                                 const std::array<std::string, 3>& eEquations,
                                 const std::array<std::string, 3>& bEquations,
                                 const std::map<std::string, double>& parameters)
    : FieldBase(name), // Type determined automatically below
      fEEquations(eEquations), 
      fBEquations(bEquations),
      fParameters(parameters) 
{
    // Determine field type automatically
    bool hasE = false;
    bool hasB = false;
    
    for (const auto& eq : fEEquations) {
        if (!eq.empty() && eq != "0") hasE = true;
    }
    for (const auto& eq : fBEquations) {
        if (!eq.empty() && eq != "0") hasB = true;
    }

    if (hasE && hasB) fType = FieldType::kCombined;
    else if (hasE) fType = FieldType::kElectric;
    else fType = FieldType::kMagnetic;

    // Build variable list: x, y, z + parameter names
    fVariableNames = {"x", "y", "z"};
    for (const auto& [name, val] : fParameters) {
        fVariableNames.push_back(name);
    }

    // Compile expressions immediately in the constructor
    CompileExpressions();
}

/// @brief Copy constructor used by Clone() — re-compiles expressions.
/// @param other Source ParametricField to copy.
ParametricField::ParametricField(const ParametricField& other)
    : FieldBase(other.fName, other.fType),
      fEEquations(other.fEEquations),
      fBEquations(other.fBEquations),
      fParameters(other.fParameters),
      fVariableNames(other.fVariableNames)
{
    // Recompile expressions for this new (clone) instance
    CompileExpressions();
}

/// @brief Compiles all 6 field-component expressions using ExpressionEvaluator.
void ParametricField::CompileExpressions() {
    ExpressionEvaluator evaluator;
    
    // Compile E (if equations are non-empty)
    if (fEEquations[0] != "0" && !fEEquations[0].empty())
        fExprEx = evaluator.Precompile(fEEquations[0], fVariableNames);
    if (fEEquations[1] != "0" && !fEEquations[1].empty())
        fExprEy = evaluator.Precompile(fEEquations[1], fVariableNames);
    if (fEEquations[2] != "0" && !fEEquations[2].empty())
        fExprEz = evaluator.Precompile(fEEquations[2], fVariableNames);

    // Compile B
    if (fBEquations[0] != "0" && !fBEquations[0].empty())
        fExprBx = evaluator.Precompile(fBEquations[0], fVariableNames);
    if (fBEquations[1] != "0" && !fBEquations[1].empty())
        fExprBy = evaluator.Precompile(fBEquations[1], fVariableNames);
    if (fBEquations[2] != "0" && !fBEquations[2].empty())
        fExprBz = evaluator.Precompile(fBEquations[2], fVariableNames);
}

/// @brief Evaluates all 6 field components and fills {Ex,Ey,Ez,Bx,By,Bz}.
/// @param point  Position-time array {x, y, z, t}.
/// @param[out] field Output array of 6 doubles.
void ParametricField::GetFieldValue(const G4double point[4], G4double* field) const {
    // Prepare variable values
    std::map<std::string, double> values;
    values["x"] = point[0];
    values["y"] = point[1];
    values["z"] = point[2];
    
    // Add parameters
    for (const auto& [name, val] : fParameters) {
        values[name] = val;
    }

    ExpressionEvaluator evalHelper;
    
    // Evaluate E (if expressions exist)
    field[0] = fExprEx ? evalHelper.Execute(fExprEx.get(), values) : 0.0;
    field[1] = fExprEy ? evalHelper.Execute(fExprEy.get(), values) : 0.0;
    field[2] = fExprEz ? evalHelper.Execute(fExprEz.get(), values) : 0.0;

    // Evaluate B
    field[3] = fExprBx ? evalHelper.Execute(fExprBx.get(), values) : 0.0;
    field[4] = fExprBy ? evalHelper.Execute(fExprBy.get(), values) : 0.0;
    field[5] = fExprBz ? evalHelper.Execute(fExprBz.get(), values) : 0.0;
}

/// @brief Creates a thread-local copy for Geant4 MT.
/// @return Pointer to a new ParametricField with its own compiled expressions.
FieldBase* ParametricField::Clone() const {
    return new ParametricField(*this);
}