//==============================================================================
// G4CARE - mesh-measure.js — Measurement mesh rendering
//
// Measurement mesh rendering
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { actors, objectInfo, IDENTITY_MATRIX } from '../state.js';
import { closestPointOnTriangle, triArea, cross3, dot3 } from '../math3d.js';
import { dbg } from '../api.js';

export function volumeMatrix(name) {
  const a = actors.get(name);
  return a ? a.getMatrix() : IDENTITY_MATRIX; // GDML meshes are already in world coords
}

export function closestPointOnMesh(name, worldPos) {
  const info = objectInfo.get(name);
  if (!info) return null;
  const poly = info.source.getOutputData();
  if (!poly) return null;
  const m = volumeMatrix(name);
  const tx = worldPos[0]-m[3], ty = worldPos[1]-m[7], tz = worldPos[2]-m[11];
  const lp = [m[0]*tx+m[1]*ty+m[2]*tz, m[4]*tx+m[5]*ty+m[6]*tz, m[8]*tx+m[9]*ty+m[10]*tz];
  const pts = poly.getPoints();
  const cells = poly.getPolys().getData();
  if (!cells) return null;
  let best = null, bestD = Infinity;
  let c = 0;
  while (c < cells.length) {
    const n = cells[c];
    const ids = [cells[c+1], cells[c+2], cells[c+3]];
    const tris = n === 4 ? [[0,1,2],[0,2,3]] : [[0,1,2]];
    for (const tr of tris) {
      const cl = closestPointOnTriangle(lp, pts.getPoint(ids[tr[0]]), pts.getPoint(ids[tr[1]]), pts.getPoint(ids[tr[2]]));
      const d = (cl[0]-lp[0])**2 + (cl[1]-lp[1])**2 + (cl[2]-lp[2])**2;
      if (d < bestD) { bestD = d; best = cl; }
    }
    c += n + 1;
  }
  if (!best) return null;
  return [m[0]*best[0]+m[4]*best[1]+m[8]*best[2]+m[3], m[1]*best[0]+m[5]*best[1]+m[9]*best[2]+m[7], m[2]*best[0]+m[6]*best[1]+m[10]*best[2]+m[11]];
}

export function sampleMeshSurfacePoints(name, count) {
  const info = objectInfo.get(name);
  if (!info) return [];
  const poly = info.source.getOutputData();
  if (!poly) return [];
  const m = volumeMatrix(name);
  const pts = poly.getPoints();
  if (!pts) return [];
  const cells = poly.getPolys().getData();
  const tris = [];
  let totalArea = 0;
  if (cells && cells.length >= 4) {
    let c = 0;
    while (c < cells.length) {
      const n = cells[c];
      if (!n || c + n >= cells.length) break;
      const ids = [cells[c+1], cells[c+2], cells[c+3]];
      const sub = n === 4 ? [[0,1,2],[0,2,3]] : [[0,1,2]];
      for (const tr of sub) {
        const A = pts.getPoint(ids[tr[0]]), B = pts.getPoint(ids[tr[1]]), C = pts.getPoint(ids[tr[2]]);
        const area = triArea(A, B, C);
        if (Number.isFinite(area) && area > 0) {
          tris.push({ A, B, C, area });
          totalArea += area;
        }
      }
      c += n + 1;
    }
  }
  const out = [];
  const toWorld = (lp) => [m[0]*lp[0]+m[4]*lp[1]+m[8]*lp[2]+m[3], m[1]*lp[0]+m[5]*lp[1]+m[9]*lp[2]+m[7], m[2]*lp[0]+m[6]*lp[1]+m[10]*lp[2]+m[11]];
  if (totalArea > 0) {
    for (let k = 0; k < count; k++) {
      let r = Math.random() * totalArea;
      for (const t of tris) {
        r -= t.area;
        if (r <= 0) {
          let u = Math.random(), v = Math.random();
          if (u + v > 1) { u = 1 - u; v = 1 - v; }
          out.push(toWorld([t.A[0] + u*(t.B[0]-t.A[0]) + v*(t.C[0]-t.A[0]), t.A[1] + u*(t.B[1]-t.A[1]) + v*(t.C[1]-t.A[1]), t.A[2] + u*(t.B[2]-t.A[2]) + v*(t.C[2]-t.A[2])]));
          break;
        }
      }
    }
  } else {
    const n = pts.getNumberOfPoints();
    const step = Math.max(1, Math.floor(n / count));
    for (let i = 0; i < n && out.length < count; i += step) out.push(toWorld(pts.getPoint(i)));
  }
  dbg('sample ' + name + ' cells=' + (cells ? cells.length : 0) + ' tris=' + tris.length + ' out=' + out.length);
  return out;
}

export function rayMeshIntersect(name, origin, dir) {
  const info = objectInfo.get(name);
  if (!info) return null;
  const poly = info.source.getOutputData();
  if (!poly) return null;
  const m = volumeMatrix(name);
  const tx = origin[0] - m[3], ty = origin[1] - m[7], tz = origin[2] - m[11];
  const lo = [m[0]*tx + m[1]*ty + m[2]*tz, m[4]*tx + m[5]*ty + m[6]*tz, m[8]*tx + m[9]*ty + m[10]*tz];
  const ld = [m[0]*dir[0] + m[1]*dir[1] + m[2]*dir[2], m[4]*dir[0] + m[5]*dir[1] + m[6]*dir[2], m[8]*dir[0] + m[9]*dir[1] + m[10]*dir[2]];
  const pts = poly.getPoints();
  const cells = poly.getPolys().getData();
  if (!cells) return null;
  let best = Infinity, bestT = null;
  let c = 0;
  while (c < cells.length) {
    const n = cells[c];
    const ids = [cells[c+1], cells[c+2], cells[c+3]];
    const tris = n === 4 ? [[0,1,2],[0,2,3]] : [[0,1,2]];
    for (const tr of tris) {
      const v0 = pts.getPoint(ids[tr[0]]);
      const v1 = pts.getPoint(ids[tr[1]]);
      const v2 = pts.getPoint(ids[tr[2]]);
      const e1 = [v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]];
      const e2 = [v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]];
      const pv = cross3(ld, e2);
      const det = dot3(e1, pv);
      if (Math.abs(det) < 1e-12) continue;
      const inv = 1/det;
      const tv = [lo[0]-v0[0], lo[1]-v0[1], lo[2]-v0[2]];
      const u = dot3(tv, pv) * inv;
      if (u < 0 || u > 1) continue;
      const qv = cross3(tv, e1);
      const v = dot3(ld, qv) * inv;
      if (v < 0 || u + v > 1) continue;
      const t = dot3(e2, qv) * inv;
      if (t > 1e-4 && t < best) { best = t; bestT = [origin[0]+dir[0]*t, origin[1]+dir[1]*t, origin[2]+dir[2]*t]; }
    }
    c += n + 1;
  }
  return bestT;
}
