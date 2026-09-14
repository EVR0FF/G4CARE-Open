//==============================================================================
// G4CARE
// @file    GridField.cc
// @brief   3D grid-based electromagnetic field that loads E/B field maps
//          from CSV and interpolates using trilinear or nearest-neighbor.
// @details Supports both regular (uniform-grid) and scattered-node CSV formats.
//   Implements FieldBase with separate E and B grids, each with scale factors.
//   GetFieldValue fills the 6-component array {Ex,Ey,Ez,Bx,By,Bz}; Clone()
//   provides a thread-local copy for MT mode.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "GridField.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>

/// @brief Constructs a grid field from CSV files with interpolation parameters.
/// @param name   Field name.
/// @param eFilename CSV file for electric field (empty = none).
/// @param bFilename CSV file for magnetic field (empty = none).
/// @param interp   Interpolation method ("trilinear" or "nearest").
/// @param eScale   Unit scale factor for E.
/// @param bScale   Unit scale factor for B.
GridField::GridField(const std::string& name, 
                     const std::string& eFilename, 
                     const std::string& bFilename,
                     const std::string& interp,
                     double eScale,
                     double bScale)
    : FieldBase(name), 
      fERegular(false), fENx(0), fENy(0), fENz(0), fEScale(eScale),
      fBRegular(false), fBNx(0), fBNy(0), fBNz(0), fBScale(bScale),
      fInterpMethod(interp)
{
    bool hasE = !eFilename.empty();
    bool hasB = !bFilename.empty();
    
    if (hasE && hasB) fType = FieldType::kCombined;
    else if (hasE) fType = FieldType::kElectric;
    else fType = FieldType::kMagnetic;

    if (hasE) {
        LoadCSV(eFilename, fENodes, fERegular, fENx, fENy, fENz,
                fEX0, fEY0, fEZ0, fEDx, fEDy, fEDz, fERegularData);
        G4cout << "GridField [" << name << "]: Loaded E-field from " << eFilename 
               << (fERegular ? " (Regular)" : " (Irregular)") << G4endl;
    }

    if (hasB) {
        LoadCSV(bFilename, fBNodes, fBRegular, fBNx, fBNy, fBNz,
                fBX0, fBY0, fBZ0, fBDx, fBDy, fBDz, fBRegularData);
        G4cout << "GridField [" << name << "]: Loaded B-field from " << bFilename 
               << (fBRegular ? " (Regular)" : " (Irregular)") << G4endl;
    }
}

GridField::~GridField() {}

/// @brief Loads field data from a CSV file and determines whether the grid
///        is regular or scattered.
/// @param filename      Path to CSV file.
/// @param[out] nodes     Output vector of nodes.
/// @param[out] isRegular Whether the grid is uniform.
/// @param[out] nx, ny, nz Grid dimensions.
/// @param[out] x0, y0, z0 Origin.
/// @param[out] dx, dy, dz Grid spacing.
/// @param[out] regularData Flattened regular-grid data.
void GridField::LoadCSV(const std::string& filename, std::vector<Node>& nodes, 
                        bool& isRegular, int& nx, int& ny, int& nz,
                        double& x0, double& y0, double& z0,
                        double& dx, double& dy, double& dz,
                        std::vector<double>& regularData)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        G4cerr << "GridField: Cannot open file " << filename << G4endl;
        return;
    }

    std::string line;
    if (std::getline(file, line)) {
        std::istringstream iss(line);
        double test;
        if (!(iss >> test)) { /* header */ } 
        else { file.clear(); file.seekg(0); }
    }

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        Node n;
        if (iss >> n.x >> n.y >> n.z >> n.vx >> n.vy >> n.vz) {
            nodes.push_back(n);
        }
    }
    
    if (nodes.empty()) { isRegular = false; return; }

    std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });

    std::vector<double> xs, ys, zs;
    for (const auto& n : nodes) {
        if (xs.empty() || std::abs(xs.back() - n.x) > 1e-6) xs.push_back(n.x);
        if (ys.empty() || std::abs(ys.back() - n.y) > 1e-6) ys.push_back(n.y);
        if (zs.empty() || std::abs(zs.back() - n.z) > 1e-6) zs.push_back(n.z);
    }

    nx = xs.size(); ny = ys.size(); nz = zs.size();

    if (nx * ny * nz == static_cast<int>(nodes.size())) {
        bool regX = (nx > 1) ? (std::abs((xs.back() - xs.front())/(nx-1) - (xs[1]-xs[0])) < 1e-6) : true;
        bool regY = (ny > 1) ? (std::abs((ys.back() - ys.front())/(ny-1) - (ys[1]-ys[0])) < 1e-6) : true;
        bool regZ = (nz > 1) ? (std::abs((zs.back() - zs.front())/(nz-1) - (zs[1]-zs[0])) < 1e-6) : true;

        if (regX && regY && regZ) {
            isRegular = true;
            x0 = xs.front(); y0 = ys.front(); z0 = zs.front();
            dx = (nx > 1) ? (xs[1] - xs[0]) : 1.0;
            dy = (ny > 1) ? (ys[1] - ys[0]) : 1.0;
            dz = (nz > 1) ? (zs[1] - zs[0]) : 1.0;

            regularData.resize(nodes.size() * 3);
            for (const auto& n : nodes) {
                int i = std::round((n.x - x0) / dx);
                int j = std::round((n.y - y0) / dy);
                int k = std::round((n.z - z0) / dz);
                size_t idx = (k * ny * nx + j * nx + i) * 3;
                regularData[idx] = n.vx;
                regularData[idx+1] = n.vy;
                regularData[idx+2] = n.vz;
            }
        }
    }
}

/// @brief Fills the 6-component array {Ex,Ey,Ez,Bx,By,Bz} by interpolating
///        the E and B grids at the given position.
/// @param point  Position-time array {x, y, z, t}.
/// @param[out] field Output array of 6 doubles.
void GridField::GetFieldValue(const G4double point[4], G4double* field) const {
    double x = point[0];
    double y = point[1];
    double z = point[2];

    // Interpolate E with scale factor applied
    if (!fENodes.empty()) {
        Interpolate(fENodes, fERegular, fENx, fENy, fENz,
                    fEX0, fEY0, fEZ0, fEDx, fEDy, fEDz, fERegularData,
                    x, y, z, &field[0]);
        field[0] *= fEScale;
        field[1] *= fEScale;
        field[2] *= fEScale;
    } else {
        field[0] = field[1] = field[2] = 0.0;
    }

    // Interpolate B with scale factor applied
    if (!fBNodes.empty()) {
        Interpolate(fBNodes, fBRegular, fBNx, fBNy, fBNz,
                    fBX0, fBY0, fBZ0, fBDx, fBDy, fBDz, fBRegularData,
                    x, y, z, &field[3]);
        field[3] *= fBScale;
        field[4] *= fBScale;
        field[5] *= fBScale;
    } else {
        field[3] = field[4] = field[5] = 0.0;
    }
}

/// @brief Interpolates field values at (x, y, z) using trilinear (regular)
///        or nearest-neighbor (scattered) interpolation.
/// @param nodes        Field nodes.
/// @param isRegular    Whether the grid is regular.
/// @param nx, ny, nz   Grid dimensions.
/// @param x0, y0, z0   Origin.
/// @param dx, dy, dz   Grid spacing.
/// @param regularData  Regular grid data.
/// @param x, y, z      Query point.
/// @param[out] result  3-component field vector.
void GridField::Interpolate(const std::vector<Node>& nodes, bool isRegular,
                            int nx, int ny, int nz,
                            double x0, double y0, double z0,
                            double dx, double dy, double dz,
                            const std::vector<double>& regularData,
                            double x, double y, double z, double* result) const
{
    if (isRegular) {
        double nx_norm = (x - x0) / dx;
        double ny_norm = (y - y0) / dy;
        double nz_norm = (z - z0) / dz;

        int i0 = static_cast<int>(std::floor(nx_norm));
        int j0 = static_cast<int>(std::floor(ny_norm));
        int k0 = static_cast<int>(std::floor(nz_norm));

        if (i0 < 0 || i0 >= nx-1 || j0 < 0 || j0 >= ny-1 || k0 < 0 || k0 >= nz-1) {
            result[0] = result[1] = result[2] = 0.0;
            return;
        }

        double ddx = nx_norm - i0;
        double ddy = ny_norm - j0;
        double ddz = nz_norm - k0;

        auto getVal = [&](int i, int j, int k, int comp) -> double {
            size_t idx = (k * ny * nx + j * nx + i) * 3 + comp;
            return regularData[idx];
        };

        for (int c = 0; c < 3; ++c) {
            double v000 = getVal(i0, j0, k0, c);
            double v100 = getVal(i0+1, j0, k0, c);
            double v010 = getVal(i0, j0+1, k0, c);
            double v110 = getVal(i0+1, j0+1, k0, c);
            double v001 = getVal(i0, j0, k0+1, c);
            double v101 = getVal(i0+1, j0, k0+1, c);
            double v011 = getVal(i0, j0+1, k0+1, c);
            double v111 = getVal(i0+1, j0+1, k0+1, c);

            double v00 = v000 * (1-ddx) + v100 * ddx;
            double v01 = v010 * (1-ddx) + v110 * ddx;
            double v10 = v001 * (1-ddx) + v101 * ddx;
            double v11 = v011 * (1-ddx) + v111 * ddx;

            double v0 = v00 * (1-ddy) + v01 * ddy;
            double v1 = v10 * (1-ddy) + v11 * ddy;

            result[c] = v0 * (1-ddz) + v1 * ddz;
        }
    } else {
        double minDist2 = std::numeric_limits<double>::max();
        const Node* closest = nullptr;
        for (const auto& n : nodes) {
            double d2 = (n.x-x)*(n.x-x) + (n.y-y)*(n.y-y) + (n.z-z)*(n.z-z);
            if (d2 < minDist2) { minDist2 = d2; closest = &n; }
        }
        if (closest) {
            result[0] = closest->vx;
            result[1] = closest->vy;
            result[2] = closest->vz;
        } else {
            result[0] = result[1] = result[2] = 0.0;
        }
    }
}

/// @brief Creates a thread-local copy for Geant4 MT.
/// @return Pointer to a new GridField identical to this one.
FieldBase* GridField::Clone() const {
    return new GridField(*this);
}