//==============================================================================
// G4CARE - measure.js — Measure tool
//
// Measure tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - measurements (ruler, angle, min-distance, thickness).
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import { renderer, renderWindow, fullScreen } from '../context.js';
import { view, measure, measureExtras } from '../state.js';
import { fmtNum } from '../ui/format.js';
import { DEG2RAD } from '../math3d.js';
import { makeMarkerActor, updateMarkerScales } from '../render/actors.js';
import { pickAt } from '../render/picking.js';
import { resolvePoint } from './primitives.js';
import { btnStyle } from '../ui/dom.js';
import { dbg } from '../api.js';

let addGroup = null;
let setActiveMode = () => {};
let displayCoord = () => [0, 0];
let projectToGridPlane = () => null;
let sampleMeshSurfacePoints = () => [];
let closestPointOnMesh = () => null;
let rayMeshIntersect = () => null;
let surfaceNormalAt = () => null;
let toggleCoordMode = () => {};

let measureLabel = null;
let measureBtn = null;
let measureValue = null;
let angleBtn = null;
let mindistBtn = null;
let thicknessBtn = null;

export function setupMeasure(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  displayCoord = h.displayCoord;
  projectToGridPlane = h.projectToGridPlane;
  sampleMeshSurfacePoints = h.sampleMeshSurfacePoints;
  closestPointOnMesh = h.closestPointOnMesh;
  rayMeshIntersect = h.rayMeshIntersect;
  surfaceNormalAt = h.surfaceNormalAt;
  toggleCoordMode = h.toggleCoordMode;

  measureLabel = document.createElement('div');
  measureLabel.style.cssText = 'position:absolute;pointer-events:none;font:bold 13px Arial;color:#ffd54f;transform:translate(-50%,-50%);text-shadow:0 0 3px #000;display:none;';
  fullScreen.getContainer().appendChild(measureLabel);

  measureBtn = document.createElement('button');
  bindI18n(measureBtn, 'tools.measure');
  measureBtn.style.cssText = btnStyle;
  const clearBtn = document.createElement('button');
  bindI18n(clearBtn, 'tools.clear');
  clearBtn.style.cssText = btnStyle;
  measureValue = document.createElement('span');
  measureValue.style.cssText = 'color:#ffd54f;font:bold 13px Arial;text-shadow:0 0 3px #000;';
  const measureGroup = addGroup('group.measure');
  measureGroup.appendChild(measureBtn);
  measureGroup.appendChild(clearBtn);
  measureGroup.appendChild(measureValue);

  const coordBtn = document.createElement('button');
  bindI18n(coordBtn, 'tools.preciseInput');
  coordBtn.style.cssText = btnStyle;
  coordBtn.setAttribute('data-i18n-title', 'tip.preciseInput'); coordBtn.title = t('tip.preciseInput');
  coordBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    toggleCoordMode();
    coordBtn.style.background = view.coordMode ? '#b45f06' : '#3a3a3a';
    coordBtn.style.color = '#fff';
  });
  measureGroup.appendChild(coordBtn);

  measureBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleMeasure(); });
  clearBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); clearRuler(); setMeasureMode(false); });

  angleBtn = document.createElement('button');
  bindI18n(angleBtn, 'measure.angle');
  angleBtn.style.cssText = btnStyle;
  angleBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleMeasureSubMode('angle'); });
  measureGroup.appendChild(angleBtn);

  mindistBtn = document.createElement('button');
  bindI18n(mindistBtn, 'tools.minDist');
  mindistBtn.style.cssText = btnStyle;
  mindistBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleMeasureSubMode('mindist'); });
  measureGroup.appendChild(mindistBtn);

  thicknessBtn = document.createElement('button');
  bindI18n(thicknessBtn, 'tools.thickness');
  thicknessBtn.style.cssText = btnStyle;
  thicknessBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleMeasureSubMode('thickness'); });
  measureGroup.appendChild(thicknessBtn);
}

export function clearRuler() {
  if (measure.lineActor) { renderer.removeActor(measure.lineActor); measure.lineActor = null; }
  measure.markers.forEach((m) => renderer.removeActor(m));
  measure.markers = [];
  measure.pts = [];
  measureLabel.style.display = 'none';
  measureValue.textContent = '';
  clearMeasureExtras();
  renderWindow.render();
}

export function updateMeasureLabel() {
  if (measure.pts.length !== 2) return;
  const [p1, p2] = measure.pts;
  const mid = [(p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2, (p1[2] + p2[2]) / 2];
  const d = displayCoord(renderer, mid);
  const container = fullScreen.getContainer();
  const c = container.querySelector('canvas');
  if (!c || !container.clientWidth) return;
  const dprX = c.width / container.clientWidth;
  const dprY = c.height / container.clientHeight;
  measureLabel.style.left = `${d[0] / dprX}px`;
  measureLabel.style.top = `${(c.height - d[1]) / dprY}px`;
}

export function addMeasureMarker(p) {
  const a = makeMarkerActor(1, 1, 0);
  a.setPosition(p[0], p[1], p[2]);
  measure.markers.push(a);
  updateMarkerScales();
  return a;
}

export function addMeasurePointAt(p) {
  if (!p) return;
  measure.pts.push(p);
  addMeasureMarker(p);
  if (measure.pts.length === 2) {
    const [p1, p2] = measure.pts;
    const line = vtkLineSource.newInstance();
    line.setPoint1(p1);
    line.setPoint2(p2);
    const m = vtkMapper.newInstance();
    m.setInputConnection(line.getOutputPort());
    const a = vtkActor.newInstance();
    a.setMapper(m);
    a.getProperty().setColor(1, 1, 0);
    a.getProperty().setLineWidth(3);
    a.setPickable(false);
    renderer.addActor(a);
    measure.lineActor = a;
    const d = Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
    measureValue.textContent = `${fmtNum(d)} mm`;
    measureLabel.textContent = `${fmtNum(d)} mm`;
    measureLabel.style.display = 'block';
    updateMeasureLabel();
    setMeasureMode(false);
  }
  renderWindow.render();
}

export function placeMeasurePoint(x, y) {
  addMeasurePointAt(resolvePoint(x, y));
}

export function setMeasureMode(on) {
  measure.active = on;
  measureBtn.style.background = on ? '#b45f06' : '#333';
  measureBtn.style.color = on ? '#fff' : '#ddd';
}

export function toggleMeasure() {
  if (measure.active) setMeasureMode(false);
  else { clearRuler(); setActiveMode('measure'); setMeasureMode(true); }
}

export function clearMeasureExtras() {
  for (const a of measureExtras) renderer.removeActor(a);
  measureExtras.length = 0;
}

export function makeMeasureLine(p1, p2, color) {
  const ls = vtkLineSource.newInstance();
  ls.setPoint1(p1[0], p1[1], p1[2]);
  ls.setPoint2(p2[0], p2[1], p2[2]);
  const mm = vtkMapper.newInstance();
  mm.setInputConnection(ls.getOutputPort());
  const aa = vtkActor.newInstance();
  aa.setMapper(mm);
  aa.getProperty().setColor(color[0], color[1], color[2]);
  aa.getProperty().setLineWidth(3);
  aa.setPickable(false);
  renderer.addActor(aa);
  measureExtras.push(aa);
  return aa;
}

export function toggleMeasureSubMode(kind) {
  clearRuler();
  view.angleMode = kind === 'angle';
  view.mindistMode = kind === 'mindist';
  view.thicknessMode = kind === 'thickness';
  view.anglePts = [];
  view.mindistNames = [];
  setMeasureMode(false);
  updateMeasureSubBtnUI();
  if (view.angleMode || view.mindistMode || view.thicknessMode) setActiveMode('measure');
  dbg('submode=' + kind + ' angle=' + view.angleMode + ' mindist=' + view.mindistMode + ' thick=' + view.thicknessMode);
}

export function updateMeasureSubBtnUI() {
  angleBtn.style.background = view.angleMode ? '#b45f06' : '#3a3a3a';
  angleBtn.style.color = '#fff';
  mindistBtn.style.background = view.mindistMode ? '#b45f06' : '#3a3a3a';
  mindistBtn.style.color = '#fff';
  thicknessBtn.style.background = view.thicknessMode ? '#b45f06' : '#3a3a3a';
  thicknessBtn.style.color = '#fff';
}

export function placeAnglePoint(x, y) {
  const p = resolvePoint(x, y);
  dbg('angle pt=' + view.anglePts.length + ' p=' + (p ? p.map(Math.round).join(',') : 'null'));
  if (!p) return;
  view.anglePts.push(p);
  addMeasureMarker(p);
  if (view.anglePts.length >= 3) {
    const [A, B, C] = view.anglePts;
    const v1 = [A[0]-B[0], A[1]-B[1], A[2]-B[2]];
    const v2 = [C[0]-B[0], C[1]-B[1], C[2]-B[2]];
    const l1 = Math.hypot(v1[0], v1[1], v1[2]), l2 = Math.hypot(v2[0], v2[1], v2[2]);
    if (l1 < 1e-9 || l2 < 1e-9) { view.anglePts = []; return; }
    const cosA = (v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2]) / (l1 * l2);
    const ang = Math.acos(Math.max(-1, Math.min(1, cosA))) / DEG2RAD;
    makeMeasureLine(A, B, [0.4, 0.9, 1.0]);
    makeMeasureLine(B, C, [0.4, 0.9, 1.0]);
    measureValue.textContent = `${fmtNum(ang)}°`;
    measureLabel.textContent = `${fmtNum(ang)}°`;
    measureLabel.style.display = 'block';
    renderWindow.render();
    view.angleMode = false;
    view.anglePts = [];
    updateMeasureSubBtnUI();
  }
}

export function placeMindistPick(x, y) {
  const hit = pickAt(x, y);
  dbg('mindistPick hit=' + (hit ? (hit.name || hit.primIndex) : 'null') + ' n=' + view.mindistNames.length);
  if (hit && hit.name && !view.mindistNames.includes(hit.name)) {
    view.mindistNames.push(hit.name);
    if (view.mindistNames.length >= 2) {
      computeMinDistance(view.mindistNames[0], view.mindistNames[1]);
      view.mindistMode = false;
      view.mindistNames = [];
      updateMeasureSubBtnUI();
    }
  }
}

export function computeMinDistance(nameA, nameB) {
  dbg('mindist ' + nameA + ' ' + nameB);
  const samples = sampleMeshSurfacePoints(nameA, 160);
  dbg('mindist samples=' + samples.length);
  let best = Infinity, bp1 = null, bp2 = null;
  for (const sp of samples) {
    const np = closestPointOnMesh(nameB, sp);
    if (!np) continue;
    const d = Math.hypot(np[0]-sp[0], np[1]-sp[1], np[2]-sp[2]);
    if (d < best) { best = d; bp1 = sp; bp2 = np; }
  }
  dbg('mindist best=' + best + ' bp1=' + (bp1 ? bp1.map(Math.round).join(',') : 'null'));
  if (bp1 && bp2) {
    makeMeasureLine(bp1, bp2, [0.2, 1.0, 0.4]);
    measureValue.textContent = `${fmtNum(best)} mm`;
    measureLabel.textContent = `${fmtNum(best)} mm`;
    measureLabel.style.display = 'block';
    renderWindow.render();
  }
}

export function placeThicknessPick(x, y) {
  const hit = pickAt(x, y);
  dbg('thickness hit=' + (hit ? (hit.name || hit.primIndex) : 'null'));
  if (!hit || !hit.name) return;
  const n = surfaceNormalAt(hit.name, hit.pos);
  if (!n) { dbg('thickness noNormal'); return; }
  const inward = [-n[0], -n[1], -n[2]];
  const hit2 = rayMeshIntersect(hit.name, hit.pos, inward);
  if (!hit2) { dbg('thickness noRay'); return; }
  const d = Math.hypot(hit2[0]-hit.pos[0], hit2[1]-hit.pos[1], hit2[2]-hit.pos[2]);
  makeMeasureLine(hit.pos, hit2, [1.0, 0.6, 0.2]);
  measureValue.textContent = `${fmtNum(d)} mm`;
  measureLabel.textContent = `${fmtNum(d)} mm`;
  measureLabel.style.display = 'block';
  renderWindow.render();
  view.thicknessMode = false;
  updateMeasureSubBtnUI();
}
