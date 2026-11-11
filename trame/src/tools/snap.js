//==============================================================================
// G4CARE - snap.js — Snap tool
//
// Snap tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - snapping (vertices, centers, grid, auxiliary points).
import vtkPointPicker from '@kitware/vtk.js/Rendering/Core/PointPicker';
import { renderer, renderWindow } from '../context.js';
import { snap, auxMarkers, view, gridState, localGrid, objectInfo, objectMeta, actorNameMap, actors, prims, objectMoveHook } from '../state.js';
import { makeMarkerActor, removeMarker, updateMarkerScales } from '../render/actors.js';
import { pickAt } from '../render/picking.js';
import { primCenterOf, primSnapTargets, resolvePoint } from './primitives.js';
import { segIntersect, circleTangentPoints } from '../math3d.js';
import { worldToObj, objToWorld } from './attachment.js';
import { btnStyle } from '../ui/dom.js';
import { pushHistory } from '../history.js';

let addGroup = null;
let setActiveMode = () => {};
let setMeasureMode = () => {};
let displayCoord = () => [0, 0];
let projectToGridPlane = () => null;

let snapBtn = null;
let auxBtn = null;
let snapModeSel = null;

const SNAP_PX = 20;
const pointPicker = vtkPointPicker.newInstance();

export function setupSnap(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  setMeasureMode = h.setMeasureMode;
  displayCoord = h.displayCoord;
  projectToGridPlane = h.projectToGridPlane;

  snapBtn = document.createElement('button');
  bindI18n(snapBtn, 'tools.snap');
  snapBtn.style.cssText = btnStyle;
  snapBtn.style.background = '#b45f06';
  snapBtn.style.color = '#fff';
  snapBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleSnap(); });

  auxBtn = document.createElement('button');
  bindI18n(auxBtn, 'tools.point');
  auxBtn.style.cssText = btnStyle;
  auxBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleAuxMode(); });

  snapModeSel = document.createElement('select');
  snapModeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
  [['snap.auto', 'auto'], ['snap.centerVolume', 'center'], ['snap.faceCenter', 'face'], ['snap.midpoint', 'midpoint'], ['snap.surface', 'surface'], ['snap.intersect', 'intersect'], ['snap.tangent', 'tangent']].forEach(([key, val]) => {
    const opt = document.createElement('option');
    opt.value = val;
    bindI18n(opt, key);
    snapModeSel.appendChild(opt);
  });
  snapModeSel.value = 'auto';
  snapModeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
  snapModeSel.addEventListener('change', () => { snap.mode = snapModeSel.value; });

  const clearPointsBtn = document.createElement('button');
  bindI18n(clearPointsBtn, 'tools.clear');
  clearPointsBtn.style.cssText = btnStyle;
  clearPointsBtn.setAttribute('data-i18n-title', 'tip.clearPoints'); clearPointsBtn.title = t('tip.clearPoints');
  clearPointsBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); pushHistory(); snap.auxPoints = []; snap.auxMeta = []; updateAuxMarkers(); });

  const snapGroup = addGroup('group.snap');
  snapGroup.appendChild(snapBtn);
  snapGroup.appendChild(snapModeSel);
  snapGroup.appendChild(auxBtn);
  snapGroup.appendChild(clearPointsBtn);

  objectMoveHook.fns.push(moveAuxWithObject);
}

export function resetAuxMode() {
  snap.auxMode = false;
  auxBtn.style.background = '#3a3a3a';
  auxBtn.style.color = '#ddd';
}

export function resetDeleteMode() {
  snap.deleteMode = false;
}

export function toggleSnap() {
  snap.enabled = !snap.enabled;
  snapBtn.style.background = snap.enabled ? '#b45f06' : '#3a3a3a';
  snapBtn.style.color = '#fff';
}

export function toggleAuxMode() {
  if (snap.auxMode) setActiveMode('select');
  else { setActiveMode('aux'); snap.auxMode = true; auxBtn.style.background = '#b45f06'; auxBtn.style.color = '#fff'; }
}

export function toggleDeleteMode() {
  if (snap.deleteMode) setActiveMode('select');
  else { setActiveMode('delete'); snap.deleteMode = true; }
}

export function updateAuxMarkers() {
  for (const a of auxMarkers) removeMarker(a);
  auxMarkers.length = 0;
  if (!snap.auxPoints.length) { renderWindow.render(); return; }
  for (const p of snap.auxPoints) {
    const a = makeMarkerActor(0.2, 1.0, 0.3);
    a.setPosition(p[0], p[1], p[2]);
    auxMarkers.push(a);
  }
  updateMarkerScales();
  renderWindow.render();
}

export function addAuxPointAt(p, parentName) {
  if (!p) return;
  pushHistory();
  snap.auxPoints.push(p);
  snap.auxMeta.push({
    name: t('prop.pointPrefix') + snap.auxPoints.length,
    layer: '',
    parent: parentName ? { name: parentName, local: worldToObj(parentName, p) } : null,
  });
  updateAuxMarkers();
}

export function placeAuxPoint(x, y) {
  const hit = pickAt(x, y);
  addAuxPointAt(resolvePoint(x, y), hit && hit.name ? hit.name : null);
}

// Recompute aux points attached to an object from their stored local frame
// (follows both translation and rotation of the parent).
export function moveAuxWithObject(name) {
  let changed = false;
  for (let i = 0; i < snap.auxPoints.length; i++) {
    const meta = snap.auxMeta[i];
    if (meta && meta.parent && meta.parent.name === name && meta.parent.local) {
      snap.auxPoints[i] = objToWorld(name, meta.parent.local);
      changed = true;
    }
  }
  if (changed) updateAuxMarkers();
}

export function deletePointAt(x, y) {
  if (!snap.auxPoints.length) return;
  let idx = -1, bestD = 15;
  for (let i = 0; i < snap.auxPoints.length; i++) {
    const d = screenDist(x, y, snap.auxPoints[i]);
    if (d < bestD) { bestD = d; idx = i; }
  }
  if (idx >= 0) {
    pushHistory();
    snap.auxPoints.splice(idx, 1);
    snap.auxMeta.splice(idx, 1);
    updateAuxMarkers();
  }
}

export function screenDist(x, y, p) {
  const d = displayCoord(renderer, p);
  return Math.hypot(d[0] - x, d[1] - y);
}

export function collectSnapTargets() {
  const targets = [];
  for (const [name, info] of objectInfo) {
    const meta = objectMeta.get(name);
    if (!meta || meta.visible === false) continue;
    const shape = meta.shape;
    const dims = meta.dims || [];
    const pos = info.pos || [0, 0, 0];
    if (shape === 'sphere' || shape === 'orb') {
      targets.push([pos[0], pos[1], pos[2]]);
    } else if (shape === 'box' || shape === 'para') {
      const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
      targets.push([pos[0], pos[1], pos[2]]);
      for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1])
        targets.push([pos[0] + sx * hx, pos[1] + sy * hy, pos[2] + sz * hz]);
    } else if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
      const isCyl = shape === 'cylinder';
      const h = isCyl ? (dims[1] || 0) : (dims[2] || 0);
      targets.push([pos[0], pos[1], pos[2] - h / 2]);
      targets.push([pos[0], pos[1], pos[2] + h / 2]);
      targets.push([pos[0], pos[1], pos[2]]);
    } else if (shape === 'cone') {
      const h = dims[4] || 0;
      targets.push([pos[0], pos[1], pos[2] - h / 2]);
      targets.push([pos[0], pos[1], pos[2] + h / 2]);
    } else if (shape === 'torus') {
      targets.push([pos[0], pos[1], pos[2]]);
    }
  }
  targets.push(...primSnapTargets());
  return targets;
}

export function faceCentersOf(name) {
  const info = objectInfo.get(name);
  const meta = objectMeta.get(name);
  if (!info || !meta) return [];
  const dims = meta.dims || [];
  const pos = info.pos || [0, 0, 0];
  const shape = meta.shape;
  const out = [];
  if (shape === 'box' || shape === 'para') {
    const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
    out.push([pos[0] - hx, pos[1], pos[2]], [pos[0] + hx, pos[1], pos[2]]);
    out.push([pos[0], pos[1] - hy, pos[2]], [pos[0], pos[1] + hy, pos[2]]);
    out.push([pos[0], pos[1], pos[2] - hz], [pos[0], pos[1], pos[2] + hz]);
  } else if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
    const isCyl = shape === 'cylinder';
    const h = isCyl ? (dims[1] || 0) : (dims[2] || 0);
    out.push([pos[0], pos[1], pos[2] - h / 2], [pos[0], pos[1], pos[2] + h / 2]);
  } else if (shape === 'cone') {
    const h = dims[4] || 0;
    out.push([pos[0], pos[1], pos[2] - h / 2], [pos[0], pos[1], pos[2] + h / 2]);
  }
  return out;
}

export function edgeMidpointsOf(name) {
  const info = objectInfo.get(name);
  const meta = objectMeta.get(name);
  if (!info || !meta) return [];
  const dims = meta.dims || [];
  const pos = info.pos || [0, 0, 0];
  const shape = meta.shape;
  const out = [];
  if (shape === 'box' || shape === 'para') {
    const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
    const c = [
      [-hx, -hy, -hz], [hx, -hy, -hz], [hx, hy, -hz], [-hx, hy, -hz],
      [-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz],
    ];
    for (let i = 0; i < 8; i++) {
      for (let j = i + 1; j < 8; j++) {
        const d = [Math.abs(c[i][0] - c[j][0]), Math.abs(c[i][1] - c[j][1]), Math.abs(c[i][2] - c[j][2])];
        if (d.filter((x) => x > 1e-9).length === 1) {
          out.push([pos[0] + (c[i][0] + c[j][0]) / 2, pos[1] + (c[i][1] + c[j][1]) / 2, pos[2] + (c[i][2] + c[j][2]) / 2]);
        }
      }
    }
  } else if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
    const isCyl = shape === 'cylinder';
    const r = isCyl ? (dims[0] || 0) : (dims[1] || 0);
    const h = isCyl ? (dims[1] || 0) : (dims[2] || 0);
    for (const z of [-h / 2, h / 2]) {
      out.push([pos[0] + r, pos[1], pos[2] + z], [pos[0] - r, pos[1], pos[2] + z], [pos[0], pos[1] + r, pos[2] + z], [pos[0], pos[1] - r, pos[2] + z]);
    }
  }
  return out;
}

export function collectSegments() {
  const segs = [];
  for (const p of prims) {
    if (p.visible === false) continue;
    if (p.type === 'line' && p.pts.length >= 2) {
      segs.push([p.pts[0], p.pts[1]]);
    } else if (p.type === 'rect' && p.pts.length >= 2) {
      const a = p.pts[0], b = p.pts[1];
      const c1 = [b[0], a[1], a[2]], c2 = [a[0], b[1], a[2]];
      segs.push([a, c1], [c1, b], [b, c2], [c2, a]);
    }
  }
  return segs;
}

export function segmentIntersections() {
  const segs = collectSegments();
  const out = [];
  for (let i = 0; i < segs.length; i++) {
    for (let j = i + 1; j < segs.length; j++) {
      const p = segIntersect(segs[i][0], segs[i][1], segs[j][0], segs[j][1]);
      if (p) out.push(p);
    }
  }
  return out;
}

export function tangentPoints(ref) {
  const out = [];
  for (const p of prims) {
    if (p.visible === false) continue;
    if ((p.type === 'circle' || p.type === 'arc') && p.pts.length >= 2 && p.pts[0] && p.pts[1]) {
      const C = p.pts[0], rp = p.pts[1];
      const r = Math.hypot(rp[0] - C[0], rp[1] - C[1]);
      for (const t of circleTangentPoints(ref, C, r)) out.push(t);
    }
  }
  return out;
}

export function snapPoint(x, y) {
  if (!snap.enabled || snap.mode === 'surface') return null;
  if (snap.mode === 'center') {
    const hit = pickAt(x, y);
    if (hit) {
      if (hit.primIndex !== undefined) return primCenterOf(prims[hit.primIndex]);
      const info = objectInfo.get(hit.name);
      if (info) return [...(info.pos || [0, 0, 0])];
    }
    return null;
  }
  if (snap.mode === 'face') {
    const hit = pickAt(x, y);
    if (hit) {
      if (hit.primIndex !== undefined) return primCenterOf(prims[hit.primIndex]);
      let best = null, bestD = Infinity;
      for (const fc of faceCentersOf(hit.name)) {
        const d = Math.hypot(fc[0] - hit.pos[0], fc[1] - hit.pos[1], fc[2] - hit.pos[2]);
        if (d < bestD) { bestD = d; best = fc; }
      }
      return best;
    }
    return null;
  }
  if (snap.mode === 'midpoint') {
    const hit = pickAt(x, y);
    if (hit) {
      if (hit.primIndex !== undefined) return primCenterOf(prims[hit.primIndex]);
      let best = null, bestD = Infinity;
      const targets = edgeMidpointsOf(hit.name).concat(faceCentersOf(hit.name));
      for (const t of targets) {
        const d = Math.hypot(t[0] - hit.pos[0], t[1] - hit.pos[1], t[2] - hit.pos[2]);
        if (d < bestD) { bestD = d; best = t; }
      }
      return best;
    }
    return null;
  }
  if (snap.mode === 'intersect') {
    let best = null, bestD = SNAP_PX;
    for (const t of segmentIntersections()) {
      const d = screenDist(x, y, t);
      if (d < bestD) { bestD = d; best = t; }
    }
    return best;
  }
  if (snap.mode === 'tangent') {
    const ref = projectToGridPlane(x, y);
    if (!ref) return null;
    let best = null, bestD = SNAP_PX;
    for (const t of tangentPoints(ref)) {
      const d = screenDist(x, y, t);
      if (d < bestD) { bestD = d; best = t; }
    }
    return best;
  }
  const v = snapToVertex(x, y);
  if (v && screenDist(x, y, v) < SNAP_PX) return v;
  let best = null, bestD = SNAP_PX;
  for (const t of collectSnapTargets().concat(snap.auxPoints)) {
    const d = screenDist(x, y, t);
    if (d < bestD) { bestD = d; best = t; }
  }
  if (best) return best;
  if (snap.grid && gridState.spacing > 0 && !pickAt(x, y)) {
    const wp = projectToGridPlane(x, y);
    if (wp) {
      const g = gridState.spacing;
      const node = [Math.round(wp[0] / g) * g, Math.round(wp[1] / g) * g, 0];
      if (screenDist(x, y, node) < SNAP_PX) return node;
    }
  }
  return null;
}

export function snapToVertex(x, y) {
  pointPicker.pick([x, y, 0], renderer);
  const picked = pointPicker.getActors();
  if (picked.length && actorNameMap.has(picked[0])) {
    return pointPicker.getPickedPositions()[0];
  }
  return null;
}

