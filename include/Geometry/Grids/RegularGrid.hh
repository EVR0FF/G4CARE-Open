#ifndef REGULAR_GRID_HH
#define REGULAR_GRID_HH

//==============================================================================
// G4CARE
// @file    RegularGrid.hh
// @brief   Regular 3D grid with uniform voxel spacing
// @details Simple regular 3D grid where each voxel has the same size.
//   Provides coordinate-based cell indexing, voxel center lookup,
//   and data access via linear index. Used by ChemicalScorer and
//   other scoring structures that don't require adaptive refinement.
//   Template parameter T is the data type stored per voxel.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4ThreeVector.hh"
#include <vector>
#include <cstddef>
#include <cmath>
#include <stdexcept>

template <typename T>
class RegularGrid {
public:
    /// @brief Default constructor (empty grid)
    RegularGrid() = default;

    /// @brief Constructor with world-coordinate bounds and voxel counts
    /// @param minCorner Lower corner of the grid (world coordinates)
    /// @param maxCorner Upper corner of the grid (world coordinates)
    /// @param nX        Number of voxels along X axis
    /// @param nY        Number of voxels along Y axis
    /// @param nZ        Number of voxels along Z axis
    RegularGrid(const G4ThreeVector& minCorner,
                const G4ThreeVector& maxCorner,
                size_t nX, size_t nY, size_t nZ)
        : fMin(minCorner), fMax(maxCorner), fNX(nX), fNY(nY), fNZ(nZ)
    {
        if (nX == 0 || nY == 0 || nZ == 0) {
            throw std::invalid_argument("RegularGrid: dimensions must be > 0");
        }
        fVoxels.resize(nX * nY * nZ);
        fStep = G4ThreeVector(
            (maxCorner.x() - minCorner.x()) / nX,
            (maxCorner.y() - minCorner.y()) / nY,
            (maxCorner.z() - minCorner.z()) / nZ);
    }

    /// @brief Legacy constructor (compatibility with WeightWindowManager)
    /// @param nx,ny,nz      Number of voxels along each axis
    /// @param minX,minY,minZ Lower corner coordinates
    /// @param maxX,maxY,maxZ Upper corner coordinates
    RegularGrid(int nx, int ny, int nz,
                double minX, double minY, double minZ,
                double maxX, double maxY, double maxZ)
        : RegularGrid(G4ThreeVector(minX, minY, minZ),
                      G4ThreeVector(maxX, maxY, maxZ),
                      static_cast<size_t>(nx),
                      static_cast<size_t>(ny),
                      static_cast<size_t>(nz))
    {}

    /// @brief Reinitialize the grid with new parameters
    /// @param minCorner Lower corner (world coordinates)
    /// @param maxCorner Upper corner (world coordinates)
    /// @param nX        Number of voxels along X
    /// @param nY        Number of voxels along Y
    /// @param nZ        Number of voxels along Z
    void Initialize(const G4ThreeVector& minCorner,
                    const G4ThreeVector& maxCorner,
                    size_t nX, size_t nY, size_t nZ)
    {
        if (nX == 0 || nY == 0 || nZ == 0) {
            throw std::invalid_argument("RegularGrid: dimensions must be > 0");
        }
        fMin = minCorner;
        fMax = maxCorner;
        fNX = nX;
        fNY = nY;
        fNZ = nZ;
        fVoxels.clear();
        fVoxels.resize(nX * nY * nZ);
        fStep = G4ThreeVector(
            (maxCorner.x() - minCorner.x()) / nX,
            (maxCorner.y() - minCorner.y()) / nY,
            (maxCorner.z() - minCorner.z()) / nZ);
    }

    /// @brief Get the linear voxel index for a point
    ///
    /// Returns -1 if the point is outside the grid bounds.
    /// Clamps edge cases at the upper boundary to avoid rounding errors.
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param z Z coordinate
    /// @return Linear voxel index, or -1 if out of bounds
    int GetIndex(double x, double y, double z) const {
        if (x < fMin.x() || x >= fMax.x() ||
            y < fMin.y() || y >= fMax.y() ||
            z < fMin.z() || z >= fMax.z()) {
            return -1;
        }
        size_t ix = static_cast<size_t>((x - fMin.x()) / fStep.x());
        size_t iy = static_cast<size_t>((y - fMin.y()) / fStep.y());
        size_t iz = static_cast<size_t>((z - fMin.z()) / fStep.z());
        // Guard against rounding at the upper boundary
        if (ix >= fNX) ix = fNX - 1;
        if (iy >= fNY) iy = fNY - 1;
        if (iz >= fNZ) iz = fNZ - 1;
        return static_cast<int>(ix + iy * fNX + iz * fNX * fNY);
    }

    /// @name Element access
    /// @{
    T& operator[](int idx)             { return fVoxels[idx]; }
    const T& operator[](int idx) const { return fVoxels[idx]; }
    T& operator[](size_t idx)          { return fVoxels[idx]; }
    const T& operator[](size_t idx) const { return fVoxels[idx]; }
    /// @}

    /// @brief Get total number of voxels
    /// @return Voxel count (nx * ny * nz)
    size_t GetTotalVoxels() const { return fVoxels.size(); }

    /// @brief Get voxel dimensions
    /// @return Number of voxels along X
    size_t GetNX() const { return fNX; }
    /// @return Number of voxels along Y
    size_t GetNY() const { return fNY; }
    /// @return Number of voxels along Z
    size_t GetNZ() const { return fNZ; }

    /// @brief Get grid bounds
    const G4ThreeVector& GetMin() const { return fMin; }
    const G4ThreeVector& GetMax() const { return fMax; }
    G4ThreeVector GetStep() const { return fStep; }

    /// @brief Get voxel size along each axis
    double GetDx() const { return fStep.x(); }
    double GetDy() const { return fStep.y(); }
    double GetDz() const { return fStep.z(); }

    /// @brief Get the center of a voxel by linear index (world coordinates)
    /// @param linearIdx Linear voxel index
    /// @return Center position in world coordinates
    G4ThreeVector GetVoxelCenter(int linearIdx) const {
        size_t idx = static_cast<size_t>(linearIdx);
        if (idx >= fVoxels.size())
            return G4ThreeVector();
        size_t iz = idx / (fNX * fNY);
        size_t rem = idx % (fNX * fNY);
        size_t iy = rem / fNX;
        size_t ix = rem % fNX;
        return G4ThreeVector(
            fMin.x() + (ix + 0.5) * fStep.x(),
            fMin.y() + (iy + 0.5) * fStep.y(),
            fMin.z() + (iz + 0.5) * fStep.z());
    }

    /// @brief Reset all voxel data to default-constructed values
    void Reset() {
        for (auto& v : fVoxels) {
            v = T();
        }
    }

    /// @brief Access the raw data vector
    const std::vector<T>& GetData() const { return fVoxels; }
    std::vector<T>& GetData() { return fVoxels; }

private:
    G4ThreeVector fMin;    ///< Lower corner
    G4ThreeVector fMax;    ///< Upper corner
    G4ThreeVector fStep;   ///< Voxel spacing
    size_t fNX = 0;        ///< Number of voxels along X
    size_t fNY = 0;        ///< Number of voxels along Y
    size_t fNZ = 0;        ///< Number of voxels along Z
    std::vector<T> fVoxels; ///< Voxel data array
};

#endif // REGULAR_GRID_HH