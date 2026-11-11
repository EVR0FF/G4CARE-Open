//==============================================================================
// G4CARE - dimensions.js — Dimensions tool
//
// Dimensions tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer — persistent distance dimensions (constraints).
//
// A dimension is a live distance between two picked (snapped) points, drawn as
// two markers + a line + a value label. It is persistent (not one-shot): editing
// its value re-solves the target entity so the two points are exactly `value`
// apart, and it stays drawn / re-scaled like the ruler.
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkPointPicker from '@kitware/vtk.js/Rendering/Core/PointPicker';
import { renderer, renderWindow, fullScreen, interactor } from '../context.js';
import { view, dimensions, prims, objectMeta, snap, moveNotifier } from '../state.js';
import { makeMarkerActor, removeMarker, updateMarkerScales } from '../render/actors.js';
import { pickAt } from '../render/picking.js';
import { resolvePoint, updatePrims, primCenterOf } from './primitives.js';
import { displayCoord } from './view.js';
import { fmtNum } from '../ui/format.js';
import { btnStyle, promptText, showToast } from '../ui/dom.js';
import { pushCommand, pushHistory } from '../history.js';

let addGroup = null;
let setActiveMode = () => {};
let refreshTree = () => {};
let moveObjectFn = () => {};
let movePrimFn = () => {};
let dimBtn = null;
let dimCounter = 0;

const pointPicker = vtkPointPicker.newInstance();
const dimLineMap = new Map(); // line actor -> dimension id

export function setupDimensions(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  if (h.refreshTree) refreshTree = h.refreshTree;
  if (h.moveObject) moveObjectFn = h.moveObject;
  if (h.movePrim) movePrimFn = h.movePrim;
  moveNotifier.fns.push(notifyEntityMoved);

  dimBtn = document.createElement('button');
  bindI18n(dimBtn, 'tools.dimension');
  dimBtn.style.cssText = btnStyle;
  dimBtn.setAttribute('data-i18n-title', 'tip.dimension'); dimBtn.title = t('tip.dimension');
  dimBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleDimMode(); });

  const g = addGroup('group.deps');
  g.appendChild(dimBtn);

  // Keep labels glued to the dimension midpoints while the camera moves.
  interactor.onAnimation(refreshDimensionLabels);
  interactor.onEndAnimation(refreshDimensionLabels);
}

export function toggleDimMode() {
  if (view.dimMode) setDimMode(false);
  else { setActiveMode('select'); setDimMode(true); }
}

export function setDimMode(on) {
  view.dimMode = on;
  view.dimPts = [];
  if (dimBtn) { dimBtn.style.background = on ? '#b45f06' : '#3a3a3a'; dimBtn.style.color = '#fff'; }
  if (on) showToast(t('dim.firstPoint'));
}

export function resetDimMode() {
  if (view.dimMode) setDimMode(false);
}

// Build an anchor from a clicked (snapped) point: a live reference to an entity
// (object / primitive) with an offset, or a fixed free point. The offset lets a
// point follow its entity when the entity moves (e.g. a point on a sphere).
function anchorAt(x, y, p) {
  const hit = pickAt(x, y);
  if (hit && hit.name) {
    const meta = objectMeta.get(hit.name);
    const pos = meta && meta.pos ? meta.pos : [0, 0, 0];
    return { kind: 'object', name: hit.name, offset: [p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]] };
  }
  if (hit && hit.primIndex !== undefined) {
    const prim = prims[hit.primIndex];
    const c = prim ? (primCenterOf(prim) || [0, 0, 0]) : [0, 0, 0];
    return { kind: 'prim', index: hit.primIndex, offset: [p[0] - c[0], p[1] - c[1], p[2] - c[2]] };
  }
  return { kind: 'free', pos: [...p] };
}

export function handleDimClick(x, y) {
  const p = resolvePoint(x, y) || null;
  if (!p) return;
  const a = anchorAt(x, y, p);
  view.dimPts.push(a);
  if (view.dimPts.length === 1) {
    showToast(t('dim.secondPoint'));
    return;
  }
  const [a1, a2] = view.dimPts;
  view.dimPts = [];
  const cur = dimDistance(a1, a2);
  promptText(t('dim.distance'), fmtNum(cur), (val) => {
    const d = parseFloat(val);
    if (!Number.isFinite(d)) { showToast(t('common.invalidValue')); return; }
    const dim = createDimension(a1, a2, d);
    if (Math.abs(d - cur) > 1e-9) solveDimension(dim, d);
  });
  setDimMode(false);
}

function dimDistance(a1, a2) {
  const p1 = anchorPos(a1), p2 = anchorPos(a2);
  return Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
}

function createDimension(a1, a2, value) {
  const id = 'dim_' + (++dimCounter);
  const p1 = anchorPos(a1), p2 = anchorPos(a2);
  const m1 = makeMarkerActor(0.35, 0.9, 0.9);
  m1.setPosition(p1[0], p1[1], p1[2]);
  const m2 = makeMarkerActor(0.35, 0.9, 0.9);
  m2.setPosition(p2[0], p2[1], p2[2]);
  const ls = vtkLineSource.newInstance();
  ls.setPoint1(p1[0], p1[1], p1[2]);
  ls.setPoint2(p2[0], p2[1], p2[2]);
  const m = vtkMapper.newInstance();
  m.setInputConnection(ls.getOutputPort());
  const line = vtkActor.newInstance();
  line.setMapper(m);
  line.getProperty().setColor(0.35, 0.9, 0.9);
  line.getProperty().setLineWidth(2);
  line.setPickable(true);
  renderer.addActor(line);
  dimLineMap.set(line, id);

  const label = document.createElement('div');
  label.style.cssText = 'position:absolute;pointer-events:none;font:bold 12px Arial;color:#7fe0e0;transform:translate(-50%,-50%);text-shadow:0 0 3px #000;white-space:nowrap;';
  label.textContent = fmtNum(value) + ' ' + t('unit.mm');
  fullScreen.getContainer().appendChild(label);

  const dim = { id, e1: a1, e2: a2, p1: [...p1], p2: [...p2], value, m1, m2, line, ls, label };
  dimensions.push(dim);
  updateMarkerScales();
  renderWindow.render();
  refreshTree();
  return dim;
}

// World position of an anchor (follows its entity).
function anchorPos(a) {
  if (!a) return [0, 0, 0];
  if (a.kind === 'free') return a.pos;
  if (a.kind === 'object') {
    const meta = objectMeta.get(a.name);
    const p = meta && meta.pos ? meta.pos : [0, 0, 0];
    const off = a.offset || [0, 0, 0];
    return [p[0] + off[0], p[1] + off[1], p[2] + off[2]];
  }
  if (a.kind === 'prim') {
    const prim = prims[a.index];
    const c = prim ? (primCenterOf(prim) || [0, 0, 0]) : [0, 0, 0];
    const off = a.offset || [0, 0, 0];
    return [c[0] + off[0], c[1] + off[1], c[2] + off[2]];
  }
  if (a.kind === 'aux') {
    const pt = snap.auxPoints[a.index];
    return pt ? [pt[0], pt[1], pt[2]] : [0, 0, 0];
  }
  return [0, 0, 0];
}

function deltaVec(fromPoint, toPoint, delta) {
  const v = [toPoint[0] - fromPoint[0], toPoint[1] - fromPoint[1], toPoint[2] - fromPoint[2]];
  const len = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / len * delta, v[1] / len * delta, v[2] / len * delta];
}

// Move an entity (raw, no re-solve recursion handled by the caller's guard).
function moveEntityRaw(anchor, trans) {
  if (anchor.kind === 'object') {
    const meta = objectMeta.get(anchor.name);
    if (meta && meta.pos) {
      moveObjectFn(anchor.name, [meta.pos[0] + trans[0], meta.pos[1] + trans[1], meta.pos[2] + trans[2]]);
    }
  } else if (anchor.kind === 'prim') {
    movePrimFn(anchor.index, trans);
  } else if (anchor.kind === 'aux') {
    const pt = snap.auxPoints[anchor.index];
    if (pt) { pt[0] += trans[0]; pt[1] += trans[1]; pt[2] += trans[2]; }
  }
}

// Move the target entity (e2) so the dimension's two points are `newValue` apart.
export function solveDimension(dim, newValue) {
  const p1 = anchorPos(dim.e1), p2 = anchorPos(dim.e2);
  const cur = Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
  const delta = newValue - cur;
  dim.value = newValue;
  if (Math.abs(delta) < 1e-9) { refreshDimension(dim); return; }
  // Prefer moving e2; fall back to e1 if e2 is a fixed free point.
  if (dim.e2 && dim.e2.kind !== 'free') {
    moveEntityRaw(dim.e2, deltaVec(p1, p2, delta));
  } else if (dim.e1 && dim.e1.kind !== 'free') {
    moveEntityRaw(dim.e1, deltaVec(p2, p1, delta));
  }
  dim.p1 = anchorPos(dim.e1);
  dim.p2 = anchorPos(dim.e2);
  refreshDimension(dim);
}

// Auto re-solve: when an entity moved, any dimension whose anchor follows it is
// updated, and the opposite (movable) end is shifted to restore the distance.
// The entity that the user just moved is treated as fixed, so dragging an object
// never fights the constraint (which caused the "flew across the world" bug).
// Iterates a few passes so chains (A—B—C) and simple multi-constraint cases settle.
let solving = false;
export function notifyEntityMoved(kind, nameOrIndex) {
  if (solving) return;
  solving = true;
  try {
    let movingKey = kind + ':' + (nameOrIndex == null ? '' : nameOrIndex);
    for (let iter = 0; iter < 24; iter++) {
      let changed = false;
      for (const dim of dimensions) {
        const p1 = anchorPos(dim.e1), p2 = anchorPos(dim.e2);
        const cur = Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
        if (Math.abs(cur - dim.value) < 1e-9) {
          dim.p1 = p1; dim.p2 = p2;
          continue;
        }
        const delta = dim.value - cur;
        const keyOf = (a) => a ? (a.kind + ':' + (a.name != null ? a.name : (a.index != null ? a.index : ''))) : '';
        const e1Key = keyOf(dim.e1), e2Key = keyOf(dim.e2);
        let moved = false;
        // Prefer moving the end that is NOT the one just moved.
        if (dim.e2 && dim.e2.kind !== 'free' && e2Key !== movingKey) {
          moveEntityRaw(dim.e2, deltaVec(p1, p2, delta));
          movingKey = e2Key;
          moved = true;
        } else if (dim.e1 && dim.e1.kind !== 'free' && e1Key !== movingKey) {
          moveEntityRaw(dim.e1, deltaVec(p2, p1, delta));
          movingKey = e1Key;
          moved = true;
        }
        if (moved) {
          dim.p1 = anchorPos(dim.e1);
          dim.p2 = anchorPos(dim.e2);
          refreshDimension(dim);
          changed = true;
        }
      }
      if (!changed) break;
    }
  } finally {
    solving = false;
  }
}

export function refreshDimension(dim) {
  dim.m1.setPosition(dim.p1[0], dim.p1[1], dim.p1[2]);
  dim.m2.setPosition(dim.p2[0], dim.p2[1], dim.p2[2]);
  dim.ls.setPoint1(dim.p1[0], dim.p1[1], dim.p1[2]);
  dim.ls.setPoint2(dim.p2[0], dim.p2[1], dim.p2[2]);
  dim.label.textContent = fmtNum(dim.value) + ' ' + t('unit.mm');
  positionDimLabel(dim);
  updateMarkerScales();
  renderWindow.render();
}

export function refreshDimensionLabels() {
  for (const d of dimensions) positionDimLabel(d);
}

function positionDimLabel(dim) {
  const mid = [(dim.p1[0] + dim.p2[0]) / 2, (dim.p1[1] + dim.p2[1]) / 2, (dim.p1[2] + dim.p2[2]) / 2];
  const sc = displayCoord(renderer, mid);
  const container = fullScreen.getContainer();
  const c = container.querySelector('canvas');
  if (!c || !container.clientWidth) return;
  const dprX = c.width / container.clientWidth;
  const dprY = c.height / container.clientHeight;
  dim.label.style.left = (sc[0] / dprX) + 'px';
  dim.label.style.top = ((c.height - sc[1]) / dprY) + 'px';
}

export function removeDimension(dim) {
  renderer.removeActor(dim.line);
  dimLineMap.delete(dim.line);
  removeMarker(dim.m1);
  removeMarker(dim.m2);
  if (dim.label && dim.label.parentNode) dim.label.parentNode.removeChild(dim.label);
  const i = dimensions.indexOf(dim);
  if (i >= 0) dimensions.splice(i, 1);
  renderWindow.render();
  refreshTree();
}

export function clearDimensions() {
  for (const d of [...dimensions]) removeDimension(d);
  showToast(t('dim.deleted'));
}

// Called when a primitive is deleted: drop dimensions referencing it and shift
// the indices of the remaining ones.
export function primRemoved(index) {
  for (let i = dimensions.length - 1; i >= 0; i--) {
    const d = dimensions[i];
    const e1hit = d.e1 && d.e1.kind === 'prim' && d.e1.index === index;
    const e2hit = d.e2 && d.e2.kind === 'prim' && d.e2.index === index;
    if (e1hit || e2hit) { removeDimension(d); continue; }
    if (d.e1 && d.e1.kind === 'prim' && d.e1.index > index) d.e1.index -= 1;
    if (d.e2 && d.e2.kind === 'prim' && d.e2.index > index) d.e2.index -= 1;
  }
}

export function dimensionHitAt(x, y) {
  pointPicker.pick([x, y, 0], renderer);
  const picked = pointPicker.getActors();
  if (picked.length) {
    const id = dimLineMap.get(picked[0]);
    if (id) return dimensions.find((d) => d.id === id) || null;
  }
  return null;
}

export function editDimensionValue(dim) {
  promptText(t('dim.distance'), fmtNum(dim.value), (val) => {
    const d = parseFloat(val);
    if (!Number.isFinite(d)) { showToast(t('common.invalidValue')); return; }
    solveDimension(dim, d);
    showToast(t('dim.updated'));
  });
}

