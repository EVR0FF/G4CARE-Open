//==============================================================================
//
// G4CARE
//
// @file    DICOMResampler.cc
// @brief   Implementation of DICOMResampler.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#include "DICOMResampler.hh"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/// @brief 3x3 matrix inverse (row-major). Returns false if singular.
bool Invert3x3(const double m[9], double inv[9]) {
    const double a = m[0], b = m[1], c = m[2];
    const double d = m[3], e = m[4], f = m[5];
    const double g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12) return false;
    const double r = 1.0 / det;
    inv[0] = (e * i - f * h) * r;
    inv[1] = (c * h - b * i) * r;
    inv[2] = (b * f - c * e) * r;
    inv[3] = (f * g - d * i) * r;
    inv[4] = (a * i - c * g) * r;
    inv[5] = (c * d - a * f) * r;
    inv[6] = (d * h - e * g) * r;
    inv[7] = (b * g - a * h) * r;
    inv[8] = (a * e - b * d) * r;
    return true;
}

/// @brief Trilinear interpolation of HU at fractional (fx,fy,fz).
///        fx = column, fy = row, fz = slice (input frame).
short Trilinear(const CTSeries& in, double fx, double fy, double fz) {
    if (fx < 0.0 || fy < 0.0 || fz < 0.0) return -1000;
    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int z0 = static_cast<int>(fz);
    if (x0 >= in.nx - 1 || y0 >= in.ny - 1 || z0 >= in.nz - 1) return -1000;

    const double tx = fx - x0, ty = fy - y0, tz = fz - z0;
    const int x1 = x0 + 1, y1 = y0 + 1, z1 = z0 + 1;

    double v = 0.0;
    v += (1 - tx) * (1 - ty) * (1 - tz) * in.HU(x0, y0, z0);
    v += tx * (1 - ty) * (1 - tz) * in.HU(x1, y0, z0);
    v += (1 - tx) * ty * (1 - tz) * in.HU(x0, y1, z0);
    v += tx * ty * (1 - tz) * in.HU(x1, y1, z0);
    v += (1 - tx) * (1 - ty) * tz * in.HU(x0, y0, z1);
    v += tx * (1 - ty) * tz * in.HU(x1, y0, z1);
    v += (1 - tx) * ty * tz * in.HU(x0, y1, z1);
    v += tx * ty * tz * in.HU(x1, y1, z1);
    return static_cast<short>(std::lround(v));
}

} // namespace

bool DICOMResampler::ResampleToAxisAligned(const CTSeries& in, CTSeries& out) {
    if (!in.valid || in.hu.empty() || in.nx <= 0 || in.ny <= 0 || in.nz <= 0) {
        return false;
    }

    // Directions.
    const double row[3] = {in.direction[0], in.direction[1], in.direction[2]};
    const double col[3] = {in.direction[3], in.direction[4], in.direction[5]};
    const double nrm[3] = {
        row[1] * col[2] - row[2] * col[1],
        row[2] * col[0] - row[0] * col[2],
        row[0] * col[1] - row[1] * col[0]
    };

    // Lattice basis: voxel (ix,iy,iz) centre = origin + ix*e0 + iy*e1 + iz*e2.
    const double e0[3] = {col[0] * in.dxMm, col[1] * in.dxMm, col[2] * in.dxMm};
    const double e1[3] = {row[0] * in.dyMm, row[1] * in.dyMm, row[2] * in.dyMm};
    const double e2[3] = {nrm[0] * in.dzMm, nrm[1] * in.dzMm, nrm[2] * in.dzMm};

    const double M[9] = {
        e0[0], e1[0], e2[0],
        e0[1], e1[1], e2[1],
        e0[2], e1[2], e2[2]
    };
    double invM[9];
    if (!Invert3x3(M, invM)) return false;

    // Axis-aligned bounding box of the tilted volume.
    double bmin[3] = {in.originMm[0], in.originMm[1], in.originMm[2]};
    double bmax[3] = {in.originMm[0], in.originMm[1], in.originMm[2]};
    const int ixs[2] = {0, in.nx - 1};
    const int iys[2] = {0, in.ny - 1};
    const int izs[2] = {0, in.nz - 1};
    for (int a = 0; a < 2; ++a) {
        for (int b = 0; b < 2; ++b) {
            for (int c = 0; c < 2; ++c) {
                const int ix = ixs[a], iy = iys[b], iz = izs[c];
                const double p[3] = {
                    in.originMm[0] + ix * e0[0] + iy * e1[0] + iz * e2[0],
                    in.originMm[1] + ix * e0[1] + iy * e1[1] + iz * e2[1],
                    in.originMm[2] + ix * e0[2] + iy * e1[2] + iz * e2[2]
                };
                for (int k = 0; k < 3; ++k) {
                    bmin[k] = std::min(bmin[k], p[k]);
                    bmax[k] = std::max(bmax[k], p[k]);
                }
            }
        }
    }

    // Output grid spacing (keep original).
    const double sx = in.dxMm, sy = in.dyMm, sz = in.dzMm;
    int nxGrid = static_cast<int>(std::ceil((bmax[0] - bmin[0]) / sx)) + 1;
    int nyGrid = static_cast<int>(std::ceil((bmax[1] - bmin[1]) / sy)) + 1;
    int nzGrid = static_cast<int>(std::ceil((bmax[2] - bmin[2]) / sz)) + 1;
    if (nxGrid < 1) nxGrid = 1;
    if (nyGrid < 1) nyGrid = 1;
    if (nzGrid < 1) nzGrid = 1;

    out = in;
    // Axis-aligned convention: row dir = +X, col dir = +Y (standard axial CT).
    out.nx = nyGrid;                       // columns along Y
    out.ny = nxGrid;                       // rows along X
    out.nz = nzGrid;
    out.dxMm = sy;                         // column spacing (along Y)
    out.dyMm = sx;                         // row spacing (along X)
    out.dzMm = sz;
    out.direction[0] = 1; out.direction[1] = 0; out.direction[2] = 0;  // row = +X
    out.direction[3] = 0; out.direction[4] = 1; out.direction[5] = 0;  // col = +Y
    out.originMm[0] = bmin[0] + 0.5 * sx;
    out.originMm[1] = bmin[1] + 0.5 * sy;
    out.originMm[2] = bmin[2] + 0.5 * sz;
    out.tilted = false;
    out.tiltAngleDeg = 0.0;

    out.hu.assign(static_cast<std::size_t>(nxGrid) * nyGrid * nzGrid,
                  static_cast<short>(-1000));

    for (int gz = 0; gz < nzGrid; ++gz) {
        for (int gx = 0; gx < nxGrid; ++gx) {      // X index (row)
            for (int gy = 0; gy < nyGrid; ++gy) {  // Y index (column)
                const double p[3] = {
                    bmin[0] + (gx + 0.5) * sx,
                    bmin[1] + (gy + 0.5) * sy,
                    bmin[2] + (gz + 0.5) * sz
                };
                const double q[3] = {
                    p[0] - in.originMm[0],
                    p[1] - in.originMm[1],
                    p[2] - in.originMm[2]
                };
                const double fx = invM[0] * q[0] + invM[1] * q[1] + invM[2] * q[2];
                const double fy = invM[3] * q[0] + invM[4] * q[1] + invM[5] * q[2];
                const double fz = invM[6] * q[0] + invM[7] * q[1] + invM[8] * q[2];
                const short hu = Trilinear(in, fx, fy, fz);
                // CTSeries index: HU(ix=gy, iy=gx, iz=gz) -> [gz*nx*ny + gx*nx + gy].
                out.hu[static_cast<std::size_t>(gz) * out.nx * out.ny
                       + static_cast<std::size_t>(gx) * out.nx + gy] = hu;
            }
        }
    }

    out.valid = true;
    return true;
}
