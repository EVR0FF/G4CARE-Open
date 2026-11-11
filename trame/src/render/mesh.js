//==============================================================================
// G4CARE - mesh.js — Mesh rendering
//
// Mesh rendering
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import vtkDataArray from '@kitware/vtk.js/Common/Core/DataArray';
import { triArea } from '../math3d.js';

// Dense random surface point cloud (Geant4-style "points" view).
export function buildPointCloud(poly, density) {
  const MAX_CLOUD = 20000;
  const inPts = poly.getPoints();
  const cells = poly.getPolys().getData();
  if (!cells || cells.length < 4) return null;
  const nPer = cells[0];
  // First pass: total surface area (to cap the point count).
  let totalArea = 0;
  for (let c = 0; c + nPer <= cells.length; c += nPer + 1) {
    const n = cells[c];
    const ids = [];
    for (let i = 0; i < n; i++) ids.push(cells[c + 1 + i]);
    const verts = ids.map((id) => inPts.getPoint(id));
    totalArea += n === 4
      ? triArea(verts[0], verts[1], verts[2]) + triArea(verts[0], verts[2], verts[3])
      : triArea(verts[0], verts[1], verts[2]);
  }
  const effDensity = Math.min(density, MAX_CLOUD / Math.max(totalArea, 1e-6));
  const outPts = [];
  for (let c = 0; c + nPer <= cells.length; c += nPer + 1) {
    const n = cells[c];
    const ids = [];
    for (let i = 0; i < n; i++) ids.push(cells[c + 1 + i]);
    const verts = ids.map((id) => inPts.getPoint(id));
    const area = n === 4
      ? triArea(verts[0], verts[1], verts[2]) + triArea(verts[0], verts[2], verts[3])
      : triArea(verts[0], verts[1], verts[2]);
    const cnt = Math.round(area * effDensity);
    for (let s = 0; s < cnt; s++) {
      if (n === 3) {
        const u = Math.sqrt(Math.random());
        const v = Math.random();
        const wa = 1 - u, wb = u * (1 - v), wc = u * v;
        outPts.push([
          wa * verts[0][0] + wb * verts[1][0] + wc * verts[2][0],
          wa * verts[0][1] + wb * verts[1][1] + wc * verts[2][1],
          wa * verts[0][2] + wb * verts[1][2] + wc * verts[2][2],
        ]);
      } else {
        const u = Math.random(), v = Math.random();
        const w0 = (1 - u) * (1 - v), w1 = u * (1 - v), w2 = u * v, w3 = (1 - u) * v;
        outPts.push([
          w0 * verts[0][0] + w1 * verts[1][0] + w2 * verts[2][0] + w3 * verts[3][0],
          w0 * verts[0][1] + w1 * verts[1][1] + w2 * verts[2][1] + w3 * verts[3][1],
          w0 * verts[0][2] + w1 * verts[1][2] + w2 * verts[2][2] + w3 * verts[3][2],
        ]);
      }
    }
  }
  return pointsToPolyData(outPts);
}

export function pointsToPolyData(outPts) {
  const out = vtkPolyData.newInstance();
  const op = out.getPoints();
  op.setNumberOfPoints(outPts.length, 3);
  outPts.forEach((p, i) => op.setPoint(i, p[0], p[1], p[2]));
  const ov = out.getVerts();
  for (let i = 0; i < outPts.length; i++) ov.insertNextCell([i]);
  return out;
}

export function buildMeshPolyData(vertices, triangles) {
  const pd = vtkPolyData.newInstance();
  const npts = Math.floor(vertices.length / 3);
  const pts = pd.getPoints();
  pts.setNumberOfPoints(npts, 3);
  for (let i = 0; i < npts; i++) {
    pts.setPoint(i, vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
  }
  const polys = pd.getPolys();
  for (let i = 0; i + 2 < triangles.length; i += 3) {
    polys.insertNextCell([triangles[i], triangles[i + 1], triangles[i + 2]]);
  }
  addMeshNormals(pd, triangles);
  return pd;
}

export function addMeshNormals(pd, flatTris) {
  const pts = pd.getPoints();
  const npts = pts.getNumberOfPoints();
  const normals = new Float32Array(npts * 3);
  for (let c = 0; c + 2 < flatTris.length; c += 3) {
    const a = flatTris[c], b = flatTris[c + 1], d = flatTris[c + 2];
    const p0 = pts.getPoint(a), p1 = pts.getPoint(b), p2 = pts.getPoint(d);
    const e1 = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]];
    const e2 = [p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]];
    const nx = e1[1] * e2[2] - e1[2] * e2[1];
    const ny = e1[2] * e2[0] - e1[0] * e2[2];
    const nz = e1[0] * e2[1] - e1[1] * e2[0];
    for (const id of [a, b, d]) {
      normals[id * 3] += nx; normals[id * 3 + 1] += ny; normals[id * 3 + 2] += nz;
    }
  }
  for (let i = 0; i < npts; i++) {
    const x = normals[i * 3], y = normals[i * 3 + 1], z = normals[i * 3 + 2];
    const l = Math.hypot(x, y, z) || 1;
    normals[i * 3] = x / l; normals[i * 3 + 1] = y / l; normals[i * 3 + 2] = z / l;
  }
  pd.getPointData().setNormals(vtkDataArray.newInstance({ values: normals, numberOfComponents: 3 }));
}

export function getSphereRadius(dims) {
  if (dims && dims[1] !== undefined && dims[1] !== null) return dims[1];
  if (dims && dims[0] !== undefined && dims[0] !== null) return dims[0];
  return 10;
}

export function buildSpherePointCloud(radius, density) {
  const area = 4 * Math.PI * radius * radius;
  const cnt = Math.min(20000, Math.max(1, Math.round(area * density)));
  const outPts = [];
  for (let i = 0; i < cnt; i++) {
    const theta = 2 * Math.PI * Math.random();
    const phi = Math.acos(2 * Math.random() - 1);
    const sp = Math.sin(phi);
    outPts.push([radius * sp * Math.cos(theta), radius * sp * Math.sin(theta), radius * Math.cos(phi)]);
  }
  return pointsToPolyData(outPts);
}
