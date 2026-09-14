#ifndef IGRID_HH
#define IGRID_HH

//==============================================================================
// G4CARE
// @file    IGrid.hh
// @brief   Abstract interface for spatial grids of any type
// @details Defines a common interface for spatial grids used in
//   weight windows, scoring, chemical maps, etc. Provides navigation
//   (cell index by position, cell center, cell size, cell volume),
//   initialization from bounding box, and VTK export with optional
//   volume-name filtering.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4ThreeVector.hh"
#include <vector>
#include <string>

class IGrid {
public:
    /// @brief Virtual destructor
    virtual ~IGrid() = default;

    /// @brief Initialize the grid with bounding box and resolution parameters
    /// @param minCorner Lower corner of the grid (world coordinates)
    /// @param maxCorner Upper corner of the grid (world coordinates)
    virtual void Initialize(const G4ThreeVector& minCorner, const G4ThreeVector& maxCorner) = 0;

    /// @brief Find the cell index containing a point
    /// @param pos   Query point (world coordinates)
    /// @param index Output: cell index
    /// @return true if the point is inside the grid
    virtual bool GetCellIndex(const G4ThreeVector& pos, size_t& index) const = 0;

    /// @brief Get the center of a cell by index
    /// @param index Cell index
    /// @return Center position in world coordinates
    virtual G4ThreeVector GetCellCenter(size_t index) const = 0;

    /// @brief Get the full side length of a cell
    /// @param index Cell index
    /// @return Cell size (side length)
    virtual G4double GetCellSize(size_t index) const = 0;

    /// @brief Get the volume of a cell
    /// @param index Cell index
    /// @return Cell volume
    virtual G4double GetCellVolume(size_t index) const = 0;

    /// @brief Get the total number of active (leaf) cells
    /// @return Number of cells
    virtual size_t GetNumberOfCells() const = 0;

    /// @brief Get the grid type name for logging
    /// @return Type string (e.g., "RegularGrid", "AdaptiveGrid")
    virtual std::string GetType() const = 0;

    /// @brief Export grid cells to VTK format for visualization
    /// @param filename         Output VTK file path
    /// @param world            World physical volume (for coordinate lookup)
    /// @param targetVolumeName Optional: only export cells whose center is inside this volume
    virtual void ExportToVTK(const std::string& filename, G4VPhysicalVolume* world,
                             const std::string& targetVolumeName = "") const = 0;
};

#endif // IGRID_HH