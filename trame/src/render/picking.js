//==============================================================================
// G4CARE - picking.js — Picking
//
// Picking
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkCellPicker from '@kitware/vtk.js/Rendering/Core/CellPicker';
import { renderer, renderWindow } from '../context.js';
import { actors, actorNameMap, baseColors, objectMeta, mergedMesh, highlighted, view, planeActorMap, primActorMap, section, multiSel } from '../state.js';
import { HOVER_COLOR, SELECT_COLOR, CUT_SELECT_COLOR, LOCAL_TARGET_COLOR, DEFAULT_COLOR } from './sources.js';
import { postUpdate } from '../api.js';

// viewer.js supplies these mesh-color/cut/rebuild functions at init.
let setMeshCellColor = () => {};
let buildMergedCut = () => {};
let rebuildMergedMesh = () => {};
export function setPickingHooks(h) {
  setMeshCellColor = h.setMeshCellColor;
  buildMergedCut = h.buildMergedCut;
  rebuildMergedMesh = h.rebuildMergedMesh;
}

const picker = vtkCellPicker.newInstance();

export function pickAt(x, y) {
  picker.pick([x, y, 0], renderer);
  const picked = picker.getActors();
  if (picked.length) {
    const actor = picked[0];
    if (actor === mergedMesh.actor) {
      const cellId = picker.getCellId();
      const nameList = (section.enabled && mergedMesh.cutPd) ? mergedMesh.cutCellName : mergedMesh.cellName;
      const name = nameList[cellId];
      if (name) return { name, pos: picker.getPickedPositions()[0] };
      return null;
    }
    const name = actorNameMap.get(actor);
    if (name) return { name, pos: picker.getPickedPositions()[0] };
    const primIndex = primActorMap.get(actor);
    if (primIndex !== undefined) return { primIndex, pos: picker.getPickedPositions()[0] };
    const planeIndex = planeActorMap.get(actor);
    if (planeIndex !== undefined) return { planeIndex, pos: picker.getPickedPositions()[0] };
  }
  return null;
}

export function highlightColorFor(name) {
  if (section.scope === 'local' && name === section.localTarget) return LOCAL_TARGET_COLOR;
  if (section.selective && section.cutSet.has(name)) return CUT_SELECT_COLOR;
  if (name === view.selectedName || multiSel.has(name)) return SELECT_COLOR;
  if (name === view.hoveredName) return HOVER_COLOR;
  return null; // base color
}

export function setColorFor(name, highlightColor) {
  if (mergedMesh.nameToCells.has(name)) setMeshCellColor(name, highlightColor);
  else {
    const a = actors.get(name);
    if (a) a.getProperty().setColor(...(highlightColor || baseColors.get(name) || DEFAULT_COLOR));
  }
}

export function setVolumeVisible(name, visible) {
  setVolumesVisible([name], visible);
}

export function setVolumesVisible(names, visible) {
  let touchedMesh = false;
  for (const name of names) {
    const meta = objectMeta.get(name);
    if (!meta) continue;
    meta.visible = visible;
    if (mergedMesh.defs.some((d) => d.name === name)) touchedMesh = true;
    else {
      const a = actors.get(name);
      if (a) a.setVisibility(visible);
    }
    // Persist visibility so a geometry reload (array change, constraint, move)
    // does not reset hidden volumes back to visible.
    postUpdate({ name, visible });
  }
  if (touchedMesh) {
    rebuildMergedMesh();
    applyHighlight();
  }
  renderWindow.render();
}

export function applyHighlight() {
  const names = new Set();
  if (section.scope === 'local' && section.localTarget) names.add(section.localTarget);
  if (section.selective) for (const n of section.cutSet) names.add(n);
  if (view.selectedName) names.add(view.selectedName);
  for (const n of multiSel) if (objectMeta.has(n)) names.add(n);
  if (view.hoveredName) names.add(view.hoveredName);
  for (const n of [...highlighted]) {
    if (!names.has(n)) { setColorFor(n, null); highlighted.delete(n); }
  }
  for (const n of names) {
    setColorFor(n, highlightColorFor(n));
    highlighted.add(n);
  }
  if (section.enabled && mergedMesh.actor) {
    buildMergedCut();
    if (mergedMesh.cutPd && view.repMode !== 3) mergedMesh.mapper.setInputData(mergedMesh.cutPd);
  }
  renderWindow.render();
}
