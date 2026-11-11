//==============================================================================
// G4CARE - view.js — View tool
//
// View tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkAxesActor from '@kitware/vtk.js/Rendering/Core/AxesActor';
import vtkOrientationMarkerWidget from '@kitware/vtk.js/Interaction/Widgets/OrientationMarkerWidget';
import vtkCoordinate from '@kitware/vtk.js/Rendering/Core/Coordinate';
import { fullScreen, renderer, renderWindow, interactor, canvasSize } from '../context.js';
import { view, gridState } from '../state.js';
import { updateMarkerScales } from '../render/actors.js';
import { updateActorMappers } from './section.js';
import { updateMeasureLabel } from './measure.js';
import { updateAnnotationLabels } from './annotations.js';

const AXIS_COLORS = [[255, 0, 0], [0, 255, 0], [0, 0, 255]];

export const axesActor = vtkAxesActor.newInstance({ config: { recenter: false, shaftRadius: 0.04, tipRadius: 0.12, tipLength: 0.25 } });
axesActor.setXAxisColor(AXIS_COLORS[0]);
axesActor.setYAxisColor(AXIS_COLORS[1]);
axesActor.setZAxisColor(AXIS_COLORS[2]);

export const axesWidget = vtkOrientationMarkerWidget.newInstance({
  actor: axesActor,
  interactor,
  viewportCorner: vtkOrientationMarkerWidget.Corners.BOTTOM_LEFT,
  viewportSize: 0.2,
  interactiveRenderer: false, // marker follows the view; click-to-orient below
});
axesWidget.setEnabled(true);

export function displayCoord(ren, pt) {
  const coord = vtkCoordinate.newInstance();
  coord.setCoordinateSystemToWorld();
  coord.setValue(pt[0], pt[1], pt[2]);
  return coord.getComputedDisplayValue(ren);
}

export function isInWidget(x, y) {
  const vp = axesWidget.computeViewport();
  if (!vp) return false;
  const [w, h] = canvasSize();
  return x / w >= vp[0] && x / w <= vp[2] && y / h >= vp[1] && y / h <= vp[3];
}

// Which axis arrow (+X/+Y/+Z) is nearest the display point (x, y)? null if none.
export function axisAt(x, y) {
  const wren = axesWidget.getRenderer();
  const origin = displayCoord(wren, [0, 0, 0]);
  const dirs = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  let best = null;
  let bestDot = 0.5; // within ~60° of the arrow direction
  for (let a = 0; a < 3; a++) {
    const tip = displayCoord(wren, dirs[a]);
    const sx = tip[0] - origin[0];
    const sy = tip[1] - origin[1];
    const norm = Math.hypot(sx, sy);
    if (norm < 1e-6) continue;
    const dx = x - origin[0];
    const dy = y - origin[1];
    const dnorm = Math.hypot(dx, dy);
    if (dnorm < 1e-6) continue;
    const dot = (dx * sx + dy * sy) / (norm * dnorm);
    if (dot > bestDot) { bestDot = dot; best = a; }
  }
  return best;
}

export function orientView(axis) {
  const cam = renderer.getActiveCamera();
  const focal = [...cam.getFocalPoint()];
  const dist = cam.getDistance();
  const pos = [...focal];
  pos[axis] = focal[axis] + dist;
  cam.setPosition(...pos);
  cam.setFocalPoint(...focal);
  cam.setViewUp(axis === 2 ? [0, 1, 0] : [0, 0, 1]);
  renderer.resetCameraClippingRange();
  renderWindow.render();
  updateAxisLabels();
}

// Hover highlight of the orientation axes.
let hoveredAxis = null;
export function setHoveredAxis(axis) {
  if (axis === hoveredAxis) return;
  hoveredAxis = axis;
  const hl = [255, 255, 100];
  axesActor.setXAxisColor(axis === 0 ? hl : AXIS_COLORS[0]);
  axesActor.setYAxisColor(axis === 1 ? hl : AXIS_COLORS[1]);
  axesActor.setZAxisColor(axis === 2 ? hl : AXIS_COLORS[2]);
}

export function applyRepresentation(mode) {
  view.repMode = mode;
  updateActorMappers();
}

export function setViewFrom(dir) {
  const cam = renderer.getActiveCamera();
  const focal = [...cam.getFocalPoint()];
  const dist = cam.getDistance();
  const norm = Math.hypot(dir[0], dir[1], dir[2]) || 1;
  const ux = dir[0] / norm, uy = dir[1] / norm, uz = dir[2] / norm;
  cam.setPosition(focal[0] + ux * dist, focal[1] + uy * dist, focal[2] + uz * dist);
  cam.setFocalPoint(...focal);
  cam.setViewUp(uz > 0.99 ? [0, 1, 0] : [0, 0, 1]);
  renderer.resetCameraClippingRange();
  renderWindow.render();
  updateAxisLabels();
}

export function fitView() {
  const gridActor = gridState.actor;
  const wasVisible = gridActor ? gridActor.getVisibility() : false;
  if (gridActor) gridActor.setVisibility(false);
  renderer.resetCamera();
  if (gridActor) gridActor.setVisibility(wasVisible);
  updateMarkerScales();
  renderWindow.render();
}

export function setCursor(c) {
  fullScreen.getContainer().style.cursor = c;
}

export function projectToGridPlane(x, y) {
  const coord = vtkCoordinate.newInstance();
  coord.setCoordinateSystemToDisplay();
  coord.setValue(x, y, 0);
  const near = coord.getComputedWorldValue(renderer);
  coord.setValue(x, y, 1);
  const far = coord.getComputedWorldValue(renderer);
  const dz = far[2] - near[2];
  if (Math.abs(dz) < 1e-9) return null;
  const t = (0 - near[2]) / dz;
  return [near[0] + t * (far[0] - near[0]), near[1] + t * (far[1] - near[1]), 0];
}

// X/Y/Z labels (DOM overlay above the canvas)
const labelEls = ['X', 'Y', 'Z'].map((t) => {
  const el = document.createElement('div');
  el.textContent = t;
  el.style.cssText = 'position:absolute;pointer-events:none;font:bold 14px Arial;transform:translate(-50%,-50%);';
  fullScreen.getContainer().appendChild(el);
  return el;
});
labelEls[0].style.color = '#ff8a8a';
labelEls[1].style.color = '#8aff8a';
labelEls[2].style.color = '#8ab4ff';

export function updateAxisLabels() {
  updateMarkerScales();
  const wren = axesWidget.getRenderer();
  const container = fullScreen.getContainer();
  const c = container.querySelector('canvas');
  if (!c || !container.clientWidth) return;
  const dprX = c.width / container.clientWidth;
  const dprY = c.height / container.clientHeight;
  const tips = [[1.5, 0, 0], [0, 1.5, 0], [0, 0, 1.5]];
  for (let i = 0; i < 3; i++) {
    const d = displayCoord(wren, tips[i]);
    const left = Math.max(4, Math.min(container.clientWidth - 20, d[0] / dprX));
    const top = Math.max(4, Math.min(container.clientHeight - 20, (c.height - d[1]) / dprY));
    labelEls[i].style.left = `${left}px`;
    labelEls[i].style.top = `${top}px`;
  }
  updateMeasureLabel();
  updateAnnotationLabels();
}
