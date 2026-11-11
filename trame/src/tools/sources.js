//==============================================================================
// G4CARE - sources.js — Sources tool panel
//
// Sources tool panel
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t, onLocaleChange } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - particle source panel + solid-angle visualization.
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkSphereSource from '@kitware/vtk.js/Filters/Sources/SphereSource';
import { renderer, renderWindow } from '../context.js';
import { makeMarkerActor, makePickableMarker, removeMarker, updateMarkerScales } from '../render/actors.js';
import { showToast } from '../ui/dom.js';
import { icon } from '../../../gui/src/icons/icon.js';
import { rayMeshIntersect } from '../render/mesh-measure.js';
import { objectInfo, actors, objectMeta, view } from '../state.js';

const UNIT_MM = { nm: 1e-6, um: 1e-3, mm: 1, cm: 10, m: 1000, km: 1e6, deg: 1, rad: 57.2957795 };

// Common Geant4 particle names for the editor datalist. The authoritative
// list lives in G4ParticleTable; these cover the names used in G4CARE configs.
const PARTICLE_SUGGESTIONS = [
  'gamma', 'e-', 'e+', 'proton', 'anti_proton', 'neutron', 'anti_neutron',
  'alpha', 'deuteron', 'triton', 'He3', 'He4',
  'mu-', 'mu+', 'pi-', 'pi+', 'pi0', 'kaon-', 'kaon+', 'kaon0L', 'kaon0S',
  'opticalphoton', 'geantino',
];

// Beam colors for per-vertex ray grouping (cycled).
const BEAM_COLORS = [
  [0.3, 0.9, 1], [1, 0.7, 0.2], [0.5, 1, 0.5], [1, 0.5, 0.9],
  [0.9, 0.9, 0.3], [0.6, 0.7, 1], [1, 0.5, 0.4], [0.4, 1, 0.8],
];

function toNum(v) {
  if (typeof v === 'number') return v;
  if (typeof v === 'string') {
    const s = v.trim();
    const m = s.match(/^([-+0-9.eE]+)\s*\*\s*([A-Za-z]+)$/);
    if (m) { const u = UNIT_MM[m[2]]; return u != null ? parseFloat(m[1]) * u : NaN; }
    const n = parseFloat(s);
    if (Number.isFinite(n)) return n;
  }
  return NaN;
}

let listEl = null;
let overlayActors = [];
let sampleActors = [];
let lastSig = '';
let editingSource = null;
let currentMode = 'flow';
let pendingMacroInput = null;

// Re-render the source list/editor when the locale changes.
onLocaleChange(() => {
  if (!listEl) return;
  if (editingSource) openSourceEditor(editingSource);
  else { lastSig = ''; loadSources(); }
});

// Scene-object markers for sources whose position is a plain number (placed by
// the user via the «Источник» toolbar button). Each actor maps back to the
// source entry so the context menu can target it.
let sourceMarkers = new Map(); // actor -> { name, pos, source }

function sourceNumericPos(s) {
  const p = s.pos;
  if (!p) return null;
  const x = toNum(p.x), y = toNum(p.y), z = toNum(p.z);
  if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) return null;
  return [x, y, z];
}

function drawSourceMarkers(data) {
  for (const a of sourceMarkers.keys()) removeMarker(a);
  sourceMarkers.clear();
  for (const s of (data.sources || [])) {
    const pos = sourceNumericPos(s);
    if (!pos) continue;
    const a = makePickableMarker(1.0, 0.42, 0.42);
    a.setPosition(pos[0], pos[1], pos[2]);
    sourceMarkers.set(a, { name: s.name, pos, source: s });
  }
  updateMarkerScales();
}

export function sourceHitForActor(actor) {
  return sourceMarkers.get(actor) || null;
}

export function getSourcePos(name) {
  for (const v of sourceMarkers.values()) if (v.name === name) return v.pos;
  return null;
}

export function setSourceMarkerPos(name, pos) {
  for (const [a, v] of sourceMarkers.entries()) {
    if (v.name === name) {
      a.setPosition(pos[0], pos[1], pos[2]);
      v.pos = [...pos];
    }
  }
}

export function deleteSource(name) {
  return fetch('/api/source', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, delete: true }),
  }).then((r) => r.json()).catch(() => null);
}

export function setupSources(hooks) {
  listEl = hooks.listEl;
  // Engine sampling result -> draw the real sampled rays.
  window.addEventListener('message', (ev) => {
    const d = ev.data;
    if (!d) return;
    if (d.type === 'g4care-sample-source-result') {
      if (d.ok && d.samples && d.samples.length) drawSamples(d.samples);
      else showToast(d.error || t('source.samplingError'));
    } else if (d.type === 'g4care-macro-path') {
      if (d.ok && d.path && pendingMacroInput) { pendingMacroInput.value = d.path; pendingMacroInput = null; }
      else if (!d.canceled) showToast(d.error || t('source.macroSelectFailed'));
    }
  });
}

function clearSamples() {
  for (const a of sampleActors) removeMarker(a);
  sampleActors = [];
}

function closestHit(origin, dir) {
  let best = null, bestD = Infinity;
  for (const name of objectInfo.keys()) {
    const hit = rayMeshIntersect(name, origin, dir);
    if (hit) {
      const d = Math.hypot(hit[0] - origin[0], hit[1] - origin[1], hit[2] - origin[2]);
      if (d < bestD) { bestD = d; best = hit; }
    }
  }
  return best;
}

function drawSamples(samples) {
  clearSamples();
  const L = 1000; // fallback ray length (mm) when no geometry is hit
  // Group samples by vertex position so multi-vertex sources render as
  // distinct beams (each in its own color) instead of one pile of rays.
  const groups = new Map();
  for (const s of samples) {
    const p = s.position || [0, 0, 0];
    const key = p.map((c) => Math.round(c * 100) / 100).join(',');
    if (!groups.has(key)) groups.set(key, { origin: p, samples: [] });
    groups.get(key).samples.push(s);
  }
  let gi = 0;
  for (const { origin, samples: grp } of groups.values()) {
    const rgb = BEAM_COLORS[gi % BEAM_COLORS.length];
    gi += 1;
    const mk = makeMarkerActor(rgb[0], rgb[1], rgb[2]);
    mk.setPosition(origin[0], origin[1], origin[2]);
    sampleActors.push(mk);
    for (const s of grp) {
      const d = s.direction || [0, 0, 1];
      const dn = Math.hypot(d[0], d[1], d[2]) || 1;
      const dir = [d[0] / dn, d[1] / dn, d[2] / dn];
      const hit = closestHit(origin, dir);
      const end = hit || [origin[0] + dir[0] * L, origin[1] + dir[1] * L, origin[2] + dir[2] * L];
      const a = lineActor(origin, end, rgb);
      a.setPickable(false);
      renderer.addActor(a);
      sampleActors.push(a);
      if (hit) {
        const hm = makeMarkerActor(rgb[0], rgb[1], rgb[2]);
        hm.setPosition(hit[0], hit[1], hit[2]);
        sampleActors.push(hm);
      }
    }
  }
  updateMarkerScales();
  renderWindow.render();
}

export function requestSample(name, n) {
  try {
    window.parent.postMessage({ type: 'g4care-sample-source', name, n }, '*');
  } catch (e) { /* ignore */ }
}

function saveSource(payload) {
  return fetch('/api/source', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  }).then((r) => r.json()).catch(() => null);
}

function field(label, val) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = val == null ? '' : String(val);
  inp.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;';
  wrap.appendChild(lbl); wrap.appendChild(inp);
  return { wrap, inp };
}

function selectField(label, options, val) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  const sel = document.createElement('select');
  sel.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;';
  options.forEach(([ol, ov]) => { const o = document.createElement('option'); o.value = ov; o.textContent = ol; sel.appendChild(o); });
  sel.value = val || '';
  wrap.appendChild(lbl); wrap.appendChild(sel);
  return { wrap, sel };
}

function vecField(label, vec) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:4px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  wrap.appendChild(lbl);
  const inputs = ['X', 'Y', 'Z'].map((axis, i) => {
    const box = document.createElement('span');
    box.style.cssText = 'display:flex;align-items:center;gap:2px;';
    const al = document.createElement('span');
    al.textContent = axis;
    al.style.cssText = 'color:#777;width:10px;';
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.value = (vec && vec[i] != null) ? String(vec[i]) : '';
    inp.style.cssText = 'width:52px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;';
    box.appendChild(al); box.appendChild(inp);
    wrap.appendChild(box);
    return inp;
  });
  return { wrap, inputs };
}

function clearOverlay() {
  for (const a of overlayActors) renderer.removeActor(a);
  overlayActors = [];
}

function addActor(a) {
  a.setPickable(false);
  renderer.addActor(a);
  overlayActors.push(a);
}

function lineActor(p0, p1, rgb) {
  const s = vtkLineSource.newInstance();
  s.setPoint1(p0[0], p0[1], p0[2]);
  s.setPoint2(p1[0], p1[1], p1[2]);
  const m = vtkMapper.newInstance();
  m.setInputConnection(s.getOutputPort());
  const a = vtkActor.newInstance();
  a.setMapper(m);
  a.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
  a.getProperty().setLighting(false);
  return a;
}

function sphereActor(pos, radius, rgb) {
  const s = vtkSphereSource.newInstance({ radius, thetaResolution: 24, phiResolution: 24 });
  s.setCenter(pos[0], pos[1], pos[2]);
  const m = vtkMapper.newInstance();
  m.setInputConnection(s.getOutputPort());
  const a = vtkActor.newInstance();
  a.setMapper(m);
  a.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
  a.getProperty().setOpacity(0.2);
  a.getProperty().setRepresentationToWireframe();
  a.getProperty().setLighting(false);
  return a;
}

// Show VERTEX_GENERATOR placement constraints: draw a non-invasive wireframe
// outline around the referenced volume/surface (never touches the shared
// highlight/selection state) and the mathematical sphere (radius + center).
function objectBounds(name) {
  const info = objectInfo.get(name);
  if (info) {
    try {
      const out = info.clipFilter && info.clipFilter.getOutputData && info.clipFilter.getOutputData();
      if (out && out.getBounds) return out.getBounds();
    } catch (e) { /* ignore */ }
  }
  const a = actors.get(name);
  if (a && a.getBounds) return a.getBounds();
  return null;
}

function boxOutlineActors(name, rgb) {
  const b = objectBounds(name);
  if (!b || !b.every(Number.isFinite)) return [];
  const [x0, x1, y0, y1, z0, z1] = b;
  const c = [
    [x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
    [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1],
  ];
  const edges = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
  return edges.map(([i, j]) => lineActor(c[i], c[j], rgb));
}

function showGeneratorConstraints(s) {
  const vg = s.VERTEX_GENERATOR || {};
  for (const name of [vg.surface, vg.volume]) {
    if (typeof name === 'string' && name && (objectInfo.has(name) || actors.has(name))) {
      for (const a of boxOutlineActors(name, [0.2, 0.9, 0.9])) addActor(a);
    }
  }
  const radius = toNum(vg.radius);
  if (Number.isFinite(radius) && radius > 0) {
    const cx = vg.center ? toNum(vg.center.x) : NaN;
    const cy = vg.center ? toNum(vg.center.y) : NaN;
    const cz = vg.center ? toNum(vg.center.z) : NaN;
    const center = [Number.isFinite(cx) ? cx : 0, Number.isFinite(cy) ? cy : 0, Number.isFinite(cz) ? cz : 0];
    addActor(sphereActor(center, radius, [0.5, 0.9, 0.5]));
  }
}

export async function loadSources() {
  editingSource = null;
  let data = { mode: 'flow', sources: [] };
  try {
    const res = await fetch('/sources.json');
    data = await res.json();
  } catch (e) { /* ignore */ }
  const sig = JSON.stringify(data);
  if (sig === lastSig) return;
  lastSig = sig;
  currentMode = data.mode || 'flow';
  clearOverlay();
  drawSourceMarkers(data);

  if (listEl) {
    listEl.innerHTML = '';
    const head = document.createElement('div');
    head.textContent = t('label.sources') + ' [' + (data.mode || 'flow') + '] · ' + (data.sources || []).length;
    head.style.cssText = 'color:#e8752a;margin-bottom:4px;';
    listEl.appendChild(head);
  }

  for (const s of (data.sources || [])) {
    if (listEl) {
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;align-items:center;gap:6px;padding:1px 4px;font:12px Arial;color:#969da8;';
      const lbl = document.createElement('span');
      lbl.style.cssText = 'flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;';
      lbl.textContent = (s.type || '?') + ' · ' + (s.name || '');
      const editBtn = document.createElement('button');
      editBtn.appendChild(icon('pencil', 11));
      editBtn.setAttribute('data-i18n-title', 'tip.configureSource'); editBtn.title = t('tip.configureSource');
      editBtn.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:1px 6px;cursor:pointer;font:12px Arial;flex:none;display:inline-flex;align-items:center;justify-content:center;';
      editBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); openSourceEditor(s); });
      const btn = document.createElement('button');
      bindI18n(btn, 'tools.sample');
      btn.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:1px 6px;cursor:pointer;font:10px Arial;flex:none;';
      btn.setAttribute('data-i18n-title', 'tip.sample'); btn.title = t('tip.sample');
      btn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); requestSample(s.name, 50); });
      row.appendChild(lbl);
      row.appendChild(editBtn);
      row.appendChild(btn);
      listEl.appendChild(row);
    }

    // Static particle position/direction is intentionally NOT drawn: source
    // parameters are ExprTK expressions in general, so only the engine's
    // sampled rays are authoritative. Show VERTEX_GENERATOR placement
    // constraints (volume / surface / math sphere) when configured.
    showGeneratorConstraints(s);
  }
  renderWindow.render();
}

export function clearSources() {
  clearOverlay();
  lastSig = '';
  editingSource = null;
  if (listEl) listEl.innerHTML = '';
}

export function openSourceEditor(s) {
  if (!listEl) return;
  editingSource = s;
  listEl.innerHTML = '';
  const box = document.createElement('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:6px;padding:10px;color:#e2e5ea;font:12px Arial;';

  const titleRow = document.createElement('div');
  titleRow.style.cssText = 'display:flex;align-items:center;gap:6px;margin-bottom:8px;';
  const backBtn = document.createElement('button');
  backBtn.textContent = '←';
  backBtn.setAttribute('data-i18n-title', 'tip.backSources'); backBtn.title = t('tip.backSources');
  backBtn.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:1px 8px;cursor:pointer;font:12px Arial;';
  backBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); lastSig = ''; loadSources(); });
  const title = document.createElement('div');
  title.textContent = t('label.source') + ': ' + s.name;
  title.style.cssText = 'font-weight:bold;color:#e8752a;flex:1;';
  titleRow.appendChild(backBtn); titleRow.appendChild(title);
  box.appendChild(titleRow);

  const fType = selectField('type', [[t('source.type.particleGun'), 'particleGun'], [t('source.type.gps'), 'GPS'], [t('source.type.radioactive'), 'radioactive'], [t('source.type.activation'), 'activation'], [t('source.type.mixture'), 'mixture'], [t('source.type.table'), 'table']], s.type || 'particleGun');
  const fSrcMode = selectField(t('source.sourceMode'), [['flow', 'flow'], ['event', 'event']], currentMode);
  box.appendChild(fType.wrap);
  box.appendChild(fSrcMode.wrap);

  const dl = document.createElement('datalist');
  dl.id = 'g4care-particles';
  PARTICLE_SUGGESTIONS.forEach((p) => { const o = document.createElement('option'); o.value = p; dl.appendChild(o); });
  box.appendChild(dl);

  const objDl = document.createElement('datalist');
  objDl.id = 'g4care-objects';
  const objNames = new Set([...objectInfo.keys(), ...objectMeta.keys()]);
  [...objNames].sort().forEach((n) => { const o = document.createElement('option'); o.value = n; objDl.appendChild(o); });
  box.appendChild(objDl);

  const dataDl = document.createElement('datalist');
  dataDl.id = 'g4care-data-sections';
  box.appendChild(dataDl);
  fetch('/data.json').then((r) => r.json()).then((names) => {
    (names || []).forEach((n) => { const o = document.createElement('option'); o.value = n; dataDl.appendChild(o); });
  }).catch(() => {});

  const body = document.createElement('div');
  box.appendChild(body);

  let collectFn = () => ({});
  function buildBody() {
    body.innerHTML = '';
    const t = (fType.sel.value || '').toLowerCase();
    if (t === 'gps') collectFn = buildGps(s, body);
    else if (t === 'radioactive') collectFn = buildRadioactive(s, body);
    else if (t === 'activation') collectFn = buildActivation(s, body);
    else if (t === 'mixture') collectFn = buildMixture(s, body);
    else if (t === 'table') collectFn = buildTable(s, body);
    else collectFn = buildParticleGun(s, body);
  }
  buildBody();
  fType.sel.addEventListener('change', buildBody);

  const btnRow = document.createElement('div');
  btnRow.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;margin-top:10px;';
  const saveBtn = document.createElement('button');
  bindI18n(saveBtn, 'tools.save');
  saveBtn.style.cssText = 'background:#2a5a2a;color:#cfc;border:1px solid #484;border-radius:4px;padding:4px 12px;cursor:pointer;font:12px Arial;';
  const cancelBtn = document.createElement('button');
  bindI18n(cancelBtn, 'settings.cancel');
  cancelBtn.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:4px 12px;cursor:pointer;font:12px Arial;';
  btnRow.appendChild(saveBtn); btnRow.appendChild(cancelBtn);
  box.appendChild(btnRow);
  listEl.appendChild(box);

  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); lastSig = ''; loadSources(); });
  saveBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const payload = { name: s.name, type: fType.sel.value, SOURCE_MODE: fSrcMode.sel.value };
    Object.assign(payload, collectFn());
    const done = (res) => {
      showToast(res && res.ok ? t('source.saved') : (t('error.generic') + ((res && res.error) || '')));
      lastSig = '';
      loadSources();
    };
    if (fType.sel.value === 'GPS') {
      // GPS is driven by the /gps/... macro, so regenerate it from the current
      // fields too (otherwise the particle type would silently stay 'gamma').
      const pp = payload.pos || {};
      const dd = payload.direction || {};
      const macro = buildGpsMacro(s.name, {
        particle: payload.particle || 'gamma',
        energy: payload.energy || '1',
        spectrum: payload.spectrum || 'Mono',
        pos: [pp.x, pp.y, pp.z],
        direction: [dd.x, dd.y, dd.z],
      });
      fetch('/api/gps-macro', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: s.name, macro }),
      }).then((r) => r.json()).then(() => {
        const fields = { ...payload };
        delete fields.macro; // macro path is set by /api/gps-macro
        saveSource(fields).then(done);
      }).catch(() => { saveSource(payload).then(done); });
    } else {
      saveSource(payload).then(done);
    }
  });
}

function sectionTitle(text) {
  const d = document.createElement('div');
  d.textContent = text;
  d.style.cssText = 'color:#9ac;margin:8px 0 2px;font-weight:bold;';
  return d;
}

function note(text) {
  const d = document.createElement('div');
  d.textContent = text;
  d.style.cssText = 'color:#777;font:12px Arial;margin:4px 0;';
  return d;
}

function boolField(label, val) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  const inp = document.createElement('input');
  inp.type = 'checkbox';
  inp.checked = !!val;
  wrap.appendChild(lbl); wrap.appendChild(inp);
  return { wrap, inp };
}

function particleField(label, val) {
  const f = field(label, val);
  f.inp.setAttribute('list', 'g4care-particles');
  return f;
}

function objectField(label, val) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = val == null ? '' : String(val);
  inp.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;';
  inp.setAttribute('list', 'g4care-objects');
  inp.placeholder = t('ph.objectName');
  const pickBtn = document.createElement('button');
  pickBtn.textContent = '🎯';
  pickBtn.setAttribute('data-i18n-title', 'tip.pickObject'); pickBtn.title = t('tip.pickObject');
  pickBtn.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 6px;flex:none;';
  pickBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    if (view.selectedName) inp.value = view.selectedName;
    else showToast(t('source.selectObjectFirst'));
  });
  wrap.appendChild(lbl); wrap.appendChild(inp); wrap.appendChild(pickBtn);
  return { wrap, inp };
}

function macroField(label, val) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:4px;margin:3px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:104px;color:#9ac;flex:none;';
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = val == null ? '' : String(val);
  inp.style.cssText = 'flex:1;min-width:0;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;';
  inp.placeholder = t('ph.macPath');
  const mkBtn = (iconName, title, type) => {
    const b = document.createElement('button');
    b.title = title;
    b.style.cssText = 'flex:none;background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 5px;display:inline-flex;align-items:center;justify-content:center;';
    b.appendChild(icon(iconName, 12));
    b.addEventListener('pointerdown', (e) => {
      e.stopPropagation(); e.preventDefault();
      pendingMacroInput = inp;
      try { window.parent.postMessage({ type }, '*'); } catch (err) { /* ignore */ }
    });
    return b;
  };
  const openBtn = mkBtn('folder-open', t('source.openMacro'), 'g4care-open-macro');
  const newBtn = mkBtn('pencil', t('source.newMacro'), 'g4care-new-macro');
  wrap.appendChild(lbl); wrap.appendChild(inp); wrap.appendChild(openBtn); wrap.appendChild(newBtn);
  return { wrap, inp };
}

function dataField(label, val) {
  const f = field(label, val);
  f.inp.setAttribute('list', 'g4care-data-sections');
  f.inp.placeholder = t('ph.dataSection');
  return f;
}

function hasAnyValue(obj) {
  return Object.values(obj).some((v) => {
    if (v && typeof v === 'object') return Object.values(v).some((x) => String(x ?? '').trim() !== '');
    return String(v ?? '').trim() !== '';
  });
}

// The engine reads angle fields in radians via GetValueWithUnits; a bare number
// like "15" becomes 15 rad (≈859°). Append *deg so "15" means 15 degrees, while
// expressions and unit-qualified values pass through unchanged.
function angDeg(v) {
  const s = String(v == null ? '' : v).trim();
  if (!s) return s;
  if (/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(s)) return s + '*deg';
  return s;
}

// Unified particle-definition block shared by the "Пучок", "Вершины" and
// "Генератор вершин" modes. Returns a collect() that produces the full
// per-particle dict (including `direction`), matching ParticleUtils::ParseParticles.
function buildParticleBlock(p, parent) {
  const fParticle = particleField('particle', p.particle != null ? p.particle : p.type);
  const fEnergy = field('energy', p.energy);
  const fWeight = field('weight', p.weight);
  const fDir = vecField('direction', p.direction ? [p.direction.x, p.direction.y, p.direction.z] : null);
  const fMode = selectField('direction_mode', [['', ''], ['fixed', 'fixed'], ['isotropic', 'isotropic'], ['cone', 'cone'], ['gauss', 'gauss']],
    p.direction_mode || (p.cone_angle ? 'cone' : (p.sigma_theta || p.sigma_phi ? 'gauss' : '')));
  const fCone = field('cone_angle', p.cone_angle);
  const fSigT = field('sigma_theta', p.sigma_theta);
  const fSigP = field('sigma_phi', p.sigma_phi);
  const fPx = field('px', p.px);
  const fPy = field('py', p.py);
  const fPz = field('pz', p.pz);
  const fPol = vecField('polarization', p.polarization ? [p.polarization.x, p.polarization.y, p.polarization.z] : null);
  const fProper = field('proper_time', p.proper_time);
  const fIaea = field('iaea_code', p.iaea_code);
  const fZ = field('Z', p.Z);
  const fA = field('A', p.A);
  const fExc = field('excitation', p.excitation);
  const fMag = field('magnetic_moment', p.magnetic_moment);
  const fLife = field('lifetime', p.lifetime);
  [fParticle, fEnergy, fWeight, fDir, fMode, fCone, fSigT, fSigP, fPx, fPy, fPz, fPol, fProper, fIaea, fZ, fA, fExc, fMag, fLife].forEach((f) => parent.appendChild(f.wrap));
  parent.appendChild(note(t('source.anglesNote')));
  return () => ({
    particle: fParticle.inp.value,
    energy: fEnergy.inp.value,
    weight: fWeight.inp.value,
    direction: { x: fDir.inputs[0].value, y: fDir.inputs[1].value, z: fDir.inputs[2].value },
    direction_mode: fMode.sel.value,
    cone_angle: angDeg(fCone.inp.value),
    sigma_theta: angDeg(fSigT.inp.value),
    sigma_phi: angDeg(fSigP.inp.value),
    px: fPx.inp.value, py: fPy.inp.value, pz: fPz.inp.value,
    polarization: { x: fPol.inputs[0].value, y: fPol.inputs[1].value, z: fPol.inputs[2].value },
    proper_time: fProper.inp.value, iaea_code: fIaea.inp.value,
    Z: fZ.inp.value, A: fA.inp.value, excitation: fExc.inp.value,
    magnetic_moment: fMag.inp.value, lifetime: fLife.inp.value,
  });
}

// Advanced per-particle fields that cannot be expressed with the top-level
// particleGun sugar (particle/energy/weight/direction) and therefore require
// writing PARTICLES.0 for the engine to pick them up.
function hasAdvancedParticleFields(p) {
  const vecNonEmpty = (v) => v && typeof v === 'object' && [v.x, v.y, v.z].some((x) => String(x ?? '').trim() !== '');
  return ['direction_mode', 'cone_angle', 'sigma_theta', 'sigma_phi', 'px', 'py', 'pz',
          'proper_time', 'iaea_code', 'Z', 'A', 'excitation', 'magnetic_moment', 'lifetime']
    .some((k) => String(p[k] ?? '').trim() !== '') || vecNonEmpty(p.polarization);
}

function buildVertexGenerator(s, body) {
  body.appendChild(sectionTitle('VERTEX_GENERATOR'));
  const vg = s.VERTEX_GENERATOR || {};
  const fType = selectField('type', [[t('source.generic.none'), ''], ['line', 'line'], ['grid (TODO)', 'grid'], ['random_in_volume (TODO)', 'random_in_volume'], ['custom (TODO)', 'custom']], vg.type);
  const fCount = field('count', vg.count);
  const fPos = vecField('pos', vg.pos ? [vg.pos.x, vg.pos.y, vg.pos.z] : null);
  const fVolume = objectField('volume', vg.volume);
  const fSurface = objectField('surface', vg.surface);
  const fRadius = field('radius', vg.radius);
  const fCenter = vecField('center', vg.center ? [vg.center.x, vg.center.y, vg.center.z] : null);
  const fTime = field('time', vg.time);
  const fWeight = field('weight', vg.weight);
  const fReject = field('reject_if', vg.reject_if);
  [fType, fCount, fPos, fVolume, fSurface, fRadius, fCenter, fTime, fWeight, fReject].forEach((f) => body.appendChild(f.wrap));
  body.appendChild(note(t('source.positionNote')));

  body.appendChild(sectionTitle(t('source.particle')));
  const vgP0 = (vg.PARTICLES && vg.PARTICLES['0']) || {};
  const collectParticle = buildParticleBlock(vgP0, body);

  return () => {
    const out = {};
    if (fType.sel.value) out.type = fType.sel.value;
    if (fCount.inp.value.trim()) out.count = fCount.inp.value;
    const pv = [fPos.inputs[0].value, fPos.inputs[1].value, fPos.inputs[2].value];
    if (pv.some((x) => x.trim())) out.pos = { x: pv[0], y: pv[1], z: pv[2] };
    if (fVolume.inp.value.trim()) out.volume = fVolume.inp.value;
    if (fSurface.inp.value.trim()) out.surface = fSurface.inp.value;
    if (fRadius.inp.value.trim()) out.radius = fRadius.inp.value;
    const cv = [fCenter.inputs[0].value, fCenter.inputs[1].value, fCenter.inputs[2].value];
    if (cv.some((x) => x.trim())) out.center = { x: cv[0], y: cv[1], z: cv[2] };
    if (fTime.inp.value.trim()) out.time = fTime.inp.value;
    if (fWeight.inp.value.trim()) out.weight = fWeight.inp.value;
    if (fReject.inp.value.trim()) out.reject_if = fReject.inp.value;
    const p0 = collectParticle();
    if (hasAnyValue(p0)) out.PARTICLES = { '0': p0 };
    return out;
  };
}

function buildParticleGun(s, body) {
  const hasVG = !!(s.VERTEX_GENERATOR && Object.keys(s.VERTEX_GENERATOR).length);
  const hasV = !!(s.VERTICES && Object.keys(s.VERTICES).length);
  const fMode = selectField(t('source.mode'), [[t('source.mode.beam'), 'beam'], [t('source.mode.vertices'), 'vertices'], [t('source.mode.vg'), 'vg']], hasVG ? 'vg' : (hasV ? 'vertices' : 'beam'));
  body.appendChild(fMode.wrap);

  const beamDiv = document.createElement('div');
  const vertDiv = document.createElement('div');
  const vgDiv = document.createElement('div');
  body.appendChild(beamDiv);
  body.appendChild(vertDiv);
  body.appendChild(vgDiv);

  // ── Пучок: одна вершина + одна частица (полный блок физики частицы). ──
  const fPos = vecField('pos', s.pos ? [s.pos.x, s.pos.y, s.pos.z] : null);
  const fIso = vecField('isocenter', s.isocenter ? [s.isocenter.x, s.isocenter.y, s.isocenter.z] : null);
  const fTime = field('time', s.time);
  const fReject = field('reject_if', s.reject_if);
  const fAlias = field('data_alias', s.data_alias);
  [fIso, fPos, fTime, fReject, fAlias].forEach((f) => beamDiv.appendChild(f.wrap));
  beamDiv.appendChild(sectionTitle(t('source.particle')));
  const beamP0 = Object.assign(
    { particle: s.particle, energy: s.energy, weight: s.weight, direction: s.direction },
    (s.PARTICLES && s.PARTICLES['0']) || {}
  );
  const collectBeamParticle = buildParticleBlock(beamP0, beamDiv);

  // ── Вершины / Генератор вершин ──
  const vg = buildVertexGenerator(s, vgDiv);
  const vertices = buildVertices(s, vertDiv);

  function sync() {
    const m = fMode.sel.value;
    beamDiv.style.display = m === 'beam' ? 'block' : 'none';
    vertDiv.style.display = m === 'vertices' ? 'block' : 'none';
    vgDiv.style.display = m === 'vg' ? 'block' : 'none';
  }
  sync();
  fMode.sel.addEventListener('change', sync);

  return () => {
    const m = fMode.sel.value;
    const out = {};
    // Normalise the decimal separator (users on RU locales type "1,33");
    // ExprTK/Geant4 expect a dot, and a comma would be parsed as an operator.
    const num = (v) => String(v == null ? '' : v).trim().replace(/,/g, '.');
    if (m === 'beam') {
      const p0 = collectBeamParticle();
      out.particle = p0.particle;
      out.energy = num(p0.energy);
      out.weight = num(p0.weight);
      out.direction = { x: num(p0.direction.x), y: num(p0.direction.y), z: num(p0.direction.z) };
      out.pos = { x: num(fPos.inputs[0].value), y: num(fPos.inputs[1].value), z: num(fPos.inputs[2].value) };
      out.isocenter = { x: num(fIso.inputs[0].value), y: num(fIso.inputs[1].value), z: num(fIso.inputs[2].value) };
      out.time = fTime.inp.value;
      out.reject_if = fReject.inp.value;
      out.data_alias = fAlias.inp.value;
      // Write PARTICLES.0 (including `direction`) only when the engine needs the
      // advanced per-particle fields; simple sources stay as top-level sugar.
      out.PARTICLES = hasAdvancedParticleFields(p0) ? { '0': p0 } : {};
      out.VERTICES = {};
      out.VERTEX_GENERATOR = {};
    } else if (m === 'vertices') {
      out.particle = ''; out.energy = ''; out.direction = { x: '', y: '', z: '' };
      out.PARTICLES = {};
      out.VERTEX_GENERATOR = {};
      const vOut = vertices();
      out.VERTICES = Object.keys(vOut).length ? vOut : {};
    } else {
      out.particle = ''; out.energy = ''; out.direction = { x: '', y: '', z: '' };
      out.PARTICLES = {};
      out.VERTICES = {};
      const vgOut = vg();
      out.VERTEX_GENERATOR = Object.keys(vgOut).length ? vgOut : {};
    }
    return out;
  };
}

function buildGps(s, body) {
  const pos = s.pos || {};
  const dir = s.direction || {};
  const fParticle = particleField(t('source.particle'), s.particle || 'gamma');
  const fEnergy = field(t('source.energy'), s.energy || '1');
  const fSpectrum = selectField(t('source.spectrum'), [['Mono', 'Mono'], ['Lin', 'Lin'], ['Pow', 'Pow'], ['Exp', 'Exp'], ['Gauss', 'Gauss'], ['Arb', 'Arb'], ['User', 'User']], s.spectrum || 'Mono');
  const fMin = field(t('source.minEnergy'), s.min);
  const fMax = field(t('source.maxEnergy'), s.max);
  const fSigma = field('sigma (Gauss)', s.sigma);
  const fPos = vecField(t('source.position'), [pos.x, pos.y, pos.z]);
  const fDir = vecField(t('source.direction'), [dir.x, dir.y, dir.z]);
  const fMacro = macroField('macro', s.macro);
  [fParticle, fEnergy, fSpectrum, fMin, fMax, fSigma, fPos, fDir, fMacro].forEach((f) => body.appendChild(f.wrap));

  const genBtn = document.createElement('button');
  bindI18n(genBtn, 'tools.genMacro');
  genBtn.setAttribute('data-i18n-title', 'tip.genMacro'); genBtn.title = t('tip.genMacro');
  genBtn.style.cssText = 'background:#2a4a6a;color:#cde;border:1px solid #468;border-radius:4px;padding:4px 10px;cursor:pointer;font:12px Arial;margin-top:4px;';
  genBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const macro = buildGpsMacro(s.name, {
      particle: fParticle.inp.value.trim() || 'gamma',
      energy: fEnergy.inp.value.trim() || '1',
      spectrum: fSpectrum.sel.value || 'Mono',
      min: fMin.inp.value.trim(),
      max: fMax.inp.value.trim(),
      sigma: fSigma.inp.value.trim(),
      pos: fPos.inputs.map((i) => i.value.trim()),
      direction: fDir.inputs.map((i) => i.value.trim()),
    });
    fetch('/api/gps-macro', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: s.name, macro }),
    }).then((r) => r.json()).then((res) => {
      showToast(res && res.ok ? t('source.macroSaved') + res.error : (t('error.generic') + ((res && res.error) || '')));
      lastSig = '';
      loadSources();
    }).catch(() => { showToast(t('source.macroGenFailed')); });
  });
  body.appendChild(genBtn);
  body.appendChild(note(t('source.gpsNote')));

  return () => ({
    macro: fMacro.inp.value,
    particle: fParticle.inp.value.trim(),
    energy: fEnergy.inp.value.trim(),
    spectrum: fSpectrum.sel.value,
    min: fMin.inp.value.trim(),
    max: fMax.inp.value.trim(),
    sigma: fSigma.inp.value.trim(),
    pos: { x: fPos.inputs[0].value.trim(), y: fPos.inputs[1].value.trim(), z: fPos.inputs[2].value.trim() },
    direction: { x: fDir.inputs[0].value.trim(), y: fDir.inputs[1].value.trim(), z: fDir.inputs[2].value.trim() },
  });
}

// Build a Geant4 GPS macro from basic parameters. Exported for the toolbar
// placement flow (GPS placed by two points generates a minimal macro too).
export function buildGpsMacro(name, p) {
  const num = (v, dflt) => { const n = parseFloat(v); return Number.isFinite(n) ? n : dflt; };
  const x = num(p.pos && p.pos[0], 0);
  const y = num(p.pos && p.pos[1], 0);
  const z = num(p.pos && p.pos[2], 0);
  const d0 = p.direction && p.direction[0];
  const d1 = p.direction && p.direction[1];
  const d2 = p.direction && p.direction[2];
  const hasDir = [d0, d1, d2].some((v) => v != null && String(v).trim() !== '');
  const dx = num(d0, 0), dy = num(d1, 0), dz = num(d2, 0);
  const lines = [
    '# G4CARE GPS source: ' + name,
    '/gps/particle ' + (p.particle || 'gamma'),
    '/gps/pos/type Point',
    '/gps/pos/centre ' + x + ' ' + y + ' ' + z + ' mm',
  ];
  if (!hasDir || (dx === 0 && dy === 0 && dz === 0)) {
    lines.push('/gps/ang/type iso');
  } else {
    lines.push('/gps/ang/type beam1d');
    lines.push('/gps/direction ' + dx + ' ' + dy + ' ' + dz);
  }
  const spec = p.spectrum || 'Mono';
  const enRaw = (p.energy || '1').trim();
  const enNum = parseFloat(enRaw);
  const en = Number.isFinite(enNum) ? enNum : enRaw;
  if (spec === 'Mono') {
    lines.push('/gps/ene/type Mono');
    lines.push('/gps/ene/mono ' + en + ' MeV');
  } else if (spec === 'Arb' || spec === 'User') {
    lines.push('/gps/ene/type ' + spec);
    lines.push('# NOTE: ' + spec + ' spectrum needs arb/user histogram points — add them manually');
  } else {
    lines.push('/gps/ene/type ' + spec);
    const min = num(p.min, NaN);
    const max = num(p.max, NaN);
    const sigma = num(p.sigma, NaN);
    if (Number.isFinite(min)) lines.push('/gps/ene/min ' + min + ' MeV');
    if (Number.isFinite(max)) lines.push('/gps/ene/max ' + max + ' MeV');
    if (spec === 'Gauss' && Number.isFinite(sigma)) lines.push('/gps/ene/sigma ' + sigma + ' MeV');
  }
  return lines.join('\n') + '\n';
}

function buildRadioactive(s, body) {
  const fObject = objectField(t('source.object'), s.object);
  const fTime = field('time', s.time);
  const fWeight = field('weight', s.weight);
  const fIsomer = field('isomer_table', s.isomer_table);
  const fVertexMode = selectField('vertex_mode', [['sequential', 'sequential'], ['random', 'random']], s.vertex_mode || 'sequential');
  [fObject, fTime, fWeight, fIsomer, fVertexMode].forEach((f) => body.appendChild(f.wrap));

  body.appendChild(sectionTitle('decay'));
  const decay = s.decay || {};
  // Core defaults both decay flags to true; reflect that in the UI.
  const fPhEvap = boolField('photo_evaporation', decay.enable_photo_evaporation !== false);
  const fArm = boolField('arm', decay.enable_arm !== false);
  const fDecayMacro = macroField('macro', decay.macro);
  [fPhEvap, fArm, fDecayMacro].forEach((f) => body.appendChild(f.wrap));

  body.appendChild(sectionTitle('ISOTOPES'));
  const isoList = document.createElement('div');
  body.appendChild(isoList);
  const isoRows = [];
  function addIsoRow(iso) {
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:4px;margin:2px 0;';
    const inpName = document.createElement('input');
    inpName.type = 'text'; inpName.placeholder = 'name'; inpName.value = iso ? (iso.name || '') : '';
    inpName.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;';
    const mk = (ph) => { const i = document.createElement('input'); i.type = 'text'; i.placeholder = ph; i.style.cssText = 'width:44px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;'; return i; };
    const inpZ = mk('Z'); inpZ.value = iso && iso.Z != null ? String(iso.Z) : '';
    const inpA = mk('A'); inpA.value = iso && iso.A != null ? String(iso.A) : '';
    const inpExc = mk('exc'); inpExc.value = iso && iso.excitation != null ? String(iso.excitation) : '';
    const inpAb = mk('abund'); inpAb.value = iso && iso.abundance != null ? String(iso.abundance) : '';
    const inpLife = mk('lifet'); inpLife.value = iso && iso.lifetime != null ? String(iso.lifetime) : '';
    const rm = document.createElement('button');
    rm.textContent = '×';
    rm.style.cssText = 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 5px;flex:none;';
    rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const idx = isoRows.indexOf(row); if (idx >= 0) isoRows.splice(idx, 1); });
    row.appendChild(inpName); row.appendChild(inpZ); row.appendChild(inpA); row.appendChild(inpExc); row.appendChild(inpAb); row.appendChild(inpLife); row.appendChild(rm);
    isoList.appendChild(row);
    isoRows.push({ row, inpName, inpZ, inpA, inpExc, inpAb, inpLife });
  }
  const isoDict = s.ISOTOPES || {};
  Object.values(isoDict).forEach(addIsoRow);
  const addIso = document.createElement('button');
  bindI18n(addIso, 'tools.addIsotope');
  addIso.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;';
  addIso.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); addIsoRow(null); });
  body.appendChild(addIso);

  const collectVertices = buildRadioactiveVertices(s, body);

  return () => {
    const out = { object: fObject.inp.value, time: fTime.inp.value, weight: fWeight.inp.value, isomer_table: fIsomer.inp.value };
    if (fVertexMode.sel.value) out.vertex_mode = fVertexMode.sel.value;
    const isotopes = {};
    isoRows.forEach((r, i) => {
      if (!r.inpName.value && !r.inpZ.value && !r.inpA.value) return;
      isotopes[String(i)] = {
        name: r.inpName.value,
        Z: r.inpZ.value ? parseInt(r.inpZ.value, 10) : undefined,
        A: r.inpA.value ? parseInt(r.inpA.value, 10) : undefined,
        excitation: r.inpExc.value || undefined,
        abundance: r.inpAb.value ? parseFloat(r.inpAb.value) : undefined,
        lifetime: r.inpLife.value || undefined,
      };
    });
    if (Object.keys(isotopes).length) out.ISOTOPES = isotopes;
    const decayOut = {};
    if (fPhEvap.inp.checked) decayOut.enable_photo_evaporation = true;
    if (fArm.inp.checked) decayOut.enable_arm = true;
    if (fDecayMacro.inp.value.trim()) decayOut.macro = fDecayMacro.inp.value;
    out.decay = decayOut;
    const vOut = collectVertices();
    if (Object.keys(vOut).length) out.VERTICES = vOut;
    return out;
  };
}

function buildRadioactiveVertices(s, body) {
  body.appendChild(sectionTitle(t('source.vertices')));
  const vList = document.createElement('div');
  body.appendChild(vList);
  const vRows = [];
  function addVRow(v) {
    const pos = (v && v.position) || {};
    const row = document.createElement('div');
    row.style.cssText = 'border:1px solid #343941;border-radius:4px;padding:6px;margin:4px 0;';
    const head = document.createElement('div');
    head.style.cssText = 'display:flex;align-items:center;justify-content:space-between;margin-bottom:2px;';
    const lbl = document.createElement('span');
    bindI18n(lbl, 'tools.vertex');
    lbl.style.cssText = 'color:#9ac;font-weight:bold;font:12px Arial;';
    const rm = document.createElement('button');
    rm.textContent = '×';
    rm.setAttribute('data-i18n-title', 'tip.deleteVertex'); rm.title = t('tip.deleteVertex');
    rm.style.cssText = 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 6px;';
    rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const i = vRows.indexOf(row); if (i >= 0) vRows.splice(i, 1); });
    head.appendChild(lbl); head.appendChild(rm);
    row.appendChild(head);

    const fType = selectField(t('source.position'), [[t('source.position.expression'), ''], [t('source.position.volume'), 'volume'], [t('source.position.surface'), 'surface']], pos.type);
    const fVolume = objectField(t('source.volume'), pos.volume);
    const fPos = vecField('pos', pos.x != null ? [pos.x, pos.y, pos.z] : null);
    const fTime = field('time', v && v.time);
    const fWeight = field('weight', v && v.weight);
    [fType, fVolume, fPos, fTime, fWeight].forEach((f) => row.appendChild(f.wrap));

    vList.appendChild(row);
    vRows.push({ row, fType, fVolume, fPos, fTime, fWeight });
  }
  Object.values(s.VERTICES || {}).forEach(addVRow);
  const addV = document.createElement('button');
  bindI18n(addV, 'tools.addVertex');
  addV.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;margin-top:4px;';
  addV.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); addVRow(null); });
  body.appendChild(addV);

  return () => {
    const out = {};
    vRows.forEach((r, i) => {
      const v = {};
      const position = {};
      if (r.fType.sel.value) {
        position.type = r.fType.sel.value;
        if (r.fVolume.inp.value.trim()) position.volume = r.fVolume.inp.value.trim();
      } else {
        const pv = [r.fPos.inputs[0].value, r.fPos.inputs[1].value, r.fPos.inputs[2].value];
        if (pv.some((x) => x.trim())) { position.x = pv[0]; position.y = pv[1]; position.z = pv[2]; }
      }
      if (Object.keys(position).length) v.position = position;
      if (r.fTime.inp.value.trim()) v.time = r.fTime.inp.value;
      if (r.fWeight.inp.value.trim()) v.weight = r.fWeight.inp.value;
      if (Object.keys(v).length) out[String(i)] = v;
    });
    return out;
  };
}

function buildActivation(s, body) {
  const fData = dataField('data', s.data);
  const fMode = selectField('mode', [['sequential', 'sequential'], ['random', 'random']], s.mode);
  const fWeight = field('weight', s.weight);
  const fReset = boolField('reset_time', s.reset_time);
  const fLoop = boolField('loop', s.loop);
  [fData, fMode, fWeight, fReset, fLoop].forEach((f) => body.appendChild(f.wrap));
  body.appendChild(note(t('source.dataNote')));
  return () => ({ data: fData.inp.value, mode: fMode.sel.value, weight: fWeight.inp.value, reset_time: fReset.inp.checked, loop: fLoop.inp.checked });
}

function buildMixture(s, body) {
  const fWeight = field('weight', s.weight);
  body.appendChild(fWeight.wrap);
  body.appendChild(sectionTitle('components'));
  const compList = document.createElement('div');
  body.appendChild(compList);
  const compRows = [];
  function addCompRow(c) {
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:4px;margin:2px 0;';
    const inpSrc = document.createElement('input');
    inpSrc.type = 'text'; inpSrc.placeholder = 'source'; inpSrc.value = c ? (c.source || '') : '';
    inpSrc.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;';
    const inpW = document.createElement('input');
    inpW.type = 'text'; inpW.placeholder = 'weight'; inpW.value = c && c.weight != null ? String(c.weight) : '';
    inpW.style.cssText = 'width:60px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;';
    const rm = document.createElement('button');
    rm.textContent = '×';
    rm.style.cssText = 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 5px;flex:none;';
    rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const idx = compRows.indexOf(row); if (idx >= 0) compRows.splice(idx, 1); });
    row.appendChild(inpSrc); row.appendChild(inpW); row.appendChild(rm);
    compList.appendChild(row);
    compRows.push({ row, inpSrc, inpW });
  }
  const compDict = s.components || {};
  Object.values(compDict).forEach(addCompRow);
  const addComp = document.createElement('button');
  addComp.bindI18n(undefined, 'label.component');
  addComp.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;';
  addComp.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); addCompRow(null); });
  body.appendChild(addComp);

  return () => {
    const components = {};
    compRows.forEach((r, i) => {
      if (!r.inpSrc.value) return;
      components[String(i)] = { source: r.inpSrc.value, weight: r.inpW.value ? parseFloat(r.inpW.value) : undefined };
    });
    const out = {};
    if (fWeight.inp.value.trim()) out.weight = fWeight.inp.value;
    if (Object.keys(components).length) out.components = components;
    return out;
  };
}

function buildTable(s, body) {
  const fFile = field('file (IAEA)', s.file);
  const fMode = selectField('mode', [['random', 'random'], ['sequential', 'sequential']], s.mode || 'random');
  const fLoop = selectField('loop', [['true', 'true'], ['false', 'false']], s.loop == null ? 'true' : String(s.loop));
  body.appendChild(fFile.wrap);
  body.appendChild(fMode.wrap);
  body.appendChild(fLoop.wrap);

  body.appendChild(sectionTitle(t('source.placement')));

  const fPos = vecField(t('source.posCm'), s && s.pos ? [s.pos.x, s.pos.y, s.pos.z] : null);
  const fOffset = vecField(t('source.offsetCm'), s && s.offset ? [s.offset.x, s.offset.y, s.offset.z] : null);
  const fObject = field(t('source.objectVolume'), s.object);
  const fSurface = field(t('source.surface'), s.surface);
  [fPos, fOffset, fObject, fSurface].forEach((f) => body.appendChild(f.wrap));

  body.appendChild(sectionTitle('ExprTK ($$...$$)'));

  const fParticle = field('particle', s.particle);
  const fEnergy = field(t('source.energyMeV'), s.energy);
  const fWeight = field('weight', s.weight);
  const fReject = field('reject_if', s.reject_if);
  [fParticle, fEnergy, fWeight, fReject].forEach((f) => body.appendChild(f.wrap));

  body.appendChild(note(t('source.iaeaNote')));
  return () => {
    const out = {};
    if (fFile.inp.value.trim()) out.file = fFile.inp.value.trim();
    if (fMode.sel.value && fMode.sel.value !== 'random') out.mode = fMode.sel.value;
    if (fLoop.sel.value === 'false') out.loop = false;

    const px = fPos.inputs[0].value.trim(), py = fPos.inputs[1].value.trim(), pz = fPos.inputs[2].value.trim();
    if (px || py || pz) out.pos = { x: px, y: py, z: pz };
    const ox = fOffset.inputs[0].value.trim(), oy = fOffset.inputs[1].value.trim(), oz = fOffset.inputs[2].value.trim();
    if (ox || oy || oz) out.offset = { x: ox, y: oy, z: oz };
    if (fObject.inp.value.trim()) out.object = fObject.inp.value.trim();
    if (fSurface.inp.value.trim()) out.surface = fSurface.inp.value.trim();
    if (fParticle.inp.value.trim()) out.particle = fParticle.inp.value.trim();
    if (fEnergy.inp.value.trim()) out.energy = fEnergy.inp.value.trim();
    if (fWeight.inp.value.trim()) out.weight = fWeight.inp.value.trim();
    if (fReject.inp.value.trim()) out.reject_if = fReject.inp.value.trim();
    return out;
  };
}

function buildVertices(s, body) {
  body.appendChild(sectionTitle(t('source.vertices')));
  const vList = document.createElement('div');
  body.appendChild(vList);
  const vRows = [];
  function addVRow(v) {
    const p = (v && v.PARTICLES && v.PARTICLES['0']) || {};
    const row = document.createElement('div');
    row.style.cssText = 'border:1px solid #343941;border-radius:4px;padding:6px;margin:4px 0;';

    const head = document.createElement('div');
    head.style.cssText = 'display:flex;align-items:center;justify-content:space-between;margin-bottom:2px;';
    const lbl = document.createElement('span');
    bindI18n(lbl, 'tools.vertex');
    lbl.style.cssText = 'color:#9ac;font-weight:bold;font:12px Arial;';
    const rm = document.createElement('button');
    rm.textContent = '×';
    rm.setAttribute('data-i18n-title', 'tip.deleteVertex'); rm.title = t('tip.deleteVertex');
    rm.style.cssText = 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 6px;';
    rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const i = vRows.indexOf(row); if (i >= 0) vRows.splice(i, 1); });
    head.appendChild(lbl); head.appendChild(rm);
    row.appendChild(head);

    const fPos = vecField('pos', v && v.pos ? [v.pos.x, v.pos.y, v.pos.z] : null);
    const fTime = field('time', v && v.time);
    const fWeight = field('weight', v && v.weight);
    const fReject = field('reject_if', v && v.reject_if);
    [fPos, fTime, fWeight, fReject].forEach((f) => row.appendChild(f.wrap));
    row.appendChild(sectionTitle(t('source.particle')));
    const collectParticle = buildParticleBlock(p, row);

    vList.appendChild(row);
    vRows.push({ row, fPos, fTime, fWeight, fReject, collectParticle });
  }
  const vDict = s.VERTICES || {};
  Object.values(vDict).forEach(addVRow);
  const addV = document.createElement('button');
  bindI18n(addV, 'tools.addVertex');
  addV.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;margin-top:4px;';
  addV.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); addVRow(null); });
  body.appendChild(addV);

  return () => {
    const out = {};
    vRows.forEach((r, i) => {
      const pos = [r.fPos.inputs[0].value, r.fPos.inputs[1].value, r.fPos.inputs[2].value];
      const hasPos = pos.some((x) => x.trim());
      const p0 = r.collectParticle();
      const hasParticle = hasAnyValue(p0);
      if (!hasPos && !hasParticle) return;
      const v = {};
      if (hasPos) v.pos = { x: pos[0], y: pos[1], z: pos[2] };
      if (r.fTime.inp.value.trim()) v.time = r.fTime.inp.value;
      if (r.fWeight.inp.value.trim()) v.weight = r.fWeight.inp.value;
      if (r.fReject.inp.value.trim()) v.reject_if = r.fReject.inp.value;
      if (hasParticle) v.PARTICLES = { '0': p0 };
      out[String(i)] = v;
    });
    return out;
  };
}
