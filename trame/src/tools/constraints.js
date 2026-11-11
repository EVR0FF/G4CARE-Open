//==============================================================================
// G4CARE - constraints.js — Constraints tool
//
// Constraints tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer — geometric constraints (CAD dependencies) for primitives.
//
// Constraint types (2D-sketch primitives):
//   coincident    — two points coincide (translates the second prim).
//   parallel      — two line segments are parallel.
//   perpendicular — two line segments are perpendicular.
//   equal         — equal line lengths or equal circle radii.
//   horizontal    — a line segment is horizontal (same Y).
//   vertical      — a line segment is vertical (same X).
//   fix           — a primitive is locked in place.
//
// Constraints persist in `constraints` and re-solve automatically when a
// constrained primitive moves (via the moveNotifier hook).
import { view, constraints, prims, objectMeta, moveNotifier } from '../state.js';
import { pickAt } from '../render/picking.js';
import { resolvePoint, updatePrims } from './primitives.js';
import { objectAxisDir, objectPos, objectOrientationQuat } from './attachment.js';
import { setObjectPosition, setObjectRotation, isReplicaObject } from '../panels/objects.js';
import { normalize3, cross3, dot3, quatFromAxisAngle, quatRotate, primBasis, quatMul, quatToEulerXYZ, orientationQuat, quatInverse } from '../math3d.js';
import { btnStyle, showToast, promptText } from '../ui/dom.js';
import { fmtNum } from '../ui/format.js';
import { pushCommand, captureSceneState, restoreSceneState } from '../history.js';

let addGroup = null;
let setActiveMode = () => {};
let refreshTree = () => {};
let cTypeSel = null;
let applyBtn = null;
let constraintCounter = 0;
let pendingA = null;

export function setupConstraints(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  if (h.refreshTree) refreshTree = h.refreshTree;

  cTypeSel = document.createElement('select');
  cTypeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
  [['con.coincident', 'coincident'], ['con.parallel', 'parallel'], ['con.perpendicular', 'perpendicular'], ['con.equal', 'equal'], ['con.horizontal', 'horizontal'], ['con.vertical', 'vertical'], ['con.angle', 'angle'], ['con.radius', 'radius'], ['con.fix', 'fix'], ['con.coaxial', 'coaxial'], ['con.coincide', 'coincide'], ['con.angleObj', 'angleObj'], ['con.faceCoincide', 'faceCoincide'], ['con.tangent', 'tangent']].forEach(([k, v]) => {
    const o = document.createElement('option'); o.value = v; bindI18n(o, k); cTypeSel.appendChild(o);
  });
  cTypeSel.value = view.constraintType || 'coincident';
  cTypeSel.setAttribute('data-i18n-title', 'tip.constraintType'); cTypeSel.title = t('tip.constraintType');
  cTypeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
  cTypeSel.addEventListener('change', () => { view.constraintType = cTypeSel.value; });

  applyBtn = document.createElement('button');
  bindI18n(applyBtn, 'tools.applyConstraint');
  applyBtn.style.cssText = btnStyle;
  applyBtn.setAttribute('data-i18n-title', 'tip.applyConstraint'); applyBtn.title = t('tip.applyConstraint');
  applyBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleConstraintMode(); });

  const g = addGroup('group.deps');
  g.appendChild(cTypeSel);
  g.appendChild(applyBtn);

  moveNotifier.fns.push(solveConstraintsOnMove);
}

export function toggleConstraintMode() {
  if (view.constraintMode) setConstraintMode(false);
  else { setActiveMode('select'); setConstraintMode(true); }
}

export function setConstraintMode(on) {
  view.constraintMode = on;
  pendingA = null;
  if (applyBtn) { applyBtn.style.background = on ? '#b45f06' : '#3a3a3a'; applyBtn.style.color = '#fff'; }
  if (on) showToast(t('con.selectFigure'));
}

export function resetConstraintMode() {
  if (view.constraintMode) setConstraintMode(false);
}

function nearestPrimPoint(primIndex, wp) {
  const prim = prims[primIndex];
  if (!prim || !prim.pts || !prim.pts.length) return 0;
  let best = 0, bestD = Infinity;
  prim.pts.forEach((pt, i) => {
    const d = Math.hypot(pt[0] - wp[0], pt[1] - wp[1], pt[2] - wp[2]);
    if (d < bestD) { bestD = d; best = i; }
  });
  return best;
}

export function handleConstraintClick(x, y) {
  const hit = pickAt(x, y);
  const type = view.constraintType || 'coincident';
  // Object-level constraints (coaxial / coincide / angle between axes / face coincide).
  if (type === 'coaxial' || type === 'coincide' || type === 'angleObj' || type === 'faceCoincide' || type === 'tangent') {
    if (!hit || !hit.name) { showToast(t('common.selectObject')); return; }
    if (isReplicaObject(hit.name)) {
      showToast(t('con.replicaLocked'));
      return;
    }
    const face = type === 'faceCoincide' ? detectFace(hit.name, hit.pos) : null;
    const ref = type === 'faceCoincide' ? { name: hit.name, faceN: face.n, faceC: face.c } : { name: hit.name };
    if (!pendingA) {
      pendingA = ref;
      showToast(t('con.firstAnchor'));
      return;
    }
    const a = pendingA;
    if (a.name === ref.name) {
      showToast(t('con.otherObject'));
      pendingA = a;
      return;
    }
    pendingA = null;
    if (type === 'angleObj') {
      const cur = objAngle(a.name, ref.name);
      promptText(t('con.angleBetween'), fmtNum(cur), (val) => {
        const d = parseFloat(val);
        if (Number.isFinite(d)) createConstraint({ type, a, b: ref, value: d });
        else showToast(t('common.invalidValue'));
      });
      setConstraintMode(false);
      return;
    }
    createConstraint({ type, a, b: ref });
    setConstraintMode(false);
    return;
  }
  if (!hit || hit.primIndex === undefined) { showToast(t('con.selectPrimFigure')); return; }
  const primIndex = hit.primIndex;
  const p = resolvePoint(x, y) || null;
  if (type === 'fix' || type === 'horizontal' || type === 'vertical') {
    createConstraint({ type, a: { primIndex }, b: null });
    setConstraintMode(false);
    return;
  }
  if (type === 'radius') {
    const prim = prims[primIndex];
    const r = circleRadius(prim) || 1;
    promptText(t('con.radiusMm'), fmtNum(r), (val) => {
      const d = parseFloat(val);
      if (Number.isFinite(d)) createConstraint({ type, a: { primIndex }, b: null, value: d });
      else showToast(t('common.invalidValue'));
    });
    setConstraintMode(false);
    return;
  }
  const ref = { primIndex, ptIndex: type === 'coincident' ? nearestPrimPoint(primIndex, p) : null };
  if (!pendingA) {
    pendingA = ref;
    showToast(t('con.secondFigure'));
    return;
  }
  const a = pendingA;
  if (a.primIndex === ref.primIndex) {
    showToast(t('con.otherFigure'));
    pendingA = a;
    return;
  }
  pendingA = null;
  if (type === 'angle') {
    const cur = primAngle(prims[a.primIndex], prims[ref.primIndex]);
    promptText(t('con.angleDeg'), fmtNum(cur), (val) => {
      const d = parseFloat(val);
      if (Number.isFinite(d)) createConstraint({ type, a, b: ref, value: d });
      else showToast(t('common.invalidValue'));
    });
    setConstraintMode(false);
    return;
  }
  createConstraint({ type, a, b: ref });
  setConstraintMode(false);
}

function createConstraint(c) {
  c.id = 'c_' + (++constraintCounter);
  const before = captureSceneState();
  // Apply first, THEN register the constraint. Applying fires the object-move
  // notifier; if the constraint were already in the list, solveConstraintsOnMove
  // would immediately re-solve it in the opposite direction and yank the other
  // object (e.g. the sphere jumping to the array centre when constraining a
  // cylinder from an array).
  applyInitial(c);
  constraints.push(c);
  const after = captureSceneState();
  pushCommand({
    label: t('con.constraint'),
    undo: () => {
      const i = constraints.indexOf(c);
      if (i >= 0) constraints.splice(i, 1);
      restoreSceneState(before);
    },
    redo: () => {
      constraints.push(c);
      restoreSceneState(after);
    },
  });
  refreshTree();
  showToast(t('con.applied'));
}

export function removeConstraint(c) {
  const i = constraints.indexOf(c);
  if (i >= 0) constraints.splice(i, 1);
  refreshTree();
}

export function clearConstraints() {
  constraints.length = 0;
  refreshTree();
}

// Called when a primitive is deleted: drop constraints referencing it and shift
// the indices of the remaining ones (prims are referenced by index).
export function primRemoved(index) {
  for (let i = constraints.length - 1; i >= 0; i--) {
    const c = constraints[i];
    const refs = [c.a, c.b].filter(Boolean);
    if (refs.some((r) => r.primIndex === index)) { constraints.splice(i, 1); continue; }
    for (const r of refs) if (r.primIndex > index) r.primIndex -= 1;
  }
}

export function constraintLabel(c) {
  const names = { coincident: t('con.coincident'), parallel: t('con.parallel'), perpendicular: t('con.perpendicular'), equal: t('con.equal'), horizontal: t('con.horizontal'), vertical: t('con.vertical'), angle: t('con.angle'), radius: t('con.radius'), fix: t('con.fix'), coaxial: t('con.coaxial'), coincide: t('con.coincideCenters'), angleObj: t('con.angleAxes'), faceCoincide: t('con.faceCoincide'), tangent: t('con.tangent') };
  const nm = (r) => { if (r && r.name) return r.name; const pr = prims[r && r.primIndex]; return pr ? pr.name : '?'; };
  let base = names[c.type] || c.type;
  if (c.type === 'angle') base += ' ' + fmtNum(c.value) + '°';
  if (c.type === 'angleObj') base += ' ' + fmtNum(c.value) + '°';
  if (c.type === 'radius') base += ' ' + fmtNum(c.value) + ' ' + t('unit.mm');
  if (c.type === 'fix' || c.type === 'horizontal' || c.type === 'vertical' || c.type === 'radius') return base + ' · ' + nm(c.a);
  return base + ' · ' + nm(c.a) + ' ↔ ' + nm(c.b);
}

// Called when an object is deleted: drop constraints referencing it.
export function objectRemoved(name) {
  for (let i = constraints.length - 1; i >= 0; i--) {
    const c = constraints[i];
    const refs = [c.a, c.b].filter(Boolean);
    if (refs.some((r) => r.name === name)) { constraints.splice(i, 1); }
  }
}

// ── geometry helpers ──
function shiftPrim(prim, trans) {
  prim.pts = prim.pts.map((pt) => [pt[0] + trans[0], pt[1] + trans[1], pt[2] + trans[2]]);
}

function lineDir(p) {
  if (!p.pts || p.pts.length < 2) return null;
  return normalize3([p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]]);
}

function lineLen(p) {
  if (!p.pts || p.pts.length < 2) return null;
  return Math.hypot(p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]);
}

function circleRadius(p) {
  if (!p.pts || p.pts.length < 2) return null;
  return Math.hypot(p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]);
}

function rotatePrimAround(prim, center, axis, deg) {
  const q = quatFromAxisAngle(axis, deg);
  prim.pts = prim.pts.map((pt) => {
    const rel = [pt[0] - center[0], pt[1] - center[1], pt[2] - center[2]];
    const r = quatRotate(rel, q);
    return [center[0] + r[0], center[1] + r[1], center[2] + r[2]];
  });
  if (prim.normal && prim.normal.length === 3) prim.normal = quatRotate(prim.normal, q);
}

function alignLineDirection(a, b, perpendicular) {
  const da = lineDir(a);
  const db = lineDir(b);
  if (!da || !db) return;
  let target = da;
  if (perpendicular) {
    const n = a.normal || [0, 0, 1];
    target = normalize3(cross3(da, n));
    if (Math.hypot(target[0], target[1], target[2]) < 1e-9) target = [da[1], -da[0], 0];
  }
  const axis = normalize3(cross3(db, target));
  if (Math.hypot(axis[0], axis[1], axis[2]) < 1e-9) return;
  const dotv = Math.min(1, Math.max(-1, dot3(db, target)));
  const angle = Math.acos(dotv) * 180 / Math.PI;
  rotatePrimAround(b, [...b.pts[0]], axis, angle);
}

function equalizePrim(a, b) {
  const la = lineLen(a), lb = lineLen(b);
  if (la != null && lb != null && la > 1e-9) {
    const dir = lineDir(b) || [1, 0, 0];
    b.pts[1] = [b.pts[0][0] + dir[0] * la, b.pts[0][1] + dir[1] * la, b.pts[0][2] + dir[2] * la];
    return;
  }
  const ra = circleRadius(a), rb = circleRadius(b);
  if (ra != null && rb != null && ra > 1e-9) {
    const dir = normalize3([b.pts[1][0] - b.pts[0][0], b.pts[1][1] - b.pts[0][1], b.pts[1][2] - b.pts[0][2]]) || [1, 0, 0];
    b.pts[1] = [b.pts[0][0] + dir[0] * ra, b.pts[0][1] + dir[1] * ra, b.pts[0][2] + dir[2] * ra];
  }
}

// Signed 2D angle (degrees) between two directions projected onto a plane basis.
function signedAngleInPlane(da, db, u, v) {
  const a = [dot3(da, u), dot3(da, v)];
  const b = [dot3(db, u), dot3(db, v)];
  const cross = a[0] * b[1] - a[1] * b[0];
  const dot = a[0] * b[0] + a[1] * b[1];
  return Math.atan2(cross, dot) * 180 / Math.PI;
}

// Signed angle (degrees) from line a's direction to line b's direction, measured
// in the sketch plane of a (the prims are planar 2D sketches, so this is the
// well-defined angle — not the ambiguous unsigned 3D angle).
function primAngle(a, b) {
  const da = lineDir(a), db = lineDir(b);
  if (!da || !db) return 0;
  const n = a.normal || [0, 0, 1];
  const { u, v } = primBasis(n);
  return signedAngleInPlane(da, db, u, v);
}

// Rotate line b (in a's sketch plane) so the signed angle a→b becomes `value`.
function applyAngle(a, b, value) {
  const da = lineDir(a), db = lineDir(b);
  if (!da || !db) return;
  const n = a.normal || [0, 0, 1];
  const { u, v } = primBasis(n);
  const cur = signedAngleInPlane(da, db, u, v);
  const delta = value - cur;
  if (Math.abs(delta) < 1e-9) return;
  rotatePrimAround(b, [...b.pts[0]], normalize3(n), delta);
}

// Resize a circle/arc so its radius becomes `value`.
function applyRadius(prim, value) {
  if (!prim || !prim.pts || prim.pts.length < 2) return;
  const dir = normalize3([prim.pts[1][0] - prim.pts[0][0], prim.pts[1][1] - prim.pts[0][1], prim.pts[1][2] - prim.pts[0][2]]) || [1, 0, 0];
  prim.pts[1] = [prim.pts[0][0] + dir[0] * value, prim.pts[0][1] + dir[1] * value, prim.pts[0][2] + dir[2] * value];
}

// Make object b coaxial with object a (copy rotation + project b's center onto a's axis).
function applyCoaxial(a, b) {
  const ma = objectMeta.get(a.name), mb = objectMeta.get(b.name);
  if (!ma || !mb || !ma.pos || !mb.pos) return;
  const axis = objectAxisDir(a.name);
  const t = dot3([mb.pos[0] - ma.pos[0], mb.pos[1] - ma.pos[1], mb.pos[2] - ma.pos[2]], axis);
  const newPos = [ma.pos[0] + axis[0] * t, ma.pos[1] + axis[1] * t, ma.pos[2] + axis[2] * t];
  setObjectRotation(b.name, [...(ma.rot || [0, 0, 0])]);
  setObjectPosition(b.name, newPos);
}

// Coincide the centers of two objects.
function applyCoincide(a, b) {
  const ma = objectMeta.get(a.name);
  if (!ma || !ma.pos) return;
  setObjectPosition(b.name, [...ma.pos]);
}

// Unsigned 3D angle (degrees) between two objects' axes.
function objAngle(nameA, nameB) {
  const a = objectAxisDir(nameA);
  const b = objectAxisDir(nameB);
  const dotv = Math.min(1, Math.max(-1, dot3(a, b)));
  return Math.acos(dotv) * 180 / Math.PI;
}

// Rotate object b's orientation so the angle between the two axes becomes `value`.
function applyAngleObj(a, b, value) {
  const ma = objectMeta.get(a.name), mb = objectMeta.get(b.name);
  if (!ma || !mb) return;
  const axisA = objectAxisDir(a.name);
  const axisB = objectAxisDir(b.name);
  const dotv = Math.min(1, Math.max(-1, dot3(axisA, axisB)));
  const cur = Math.acos(dotv) * 180 / Math.PI;
  const delta = value - cur;
  if (Math.abs(delta) < 1e-9) return;
  const axis = normalize3(cross3(axisA, axisB));
  if (Math.hypot(axis[0], axis[1], axis[2]) < 1e-9) return;
  const Qd = quatFromAxisAngle(axis, delta);
  const qEulerB = orientationQuat(mb.rot || [0, 0, 0], 0);
  const qNew = quatMul(Qd, qEulerB);
  const rotNew = quatToEulerXYZ(qNew);
  setObjectRotation(b.name, rotNew);
}

// ── face coincidence (box faces) ──
// Detect which box face a world point lies on, returning { n (local normal), c (local center) }.
function detectFace(name, worldPoint) {
  const meta = objectMeta.get(name);
  if (!meta || !meta.dims) return { n: [0, 0, 1], c: [0, 0, 0] };
  const pos = objectPos(name);
  const Q = objectOrientationQuat(name);
  const rel = [worldPoint[0] - pos[0], worldPoint[1] - pos[1], worldPoint[2] - pos[2]];
  const local = quatRotate(rel, quatInverse(Q));
  const shape = meta.shape;
  const dims = meta.dims;
  // Cylinder / tube / tubs: pick the nearest flat end face. The vtk.js source
  // is built along Y (rotated +90° about X later), so the end faces lie at
  // local Y = ±h/2 with normals ±Y.
  if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
    const h = shape === 'cylinder' ? (dims[1] || 0) : (dims[2] || 0);
    return local[1] >= 0 ? { n: [0, 1, 0], c: [0, h / 2, 0] } : { n: [0, -1, 0], c: [0, -h / 2, 0] };
  }
  const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
  const faces = [
    { n: [1, 0, 0], c: [hx, 0, 0], d: Math.abs(local[0] - hx) },
    { n: [-1, 0, 0], c: [-hx, 0, 0], d: Math.abs(local[0] + hx) },
    { n: [0, 1, 0], c: [0, hy, 0], d: Math.abs(local[1] - hy) },
    { n: [0, -1, 0], c: [0, -hy, 0], d: Math.abs(local[1] + hy) },
    { n: [0, 0, 1], c: [0, 0, hz], d: Math.abs(local[2] - hz) },
    { n: [0, 0, -1], c: [0, 0, -hz], d: Math.abs(local[2] + hz) },
  ];
  faces.sort((x, y) => x.d - y.d);
  return { n: faces[0].n, c: faces[0].c };
}

// World position of an object's local face center.
function objectFaceCenterWorld(name, localCenter) {
  const pos = objectPos(name);
  const Q = objectOrientationQuat(name);
  const r = quatRotate(localCenter, Q);
  return [pos[0] + r[0], pos[1] + r[1], pos[2] + r[2]];
}

// Rotate an object so one of its local face normals points to a world target normal.
function alignObjectFaceNormal(name, localNormal, targetNormal) {
  const meta = objectMeta.get(name);
  if (!meta) return;
  const Q = objectOrientationQuat(name);
  const cur = normalize3(quatRotate(localNormal, Q));
  const axis = normalize3(cross3(cur, targetNormal));
  if (Math.hypot(axis[0], axis[1], axis[2]) < 1e-9) return;
  const dotv = Math.min(1, Math.max(-1, dot3(cur, targetNormal)));
  const angle = Math.acos(dotv) * 180 / Math.PI;
  const Qd = quatFromAxisAngle(axis, angle);
  const qEuler = orientationQuat(meta.rot || [0, 0, 0], 0);
  const qNew = quatMul(Qd, qEuler);
  setObjectRotation(name, quatToEulerXYZ(qNew));
}

// Coincide two object faces (b's face touches a's face, normals opposite).
function applyFaceCoincide(a, b) {
  const ma = objectMeta.get(a.name), mb = objectMeta.get(b.name);
  if (!ma || !mb) return;
  const nA = normalize3(quatRotate(a.faceN, objectOrientationQuat(a.name)));
  const targetN = [-nA[0], -nA[1], -nA[2]];
  alignObjectFaceNormal(b.name, b.faceN, targetN);
  const cA = objectFaceCenterWorld(a.name, a.faceC);
  const cB = objectFaceCenterWorld(b.name, b.faceC);
  const bp = objectPos(b.name);
  const newPos = [bp[0] + (cA[0] - cB[0]), bp[1] + (cA[1] - cB[1]), bp[2] + (cA[2] - cB[2])];
  setObjectPosition(b.name, newPos);
}

// ── tangency (outer contact) ──
// Outer radius of a parametric object (bounding sphere radius).
function boundingRadius(name) {
  const meta = objectMeta.get(name);
  if (!meta) return 0;
  const dims = meta.dims || [];
  const shape = meta.shape;
  if (shape === 'sphere') return dims[1] || dims[0] || 0;
  if (shape === 'orb') return dims[0] || 0;
  if (shape === 'cylinder') return dims[0] || 0;
  if (shape === 'tube' || shape === 'tubs') return dims[1] || 0;
  if (shape === 'box' || shape === 'para') {
    const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
    return Math.hypot(hx, hy, hz);
  }
  return 0;
}

// Move b so it just touches a (distance between centers = ra + rb).
function applyTangent(a, b) {
  const pa = objectPos(a.name), pb = objectPos(b.name);
  const ra = boundingRadius(a.name), rb = boundingRadius(b.name);
  const dir = [pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]];
  let d = normalize3(dir);
  if (Math.hypot(d[0], d[1], d[2]) < 1e-9) d = [1, 0, 0];
  const target = ra + rb;
  const newPos = [pa[0] + d[0] * target, pa[1] + d[1] * target, pa[2] + d[2] * target];
  setObjectPosition(b.name, newPos);
}

function makeHorizontal(p) {
  if (!p.pts || p.pts.length < 2) return;
  const y = (p.pts[0][1] + p.pts[1][1]) / 2;
  p.pts[0][1] = y; p.pts[1][1] = y;
}

function makeVertical(p) {
  if (!p.pts || p.pts.length < 2) return;
  const x = (p.pts[0][0] + p.pts[1][0]) / 2;
  p.pts[0][0] = x; p.pts[1][0] = x;
}

// ── solver ──
function applyConstraintTo(c, anchor, adjust) {
  // Object-level constraints (refs carry `.name`).
  if (c[anchor].name) {
    // Array copies are read-only: never solve constraints that touch them.
    if (isReplicaObject(c[anchor].name)) return;
    if (c[adjust] && c[adjust].name && isReplicaObject(c[adjust].name)) return;
    if (c.type === 'coaxial') applyCoaxial(c[anchor], c[adjust]);
    else if (c.type === 'coincide') applyCoincide(c[anchor], c[adjust]);
    else if (c.type === 'angleObj') applyAngleObj(c[anchor], c[adjust], c.value);
    else if (c.type === 'faceCoincide') applyFaceCoincide(c[anchor], c[adjust]);
    else if (c.type === 'tangent') applyTangent(c[anchor], c[adjust]);
    return;
  }
  const pa = prims[c[anchor].primIndex];
  const pb = prims[c[adjust].primIndex];
  if (!pa || !pb) return;
  if (c.type === 'coincident') {
    const pt = pa.pts[c[anchor].ptIndex];
    const qt = pb.pts[c[adjust].ptIndex];
    shiftPrim(pb, [pt[0] - qt[0], pt[1] - qt[1], pt[2] - qt[2]]);
  } else if (c.type === 'parallel' || c.type === 'perpendicular') {
    alignLineDirection(pa, pb, c.type === 'perpendicular');
  } else if (c.type === 'equal') {
    equalizePrim(pa, pb);
  } else if (c.type === 'angle') {
    applyAngle(pa, pb, c.value);
  }
}

function applyInitial(c) {
  if (c.type === 'coaxial' || c.type === 'coincide' || c.type === 'angleObj' || c.type === 'faceCoincide' || c.type === 'tangent') { applyConstraintTo(c, 'a', 'b'); return; }
  if (c.type === 'fix') {
    const p = prims[c.a.primIndex];
    if (p && p.pts) c.fixed = p.pts.map((pt) => [...pt]);
    return;
  }
  if (c.type === 'horizontal') { const p = prims[c.a.primIndex]; if (p) makeHorizontal(p); return; }
  if (c.type === 'vertical') { const p = prims[c.a.primIndex]; if (p) makeVertical(p); return; }
  if (c.type === 'radius') { const p = prims[c.a.primIndex]; if (p) applyRadius(p, c.value); return; }
  applyConstraintTo(c, 'a', 'b');
  updatePrims();
}

// Called whenever a primitive/object moves: re-solve constraints, adjusting the
// OTHER end (the moved entity is treated as the anchor).
let solving = false;
export function solveConstraintsOnMove(kind, key) {
  if (solving) return;
  solving = true;
  try {
    let changed = false;
    for (const c of constraints) {
      // Object-level constraints.
      if (c.type === 'coaxial' || c.type === 'coincide' || c.type === 'angleObj' || c.type === 'faceCoincide' || c.type === 'tangent') {
        if (kind !== 'object') continue;
        const aMoved = c.a.name === key;
        const bMoved = c.b && c.b.name === key;
        if (aMoved && !bMoved) { applyConstraintTo(c, 'a', 'b'); changed = true; }
        else if (bMoved && !aMoved) { applyConstraintTo(c, 'b', 'a'); changed = true; }
        continue;
      }
      if (kind !== 'prim') continue;
      if (c.type === 'fix') {
        const p = prims[c.a.primIndex];
        if (p && c.fixed && c.a.primIndex === key) { p.pts = c.fixed.map((pt) => [...pt]); changed = true; }
        continue;
      }
      if (c.type === 'horizontal') { if (c.a.primIndex === key) { makeHorizontal(prims[c.a.primIndex]); changed = true; } continue; }
      if (c.type === 'vertical') { if (c.a.primIndex === key) { makeVertical(prims[c.a.primIndex]); changed = true; } continue; }
      if (c.type === 'radius') { if (c.a.primIndex === key) { const p = prims[c.a.primIndex]; if (p) { applyRadius(p, c.value); changed = true; } } continue; }
      const aMoved = c.a.primIndex === key;
      const bMoved = c.b && c.b.primIndex === key;
      if (aMoved && !bMoved) { applyConstraintTo(c, 'a', 'b'); changed = true; }
      else if (bMoved && !aMoved) { applyConstraintTo(c, 'b', 'a'); changed = true; }
    }
    if (changed) updatePrims();
  } finally {
    solving = false;
  }
}

