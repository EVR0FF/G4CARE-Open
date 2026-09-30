//==============================================================================
//
// G4CARE
//
// @file    Rasterizer.cc
// @brief   Implementation of DICOMRasterizer.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#include "Rasterizer.hh"

#include "CTSeries.hh"
#include "RTStruct.hh"

#include <cmath>
#include <cstddef>

namespace {

/// @brief 2D point-in-polygon test (ray casting, even-odd rule).
bool PointInPolygon(const std::vector<double>& px, const std::vector<double>& py,
                    double x, double y) {
    bool inside = false;
    const std::size_t n = px.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const bool cross = ((py[i] > y) != (py[j] > y)) &&
                           (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]);
        if (cross) inside = !inside;
    }
    return inside;
}

} // namespace

bool DICOMRasterizer::Rasterize(const CTSeries& ct, const RTStruct& rt,
                                std::vector<int>& labels) {
    if (!ct.valid || ct.hu.empty() || !rt.valid) return false;
    const int nx = ct.nx, ny = ct.ny, nz = ct.nz;
    labels.assign(static_cast<std::size_t>(nx) * ny * nz, -1);

    const double row[3] = {ct.direction[0], ct.direction[1], ct.direction[2]};
    const double col[3] = {ct.direction[3], ct.direction[4], ct.direction[5]};
    const double nrm[3] = {
        row[1] * col[2] - row[2] * col[1],
        row[2] * col[0] - row[0] * col[2],
        row[0] * col[1] - row[1] * col[0]
    };
    const double nn = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
    if (nn <= 0.0) return false;

    const double ox = ct.originMm[0], oy = ct.originMm[1], oz = ct.originMm[2];
    const double dx = (ct.dxMm > 0.0) ? ct.dxMm : 1.0;
    const double dy = (ct.dyMm > 0.0) ? ct.dyMm : 1.0;
    const double dz = (ct.dzMm > 0.0) ? ct.dzMm : 1.0;

    for (std::size_t r = 0; r < rt.rois.size(); ++r) {
        const RTStruct::ROI& roi = rt.rois[r];
        for (const RTStruct::Contour& c : roi.contours) {
            if (c.x.size() < 3) continue;

            // Average z of the contour -> nearest slice index.
            double zavg = 0.0;
            for (double z : c.z) zavg += z;
            zavg /= static_cast<double>(c.z.size());
            int iz = static_cast<int>(std::lround((zavg - oz) / dz));
            if (iz < 0 || iz >= nz) continue;

            // Project contour points to (column, row) index space.
            std::vector<double> pc, pr;
            pc.reserve(c.x.size());
            pr.reserve(c.x.size());
            for (std::size_t k = 0; k < c.x.size(); ++k) {
                const double ddx = c.x[k] - ox;
                const double ddy = c.y[k] - oy;
                const double ddz = c.z[k] - oz;
                const double colCoord = (ddx * col[0] + ddy * col[1] + ddz * col[2]) / dx;
                const double rowCoord = (ddx * row[0] + ddy * row[1] + ddz * row[2]) / dy;
                pc.push_back(colCoord);
                pr.push_back(rowCoord);
            }

            // Label voxels whose centre (integer col/row index) is inside.
            for (int iy = 0; iy < ny; ++iy) {
                for (int ix = 0; ix < nx; ++ix) {
                    if (PointInPolygon(pc, pr, static_cast<double>(ix),
                                       static_cast<double>(iy))) {
                        labels[static_cast<std::size_t>(iz) * (nx * ny)
                               + static_cast<std::size_t>(iy) * nx + ix] = static_cast<int>(r);
                    }
                }
            }
        }
    }
    return true;
}
