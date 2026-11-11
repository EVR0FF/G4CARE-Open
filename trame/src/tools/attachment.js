//==============================================================================
// G4CARE - attachment.js — Attachment tool
//
// Attachment tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { objectMeta } from '../state.js';
import { orientationQuat, quatRotate, quatInverse } from '../math3d.js';
import { Z_AXIS_SHAPES } from '../render/sources.js';

export function objectPos(name) {
  const meta = objectMeta.get(name);
  return meta && meta.pos ? meta.pos : [0, 0, 0];
}

export function objectOrientationQuat(name) {
  const meta = objectMeta.get(name);
  if (!meta) return [0, 0, 0, 1];
  const rot = meta.rot || [0, 0, 0];
  const baseX = Z_AXIS_SHAPES.has(meta.shape) ? 90 : 0;
  return orientationQuat(rot, baseX);
}

// World direction of the object's axis (the local Z axis of bodies of
// revolution). vtk.js cylinder/cone sources are built along Y and rotated by
// +90° about X (baseX), so the revolution axis is the LOCAL Y direction of the
// vtk source — not [0,0,1]. Using [0,0,1] here made coaxial/angle constraints
// use the wrong axis for cylinder/tube/cone objects.
export function objectAxisDir(name) {
  const meta = objectMeta.get(name);
  if (!meta) return [0, 0, 1];
  const localAxis = Z_AXIS_SHAPES.has(meta.shape) ? [0, 1, 0] : [0, 0, 1];
  return quatRotate(localAxis, objectOrientationQuat(name));
}

// World point -> local point in the object's frame.
export function worldToObj(name, wp) {
  const pos = objectPos(name);
  const Q = objectOrientationQuat(name);
  const rel = [wp[0] - pos[0], wp[1] - pos[1], wp[2] - pos[2]];
  return quatRotate(rel, quatInverse(Q));
}

// Local point -> world point (object frame -> world).
export function objToWorld(name, lp) {
  const pos = objectPos(name);
  const Q = objectOrientationQuat(name);
  const rel = quatRotate(lp, Q);
  return [pos[0] + rel[0], pos[1] + rel[1], pos[2] + rel[2]];
}

// World direction -> local direction (rotation only, no translation).
export function worldDirToObj(name, d) {
  const Q = objectOrientationQuat(name);
  return quatRotate(d, quatInverse(Q));
}

// Local direction -> world direction.
export function objDirToWorld(name, d) {
  const Q = objectOrientationQuat(name);
  return quatRotate(d, Q);
}
