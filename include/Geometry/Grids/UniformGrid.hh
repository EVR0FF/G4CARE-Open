#ifndef UNIFORM_GRID_HH
#define UNIFORM_GRID_HH

//==============================================================================
// G4CARE
// @file    UniformGrid.hh
// @brief   Abstract interface for spatial grids with uniform access
// @details Defines a common interface for spatial grids (regular or
//   adaptive) that store data per cell and provide coordinate-based
//   navigation and indexed data access. Used for weight windows,
//   scoring maps, chemical maps, etc. Template parameter T is the
//   data type stored per cell (must be copyable).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4Types.hh"
#include "G4ThreeVector.hh"
#include <vector>
#include <stdexcept>

template<typename T>
class UniformGrid {
public:
    virtual ~UniformGrid() = default;

    /// @name Geometry parameters
    /// @{
    virtual G4int    GetTotalVoxels() const = 0;
    virtual G4int    GetNx() const = 0;
    virtual G4int    GetNy() const = 0;
    virtual G4int    GetNz() const = 0;
    virtual G4double GetMinX() const = 0;
    virtual G4double GetMinY() const = 0;
    virtual G4double GetMinZ() const = 0;
    virtual G4double GetMaxX() const = 0;
    virtual G4double GetMaxY() const = 0;
    virtual G4double GetMaxZ() const = 0;
    virtual G4double GetDx() const = 0;
    virtual G4double GetDy() const = 0;
    virtual G4double GetDz() const = 0;
    /// @}

    /// @name Grid navigation
    /// @{

    /// @brief Get the linear index of the cell containing a point
    ///
    /// If the point is outside the grid, returns the nearest boundary cell index.
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param z Z coordinate
    /// @return Linear cell index
    virtual G4int GetIndex(G4double x, G4double y, G4double z) const = 0;

    /// @brief Get the center coordinates of a cell by linear index
    /// @param idx Linear cell index
    /// @return Center position
    virtual G4ThreeVector GetVoxelCenter(G4int idx) const = 0;

    /// @brief Convert a linear index to (ix, iy, iz) triplet
    /// @param idx Linear cell index
    /// @param ix  Output: X index
    /// @param iy  Output: Y index
    /// @param iz  Output: Z index
    virtual void GetIndices(G4int idx, G4int& ix, G4int& iy, G4int& iz) const = 0;
    /// @}

    /// @name Data access
    /// @{
    virtual T& operator[](G4int idx) = 0;
    virtual const T& operator[](G4int idx) const = 0;

    /// @brief Fill all cells with a given value
    /// @param value Value to set
    virtual void Fill(const T& value) = 0;

    /// @brief Get a const reference to the internal data container
    /// @return Const reference to the data vector
    virtual const std::vector<T>& GetData() const = 0;
    /// @}
};

#endif // UNIFORM_GRID_HH