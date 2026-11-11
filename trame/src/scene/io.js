//==============================================================================
// G4CARE - io.js — Scene I/O
//
// Scene I/O
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkClipClosedSurface from '@kitware/vtk.js/Filters/General/ClipClosedSurface';
import { renderer, renderWindow } from '../context.js';
import { actors, actorNameMap, baseColors, objectInfo, objectMeta, mergedMesh, gridState, view, section, groups } from '../state.js';
import { makeSource, Z_AXIS_SHAPES, DEFAULT_COLOR } from '../render/sources.js';
import { orientationQuat } from '../math3d.js';
import { buildMeshPolyData, buildPointCloud, buildSpherePointCloud, getSphereRadius } from '../render/mesh.js';
import { removeAllActors, computeSceneBounds, rebuildGrid } from '../render/actors.js';

// viewer.js supplies these UI/rendering functions at init (avoids a circular import).
let clearClientSceneState = () => {};
let rebuildMergedMesh = () => {};
let updateAxisLabels = () => {};
let updateActorMappers = () => {};
let updateTreeUI = () => {};
let updateLayerUI = () => {};
let applyHighlight = () => {};
let updateProps = () => {};
let axesWidget = null;

export function setSceneHooks(h) {
  clearClientSceneState = h.clearClientSceneState;
  rebuildMergedMesh = h.rebuildMergedMesh;
  updateAxisLabels = h.updateAxisLabels;
  updateActorMappers = h.updateActorMappers;
  updateTreeUI = h.updateTreeUI;
  updateLayerUI = h.updateLayerUI;
  applyHighlight = h.applyHighlight;
  updateProps = h.updateProps;
  axesWidget = h.axesWidget;
}

let lastGeometrySig = '';
let lastExternal = null;
let lastSettings = '';

export async function loadGeometry() {
  try {
    const res = await fetch('/geometry.json');
    const data = await res.json();
    const objs = Array.isArray(data) ? data : (data.objects || []);
    const meshes = (data && Array.isArray(data.meshes)) ? data.meshes : [];
    const signature = JSON.stringify(objs) + '|' + meshes
      .map((m) => (m.name || '') + ':' + ((m.vertices || []).length) + ':' + ((m.triangles || []).length)).join(';');
    if (signature === view.lastSignature) { updateLayerUI(); return; } // nothing changed → keep camera
    view.lastSignature = signature;
    if (!Array.isArray(data)) view.globalLayers = data.layers || [];

    const prevSelected = view.selectedName;
    const prevNames = new Set([...objectMeta.keys()]);
    const newNames = new Set([...objs.map((o) => o.name), ...meshes.map((m) => m.name)].filter(Boolean));
    // Client-only state (primitives, aux points, planes, dimensions) must survive
    // property edits of the SAME scene (e.g. moving an object). Only clear it
    // when the object set actually changed (a different YAML/scene).
    const sameScene = prevNames.size === newNames.size && [...prevNames].every((n) => newNames.has(n));
    removeAllActors();
    objectMeta.clear();
    if (!sameScene) clearClientSceneState();
    view.selectedName = null;
    view.hoveredName = null;
    view.selectedPlaneIndex = -1;
    view.selectedAuxIndex = -1;
    view.selectedPrimIndex = -1;
    view.selectedAnnotationIndex = -1;
    for (const obj of objs) {
      const src = makeSource(obj.shape, obj.dims);
      const rotated = Z_AXIS_SHAPES.has(obj.shape);
      const pos = obj.pos || [0, 0, 0];
      const clipFilter = vtkClipClosedSurface.newInstance({ clippingPlanes: [] });
      clipFilter.setInputConnection(src.getOutputPort());
      const mapper = vtkMapper.newInstance();
      mapper.setInputConnection(src.getOutputPort());
      const actor = vtkActor.newInstance();
      actor.setMapper(mapper);
      actor.setPosition(...(obj.pos || [0, 0, 0]));
      const rot = Array.isArray(obj.rotation) ? obj.rotation.map(Number).slice(0, 3)
        : (Array.isArray(obj.rot) ? obj.rot.map(Number).slice(0, 3) : [0, 0, 0]);
      const baseX = Z_AXIS_SHAPES.has(obj.shape) ? 90 : 0;
      actor.setOrientationFromQuaternion(orientationQuat(rot, baseX));
      const baseColor = obj.color && obj.color.length >= 3 ? obj.color : DEFAULT_COLOR;
      baseColors.set(obj.name, baseColor);
      actor.getProperty().setColor(...baseColor);
      if (obj.opacity !== null && obj.opacity !== undefined) {
        actor.getProperty().setOpacity(obj.opacity);
      }
      if (obj.visible === false) {
        actor.setVisibility(false);
      }
      actor.getProperty().setEdgeVisibility(true);
      actor.getProperty().setEdgeColor(0.1, 0.1, 0.1);
      renderer.addActor(actor);
      actors.set(obj.name, actor);
      actorNameMap.set(actor, obj.name);
      const pointCloud = (obj.shape === 'sphere' || obj.shape === 'orb')
        ? buildSpherePointCloud(getSphereRadius(obj.dims), 0.1)
        : buildPointCloud(src.getOutputData(), 0.1);
      objectInfo.set(obj.name, { source: src, clipFilter, pointCloud, rotated, pos });
      objectMeta.set(obj.name, { shape: obj.shape, dims: obj.dims, pos: obj.pos, material: obj.material, mother: obj.mother, isSensitive: obj.isSensitive, layer: obj.layer, color: obj.color, opacity: obj.opacity, visible: obj.visible, rot, source: obj.source || { kind: 'primitive' } });
    }
    // GDML meshes: store metadata + per-volume polydata, then merge into ONE actor.
    mergedMesh.defs = [];
    for (const m of meshes) {
      if (!m || !m.vertices || !m.triangles) continue;
      const pd = buildMeshPolyData(m.vertices, m.triangles);
      const src = { getOutputData: () => pd };
      const clipFilter = vtkClipClosedSurface.newInstance({ clippingPlanes: [] });
      clipFilter.setInputData(pd);
      const pointCloud = buildPointCloud(pd, 0.1);
      objectInfo.set(m.name, { source: src, clipFilter, pointCloud, rotated: false, pos: [0, 0, 0], isMesh: true });
      objectMeta.set(m.name, { shape: 'mesh', dims: [], pos: m.pos || [0, 0, 0], material: m.material || null, mother: m.mother || null, volume: m.volume || null, isSensitive: false, layer: null, color: m.color || null, opacity: m.opacity != null ? m.opacity : 1, visible: m.visible !== false, rot: [0, 0, 0], source: m.source || { kind: 'gdml' } });
      baseColors.set(m.name, m.color && m.color.length >= 3 ? m.color : DEFAULT_COLOR);
      mergedMesh.defs.push({ name: m.name, vertices: m.vertices, triangles: m.triangles, opacity: m.opacity });
    }
    rebuildMergedMesh();
    if (!sameScene) renderer.resetCamera(); // keep the camera during object edits/drags
    const b = computeSceneBounds();
    if (b) {
      gridState.halfSize = Math.max(Math.abs(b[0]), Math.abs(b[1]), Math.abs(b[2]), Math.abs(b[3]), Math.abs(b[4]), Math.abs(b[5])) + 50;
    }
    rebuildGrid();
    renderWindow.render();
    updateAxisLabels();
    updateActorMappers();
    updateTreeUI();
    updateLayerUI();
    if (prevSelected && objectMeta.has(prevSelected)) view.selectedName = prevSelected;
    applyHighlight();
    updateProps();
  } catch (e) {
    console.error('loadGeometry', e);
  }
}

export async function loadGroups() {
  try {
    const res = await fetch('/groups.json');
    const data = await res.json();
    groups.clear();
    groups.phantom = null;
    if (data && typeof data === 'object' && !Array.isArray(data)) {
      for (const [name, g] of Object.entries(data)) {
        if (name.startsWith('@')) continue; // reserved keys (phantom label etc.)
        groups.set(name, { parent: (g && g.parent) || null, children: Array.isArray(g && g.children) ? g.children : [] });
      }
      // Expose reserved phantom settings too.
      groups.phantom = data['@phantom'] || null;
    }
    updateTreeUI();
  } catch (e) {
    console.error('loadGroups', e);
  }
}

export async function pollGeometrySig() {
  try {
    const res = await fetch('/geometry.sig');
    const sig = await res.text();
    if (sig !== lastGeometrySig) {
      lastGeometrySig = sig;
      loadGeometry(); // full fetch + rebuild only when the scene actually changed
    } else {
      updateLayerUI();
    }
  } catch (e) {
    // If /geometry.sig is unavailable, fall back to the full poll.
    loadGeometry();
  }
}

export async function pollSideChannels() {
  try {
    const res = await fetch('/highlight.json');
    const data = await res.json();
    const name = data.name || null;
    if (name !== lastExternal) {
      lastExternal = name;
      if (name) { view.selectedName = name; applyHighlight(); }
    }
  } catch (e) { /* ignore */ }

  try {
    const res = await fetch('/settings.json');
    const data = await res.json();
    const sig = JSON.stringify(data);
    if (sig !== lastSettings) {
      lastSettings = sig;
      const g = (data && data.grid) || {};
      gridState.show = !!g.show;
      gridState.spacing = parseFloat(g.spacing) || 10;
      gridState.snap = !!g.snap;
      rebuildGrid();
      const asize = data && data.axes_size ? parseFloat(data.axes_size) : 20;
      axesWidget.setViewportSize(Math.max(0.08, Math.min(0.5, asize / 100)));
      updateAxisLabels();
    }
  } catch (e) { /* ignore */ }
}
