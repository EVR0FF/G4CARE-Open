//==============================================================================
// G4CARE - section.js — Section (cutting) tool
//
// Section (cutting) tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - section / clipping planes.
import vtkPlane from '@kitware/vtk.js/Common/DataModel/Plane';
import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import { renderer, renderWindow } from '../context.js';
import { section, planeBuild, view, prims, objectInfo, mergedMesh, planeActorMap, actors, objectMeta } from '../state.js';
import { makeMarkerActor, removeMarker, updateMarkerScales, computeSceneBounds } from '../render/actors.js';
import { pickAt, applyHighlight } from '../render/picking.js';
import { dbg } from '../api.js';
import { normalize3, cross3, dot3, closestPointOnTriangle, DEG2RAD, orientationQuat, quatRotate, quatInverse } from '../math3d.js';
import { Z_AXIS_SHAPES } from '../render/sources.js';
import { snapPoint } from './snap.js';
import { btnStyle, promptText } from '../ui/dom.js';

let addGroup = null;
let setActiveMode = () => {};
let updateTreeUI = () => {};
let updateProps = () => {};
let buildMergedCut = () => {};
let volumeMatrix = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

let sectionBtn = null;
let scopeBtn = null;
let selectBtn = null;
let sectionPanel = null;
let planeModeSel = null;
let planeBuildBtn = null;
let planeBuildHint = null;
let planesListEl = null;
let cutBtn = null;
let sectionSlider = null;

const planeBuildMarkers = [];
export const planeVisualActors = [];
const pickMarkerActors = [];

const PLANE_MODE_HINTS = {
  points: t('sec.hint.points'),
  face: t('sec.hint.face'),
  parallel: t('sec.hint.parallel'),
  prims: t('sec.hint.prims'),
  tangent: t('sec.hint.tangent'),
  section: t('sec.hint.section'),
  perp: t('sec.hint.perp'),
  angle: t('sec.hint.angle'),
  mid: t('sec.hint.mid'),
};

export function setupSection(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  updateTreeUI = h.updateTreeUI;
  updateProps = h.updateProps;
  buildMergedCut = h.buildMergedCut;
  volumeMatrix = h.volumeMatrix;

  const planeGroup = addGroup('group.planes');
  const sectionGroup = addGroup('group.section');

  sectionBtn = document.createElement('button');
  bindI18n(sectionBtn, 'tools.section');
  sectionBtn.style.cssText = btnStyle;
  sectionGroup.appendChild(sectionBtn);

  scopeBtn = document.createElement('button');
  bindI18n(scopeBtn, 'tools.global');
  scopeBtn.style.cssText = btnStyle;
  scopeBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    e.preventDefault();
    section.scope = section.scope === 'global' ? 'local' : 'global';
    scopeBtn.textContent = section.scope === 'global' ? 'Global' : 'Local';
    scopeBtn.style.background = section.scope === 'local' ? '#b45f06' : '#3a3a3a';
    scopeBtn.style.color = '#fff';
    updateActorMappers();
    applyHighlight();
  });
  sectionGroup.appendChild(scopeBtn);

  selectBtn = document.createElement('button');
  bindI18n(selectBtn, 'tools.sel');
  selectBtn.style.cssText = btnStyle;
  selectBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    e.preventDefault();
    section.selective = !section.selective;
    selectBtn.style.background = section.selective ? '#b45f06' : '#3a3a3a';
    selectBtn.style.color = '#fff';
    updateActorMappers();
    applyHighlight();
  });
  sectionGroup.appendChild(selectBtn);

  sectionPanel = document.createElement('div');
  sectionPanel.style.cssText = 'display:none;align-items:center;gap:4px;flex-wrap:wrap;';

  const axisBtns = ['X', 'Y', 'Z'].map((t, i) => {
    const b = document.createElement('button');
    b.textContent = t;
    b.style.cssText = btnStyle;
    b.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      const n = [0, 0, 0];
      n[i] = 1;
      const bnd = computeSceneBounds();
      addSectionPlane(n, bnd ? (bnd[2 * i] + bnd[2 * i + 1]) / 2 : 0);
    });
    return b;
  });

  planeModeSel = document.createElement('select');
  planeModeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
  [['sec.mode.none', ''], ['sec.mode.points', 'points'], ['sec.mode.face', 'face'], ['sec.mode.parallel', 'parallel'], ['sec.mode.prims', 'prims'], ['sec.mode.tangent', 'tangent'], ['sec.mode.section', 'section'], ['sec.mode.perp', 'perp'], ['sec.mode.angle', 'angle'], ['sec.mode.mid', 'mid']].forEach(([k, v]) => { const o = document.createElement('option'); o.value = v; bindI18n(o, k); planeModeSel.appendChild(o); });
  planeModeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
  planeModeSel.addEventListener('change', () => { setPlaneMode(planeModeSel.value); });

  planeBuildBtn = document.createElement('button');
  bindI18n(planeBuildBtn, 'tools.create');
  planeBuildBtn.style.cssText = btnStyle;
  planeBuildBtn.style.display = 'none';
  planeBuildBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); commitPlaneBuild(); });

  planeBuildHint = document.createElement('span');
  planeBuildHint.style.cssText = 'color:#777;font:10px Arial;margin-left:2px;white-space:nowrap;';

  planesListEl = document.createElement('span');
  planesListEl.style.cssText = 'display:inline-flex;align-items:center;gap:3px;';

  const removePlaneBtn = document.createElement('button');
  removePlaneBtn.textContent = '−';
  removePlaneBtn.style.cssText = btnStyle;
  removePlaneBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); removeActivePlane(); });

  cutBtn = document.createElement('button');
  bindI18n(cutBtn, 'tools.cut');
  cutBtn.style.cssText = btnStyle;
  cutBtn.setAttribute('data-i18n-title', 'tip.cut'); cutBtn.title = t('tip.cut');
  cutBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleActiveCut(); });

  sectionSlider = document.createElement('input');
  sectionSlider.type = 'range';
  sectionSlider.min = '-500';
  sectionSlider.max = '500';
  sectionSlider.step = '1';
  sectionSlider.value = '0';
  sectionSlider.style.cssText = 'width:110px;';
  sectionSlider.addEventListener('pointerdown', (e) => e.stopPropagation());
  sectionSlider.addEventListener('input', () => setActivePosition(parseFloat(sectionSlider.value)));

  const flipBtn = document.createElement('button');
  bindI18n(flipBtn, 'tools.flip');
  flipBtn.style.cssText = btnStyle;
  flipBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); flipActivePlane(); });

  const clearSectionBtn = document.createElement('button');
  bindI18n(clearSectionBtn, 'tools.clear');
  clearSectionBtn.style.cssText = btnStyle;
  clearSectionBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); clearSectionPlanes(); });

  axisBtns.forEach((b) => planeGroup.appendChild(b));
  planeGroup.appendChild(planeModeSel);
  planeGroup.appendChild(planeBuildBtn);
  planeGroup.appendChild(planeBuildHint);
  planeGroup.appendChild(planesListEl);
  planeGroup.appendChild(removePlaneBtn);
  sectionPanel.appendChild(cutBtn);
  sectionPanel.appendChild(sectionSlider);
  sectionPanel.appendChild(flipBtn);
  sectionPanel.appendChild(clearSectionBtn);
  sectionGroup.appendChild(sectionPanel);

  sectionBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    e.preventDefault();
    setSectionEnabled(!section.enabled);
  });
}

export function activePlane() { return section.planes[section.active] ? section.planes[section.active].plane : undefined; }

export function setPlaneMode(mode) {
  if (mode) setActiveMode('select'); // mutually exclusive with measure/aux/delete/prim
  planeBuild.mode = mode || null;
  planeBuild.pts = [];
  planeBuild.selPrims = [];
  planeBuild.selPlanes = [];
  planeBuild.refPoint = null;
  planeBuild.refPlane = -1;
  planeBuild.angle = 0;
  clearPlaneBuildMarkers();
  if (planeBuild.mode === 'section') commitPlaneBuild();
  else {
    planeBuildBtn.style.display = (planeBuild.mode === 'points') ? 'inline-block' : 'none';
    planeBuildHint.textContent = planeBuild.mode ? (PLANE_MODE_HINTS[planeBuild.mode] || '') : '';
  }
}

export function resetPlaneBuild() {
  planeModeSel.value = '';
  setPlaneMode('');
}

export function clearPlaneBuildMarkers() {
  for (const a of planeBuildMarkers) removeMarker(a);
  planeBuildMarkers.length = 0;
}

export function updatePlaneBuildMarkers() {
  clearPlaneBuildMarkers();
  for (const p of planeBuild.pts) {
    const a = makeMarkerActor(0.2, 0.8, 1.0);
    a.setPosition(p[0], p[1], p[2]);
    planeBuildMarkers.push(a);
  }
  updateMarkerScales();
  renderWindow.render();
}

export function addPlaneWithOrigin(normal, origin) {
  const n = normalize3(normal);
  if (Math.hypot(n[0], n[1], n[2]) < 1e-9) return;
  addSectionPlane(n, dot3(n, origin));
}

// Detect the object face under a world point → { normal, origin } in world
// coordinates (box faces, cylinder/tube end faces, sphere tangent plane).
export function facePlaneAt(name, wp) {
  const meta = objectMeta.get(name);
  if (!meta || !meta.dims) return null;
  const pos = meta.pos || [0, 0, 0];
  const rot = meta.rot || [0, 0, 0];
  const baseX = Z_AXIS_SHAPES.has(meta.shape) ? 90 : 0;
  const Q = orientationQuat(rot, baseX);
  const rel = [wp[0] - pos[0], wp[1] - pos[1], wp[2] - pos[2]];
  const local = quatRotate(rel, quatInverse(Q));
  const dims = meta.dims;
  const shape = meta.shape;
  let ln, lc;
  if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
    const h = shape === 'cylinder' ? (dims[1] || 0) : (dims[2] || 0);
    ln = local[1] >= 0 ? [0, 1, 0] : [0, -1, 0];
    lc = local[1] >= 0 ? [0, h / 2, 0] : [0, -h / 2, 0];
  } else if (shape === 'sphere' || shape === 'orb') {
    const r = Math.hypot(rel[0], rel[1], rel[2]) || 1;
    return { normal: [rel[0] / r, rel[1] / r, rel[2] / r], origin: wp };
  } else {
    const hx = (dims[0] || 0) / 2, hy = (dims[1] || 0) / 2, hz = (dims[2] || 0) / 2;
    const faces = [
      { n: [1, 0, 0], c: [hx, 0, 0], d: Math.abs(local[0] - hx) },
      { n: [-1, 0, 0], c: [-hx, 0, 0], d: Math.abs(local[0] + hx) },
      { n: [0, 1, 0], c: [0, hy, 0], d: Math.abs(local[1] - hy) },
      { n: [0, -1, 0], c: [0, -hy, 0], d: Math.abs(local[1] + hy) },
      { n: [0, 0, 1], c: [0, 0, hz], d: Math.abs(local[2] - hz) },
      { n: [0, 0, -1], c: [0, 0, -hz], d: Math.abs(local[2] + hz) },
    ];
    faces.sort((a, b) => a.d - b.d);
    ln = faces[0].n; lc = faces[0].c;
  }
  const nw = quatRotate(ln, Q);
  const cw = quatRotate(lc, Q);
  return { normal: nw, origin: [pos[0] + cw[0], pos[1] + cw[1], pos[2] + cw[2]] };
}

// ── Face hover highlight (режим «По грани») ──
let faceHoverActor = null;

function makeFaceHoverActor() {
  const pd = vtkPolyData.newInstance();
  pd.getPoints().setNumberOfPoints(4, 3);
  for (let i = 0; i < 4; i++) pd.getPoints().setPoint(i, 0, 0, 0);
  pd.getPolys().insertNextCell([0, 1, 2, 3]);
  const mapper = vtkMapper.newInstance();
  mapper.setInputData(pd);
  const a = vtkActor.newInstance();
  a.setMapper(mapper);
  a.getProperty().setColor(1.0, 0.7, 0.0);
  a.getProperty().setOpacity(0.6);
  a.getProperty().setLighting(false);
  a.getProperty().setEdgeVisibility(true);
  a.getProperty().setEdgeColor(1.0, 0.85, 0.2);
  a.setVisibility(false);
  renderer.addActor(a);
  return a;
}

// Called on mouse move while building a plane: highlight the face under the
// cursor so the user sees exactly which face will become the plane.
export function updateFaceHover(hit) {
  if (!faceHoverActor) faceHoverActor = makeFaceHoverActor();
  if (planeBuild.mode !== 'face' || !hit || !hit.name) {
    if (faceHoverActor.getVisibility()) { faceHoverActor.setVisibility(false); renderWindow.render(); }
    return;
  }
  const fp = facePlaneAt(hit.name, hit.pos);
  if (!fp) {
    if (faceHoverActor.getVisibility()) { faceHoverActor.setVisibility(false); renderWindow.render(); }
    return;
  }
  const n = normalize3(fp.normal);
  const ref = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
  const u = normalize3(cross3(ref, n));
  const v = cross3(n, u);
  const h = 8; // half-size of the highlight quad
  const o = fp.origin;
  const corners = [
    [o[0] - u[0]*h - v[0]*h, o[1] - u[1]*h - v[1]*h, o[2] - u[2]*h - v[2]*h],
    [o[0] + u[0]*h - v[0]*h, o[1] + u[1]*h - v[1]*h, o[2] + u[2]*h - v[2]*h],
    [o[0] + u[0]*h + v[0]*h, o[1] + u[1]*h + v[1]*h, o[2] + u[2]*h + v[2]*h],
    [o[0] - u[0]*h + v[0]*h, o[1] - u[1]*h + v[1]*h, o[2] - u[2]*h + v[2]*h],
  ];
  const pts = faceHoverActor.getMapper().getInputData().getPoints();
  corners.forEach((c, i) => pts.setPoint(i, c[0], c[1], c[2]));
  faceHoverActor.getMapper().getInputData().modified();
  faceHoverActor.setVisibility(true);
  renderWindow.render();
}

export function commitPlaneBuild() {
  const mode = planeBuild.mode;
  if (mode === 'points') {
    if (planeBuild.pts.length < 3) return;
    const [a, b, c] = planeBuild.pts;
    addPlaneWithOrigin(cross3([b[0]-a[0],b[1]-a[1],b[2]-a[2]], [c[0]-a[0],c[1]-a[1],c[2]-a[2]]), a);
    resetPlaneBuild();
  } else if (mode === 'section') {
    const p = activePlane();
    if (p) addPlaneWithOrigin(p.getNormal(), p.getOrigin());
    resetPlaneBuild();
  }
}

export function primPlane(prim) {
  if (prim && prim.pts.length >= 1 && prim.pts[0]) {
    return { normal: prim.normal || [0, 0, 1], origin: [prim.pts[0][0], prim.pts[0][1], prim.pts[0][2]] };
  }
  return null;
}

export function tryCommitPrims() {
  const sp = planeBuild.selPrims;
  for (const idx of sp) {
    const p = prims[idx];
    if (p && ['circle', 'polygon', 'rect', 'ellipse'].includes(p.type)) {
      const pl = primPlane(p);
      if (pl) { addPlaneWithOrigin(pl.normal, pl.origin); resetPlaneBuild(); return true; }
    }
  }
  if (planeBuild.refPoint && sp.length >= 1) {
    const line = prims[sp[0]];
    if (line && line.type === 'line' && line.pts.length >= 2) {
      const d = [line.pts[1][0]-line.pts[0][0], line.pts[1][1]-line.pts[0][1], line.pts[1][2]-line.pts[0][2]];
      const v = [planeBuild.refPoint[0]-line.pts[0][0], planeBuild.refPoint[1]-line.pts[0][1], planeBuild.refPoint[2]-line.pts[0][2]];
      addPlaneWithOrigin(cross3(d, v), line.pts[0]);
      resetPlaneBuild(); return true;
    }
  }
  if (sp.length >= 2) {
    const l1 = prims[sp[0]], l2 = prims[sp[1]];
    if (l1 && l2 && l1.type === 'line' && l2.type === 'line' && l1.pts.length >= 2 && l2.pts.length >= 2) {
      const d1 = [l1.pts[1][0]-l1.pts[0][0], l1.pts[1][1]-l1.pts[0][1], l1.pts[1][2]-l1.pts[0][2]];
      const d2 = [l2.pts[1][0]-l2.pts[0][0], l2.pts[1][1]-l2.pts[0][1], l2.pts[1][2]-l2.pts[0][2]];
      addPlaneWithOrigin(cross3(d1, d2), l1.pts[0]);
      resetPlaneBuild(); return true;
    }
  }
  return false;
}

export function makeAnglePlane(edge, refPlane, angleDeg) {
  if (!edge || edge.type !== 'line' || edge.pts.length < 2) return;
  const d = normalize3([edge.pts[1][0]-edge.pts[0][0], edge.pts[1][1]-edge.pts[0][1], edge.pts[1][2]-edge.pts[0][2]]);
  const nr = normalize3(refPlane.getNormal());
  const dot = d[0]*nr[0] + d[1]*nr[1] + d[2]*nr[2];
  let nproj = [nr[0]-dot*d[0], nr[1]-dot*d[1], nr[2]-dot*d[2]];
  const lp = Math.hypot(nproj[0], nproj[1], nproj[2]);
  if (lp < 1e-9) nproj = normalize3([d[1], -d[0], 0]);
  else nproj = [nproj[0]/lp, nproj[1]/lp, nproj[2]/lp];
  const c = Math.cos(angleDeg * DEG2RAD), s = Math.sin(angleDeg * DEG2RAD);
  const cr = cross3(d, nproj);
  const n = [c*nproj[0] + s*cr[0], c*nproj[1] + s*cr[1], c*nproj[2] + s*cr[2]];
  addPlaneWithOrigin(n, edge.pts[0]);
}

export function surfaceNormalAt(name, worldPos) {
  const info = objectInfo.get(name);
  if (!info) return null;
  const poly = info.source.getOutputData();
  if (!poly) return null;
  const m = volumeMatrix(name);
  const tx = worldPos[0] - m[3], ty = worldPos[1] - m[7], tz = worldPos[2] - m[11];
  const lp = [m[0]*tx+m[1]*ty+m[2]*tz, m[4]*tx+m[5]*ty+m[6]*tz, m[8]*tx+m[9]*ty+m[10]*tz];
  const pts = poly.getPoints();
  const cells = poly.getPolys().getData();
  if (!pts || !cells) return null;
  let bestN = null, bestD = Infinity;
  let c = 0;
  while (c < cells.length) {
    const n = cells[c];
    const ids = [cells[c+1], cells[c+2], cells[c+3]];
    const sub = n === 4 ? [[0,1,2],[0,2,3]] : [[0,1,2]];
    for (const tr of sub) {
      const A = pts.getPoint(ids[tr[0]]), B = pts.getPoint(ids[tr[1]]), C = pts.getPoint(ids[tr[2]]);
      const cl = closestPointOnTriangle(lp, A, B, C);
      const d = (cl[0]-lp[0])**2 + (cl[1]-lp[1])**2 + (cl[2]-lp[2])**2;
      if (d < bestD) {
        bestD = d;
        const e1 = [B[0]-A[0], B[1]-A[1], B[2]-A[2]];
        const e2 = [C[0]-A[0], C[1]-A[1], C[2]-A[2]];
        bestN = normalize3(cross3(e1, e2));
      }
    }
    c += n + 1;
  }
  if (!bestN) return null;
  if (dot3(bestN, lp) < 0) bestN = [-bestN[0], -bestN[1], -bestN[2]];
  return [
    m[0]*bestN[0] + m[4]*bestN[1] + m[8]*bestN[2],
    m[1]*bestN[0] + m[5]*bestN[1] + m[9]*bestN[2],
    m[2]*bestN[0] + m[6]*bestN[1] + m[10]*bestN[2],
  ];
}

export function handlePlaneBuildClick(x, y) {
  const mode = planeBuild.mode;
  const hit = pickAt(x, y);

  if (mode === 'face') {
    if (hit && hit.name) {
      const fp = facePlaneAt(hit.name, hit.pos);
      if (fp) { addPlaneWithOrigin(fp.normal, fp.origin); resetPlaneBuild(); }
      else bindI18n(planeBuildHint, 'msg.faceNotRecognized');
    }
    return;
  }

  if (mode === 'points') {
    const p = snapPoint(x, y) || (hit && hit.pos ? hit.pos : projectToGridPlane(x, y));
    if (!p) return;
    planeBuild.pts.push(p);
    updatePlaneBuildMarkers();
    planeBuildHint.textContent = t('label.points') + ': ' + planeBuild.pts.length + t('sec.min3');
    return;
  }

  if (mode === 'parallel') {
    if (hit && hit.planeIndex !== undefined) {
      const ref = section.planes[hit.planeIndex].plane;
      promptText(t('sec.offsetMm'), '0', (v) => {
        const off = parseFloat(v);
        if (!Number.isFinite(off)) return;
        const n = ref.getNormal();
        const o = ref.getOrigin();
        addPlaneWithOrigin(n, [o[0] + n[0]*off, o[1] + n[1]*off, o[2] + n[2]*off]);
        resetPlaneBuild();
      });
    }
    return;
  }

  if (mode === 'prims') {
    if (hit && hit.primIndex !== undefined) {
      if (!planeBuild.selPrims.includes(hit.primIndex)) planeBuild.selPrims.push(hit.primIndex);
    } else {
      const p = snapPoint(x, y) || (hit && hit.pos ? hit.pos : projectToGridPlane(x, y));
      if (p) planeBuild.refPoint = p;
    }
    tryCommitPrims();
    return;
  }

  if (mode === 'tangent') {
    if (hit && hit.primIndex !== undefined) {
      const prim = prims[hit.primIndex];
      if (prim && (prim.type === 'circle' || prim.type === 'arc') && prim.pts.length >= 2) {
        const C = prim.pts[0], rp = prim.pts[1];
        const r = Math.hypot(rp[0]-C[0], rp[1]-C[1], rp[2]-C[2]);
        const radial = normalize3([hit.pos[0]-C[0], hit.pos[1]-C[1], hit.pos[2]-C[2]]);
        const P = [C[0]+radial[0]*r, C[1]+radial[1]*r, C[2]+radial[2]*r];
        addPlaneWithOrigin(radial, P);
        resetPlaneBuild();
        return;
      }
    }
    if (hit && hit.name) {
      const n = surfaceNormalAt(hit.name, hit.pos);
      if (n) { addPlaneWithOrigin(n, hit.pos); resetPlaneBuild(); }
    }
    return;
  }

  if (mode === 'perp') {
    if (hit && hit.primIndex !== undefined) {
      const prim = prims[hit.primIndex];
      let t = null, origin = hit.pos;
      if (prim && prim.type === 'line' && prim.pts.length >= 2) {
        t = normalize3([prim.pts[1][0]-prim.pts[0][0], prim.pts[1][1]-prim.pts[0][1], prim.pts[1][2]-prim.pts[0][2]]);
        origin = [(prim.pts[0][0]+prim.pts[1][0])/2, (prim.pts[0][1]+prim.pts[1][1])/2, (prim.pts[0][2]+prim.pts[1][2])/2];
      } else if (prim && (prim.type === 'circle' || prim.type === 'arc') && prim.pts.length >= 2) {
        const C = prim.pts[0], rp = prim.pts[1];
        const radial = normalize3([hit.pos[0]-C[0], hit.pos[1]-C[1], hit.pos[2]-C[2]]);
        t = normalize3(cross3(prim.normal || [0, 0, 1], radial));
        origin = [C[0], C[1], C[2]];
      }
      if (t) { addPlaneWithOrigin(t, origin); resetPlaneBuild(); }
    }
    return;
  }

  if (mode === 'angle') {
    if (!planeBuild.selPrims.length) {
      if (hit && hit.primIndex !== undefined) {
        planeBuild.selPrims.push(hit.primIndex);
        bindI18n(planeBuildHint, 'hint.plane2');
      }
      return;
    }
    if (planeBuild.refPlane < 0) {
      if (hit && hit.planeIndex !== undefined) {
        planeBuild.refPlane = hit.planeIndex;
        const edge = prims[planeBuild.selPrims[0]];
        promptText(t('con.angleDeg'), '0', (v) => {
          const ang = parseFloat(v);
          if (!Number.isFinite(ang)) return;
          makeAnglePlane(edge, section.planes[planeBuild.refPlane].plane, ang);
          resetPlaneBuild();
        });
      }
    }
    return;
  }

  if (mode === 'mid') {
    if (hit && hit.planeIndex !== undefined && !planeBuild.selPlanes.includes(hit.planeIndex)) {
      planeBuild.selPlanes.push(hit.planeIndex);
      if (planeBuild.selPlanes.length >= 2) {
        const p1 = section.planes[planeBuild.selPlanes[0]].plane;
        const p2 = section.planes[planeBuild.selPlanes[1]].plane;
        const n1 = normalize3(p1.getNormal());
        const n2 = normalize3(p2.getNormal());
        let n = [n1[0]+n2[0], n1[1]+n2[1], n1[2]+n2[2]];
        const l = Math.hypot(n[0], n[1], n[2]);
        if (l < 1e-9) {
          n = normalize3([n1[1], -n1[0], 0]);
          if (Math.hypot(n[0], n[1], n[2]) < 1e-9) n = [1, 0, 0];
        } else n = [n[0]/l, n[1]/l, n[2]/l];
        const o1 = p1.getOrigin(), o2 = p2.getOrigin();
        addPlaneWithOrigin(n, [(o1[0]+o2[0])/2, (o1[1]+o2[1])/2, (o1[2]+o2[2])/2]);
        resetPlaneBuild();
      } else {
        bindI18n(planeBuildHint, 'hint.plane2click');
      }
    }
    return;
  }
}

export function isCutObject(name) {
  if (!section.enabled) return false;
  if (section.scope === 'local') return name === section.localTarget;
  return !section.selective || section.cutSet.has(name);
}

export function updateSectionPlanes() {
  for (const [name, info] of objectInfo) {
    const localPlanes = visibleSectionPlanes().map((wp) => {
      const wn = wp.getNormal();
      const wo = wp.getOrigin();
      let lp = vtkPlane.newInstance();
      if (info.isMesh) {
        lp.setNormal(wn[0], wn[1], wn[2]);
        lp.setOrigin(wo[0], wo[1], wo[2]);
      } else {
        // world -> local: R * (p - T), R = upper-left 3x3 of the actor matrix.
        // getMatrix() is ROW-major: [R00,R01,R02,Tx, R10,R11,R12,Ty, R20,R21,R22,Tz, 0,0,0,1]
        const m = volumeMatrix(name);
        const tx = wo[0] - m[3], ty = wo[1] - m[7], tz = wo[2] - m[11];
        // world -> local: R^T * (p - T). R^T rows are R's columns.
        const nlx = m[0] * wn[0] + m[4] * wn[1] + m[8] * wn[2];
        const nly = m[1] * wn[0] + m[5] * wn[1] + m[9] * wn[2];
        const nlz = m[2] * wn[0] + m[6] * wn[1] + m[10] * wn[2];
        const olx = m[0] * tx + m[4] * ty + m[8] * tz;
        const oly = m[1] * tx + m[5] * ty + m[9] * tz;
        const olz = m[2] * tx + m[6] * ty + m[10] * tz;
        lp.setNormal([nlx, nly, nlz]);
        lp.setOrigin([olx, oly, olz]);
      }
      return lp;
    });
    info.clipFilter.setClippingPlanes(localPlanes);
    info.clipFilter.modified();
  }
}

export function updateActorMappers() {
  updateSectionPlanes();
  for (const [name, actor] of actors) {
    const info = objectInfo.get(name);
    if (!info) continue;
    const mapper = actor.getMapper();
    const prop = actor.getProperty();
    if (view.repMode === 3) {
      mapper.setInputData(info.pointCloud);
      prop.setRepresentationToPoints();
      prop.setPointSize(3);
      prop.setEdgeVisibility(false);
    } else {
      mapper.setInputData(isCutObject(name) ? info.clipFilter.getOutputData() : info.source.getOutputData());
      if (view.repMode === 2) prop.setRepresentationToWireframe();
      else prop.setRepresentationToSurface();
      prop.setEdgeVisibility(view.repMode === 1);
    }
  }
  if (mergedMesh.actor) {
    const prop = mergedMesh.actor.getProperty();
    if (view.repMode === 3) {
      mergedMesh.mapper.setInputData(mergedMesh.pointCloud);
      prop.setRepresentationToPoints();
      prop.setPointSize(3);
      prop.setEdgeVisibility(false);
    } else {
      const cut = section.enabled && section.scope === 'global';
      if (cut) {
        buildMergedCut();
        mergedMesh.mapper.setInputData(mergedMesh.cutPd || mergedMesh.pd);
      } else {
        mergedMesh.mapper.setInputData(mergedMesh.pd);
      }
      if (view.repMode === 2) prop.setRepresentationToWireframe();
      else prop.setRepresentationToSurface();
      prop.setEdgeVisibility(view.repMode === 1);
    }
  }
  updatePlaneVisuals();
  renderWindow.render();
}

export function rebuildPlaneListUI() {
  planesListEl.innerHTML = '';
  section.planes.forEach((p, i) => {
    const chip = document.createElement('button');
    chip.textContent = 'P' + (i + 1);
    chip.style.cssText = btnStyle + (i === section.active ? ';background:#b45f06;color:#fff;' : '') + (p.cut === false ? ';opacity:0.55;' : '');
    chip.title = (p.name || (t('prop.planePrefix') + (i + 1))) + (p.cut === false ? t('sec.notCut') : t('sec.cut'));
    chip.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      section.active = i;
      syncActiveSlider();
      rebuildPlaneListUI();
    });
    planesListEl.appendChild(chip);
  });
  syncCutBtn();
}

export function syncActiveSlider() {
  const plane = activePlane();
  if (!plane) { sectionSlider.value = '0'; return; }
  const n = plane.getNormal();
  const pos = dot3(n, plane.getOrigin());
  const b = computeSceneBounds();
  if (b) {
    const cx = (b[0] + b[1]) / 2, cy = (b[2] + b[3]) / 2, cz = (b[4] + b[5]) / 2;
    const hx = (b[1] - b[0]) / 2, hy = (b[3] - b[2]) / 2, hz = (b[5] - b[4]) / 2;
    const proj = Math.abs(n[0]) * hx + Math.abs(n[1]) * hy + Math.abs(n[2]) * hz;
    const center = n[0] * cx + n[1] * cy + n[2] * cz;
    sectionSlider.min = (center - proj).toFixed(1);
    sectionSlider.max = (center + proj).toFixed(1);
  }
  sectionSlider.value = pos.toFixed(1);
}

export function addSectionPlane(normal, position) {
  const plane = vtkPlane.newInstance();
  plane.setNormal([normal[0], normal[1], normal[2]]);
  plane.setOrigin([normal[0] * position, normal[1] * position, normal[2] * position]);
  // New planes are construction planes (visible, NOT cutting). Cutting is an
  // explicit per-plane action via the "Резать" toggle — like CAD, not a section
  // that silently hides geometry the moment a plane appears.
  section.planes.push({ plane, visible: true, cut: false, name: t('prop.planePrefix') + (section.planes.length + 1), layer: '' });
  section.active = section.planes.length - 1;
  section.enabled = true;
  sectionPanel.style.display = 'flex';
  sectionBtn.style.background = '#b45f06';
  sectionBtn.style.color = '#fff';
  syncActiveSlider();
  rebuildPlaneListUI();
  updateActorMappers();
  updateTreeUI();
}

export function setActivePosition(pos) {
  const plane = activePlane();
  if (!plane) return;
  const n = plane.getNormal();
  plane.setOrigin([n[0] * pos, n[1] * pos, n[2] * pos]);
  updateActorMappers();
}

export function flipActivePlane() {
  const plane = activePlane();
  if (!plane) return;
  const n = plane.getNormal();
  plane.setNormal([-n[0], -n[1], -n[2]]);
  updateActorMappers();
}

export function deletePlane(i) {
  if (i < 0 || i >= section.planes.length) return;
  section.planes.splice(i, 1);
  if (section.planes.length) {
    section.active = Math.min(section.active, section.planes.length - 1);
    syncActiveSlider();
  } else {
    section.enabled = false;
    sectionPanel.style.display = 'none';
    sectionBtn.style.background = '#333';
    sectionBtn.style.color = '#ddd';
  }
  if (view.selectedPlaneIndex === i) view.selectedPlaneIndex = -1;
  else if (view.selectedPlaneIndex > i) view.selectedPlaneIndex--;
  rebuildPlaneListUI();
  updateActorMappers();
  updateTreeUI();
  updateProps();
}

export function removeActivePlane() {
  if (!section.planes.length) return;
  deletePlane(section.active);
}

export function clearSectionPlanes() {
  section.planes = [];
  section.active = 0;
  section.enabled = false;
  sectionPanel.style.display = 'none';
  sectionBtn.style.background = '#333';
  sectionBtn.style.color = '#ddd';
  rebuildPlaneListUI();
  updateActorMappers();
  updateTreeUI();
}

export function setSectionEnabled(on) {
  section.enabled = on;
  sectionPanel.style.display = on ? 'flex' : 'none';
  sectionBtn.style.background = on ? '#b45f06' : '#333';
  sectionBtn.style.color = on ? '#fff' : '#ddd';
  if (on) {
    if (!section.planes.length) addSectionPlane([0, 0, 1], 0);
    else updateActorMappers();
  } else {
    updateActorMappers();
  }
}

export function planeHalfSize() {
  const b = computeSceneBounds();
  if (!b) return 300;
  return Math.max(b[1] - b[0], b[3] - b[2], b[5] - b[4]) * 0.7;
}

export function buildPlaneGeometry(normal, origin) {
  const n = normalize3(normal);
  const ref = Math.abs(n[0]) < 0.9 ? [1, 0, 0] : [0, 1, 0];
  const u = normalize3(cross3(ref, n));
  const v = cross3(n, u);
  const h = planeHalfSize();
  const pts = [
    [origin[0] - u[0] * h - v[0] * h, origin[1] - u[1] * h - v[1] * h, origin[2] - u[2] * h - v[2] * h],
    [origin[0] + u[0] * h - v[0] * h, origin[1] + u[1] * h - v[1] * h, origin[2] + u[2] * h - v[2] * h],
    [origin[0] + u[0] * h + v[0] * h, origin[1] + u[1] * h + v[1] * h, origin[2] + u[2] * h + v[2] * h],
    [origin[0] - u[0] * h + v[0] * h, origin[1] - u[1] * h + v[1] * h, origin[2] - u[2] * h + v[2] * h],
  ];
  const pd = vtkPolyData.newInstance();
  const p = pd.getPoints();
  p.setNumberOfPoints(4, 3);
  pts.forEach((pt, i) => p.setPoint(i, pt[0], pt[1], pt[2]));
  pd.getPolys().insertNextCell([0, 1, 2, 3]);
  return pd;
}

export function updatePlaneVisuals() {
  planeActorMap.clear();
  while (planeVisualActors.length > section.planes.length) {
    renderer.removeActor(planeVisualActors.pop());
  }
  section.planes.forEach((p, i) => {
    let actor;
    if (i < planeVisualActors.length) actor = planeVisualActors[i];
    else {
      actor = vtkActor.newInstance();
      actor.getProperty().setColor(0.35, 0.65, 1.0);
      actor.getProperty().setOpacity(0.25);
      actor.getProperty().setLighting(false);
      actor.getProperty().setEdgeVisibility(true);
      actor.getProperty().setEdgeColor(0.4, 0.75, 1.0);
      actor.getProperty().setBackfaceCulling(false);
      const mapper = vtkMapper.newInstance();
      actor.setMapper(mapper);
      renderer.addActor(actor);
      planeVisualActors.push(actor);
    }
    actor.setPickable(true);
    planeActorMap.set(actor, i);
    actor.getMapper().setInputData(buildPlaneGeometry(p.plane.getNormal(), p.plane.getOrigin()));
    actor.getProperty().setOpacity(p.opacity !== undefined && p.opacity !== null ? p.opacity : 0.25);
    const cutting = p.cut !== false;
    actor.getProperty().setColor(cutting ? 0.35 : 0.5, cutting ? 0.65 : 0.5, cutting ? 1.0 : 0.55);
    actor.setVisibility(p.visible !== false);
  });
}

export function visibleSectionPlanes() {
  return section.planes.filter((p) => p.cut !== false).map((p) => p.plane);
}

export function toggleActiveCut() {
  const p = section.planes[section.active];
  if (!p) return;
  p.cut = p.cut === false;
  syncCutBtn();
  updateActorMappers();
  updateTreeUI();
  updateProps();
}

export function syncCutBtn() {
  const p = section.planes[section.active];
  const on = !!(p && p.cut !== false);
  bindI18n(cutBtn, 'tools.cut');
  cutBtn.style.background = on ? '#b45f06' : '#3a3a3a';
  cutBtn.style.color = '#fff';
}

export function sceneMarkerRadius() {
  const b = computeSceneBounds();
  if (!b) return 2;
  const d = Math.max(b[1] - b[0], b[3] - b[2], b[5] - b[4]);
  return d / 120;
}

export function updatePickMarkers() {
  for (const a of pickMarkerActors) removeMarker(a);
  pickMarkerActors.length = 0;
  if (!section.pickPoints.length) { renderWindow.render(); return; }
  for (const p of section.pickPoints) {
    const a = makeMarkerActor(1, 1, 0);
    a.setPosition(p[0], p[1], p[2]);
    pickMarkerActors.push(a);
  }
  updateMarkerScales();
  renderWindow.render();
}





