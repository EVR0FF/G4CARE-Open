//==============================================================================
// G4CARE - objects.js — Objects panel
//
// Objects panel
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkClipClosedSurface from '@kitware/vtk.js/Filters/General/ClipClosedSurface';
import { renderWindow } from '../context.js';
import { view, objectMeta, objectInfo, actors, actorNameMap, baseColors, mergedMesh, moveNotifier, objectMoveHook } from '../state.js';
import { postUpdate, postGdml } from '../api.js';
import { orientationQuat } from '../math3d.js';
import { makeSource, Z_AXIS_SHAPES } from '../render/sources.js';
import { buildPointCloud, buildSpherePointCloud, getSphereRadius } from '../render/mesh.js';
import { highlightColorFor } from '../render/picking.js';

let setMeshCellColor = () => {};
let updateTreeUI = () => {};
let updateProps = () => {};
let updateLayerUI = () => {};
let updateActorMappers = () => {};
export function setObjectsHooks(h) {
  setMeshCellColor = h.setMeshCellColor;
  updateTreeUI = h.updateTreeUI;
  updateProps = h.updateProps;
  updateLayerUI = h.updateLayerUI;
  updateActorMappers = h.updateActorMappers;
}

export const PRIMITIVE_TYPES = [
  ['primitive.box', 'box'], ['primitive.sphere', 'sphere'], ['primitive.orb', 'orb'], ['primitive.cylinder', 'cylinder'],
  ['primitive.tube', 'tube'], ['primitive.tubs', 'tubs'], ['primitive.cone', 'cone'], ['primitive.torus', 'torus'], ['primitive.para', 'para'],
];

export function isGdmlObject(name) {
  const meta = objectMeta.get(name);
  return !!(meta && meta.source && meta.source.kind === 'gdml');
}

export function isReplicaObject(name) {
  const meta = objectMeta.get(name);
  return !!(meta && meta.source && meta.source.kind === 'replica');
}

export function setObjectPosition(name, pos) {
  if (isReplicaObject(name)) return; // array copies are read-only
  const meta = objectMeta.get(name);
  if (meta) meta.pos = pos;
  const info = objectInfo.get(name);
  if (info) info.pos = pos;
  if (isGdmlObject(name)) {
    // GDML volumes live in the merged mesh (baked world coords); edit the
    // <physvol> placement in the GDML file and let the viewer reload.
    postGdml({ op: 'set_position', name, value: pos });
  } else {
    const a = actors.get(name);
    if (a) a.setPosition(pos[0], pos[1], pos[2]);
    postUpdate({ name, position: pos });
    renderWindow.render();
    updateActorMappers(); // re-apply section clip planes with the new transform
  }
  for (const fn of moveNotifier.fns) fn('object', name);
  for (const fn of objectMoveHook.fns) fn(name);
}

export function setObjectRotation(name, rot) {
  if (isReplicaObject(name)) return; // array copies are read-only
  const meta = objectMeta.get(name);
  if (meta) meta.rot = rot;
  if (isGdmlObject(name)) {
    postGdml({ op: 'set_rotation', name, value: rot });
  } else {
    const a = actors.get(name);
    if (a) {
      const baseX = (meta && Z_AXIS_SHAPES.has(meta.shape)) ? 90 : 0;
      a.setOrientationFromQuaternion(orientationQuat(rot, baseX));
    }
    postUpdate({ name, rotation: rot });
    renderWindow.render();
    updateActorMappers(); // re-apply section clip planes with the new transform
  }
  for (const fn of moveNotifier.fns) fn('object', name);
  for (const fn of objectMoveHook.fns) fn(name);
}

export function setObjectMaterial(name, mat) {
  const meta = objectMeta.get(name);
  if (meta) meta.material = mat;
  postUpdate({ name, material: mat });
}

export function setObjectMother(name, mother) {
  const meta = objectMeta.get(name);
  if (meta) meta.mother = mother || null;
  postUpdate({ name, mother: mother || null });
  updateTreeUI();
  updateProps();
}

export function setObjectName(oldName, newName) {
  newName = (newName || '').trim();
  if (!newName || newName === oldName || objectMeta.has(newName)) return;
  const meta = objectMeta.get(oldName);
  if (!meta) return;
  objectMeta.delete(oldName);
  objectMeta.set(newName, meta);
  const a = actors.get(oldName);
  if (a) { actors.delete(oldName); actors.set(newName, a); actorNameMap.set(a, newName); }
  const c = baseColors.get(oldName);
  if (c) { baseColors.delete(oldName); baseColors.set(newName, c); }
  const info = objectInfo.get(oldName);
  if (info) { objectInfo.delete(oldName); objectInfo.set(newName, info); }
  if (view.selectedName === oldName) view.selectedName = newName;
  postUpdate({ name: oldName, newName });
  updateTreeUI();
  updateProps();
}

export function setObjectType(name, shape) {
  const meta = objectMeta.get(name);
  const info = objectInfo.get(name);
  const a = actors.get(name);
  if (!meta || !info || !a) return;
  meta.shape = shape;
  const src = makeSource(shape, meta.dims);
  info.source = src;
  const clipFilter = vtkClipClosedSurface.newInstance({ clippingPlanes: [] });
  clipFilter.setInputConnection(src.getOutputPort());
  info.clipFilter = clipFilter;
  info.pointCloud = (shape === 'sphere' || shape === 'orb')
    ? buildSpherePointCloud(getSphereRadius(meta.dims), 0.1)
    : buildPointCloud(src.getOutputData(), 0.1);
  info.rotated = Z_AXIS_SHAPES.has(shape);
  const baseX = Z_AXIS_SHAPES.has(shape) ? 90 : 0;
  a.setOrientationFromQuaternion(orientationQuat(meta.rot || [0, 0, 0], baseX));
  postUpdate({ name, shape });
  updateActorMappers();
  updateProps();
}

export function setObjectSensitive(name, val) {
  const meta = objectMeta.get(name);
  if (meta) meta.isSensitive = val;
  postUpdate({ name, isSensitive: val });
  updateProps();
}

export function setObjectLayer(name, layer) {
  const meta = objectMeta.get(name);
  if (meta) meta.layer = layer;
  postUpdate({ name, layer });
  updateLayerUI();
  updateTreeUI();
}

export function setObjectColor(name, rgb) {
  const meta = objectMeta.get(name);
  if (meta) meta.color = rgb;
  baseColors.set(name, rgb);
  if (mergedMesh.nameToCells.has(name)) {
    const cells = mergedMesh.nameToCells.get(name);
    for (const ci of cells) {
      mergedMesh.baseCellColors[ci * 4] = rgb[0];
      mergedMesh.baseCellColors[ci * 4 + 1] = rgb[1];
      mergedMesh.baseCellColors[ci * 4 + 2] = rgb[2];
    }
    setMeshCellColor(name, highlightColorFor(name));
  } else {
    const a = actors.get(name);
    if (a) a.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
  }
  postUpdate({ name, color: rgb });
  renderWindow.render();
}

export function setObjectOpacity(name, o) {
  const meta = objectMeta.get(name);
  if (meta) meta.opacity = o;
  if (mergedMesh.nameToCells.has(name)) {
    const cells = mergedMesh.nameToCells.get(name);
    for (const ci of cells) mergedMesh.baseCellColors[ci * 4 + 3] = o;
    setMeshCellColor(name, highlightColorFor(name));
    postUpdate({ name, opacity: o });
    renderWindow.render();
  } else {
    const a = actors.get(name);
    if (a) { a.getProperty().setOpacity(o); postUpdate({ name, opacity: o }); renderWindow.render(); }
  }
}
