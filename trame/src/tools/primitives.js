//==============================================================================
// G4CARE - primitives.js — Primitives tool
//
// Primitives tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - 2D primitives (lines, circles, arcs, rects, polygons, ellipses).
import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import { renderer, renderWindow } from '../context.js';
import { view, prims, primMarkers, primActorMap, moveNotifier, objectMoveHook, objectMeta } from '../state.js';
import { primBasis } from '../math3d.js';
import { worldToObj, objToWorld, worldDirToObj, objDirToWorld } from './attachment.js';
import { makeMarkerActor, removeMarker } from '../render/actors.js';
import { pickAt } from '../render/picking.js';
import { btnStyle, promptText, showToast } from '../ui/dom.js';
import { pushHistory } from '../history.js';

let addGroup = null;
let setActiveMode = () => {};
let snapPoint = () => null;
let surfaceNormalAt = () => null;
let activePlane = () => undefined;
let projectToGridPlane = () => null;
let updateTreeUI = () => {};

let primTypeSel = null;
let primBtn = null;
let primHint = null;
let pendingPrimParent = null; // object name the in-progress primitive is attached to

export function setupPrimitives(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  snapPoint = h.snapPoint;
  surfaceNormalAt = h.surfaceNormalAt;
  activePlane = h.activePlane;
  projectToGridPlane = h.projectToGridPlane;
  updateTreeUI = h.updateTreeUI;

  primTypeSel = document.createElement('select');
  primTypeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
  [['prim.line', 'line'], ['prim.circle', 'circle'], ['prim.arc', 'arc'], ['prim.rect', 'rect'], ['prim.polygon', 'polygon'], ['prim.ellipse', 'ellipse'], ['prim.grid', 'grid']].forEach(([k, v]) => { const o = document.createElement('option'); o.value = v; bindI18n(o, k); primTypeSel.appendChild(o); });
  primTypeSel.value = 'line';
  primTypeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
  primTypeSel.addEventListener('change', () => { view.primType = primTypeSel.value; updatePrimHint(); });

  primBtn = document.createElement('button');
  bindI18n(primBtn, 'tools.add');
  primBtn.style.cssText = btnStyle;
  primBtn.setAttribute('data-i18n-title', 'tip.primMode'); primBtn.title = t('tip.primMode');
  primBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); togglePrimMode(); });

  const gridBtn = document.createElement('button');
  bindI18n(gridBtn, 'tools.grid');
  gridBtn.style.cssText = btnStyle;
  gridBtn.setAttribute('data-i18n-title', 'tip.grid'); gridBtn.title = t('tip.grid');
  gridBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const ap = activePlane();
    const center = ap ? [ap.getOrigin()[0], ap.getOrigin()[1], ap.getOrigin()[2]] : [0, 0, 0];
    const normal = ap ? [ap.getNormal()[0], ap.getNormal()[1], ap.getNormal()[2]] : [0, 0, 1];
    addGridPrim(center, normal, 100, 10);
  });

  const axesBtn = document.createElement('button');
  bindI18n(axesBtn, 'tools.axes');
  axesBtn.style.cssText = btnStyle;
  axesBtn.setAttribute('data-i18n-title', 'tip.axes'); axesBtn.title = t('tip.axes');
  axesBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); addAxesPrims(); });

  const contourBtn = document.createElement('button');
  bindI18n(contourBtn, 'tools.contour');
  contourBtn.style.cssText = btnStyle;
  contourBtn.setAttribute('data-i18n-title', 'tip.contour'); contourBtn.title = t('tip.contour');
  contourBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); projectContourToPlane(); });

  const primGroup = addGroup('group.figures');
  primGroup.appendChild(primTypeSel);
  primGroup.appendChild(primBtn);
  primGroup.appendChild(gridBtn);
  primGroup.appendChild(axesBtn);
  primGroup.appendChild(contourBtn);

  primHint = document.createElement('span');
  primHint.style.cssText = 'color:#777;font:10px Arial;margin-left:2px;white-space:nowrap;';
  primGroup.appendChild(primHint);

  objectMoveHook.fns.push(movePrimsWithObject);
}

const PRIM_POINT_LABELS = {
  line: [t('prim.pt.start'), t('prim.pt.end')],
  circle: [t('prim.pt.center'), t('prim.pt.circlePoint')],
  arc: [t('prim.pt.center'), t('prim.pt.arcStart'), t('prim.pt.arcEnd')],
  rect: [t('prim.pt.corner1'), t('prim.pt.oppositeCorner')],
  polygon: [t('prim.pt.center'), t('prim.pt.vertex')],
  ellipse: [t('prim.pt.center'), t('prim.pt.ellipsePoint')],
};

export function togglePrimMode() {
  if (view.primType === 'grid') {
    // Grid is created directly (no point-picking).
    const ap = activePlane();
    const center = ap ? [ap.getOrigin()[0], ap.getOrigin()[1], ap.getOrigin()[2]] : [0, 0, 0];
    const normal = ap ? [ap.getNormal()[0], ap.getNormal()[1], ap.getNormal()[2]] : [0, 0, 1];
    addGridPrim(center, normal, 100, 10);
    return;
  }
  view.primMode = !view.primMode;
  view.primPts = [];
  primBtn.style.background = view.primMode ? '#b45f06' : '#3a3a3a';
  primBtn.style.color = '#fff';
  updatePrimMarkers();
  updatePrimHint();
  if (view.primMode) setActiveMode('prim');
}

export function resetPrimMode() {
  view.primMode = false;
  view.primPts = [];
  primBtn.style.background = '#3a3a3a';
  primBtn.style.color = '#ddd';
  updatePrimMarkers();
  updatePrimHint();
}

export function updatePrimHint() {
  if (!view.primMode) { primHint.textContent = ''; return; }
  const labels = PRIM_POINT_LABELS[view.primType] || [];
  const need = { line: 2, circle: 2, rect: 2, polygon: 2, arc: 3, ellipse: 2 }[view.primType] || 2;
  if (view.primPts.length >= need) { bindI18n(primHint, 'msg.done'); return; }
  const next = labels[view.primPts.length] || (t('prim.pt.point') + (view.primPts.length + 1));
  primHint.textContent = `точка ${view.primPts.length + 1}/${need} — ${next}`;
}

export function buildCirclePolyline(center, radius, normal, segments = 64) {
  const { u, v } = primBasis(normal);
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(segments + 1, 3);
  for (let i = 0; i <= segments; i++) {
    const a = (i / segments) * 2 * Math.PI;
    const ca = radius * Math.cos(a), sa = radius * Math.sin(a);
    pts.setPoint(i, center[0] + u[0]*ca + v[0]*sa, center[1] + u[1]*ca + v[1]*sa, center[2] + u[2]*ca + v[2]*sa);
  }
  const lines = pd.getLines();
  for (let i = 0; i < segments; i++) lines.insertNextCell([i, i + 1]);
  return pd;
}

export function buildRectPolyline(a, b, normal) {
  const { u, v } = primBasis(normal);
  const du = (b[0]-a[0])*u[0] + (b[1]-a[1])*u[1] + (b[2]-a[2])*u[2];
  const dv = (b[0]-a[0])*v[0] + (b[1]-a[1])*v[1] + (b[2]-a[2])*v[2];
  const corners = [
    [a[0], a[1], a[2]],
    [a[0] + u[0]*du, a[1] + u[1]*du, a[2] + u[2]*du],
    [a[0] + u[0]*du + v[0]*dv, a[1] + u[1]*du + v[1]*dv, a[2] + u[2]*du + v[2]*dv],
    [a[0] + v[0]*dv, a[1] + v[1]*dv, a[2] + v[2]*dv],
  ];
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(5, 3);
  corners.forEach((c, i) => pts.setPoint(i, c[0], c[1], c[2]));
  pts.setPoint(4, corners[0][0], corners[0][1], corners[0][2]);
  const lines = pd.getLines();
  for (let i = 0; i < 4; i++) lines.insertNextCell([i, i + 1]);
  return pd;
}

export function buildPolygonPolyline(center, radius, n, normal) {
  const { u, v } = primBasis(normal);
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(n + 1, 3);
  for (let i = 0; i <= n; i++) {
    const a = (i / n) * 2 * Math.PI - Math.PI / 2;
    const ca = radius * Math.cos(a), sa = radius * Math.sin(a);
    pts.setPoint(i, center[0] + u[0]*ca + v[0]*sa, center[1] + u[1]*ca + v[1]*sa, center[2] + u[2]*ca + v[2]*sa);
  }
  const lines = pd.getLines();
  for (let i = 0; i < n; i++) lines.insertNextCell([i, i + 1]);
  return pd;
}

export function buildArcPolyline(center, start, end, normal, segments = 48) {
  const { u, v } = primBasis(normal);
  const sdx = start[0]-center[0], sdy = start[1]-center[1], sdz = start[2]-center[2];
  const edx = end[0]-center[0], edy = end[1]-center[1], edz = end[2]-center[2];
  const sx = sdx*u[0] + sdy*u[1] + sdz*u[2];
  const sy = sdx*v[0] + sdy*v[1] + sdz*v[2];
  const ex = edx*u[0] + edy*u[1] + edz*u[2];
  const ey = edx*v[0] + edy*v[1] + edz*v[2];
  const a0 = Math.atan2(sy, sx);
  let span = Math.atan2(ey, ex) - a0;
  while (span <= 0) span += 2 * Math.PI;
  const r = Math.hypot(sx, sy);
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(segments + 1, 3);
  for (let i = 0; i <= segments; i++) {
    const a = a0 + span * (i / segments);
    const ca = r * Math.cos(a), sa = r * Math.sin(a);
    pts.setPoint(i, center[0] + u[0]*ca + v[0]*sa, center[1] + u[1]*ca + v[1]*sa, center[2] + u[2]*ca + v[2]*sa);
  }
  const lines = pd.getLines();
  for (let i = 0; i < segments; i++) lines.insertNextCell([i, i + 1]);
  return pd;
}

export function buildEllipsePolyline(center, a, b, normal, segments = 64) {
  const { u, v } = primBasis(normal);
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(segments + 1, 3);
  for (let i = 0; i <= segments; i++) {
    const t = (i / segments) * 2 * Math.PI;
    const ca = a * Math.cos(t), sa = b * Math.sin(t);
    pts.setPoint(i, center[0] + u[0]*ca + v[0]*sa, center[1] + u[1]*ca + v[1]*sa, center[2] + u[2]*ca + v[2]*sa);
  }
  const lines = pd.getLines();
  for (let i = 0; i < segments; i++) lines.insertNextCell([i, i + 1]);
  return pd;
}

// 2D grid lines in the plane (center, normal), as a vtkPolyData (not volumetric).
export function buildGridPolyline(center, normal, halfSize, spacing) {
  const { u, v } = primBasis(normal);
  const pd = vtkPolyData.newInstance();
  const points = pd.getPoints();
  const lines = pd.getLines();
  const n = Math.max(1, Math.floor(halfSize / Math.max(spacing, 1e-12)));
  const pts = [];
  const cells = [];
  let pid = 0;
  const addLine = (a, b) => { pts.push(a, b); cells.push([pid, pid + 1]); pid += 2; };
  for (let i = -n; i <= n; i++) {
    const t = i * spacing;
    addLine(
      [center[0] + t * u[0] - halfSize * v[0], center[1] + t * u[1] - halfSize * v[1], center[2] + t * u[2] - halfSize * v[2]],
      [center[0] + t * u[0] + halfSize * v[0], center[1] + t * u[1] + halfSize * v[1], center[2] + t * u[2] + halfSize * v[2]],
    );
    addLine(
      [center[0] - halfSize * u[0] + t * v[0], center[1] - halfSize * u[1] + t * v[1], center[2] - halfSize * u[2] + t * v[2]],
      [center[0] + halfSize * u[0] + t * v[0], center[1] + halfSize * u[1] + t * v[1], center[2] + halfSize * u[2] + t * v[2]],
    );
  }
  points.setNumberOfPoints(pts.length, 3);
  pts.forEach((p, i) => points.setPoint(i, p[0], p[1], p[2]));
  cells.forEach((c) => lines.insertNextCell(c));
  return pd;
}

// Create a grid primitive (same level as lines/circles). It lives in the tree,
// is selectable, and its spacing/size are edited in the property panel or via
// right-click → «Настроить сетку».
export function addGridPrim(center, normal, halfSize, spacing) {
  const prim = {
    type: 'grid',
    name: t('prim.gridName') + (prims.length + 1),
    layer: '',
    pts: [center ? [...center] : [0, 0, 0]],
    normal: normal ? [...normal] : [0, 0, 1],
    spacing: spacing || 10,
    halfSize: halfSize || 100,
    actor: null,
    visible: true,
    parent: null,
  };
  pushHistory();
  prims.push(prim);
  updatePrims();
  updateTreeUI();
  return prim;
}

// Create the three reference axes (орты) at the origin as selectable line
// primitives — they can be picked, used in plane construction («По примитивам»)
// or as axes for other tools.
export function addAxesPrims() {
  const L = 100;
  const defs = [
    { name: t('prim.axisX'), pts: [[-L, 0, 0], [L, 0, 0]], color: [0.95, 0.45, 0.45] },
    { name: t('prim.axisY'), pts: [[0, -L, 0], [0, L, 0]], color: [0.45, 0.95, 0.45] },
    { name: t('prim.axisZ'), pts: [[0, 0, -L], [0, 0, L]], color: [0.45, 0.6, 0.95] },
  ];
  for (const d of defs) {
    if (prims.some((p) => p.name === d.name)) continue;
    prims.push({ type: 'line', name: d.name, layer: '', pts: d.pts, normal: [0, 0, 1], actor: null, visible: true, parent: null, color: d.color });
  }
  pushHistory();
  updatePrims();
  updateTreeUI();
}

// Project the selected object's silhouette onto the active plane and create a
// sketch primitive (rect for boxes, circle for spheres/cylinders).
export function projectContourToPlane() {
  const name = view.selectedName;
  if (!name || !objectMeta.has(name)) { showToast(t('common.selectObjectFirst')); return; }
  const meta = objectMeta.get(name);
  if (!meta || !meta.pos || !meta.dims) return;
  const ap = activePlane();
  if (!ap) { showToast(t('common.needPlane')); return; }
  const n = [ap.getNormal()[0], ap.getNormal()[1], ap.getNormal()[2]];
  const o = [ap.getOrigin()[0], ap.getOrigin()[1], ap.getOrigin()[2]];
  const { u, v } = primBasis(n);
  const project = (p) => {
    const d = (p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] + (p[2] - o[2]) * n[2];
    return [p[0] - d * n[0], p[1] - d * n[1], p[2] - d * n[2]];
  };
  const pos = meta.pos;
  const shape = meta.shape;
  const dims = meta.dims;
  let prim = null;
  if (shape === 'box' || shape === 'para') {
    const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
    let minU = Infinity, maxU = -Infinity, minV = Infinity, maxV = -Infinity;
    for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) {
      const c = project([pos[0] + sx * hx, pos[1] + sy * hy, pos[2] + sz * hz]);
      const cu = (c[0] - o[0]) * u[0] + (c[1] - o[1]) * u[1] + (c[2] - o[2]) * u[2];
      const cv = (c[0] - o[0]) * v[0] + (c[1] - o[1]) * v[1] + (c[2] - o[2]) * v[2];
      minU = Math.min(minU, cu); maxU = Math.max(maxU, cu);
      minV = Math.min(minV, cv); maxV = Math.max(maxV, cv);
    }
    const p1 = [o[0] + u[0] * minU + v[0] * minV, o[1] + u[1] * minU + v[1] * minV, o[2] + u[2] * minU + v[2] * minV];
    const p2 = [o[0] + u[0] * maxU + v[0] * maxV, o[1] + u[1] * maxU + v[1] * maxV, o[2] + u[2] * maxU + v[2] * maxV];
    prim = { type: 'rect', name: t('prim.contourName') + name, layer: '', pts: [p1, p2], normal: [...n], actor: null, visible: true, parent: null };
  } else {
    const r = (shape === 'sphere' || shape === 'orb') ? (dims[1] || dims[0] || 0) : (dims[0] || 0);
    const c = project(pos);
    const rp = [c[0] + u[0] * r, c[1] + u[1] * r, c[2] + u[2] * r];
    prim = { type: 'circle', name: t('prim.contourName') + name, layer: '', pts: [c, rp], normal: [...n], actor: null, visible: true, parent: null };
  }
  pushHistory();
  prims.push(prim);
  updatePrims();
  updateTreeUI();
  showToast(t('prim.contourProjected'));
}

export function buildPrimActor(prim) {
  const m = vtkMapper.newInstance();
  const a = vtkActor.newInstance();
  a.setMapper(m);
  a.getProperty().setColor(prim.color ? prim.color[0] : 0.25, prim.color ? prim.color[1] : 0.9, prim.color ? prim.color[2] : 0.45);
  a.getProperty().setLineWidth(2);
  if (prim.type === 'line') {
    if (prim.pts.length < 2) return null;
    const s = vtkLineSource.newInstance();
    s.setPoint1(prim.pts[0][0], prim.pts[0][1], prim.pts[0][2]);
    s.setPoint2(prim.pts[1][0], prim.pts[1][1], prim.pts[1][2]);
    m.setInputConnection(s.getOutputPort());
  } else if (prim.type === 'circle') {
    if (prim.pts.length < 2) return null;
    const c = prim.pts[0], rp = prim.pts[1];
    const r = Math.hypot(rp[0] - c[0], rp[1] - c[1], rp[2] - c[2]);
    m.setInputData(buildCirclePolyline(c, r, prim.normal));
  } else if (prim.type === 'rect') {
    if (prim.pts.length < 2) return null;
    m.setInputData(buildRectPolyline(prim.pts[0], prim.pts[1], prim.normal));
  } else if (prim.type === 'polygon') {
    if (prim.pts.length < 2 || !prim.n) return null;
    const c = prim.pts[0], rp = prim.pts[1];
    const r = Math.hypot(rp[0] - c[0], rp[1] - c[1], rp[2] - c[2]);
    m.setInputData(buildPolygonPolyline(c, r, prim.n, prim.normal));
  } else if (prim.type === 'arc') {
    if (prim.pts.length < 3) return null;
    m.setInputData(buildArcPolyline(prim.pts[0], prim.pts[1], prim.pts[2], prim.normal));
  } else if (prim.type === 'ellipse') {
    if (prim.pts.length < 2) return null;
    const c = prim.pts[0], p = prim.pts[1];
    const { u, v } = primBasis(prim.normal);
    const dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
    const ea = dx*u[0] + dy*u[1] + dz*u[2];
    const eb = dx*v[0] + dy*v[1] + dz*v[2];
    m.setInputData(buildEllipsePolyline(c, ea, eb, prim.normal));
  } else if (prim.type === 'grid') {
    const c = prim.pts && prim.pts[0] ? prim.pts[0] : [0, 0, 0];
    m.setInputData(buildGridPolyline(c, prim.normal || [0, 0, 1], prim.halfSize || 100, prim.spacing || 10));
    a.getProperty().setColor(0.3, 0.85, 0.45);
    a.getProperty().setLineWidth(1);
  }
  return a;
}

export function primCenter(prim) {
  if (!prim || !prim.pts || !prim.pts.length) return [0, 0, 0];
  let cx = 0, cy = 0, cz = 0;
  for (const pt of prim.pts) { cx += pt[0]; cy += pt[1]; cz += pt[2]; }
  return [cx / prim.pts.length, cy / prim.pts.length, cz / prim.pts.length];
}

export function setPrimPosition(index, pos) {
  const prim = prims[index];
  if (!prim || !prim.pts) return;
  const c = primCenter(prim);
  const d = [pos[0] - c[0], pos[1] - c[1], pos[2] - c[2]];
  for (const pt of prim.pts) { pt[0] += d[0]; pt[1] += d[1]; pt[2] += d[2]; }
  updatePrims();
  for (const fn of moveNotifier.fns) fn('prim', index);
}

export function setPrimRotation(index, rotDeg) {
  const prim = prims[index];
  if (!prim || !prim.pts) return;
  const c = primCenter(prim);
  const rx = (rotDeg[0] || 0) * Math.PI / 180;
  const rz = (rotDeg[2] || 0) * Math.PI / 180;
  const rotPt = (pt) => {
    let x = pt[0] - c[0], y = pt[1] - c[1], z = pt[2] - c[2];
    let y2 = y * Math.cos(rx) - z * Math.sin(rx);
    let z2 = y * Math.sin(rx) + z * Math.cos(rx);
    y = y2; z = z2;
    let x2 = x * Math.cos(rz) - y * Math.sin(rz);
    let y3 = x * Math.sin(rz) + y * Math.cos(rz);
    pt[0] = x2 + c[0]; pt[1] = y3 + c[1]; pt[2] = z + c[2];
  };
  for (const pt of prim.pts) rotPt(pt);
  if (prim.normal && prim.normal.length === 3) rotPt(prim.normal);
  updatePrims();
  for (const fn of moveNotifier.fns) fn('prim', index);
}

// Recompute primitives attached to an object from their stored local frame
// (follows both translation and rotation of the parent).
export function movePrimsWithObject(name) {
  let changed = false;
  for (const prim of prims) {
    if (prim.parent && prim.parent.name === name && prim.parent.localPts) {
      prim.pts = prim.parent.localPts.map((lp) => objToWorld(name, lp));
      if (prim.parent.localNormal) prim.normal = objDirToWorld(name, prim.parent.localNormal);
      changed = true;
    }
  }
  if (changed) updatePrims();
}

export function updatePrimMarkers() {
  for (const a of primMarkers) removeMarker(a);
  primMarkers.length = 0;
  for (const p of view.primPts) {
    const a = makeMarkerActor(0.9, 0.9, 0.2);
    a.setPosition(p[0], p[1], p[2]);
    primMarkers.push(a);
  }
  renderWindow.render();
}

export function updatePrims() {
  for (const p of prims) { if (p.actor) { renderer.removeActor(p.actor); primActorMap.delete(p.actor); p.actor = null; } }
  prims.forEach((p, i) => {
    if (p.visible === false) return;
    p.actor = buildPrimActor(p);
    if (p.actor) { p.actor.setPickable(true); renderer.addActor(p.actor); primActorMap.set(p.actor, i); }
  });
  updatePrimMarkers();
  renderWindow.render();
}

export function resolvePoint(x, y) {
  let p = snapPoint(x, y);
  let hit = null;
  if (!p) { hit = pickAt(x, y); p = hit ? hit.pos : projectToGridPlane(x, y); }
  // When an active plane exists and the click is on empty space, project the
  // resolved point onto the plane (CAD-like sketching/placement in a plane).
  const ap = activePlane();
  if (ap && p && !(hit && (hit.name || hit.primIndex !== undefined))) {
    const n = ap.getNormal(), o = ap.getOrigin();
    const d = (p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] + (p[2] - o[2]) * n[2];
    p = [p[0] - d * n[0], p[1] - d * n[1], p[2] - d * n[2]];
  }
  return p;
}

export function addPrimPointAt(p, normal, parentName) {
  if (!p) return;
  if (view.primPts.length === 0) {
    if (normal) view.primNormal = normal;
    pendingPrimParent = parentName || null;
  }
  view.primPts.push(p);
  updatePrimMarkers();
  updatePrimHint();
  const need = { line: 2, circle: 2, rect: 2, polygon: 2, arc: 3, ellipse: 2 }[view.primType] || 2;
  if (view.primPts.length >= need) {
    const names = { line: t('prim.line'), circle: t('prim.circle'), arc: t('prim.arc'), rect: t('prim.rect'), polygon: t('prim.polygon'), ellipse: t('prim.ellipse') };
    const prim = { type: view.primType, name: names[view.primType] + ' ' + (prims.length + 1), layer: '', pts: view.primPts.slice(), normal: view.primNormal.slice(), n: view.primType === 'polygon' ? 5 : undefined, actor: null, visible: true, parent: pendingPrimParent ? { name: pendingPrimParent, localPts: view.primPts.map((pt) => worldToObj(pendingPrimParent, pt)), localNormal: worldDirToObj(pendingPrimParent, view.primNormal) } : null };
    pendingPrimParent = null;
    view.primNormal = [0, 0, 1];
    if (view.primType === 'polygon') {
      promptText(t('prim.vertexCount'), '6', (nv) => {
        const n = parseInt(nv, 10);
        prim.n = Number.isFinite(n) && n >= 3 ? n : 6;
        pushHistory();
        prims.push(prim);
        view.primPts = [];
        updatePrims();
        updateTreeUI();
      });
    } else {
      pushHistory();
      prims.push(prim);
      view.primPts = [];
      updatePrims();
      updateTreeUI();
    }
  }
}

export function placePrimPoint(x, y) {
  const hit = pickAt(x, y);
  let p = snapPoint(x, y) || (hit && hit.pos ? hit.pos : projectToGridPlane(x, y));
  let normal = null;
  if (hit && hit.name) normal = surfaceNormalAt(hit.name, hit.pos);
  else if (hit && hit.primIndex !== undefined) {
    const pp = prims[hit.primIndex];
    if (pp && pp.normal) normal = pp.normal.slice();
  }
  if (!normal) {
    const ap = activePlane();
    if (ap) normal = [...ap.getNormal()];
  }
  // When drawing in empty space (no object/primitive under the cursor) and an
  // active plane exists, snap the point onto the plane so the sketch stays in
  // the chosen plane.
  const ap = activePlane();
  const onEmpty = !(hit && (hit.name || hit.primIndex !== undefined));
  if (ap && onEmpty) {
    const n = ap.getNormal(), o = ap.getOrigin();
    const d = (p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] + (p[2] - o[2]) * n[2];
    p = [p[0] - d * n[0], p[1] - d * n[1], p[2] - d * n[2]];
    normal = [n[0], n[1], n[2]];
  }
  addPrimPointAt(p, normal, hit && hit.name ? hit.name : null);
}

export function primSnapTargets() {
  const out = [];
  for (const p of prims) {
    if (p.visible === false) continue;
    if (p.type === 'line') {
      p.pts.forEach((pt) => out.push([pt[0], pt[1], pt[2]]));
      if (p.pts.length >= 2) out.push([(p.pts[0][0] + p.pts[1][0]) / 2, (p.pts[0][1] + p.pts[1][1]) / 2, (p.pts[0][2] + p.pts[1][2]) / 2]);
    } else if (p.type === 'arc') {
      if (p.pts[0]) out.push([p.pts[0][0], p.pts[0][1], p.pts[0][2]]);
      if (p.pts[1]) out.push([p.pts[1][0], p.pts[1][1], p.pts[1][2]]);
      if (p.pts[2]) out.push([p.pts[2][0], p.pts[2][1], p.pts[2][2]]);
      if (p.pts[1] && p.pts[2]) out.push([(p.pts[1][0] + p.pts[2][0]) / 2, (p.pts[1][1] + p.pts[2][1]) / 2, (p.pts[1][2] + p.pts[2][2]) / 2]);
    } else if (p.type === 'circle' || p.type === 'polygon') {
      if (p.pts[0]) out.push([p.pts[0][0], p.pts[0][1], p.pts[0][2]]);
      const c = p.pts[0], rp = p.pts[1];
      if (!c || !rp) continue;
      const r = Math.hypot(rp[0] - c[0], rp[1] - c[1], rp[2] - c[2]);
      const { u, v } = primBasis(p.normal);
      const n = p.type === 'circle' ? 8 : (p.n || 6);
      for (let i = 0; i < n; i++) {
        const a = (i / n) * 2 * Math.PI;
        const ca = r * Math.cos(a), sa = r * Math.sin(a);
        out.push([c[0] + u[0]*ca + v[0]*sa, c[1] + u[1]*ca + v[1]*sa, c[2] + u[2]*ca + v[2]*sa]);
      }
    } else if (p.type === 'rect') {
      const a = p.pts[0], b = p.pts[1];
      if (!a || !b) continue;
      const { u, v } = primBasis(p.normal);
      const du = (b[0]-a[0])*u[0] + (b[1]-a[1])*u[1] + (b[2]-a[2])*u[2];
      const dv = (b[0]-a[0])*v[0] + (b[1]-a[1])*v[1] + (b[2]-a[2])*v[2];
      const p1 = [a[0], a[1], a[2]];
      const p2 = [a[0] + u[0]*du, a[1] + u[1]*du, a[2] + u[2]*du];
      const p3 = [a[0] + u[0]*du + v[0]*dv, a[1] + u[1]*du + v[1]*dv, a[2] + u[2]*du + v[2]*dv];
      const p4 = [a[0] + v[0]*dv, a[1] + v[1]*dv, a[2] + v[2]*dv];
      out.push(p1, p2, p3, p4);
      out.push(
        [(p1[0]+p2[0])/2, (p1[1]+p2[1])/2, (p1[2]+p2[2])/2],
        [(p2[0]+p3[0])/2, (p2[1]+p3[1])/2, (p2[2]+p3[2])/2],
        [(p3[0]+p4[0])/2, (p3[1]+p4[1])/2, (p3[2]+p4[2])/2],
        [(p4[0]+p1[0])/2, (p4[1]+p1[1])/2, (p4[2]+p1[2])/2],
      );
    } else if (p.type === 'grid') {
      // Grid intersection nodes as snap targets (capped to keep the scan cheap).
      const c = p.pts && p.pts[0] ? p.pts[0] : [0, 0, 0];
      const spacing = p.spacing || 10;
      const halfSize = p.halfSize || 100;
      const { u, v } = primBasis(p.normal || [0, 0, 1]);
      const n = Math.min(10, Math.floor(halfSize / Math.max(spacing, 1e-9)));
      for (let i = -n; i <= n; i++) {
        for (let j = -n; j <= n; j++) {
          out.push([
            c[0] + u[0] * i * spacing + v[0] * j * spacing,
            c[1] + u[1] * i * spacing + v[1] * j * spacing,
            c[2] + u[2] * i * spacing + v[2] * j * spacing,
          ]);
        }
      }
    }
  }
  return out;
}

export function primCenterOf(prim) {
  if (!prim) return null;
  if ((prim.type === 'line' || prim.type === 'rect') && prim.pts.length >= 2) {
    return [(prim.pts[0][0] + prim.pts[1][0]) / 2, (prim.pts[0][1] + prim.pts[1][1]) / 2, (prim.pts[0][2] + prim.pts[1][2]) / 2];
  }
  if (prim.pts[0]) return [prim.pts[0][0], prim.pts[0][1], prim.pts[0][2]];
  return null;
}


