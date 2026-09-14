#ifndef VERTEXGENERATOR_HH
#define VERTEXGENERATOR_HH

//==============================================================================
// G4CARE
// @file    VertexGenerator.hh
// @brief   Abstract interface for vertex generators
// @details Defines the interface for vertex generators that create
//   primary vertices based on a map of input variables. Used by
//   MixtureSource to generate vertices from different vertex
//   generation strategies (generic, GPS, radioactive decay, etc.).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4PrimaryVertex.hh"
#include <map>
#include <string>
#include <vector>

class VertexGenerator {
public:
    /// @brief Virtual destructor
    virtual ~VertexGenerator() {}

    /// @brief Generate primary vertices from input variables
    /// @param vars Map of variable name → value pairs
    /// @return Vector of G4PrimaryVertex pointers
    virtual std::vector<G4PrimaryVertex*> GenerateVertices(const std::map<std::string, double>& vars) = 0;
};

#endif