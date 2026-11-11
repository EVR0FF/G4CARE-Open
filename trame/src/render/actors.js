//==============================================================================
// G4CARE - actors.js — VTK actors
//
// VTK actors
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkSphereSource from '@kitware/vtk.js/Filters/Sources/SphereSource';
import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import { renderer, canvasSize } from '../context.js';
import { gridState, actors, actorNameMap, highlighted, mergedMesh } from '../state.js';

// Screen-space markers (2D "sprites": constant pixel size at any zoom).
const screenMarkers = new Set();

export function makeMarkerActor(r, g, b) {
  const s = vtkSphereSource.newInstance({ radius: 1, thetaResolution: 16, phiResolution: 16 });
  const m = vtkMapper.newInstance();
  m.setInputConnection(s.getOutputPort());
  const a = vtkActor.newInstance();
  a.setMapper(m);
  a.getProperty().setColor(r, g, b);
  a.getProperty().setLighting(false);
  a.setPickable(false);
  renderer.addActor(a);
  screenMarkers.add(a);
  return a;
}

export function removeMarker(actor) {
  screenMarkers.delete(actor);
  renderer.removeActor(actor);
}

// Pickable screen-space marker (for scene objects like sources).
export function makePickableMarker(r, g, b) {
  const s = vtkSphereSource.newInstance({ radius: 1, thetaResolution: 16, phiResolution: 16 });
  const m = vtkMapper.newInstance();
  m.setInputConnection(s.getOutputPort());
  const a = vtkActor.newInstance();
  a.setMapper(m);
  a.getProperty().setColor(r, g, b);
  a.getProperty().setLighting(false);
  a.setPickable(true);
  renderer.addActor(a);
  screenMarkers.add(a);
  return a;
}

export function updateMarkerScales() {
  if (!screenMarkers.size) return;
  const cam = renderer.getActiveCamera();
  const cp = cam.getPosition();
  const vh = 2 * Math.tan((cam.getViewAngle() * Math.PI) / 360);
  const ch = canvasSize()[1] || 1;
  for (const actor of screenMarkers) {
    const p = actor.getPosition();
    const dist = Math.max(1e-9, Math.hypot(p[0] - cp[0], p[1] - cp[1], p[2] - cp[2]));
    const ws = (8 * vh * dist) / ch;
    actor.setScale(ws, ws, ws);
  }
}

export function makeGridActor(halfSize, spacing) {
  const pd = vtkPolyData.newInstance();
  const points = pd.getPoints();
  const lines = pd.getLines();
  const n = Math.max(1, Math.floor(halfSize / Math.max(spacing, 1e-9)));
  const pts = [];
  const cells = [];
  let pid = 0;
  for (let i = -n; i <= n; i++) {
    const v = i * spacing;
    pts.push([v, -halfSize, 0], [v, halfSize, 0]);
    cells.push([pid, pid + 1]); pid += 2;
    pts.push([-halfSize, v, 0], [halfSize, v, 0]);
    cells.push([pid, pid + 1]); pid += 2;
  }
  points.setNumberOfPoints(pts.length, 3);
  pts.forEach((p, i) => points.setPoint(i, p[0], p[1], p[2]));
  cells.forEach((c) => lines.insertNextCell(c));
  const mapper = vtkMapper.newInstance();
  mapper.setInputData(pd);
  const actor = vtkActor.newInstance();
  actor.setMapper(mapper);
  actor.getProperty().setColor(0.42, 0.42, 0.46);
  return actor;
}

export function rebuildGrid() {
  if (gridState.actor) renderer.removeActor(gridState.actor);
  gridState.actor = makeGridActor(gridState.halfSize, gridState.spacing);
  gridState.actor.setVisibility(gridState.show);
  renderer.addActor(gridState.actor);
}

export function removeAllActors() {
  for (const actor of actors.values()) renderer.removeActor(actor);
  actors.clear();
  actorNameMap.clear();
  highlighted.clear();
  if (mergedMesh.actor) { renderer.removeActor(mergedMesh.actor); }
  mergedMesh.actor = null; mergedMesh.mapper = null; mergedMesh.pd = null;
  mergedMesh.clipFilter = null; mergedMesh.pointCloud = null;
  mergedMesh.cutPd = null; mergedMesh.cutCellName = [];
  mergedMesh.cellName = []; mergedMesh.nameToCells.clear();
  mergedMesh.cellColors = null; mergedMesh.baseCellColors = null;
  mergedMesh.defs = [];
  if (gridState.actor) { renderer.removeActor(gridState.actor); gridState.actor = null; }
}

export function computeSceneBounds() {
  let b = null;
  const all = [...actors.values()];
  if (mergedMesh.actor) all.push(mergedMesh.actor);
  for (const actor of all) {
    const ab = actor.getBounds();
    if (!b) b = [...ab];
    else {
      for (let i = 0; i < 3; i++) {
        b[2 * i] = Math.min(b[2 * i], ab[2 * i]);
        b[2 * i + 1] = Math.max(b[2 * i + 1], ab[2 * i + 1]);
      }
    }
  }
  return b;
}
