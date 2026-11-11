//==============================================================================
// G4CARE - annotations.js — Annotations tool
//
// Annotations tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - annotation labels (callouts), persisted in YAML ANNOTATIONS.
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkCellPicker from '@kitware/vtk.js/Rendering/Core/CellPicker';
import { renderer, renderWindow, fullScreen } from '../context.js';
import { view, annotations, annotationActors, annotationLabels, annotationMarkerMap } from '../state.js';
import { makeMarkerActor, removeMarker, computeSceneBounds } from '../render/actors.js';
import { applyHighlight } from '../render/picking.js';
import { rgbCss } from '../ui/format.js';
import { btnStyle, promptText } from '../ui/dom.js';

let addGroup = null;
let setActiveMode = () => {};
let resolvePoint = () => null;
let updateTreeUI = () => {};
let updateProps = () => {};
let displayCoord = () => [0, 0];

let labelBtn = null;
let lastAnnSig = '';
let annPostTimer = null;
let annMenu = null;

const annotationPicker = vtkCellPicker.newInstance();
annotationPicker.setPickFromList(true);

export function setupAnnotations(h) {
  addGroup = h.addGroup;
  setActiveMode = h.setActiveMode;
  resolvePoint = h.resolvePoint;
  updateTreeUI = h.updateTreeUI;
  updateProps = h.updateProps;
  displayCoord = h.displayCoord;

  const labelGroup = addGroup('group.labels');
  labelBtn = document.createElement('button');
  bindI18n(labelBtn, 'tools.label');
  labelBtn.style.cssText = btnStyle;
  labelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleLabelMode(); });
  labelGroup.appendChild(labelBtn);
}

export function toggleLabelMode() {
  view.labelMode = !view.labelMode;
  labelBtn.style.background = view.labelMode ? '#b45f06' : '#3a3a3a';
  labelBtn.style.color = '#fff';
  if (view.labelMode) setActiveMode('select');
}

export function annotationOffset() {
  const b = computeSceneBounds();
  const d = b ? Math.max(b[1]-b[0], b[3]-b[2], b[5]-b[4]) : 20;
  return d * 0.06;
}

export function clearAnnotationActors() {
  for (const a of annotationActors) removeMarker(a);
  annotationActors.length = 0;
  for (const l of annotationLabels) l.el.remove();
  annotationLabels.length = 0;
  annotationMarkerMap.clear();
}

export function renderAnnotations() {
  clearAnnotationActors();
  const off = annotationOffset();
  annotations.forEach((ann, i) => {
    const pos = ann.position || [0, 0, 0];
    const col = (ann.color && ann.color.length >= 3) ? ann.color : [1, 0.9, 0.2];
    const mk = makeMarkerActor(col[0], col[1], col[2]);
    mk.setPosition(pos[0], pos[1], pos[2]);
    mk.setPickable(true);
    annotationMarkerMap.set(mk, i);
    annotationActors.push(mk);
    const tip = [pos[0], pos[1], pos[2] + off];
    const ls = vtkLineSource.newInstance();
    ls.setPoint1(pos[0], pos[1], pos[2]);
    ls.setPoint2(tip[0], tip[1], tip[2]);
    const lm = vtkMapper.newInstance();
    lm.setInputConnection(ls.getOutputPort());
    const la = vtkActor.newInstance();
    la.setMapper(lm);
    la.getProperty().setColor(col[0], col[1], col[2]);
    la.getProperty().setLineWidth(2);
    la.setPickable(false);
    renderer.addActor(la);
    annotationActors.push(la);
    const el = document.createElement('div');
    el.textContent = ann.text || '';
    el.style.cssText = 'position:absolute;pointer-events:none;color:' + rgbCss(col) + ';font:bold 12px Arial;text-shadow:0 0 3px #000;transform:translate(-50%,-100%);white-space:nowrap;';
    fullScreen.getContainer().appendChild(el);
    annotationLabels.push({ el, pos: tip });
  });
  updateAnnotationLabels();
  renderWindow.render();
}

export function updateAnnotationLabels() {
  const c = fullScreen.getContainer().querySelector('canvas');
  const cont = fullScreen.getContainer();
  if (!c || !cont.clientWidth) return;
  const dprX = c.width / cont.clientWidth;
  const dprY = c.height / cont.clientHeight;
  for (const item of annotationLabels) {
    const d = displayCoord(renderer, item.pos);
    item.el.style.left = (d[0] / dprX) + 'px';
    item.el.style.top = ((c.height - d[1]) / dprY) + 'px';
  }
}

export function placeLabelPoint(x, y) {
  const p = resolvePoint(x, y);
  if (!p) return;
  promptText(t('ann.text'), '', (text) => {
    if (!text) return;
    annotations.push({ id: 'a' + Date.now(), position: p.slice(), text, color: [1, 0.9, 0.2], layer: '', owner: 'user' });
    saveAnnotations();
    renderAnnotations();
    updateTreeUI();
  });
}

export function saveAnnotations() {
  clearTimeout(annPostTimer);
  annPostTimer = setTimeout(() => {
    fetch('/api/annotations', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ annotations }) }).catch(() => {});
  }, 400);
}

export function deleteAnnotation(i) {
  if (i < 0 || i >= annotations.length) return;
  annotations.splice(i, 1);
  if (view.selectedAnnotationIndex === i) view.selectedAnnotationIndex = -1;
  else if (view.selectedAnnotationIndex > i) view.selectedAnnotationIndex--;
  saveAnnotations();
  renderAnnotations();
  updateTreeUI();
  updateProps();
}

export function pickAnnotation(x, y) {
  if (!annotationMarkerMap.size) return -1;
  annotationPicker.initializePickList();
  for (const mk of annotationMarkerMap.keys()) annotationPicker.addPickList(mk);
  annotationPicker.pick([x, y, 0], renderer);
  const picked = annotationPicker.getActors();
  if (picked.length) {
    const idx = annotationMarkerMap.get(picked[0]);
    if (idx !== undefined) return idx;
  }
  return -1;
}

export function closeAnnMenu() {
  if (annMenu) { annMenu.remove(); annMenu = null; }
}

export function showAnnotationMenu(cx, cy, idx) {
  closeAnnMenu();
  const menu = document.createElement('div');
  menu.style.cssText = 'position:fixed;left:' + cx + 'px;top:' + cy + 'px;background:#26262b;border:1px solid #343941;border-radius:5px;padding:4px;z-index:9999;box-shadow:0 4px 12px rgba(0,0,0,.5);';
  const mk = (label, fn, danger) => {
    const b = document.createElement('button');
    b.textContent = label;
    b.style.cssText = 'display:block;width:100%;text-align:left;background:transparent;color:' + (danger ? '#f0b0b0' : '#ddd') + ';border:0;border-radius:3px;padding:5px 10px;font:12px Arial;cursor:pointer;';
    b.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); });
    b.addEventListener('click', () => { closeAnnMenu(); fn(); });
    menu.appendChild(b);
  };
  mk(t('ctx.properties'), () => {
    view.selectedName = null;
    view.selectedPlaneIndex = -1;
    view.selectedAuxIndex = -1;
    view.selectedPrimIndex = -1;
    view.selectedAnnotationIndex = idx;
    applyHighlight();
    updateProps();
    updateTreeUI();
  });
  mk(t('ctx.delete'), () => deleteAnnotation(idx), true);
  document.body.appendChild(menu);
  annMenu = menu;
  const onDown = (e) => { if (!menu.contains(e.target)) closeAnnMenu(); };
  setTimeout(() => document.addEventListener('pointerdown', onDown, { once: true }), 0);
}

export function loadAnnotations() {
  fetch('/annotations.json').then((r) => r.json()).then((list) => {
    if (!Array.isArray(list)) return;
    const sig = JSON.stringify(list);
    if (sig === lastAnnSig) return;
    lastAnnSig = sig;
    annotations.splice(0, annotations.length, ...list);
    renderAnnotations();
    updateTreeUI();
  }).catch(() => {});
}
