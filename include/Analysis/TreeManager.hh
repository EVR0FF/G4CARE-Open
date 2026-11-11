//==============================================================================
// G4CARE
// @file    TreeManager.hh
// @brief   Static column-name resolver shared by the output layer.
// @details TreeManager provides the static ResolveColumn() helper that maps
//   a configuration column name to a ColType, string-ness and the ROOT
//   branch name (with unit suffix).  The legacy G4AnalysisManager NTuple
//   path was retired; only the resolver remains.
//
//   Configuration keys read: none (static resolver).
//==============================================================================

#ifndef TREE_MANAGER_HH
#define TREE_MANAGER_HH

#include "ColumnTypes.hh"
#include <string>

/// @brief Static column resolver used by the RNTuple output layer.
class TreeManager {
public:
    /// @brief Resolve a column name to its ColType, string-ness and ROOT branch name.
    /// @return true if the name is a known built-in column.
    static bool ResolveColumn(const std::string& colName, ColType& type, bool& isString, std::string& branchName);
};

#endif
