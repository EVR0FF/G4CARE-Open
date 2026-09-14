//==============================================================================
// G4CARE
// @file    WeightWindowWorld.hh
// @brief   Parallel world for weight-window biasing using a regular grid
//          or GDML geometry.
// @details WeightWindowWorld extends G4VUserParallelWorld and allows setting
//   either a RegularGrid<double> pointer or a GDML file to define the
//   weight-window mesh.  The Construct() method builds the corresponding
//   geometry in the parallel world.
//
//   Configuration keys read: none (set programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef WEIGHT_WINDOW_WORLD_HH
#define WEIGHT_WINDOW_WORLD_HH

#include "G4VUserParallelWorld.hh"
#include "G4String.hh"
#include "RegularGrid.hh"

/// @brief Parallel world for weight-window biasing geometry.
class WeightWindowWorld : public G4VUserParallelWorld {
public:
    WeightWindowWorld(G4String worldName);
    virtual ~WeightWindowWorld();

    /// Set a regular grid (replaces the old SetMeshParameters).
    void SetGrid(const RegularGrid<double>* grid);
    
    /// Set a GDML file for loading the grid (kept for compatibility).
    void SetGdmlFile(const G4String& filename);

    virtual void Construct() override;

private:
    const RegularGrid<double>* fGrid = nullptr;
    G4String fGdmlFile;
    bool fUseGdml = false;
};

#endif
