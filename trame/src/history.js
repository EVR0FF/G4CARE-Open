//==============================================================================
// G4CARE - history.js — Undo/redo history
//
// Undo/redo history
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { renderWindow, renderer } from './context.js';
import { snap, objectMeta, actors, baseColors, objectInfo, prims, primActorMap } from './state.js';
import { DEFAULT_COLOR } from './render/sources.js';
import { postBatch } from './api.js';

let refreshAux = () => {};
let refreshHighlight = () => {};
let refreshPrims = () => {};
export function setHistoryHooks(aux, highlight, primsHook) {
  refreshAux = aux;
  refreshHighlight = highlight;
  if (typeof primsHook === 'function') refreshPrims = primsHook;
}

export const history = { undo: [], redo: [] };

export function pushCommand(cmd) {
  history.undo.push(cmd);
  if (history.undo.length > 200) history.undo.shift();
  history.redo = [];
}

export function captureSceneState() {
  const s = {
    auxPoints: snap.auxPoints.map((p) => p.slice()),
    auxMeta: snap.auxMeta.map((m) => ({ ...m })),
    objects: {},
    prims: prims.map((p) => ({
      type: p.type,
      name: p.name,
      layer: p.layer,
      pts: p.pts.map((pt) => [...pt]),
      normal: p.normal ? [...p.normal] : undefined,
      n: p.n,
      visible: p.visible,
      color: p.color ? [...p.color] : undefined,
      spacing: p.spacing,
      halfSize: p.halfSize,
      parent: p.parent ? { name: p.parent.name, localPts: p.parent.localPts ? p.parent.localPts.map((pt) => [...pt]) : undefined, localNormal: p.parent.localNormal ? [...p.parent.localNormal] : undefined } : null,
    })),
  };
  for (const [name, meta] of objectMeta) {
    const a = actors.get(name);
    s.objects[name] = {
      pos: meta.pos ? meta.pos.slice() : null,
      rot: meta.rot ? meta.rot.slice() : null,
      color: meta.color ? meta.color.slice() : null,
      renderColor: (baseColors.get(name) || DEFAULT_COLOR).slice(),
      opacity: meta.opacity,
      material: meta.material,
      actorPos: a ? a.getPosition().slice() : null,
    };
  }
  return s;
}

export function restoreSceneState(s) {
  snap.auxPoints = (s.auxPoints || []).map((p) => p.slice());
  snap.auxMeta = (s.auxMeta || []).map((m) => ({ ...m }));
  refreshAux();

  // Primitives (client-only 2D sketch objects). Remove existing actors first so
  // no "ghost" geometry is left behind when the array is replaced.
  for (const p of prims) {
    if (p.actor) { renderer.removeActor(p.actor); primActorMap.delete(p.actor); }
  }
  prims.length = 0;
  for (const p of (s.prims || [])) {
    prims.push({
      type: p.type,
      name: p.name,
      layer: p.layer,
      pts: p.pts.map((pt) => [...pt]),
      normal: p.normal ? [...p.normal] : undefined,
      n: p.n,
      visible: p.visible,
      color: p.color ? [...p.color] : undefined,
      spacing: p.spacing,
      halfSize: p.halfSize,
      actor: null,
      parent: p.parent ? { name: p.parent.name, localPts: p.parent.localPts ? p.parent.localPts.map((pt) => [...pt]) : undefined, localNormal: p.parent.localNormal ? [...p.parent.localNormal] : undefined } : null,
    });
  }
  refreshPrims();

  // Objects: restore in-memory state and re-post to the backend so YAML stays
  // consistent (this fixes the old "undo only moved it in memory" bug). Only
  // objects whose captured state differs from the current one are re-posted, so
  // an undo of an unrelated action does not rewrite the whole YAML.
  const ops = [];
  for (const [name, st] of Object.entries(s.objects)) {
    const meta = objectMeta.get(name);
    if (!meta) continue;
    const a = actors.get(name);
    const info = objectInfo.get(name);
    const isGdml = meta.source && meta.source.kind === 'gdml';
    // Pre-restore values, for diffing what actually changed before we overwrite.
    const prevPos = meta.pos;
    const prevRot = meta.rot;
    const prevMaterial = meta.material;
    const prevColor = meta.color;
    const prevOpacity = meta.opacity;

    if (st.pos) meta.pos = st.pos.slice();
    if (st.rot) meta.rot = st.rot.slice();
    meta.color = st.color ? st.color.slice() : null;
    if (st.renderColor) baseColors.set(name, st.renderColor.slice());
    if (st.opacity !== undefined) meta.opacity = st.opacity;
    if (st.material !== undefined) meta.material = st.material;
    if (info && st.pos) info.pos = st.pos.slice();
    if (a && st.actorPos) a.setPosition(st.actorPos[0], st.actorPos[1], st.actorPos[2]);
    if (a) a.getProperty().setOpacity(st.opacity !== undefined && st.opacity !== null ? st.opacity : 1);

    if (isGdml) continue;
    const op = { name };
    if (st.pos && !arrEq(st.pos, prevPos)) op.position = st.pos.slice();
    if (st.rot && !arrEq(st.rot, prevRot)) op.rotation = st.rot.slice();
    if (st.material !== undefined && st.material !== null && st.material !== prevMaterial) op.material = st.material;
    if (st.color && !arrEq(st.color, prevColor)) op.color = st.color.slice();
    if (st.opacity !== undefined && st.opacity !== null && st.opacity !== prevOpacity) op.opacity = st.opacity;
    if (Object.keys(op).length > 1) ops.push(op);
  }
  if (ops.length) postBatch(ops);

  refreshHighlight();
  renderWindow.render();
}

// Snapshot-style entry (legacy callers): capture "before" now and "after"
// lazily on the first undo.
export function pushHistory() {
  pushSnapshot(captureSceneState());
}

// Snapshot entry with an explicit "before" state (captured earlier, e.g. at the
// start of a drag so intermediate mouse-move mutations are not recorded).
export function pushSnapshot(before) {
  let after = null;
  pushCommand({
    label: 'scene',
    undo() { after = captureSceneState(); restoreSceneState(before); },
    redo() { restoreSceneState(after || before); },
  });
}

export function globalUndo() {
  if (!history.undo.length) return;
  const cmd = history.undo.pop();
  history.redo.push(cmd);
  try { cmd.undo(); } catch (err) { dbgErr('undo', err); }
}

export function globalRedo() {
  if (!history.redo.length) return;
  const cmd = history.redo.pop();
  history.undo.push(cmd);
  try { cmd.redo(); } catch (err) { dbgErr('redo', err); }
}

function dbgErr(kind, err) {
  try { fetch('/debug?' + encodeURIComponent('HISTORY_' + kind.toUpperCase() + ' ' + (err && err.message ? err.message : String(err)))).catch(() => {}); } catch (_) { /* ignore */ }
}

function arrEq(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

