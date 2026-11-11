//==============================================================================
// G4CARE - mesh-merged.js — Merged mesh rendering
//
// Merged mesh rendering
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import vtkDataArray from '@kitware/vtk.js/Common/Core/DataArray';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import { renderer, renderWindow } from '../context.js';
import { mergedMesh, objectMeta, objectInfo, baseColors, section } from '../state.js';
import { DEFAULT_COLOR } from './sources.js';
import { buildPointCloud, addMeshNormals } from './mesh.js';
import { highlightColorFor } from './picking.js';

// Callbacks registered by viewer.js to avoid a mesh-merged <-> section cycle.
let sectionHooks = { updateSectionPlanes: null, updateActorMappers: null };
export function setMergedMeshHooks(hooks) {
  sectionHooks = { ...sectionHooks, ...hooks };
}

export function buildMergedPolydata(meshList) {
  const mergedVerts = [];
  const mergedTris = [];
  const cellName = [];
  const nameToCells = new Map();
  for (const m of meshList) {
    const vBase = mergedVerts.length / 3;
    for (let i = 0; i < m.vertices.length; i++) mergedVerts.push(m.vertices[i]);
    const cells = [];
    for (let i = 0; i + 2 < m.triangles.length; i += 3) {
      mergedTris.push(m.triangles[i] + vBase, m.triangles[i + 1] + vBase, m.triangles[i + 2] + vBase);
      cells.push((mergedTris.length / 3) - 1);
    }
    // NOTE: do NOT use cellName.push(...cells.map(() => m.name)) here — for a
    // large mesh (e.g. a 100k+ triangle DICOM transfer) the spread blows the
    // JS call stack ("RangeError: Maximum call stack size exceeded"), which
    // then aborts loadGeometry() and leaves the whole scene empty.
    for (let i = 0; i < cells.length; i++) cellName.push(m.name);
    nameToCells.set(m.name, cells);
  }
  const nCells = mergedTris.length / 3;
  const cellColors = new Float32Array(nCells * 4);
  const baseCellColors = new Float32Array(nCells * 4);
  for (let i = 0; i < nCells; i++) {
    const nm = cellName[i];
    const meta = objectMeta.get(nm);
    const bc = baseColors.get(nm) || DEFAULT_COLOR;
    const op = meta && meta.opacity != null ? meta.opacity : 1;
    cellColors[i * 4] = bc[0]; cellColors[i * 4 + 1] = bc[1]; cellColors[i * 4 + 2] = bc[2]; cellColors[i * 4 + 3] = op;
    baseCellColors[i * 4] = bc[0]; baseCellColors[i * 4 + 1] = bc[1]; baseCellColors[i * 4 + 2] = bc[2]; baseCellColors[i * 4 + 3] = op;
  }
  const pd = vtkPolyData.newInstance();
  const pts = pd.getPoints();
  pts.setNumberOfPoints(mergedVerts.length / 3, 3);
  for (let i = 0; i < mergedVerts.length / 3; i++) {
    pts.setPoint(i, mergedVerts[i * 3], mergedVerts[i * 3 + 1], mergedVerts[i * 3 + 2]);
  }
  const polys = pd.getPolys();
  for (let i = 0; i + 2 < mergedTris.length; i += 3) {
    polys.insertNextCell([mergedTris[i], mergedTris[i + 1], mergedTris[i + 2]]);
  }
  addMeshNormals(pd, mergedTris);
  return { pd, cellName, nameToCells, cellColors, baseCellColors };
}

export function buildMergedCut() {
  if (!mergedMesh.pd) { mergedMesh.cutPd = null; mergedMesh.cutCellName = []; return; }
  const verts = [], tris = [], cellName = [];
  for (const [name, info] of objectInfo) {
    if (!info.isMesh) continue;
    const meta = objectMeta.get(name);
    if (meta && meta.visible === false) continue;
    const clipped = info.clipFilter.getOutputData();
    const pts = clipped.getPoints();
    const nc = clipped.getPolys().getData();
    if (!pts || !nc || nc.length < 4) continue;
    const vBase = verts.length / 3;
    for (let i = 0; i < pts.getNumberOfPoints(); i++) {
      const p = pts.getPoint(i);
      verts.push(p[0], p[1], p[2]);
    }
    let c = 0;
    while (c < nc.length) {
      const n = nc[c];
      const ids = [];
      for (let i = 0; i < n; i++) ids.push(nc[c + 1 + i] + vBase);
      if (n === 3) { tris.push(ids[0], ids[1], ids[2]); cellName.push(name); }
      else if (n === 4) {
        tris.push(ids[0], ids[1], ids[2]); cellName.push(name);
        tris.push(ids[0], ids[2], ids[3]); cellName.push(name);
      }
      c += n + 1;
    }
  }
  if (!verts.length) { mergedMesh.cutPd = null; mergedMesh.cutCellName = []; return; }
  const pd = vtkPolyData.newInstance();
  const pts2 = pd.getPoints();
  pts2.setNumberOfPoints(verts.length / 3, 3);
  for (let i = 0; i < verts.length / 3; i++) pts2.setPoint(i, verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
  const polys = pd.getPolys();
  for (let i = 0; i + 2 < tris.length; i += 3) polys.insertNextCell([tris[i], tris[i + 1], tris[i + 2]]);
  addMeshNormals(pd, tris);
  const nCells = tris.length / 3;
  const colors = new Float32Array(nCells * 4);
  for (let i = 0; i < nCells; i++) {
    const nm = cellName[i];
    const meta = objectMeta.get(nm);
    const bc = baseColors.get(nm) || DEFAULT_COLOR;
    const hl = highlightColorFor(nm);
    const op = meta && meta.opacity != null ? meta.opacity : 1;
    const rgb = hl || bc;
    colors[i * 4] = rgb[0]; colors[i * 4 + 1] = rgb[1]; colors[i * 4 + 2] = rgb[2]; colors[i * 4 + 3] = op;
  }
  pd.getCellData().setScalars(vtkDataArray.newInstance({ numberOfComponents: 4, values: colors }));
  mergedMesh.cutPd = pd;
  mergedMesh.cutCellName = cellName;
}

export function rebuildMergedMesh() {
  if (mergedMesh.actor) { renderer.removeActor(mergedMesh.actor); }
  mergedMesh.actor = null; mergedMesh.mapper = null; mergedMesh.pd = null;
  mergedMesh.clipFilter = null; mergedMesh.pointCloud = null;
  mergedMesh.cutPd = null; mergedMesh.cutCellName = [];
  mergedMesh.cellName = []; mergedMesh.nameToCells.clear();
  mergedMesh.cellColors = null; mergedMesh.baseCellColors = null;
  const visible = mergedMesh.defs.filter((d) => {
    const meta = objectMeta.get(d.name);
    return !meta || meta.visible !== false;
  });
  if (!visible.length) { renderWindow.render(); return; }
  const built = buildMergedPolydata(visible);
  mergedMesh.pd = built.pd;
  mergedMesh.cellName = built.cellName;
  mergedMesh.nameToCells = built.nameToCells;
  mergedMesh.cellColors = built.cellColors;
  mergedMesh.baseCellColors = built.baseCellColors;
  const mapper = vtkMapper.newInstance();
  mapper.setInputData(built.pd);
  const actor = vtkActor.newInstance();
  actor.setMapper(mapper);
  actor.getProperty().setEdgeVisibility(false);
  renderer.addActor(actor);
  mergedMesh.actor = actor;
  mergedMesh.mapper = mapper;
  mergedMesh.pointCloud = buildPointCloud(built.pd, 0.1);
  applyMeshCellColors();
  if (sectionHooks.updateSectionPlanes) sectionHooks.updateSectionPlanes();
  if (section.enabled && section.scope === 'global') buildMergedCut();
  if (sectionHooks.updateActorMappers) sectionHooks.updateActorMappers();
  renderWindow.render();
}

export function applyMeshCellColors() {
  if (!mergedMesh.pd || !mergedMesh.cellColors) return;
  const arr = vtkDataArray.newInstance({ numberOfComponents: 4, values: mergedMesh.cellColors.slice() });
  mergedMesh.pd.getCellData().setScalars(arr);
  mergedMesh.mapper.setScalarModeToUseCellData();
  mergedMesh.mapper.setColorModeToDirectScalars();
  mergedMesh.mapper.modified();
}

export function setMeshCellColor(name, highlightColor) {
  const cells = mergedMesh.nameToCells.get(name);
  if (!cells || !cells.length) return;
  const meta = objectMeta.get(name);
  const cc = mergedMesh.cellColors;
  const bc = mergedMesh.baseCellColors;
  const base = cells[0] * 4;
  let r, g, b, a;
  if (meta && meta.visible === false) { r = 0; g = 0; b = 0; a = 0; }
  else if (highlightColor) {
    r = highlightColor[0]; g = highlightColor[1]; b = highlightColor[2];
    a = meta && meta.opacity != null ? meta.opacity : 1;
  } else {
    r = bc[base]; g = bc[base + 1]; b = bc[base + 2]; a = bc[base + 3];
  }
  for (const ci of cells) {
    cc[ci * 4] = r; cc[ci * 4 + 1] = g; cc[ci * 4 + 2] = b; cc[ci * 4 + 3] = a;
  }
  applyMeshCellColors();
}
