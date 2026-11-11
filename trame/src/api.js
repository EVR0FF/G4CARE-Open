//==============================================================================
// G4CARE - api.js — Server API client
//
// Server API client
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

let pendingUpdates = new Map(); // name (or '__global') -> patch
let postTimer = null;

export function postUpdate(patch) {
  const key = (patch && patch.name) || '__global';
  pendingUpdates.set(key, Object.assign(pendingUpdates.get(key) || {}, patch));
  clearTimeout(postTimer);
  postTimer = setTimeout(() => {
    const ops = [...pendingUpdates.values()];
    pendingUpdates.clear();
    fetch('/api/batch', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ops }),
    }).catch(() => {});
  }, 400);
}

// Batch operations (duplicate/delete/...): applied atomically by the geometry
// server via a single POST. Returns the parsed response (or null on error).
export function postBatch(ops) {
  return fetch('/api/batch', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ops }),
  })
    .then((r) => r.json())
    .catch(() => null);
}

// Single GDML edit (physvol position/rotation/duplicate/delete). The geometry
// server edits GEOMETRY.FILE directly — placement only, no re-tessellation.
export function postGdml(op) {
  return fetch('/api/gdml', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(op),
  })
    .then((r) => r.json())
    .catch(() => null);
}

export function dbg(msg) {
  try { fetch('/debug?' + encodeURIComponent(msg)).catch(() => {}); } catch (e) { /* ignore */ }
}
