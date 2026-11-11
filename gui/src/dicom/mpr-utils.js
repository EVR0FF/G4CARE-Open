//==============================================================================
// G4CARE - mpr-utils.js — Pure MPR helpers (contours, CSS, decode) — no vtk.js
//
// Pure MPR helpers (contours, CSS, decode) — no vtk.js
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { t } from '../i18n/i18n.js';

export const VIEWS = {
  // The DICOM volume is stored in raw LPS order: ix=+X(Left), iy=+Y(Posterior),
  // iz=+Z(Superior). vtk.js draws i left→right and j bottom→top, so axial shows
  // R-left already (correct); only its vertical axis is inverted (anterior would
  // be at the bottom) and needs flipV. Coronal/sagittal are already correct.
  axial: { get label() { return t('label.axialFull'); }, k: 2, i: 0, j: 1, flipH: false, flipV: true },
  coronal: { get label() { return t('label.coronalFull'); }, k: 1, i: 0, j: 2, flipH: false, flipV: false },
  sagittal: { get label() { return t('label.sagittalFull'); }, k: 0, i: 1, j: 2, flipH: false, flipV: false },
};

export const DEFAULT_WINDOW = 400;
export const DEFAULT_LEVEL = 40;

let cssInjected = false;


// Marching squares on a binary mask (ni x nj). Returns segments as a flat array
// [x0,y0, x1,y1, ...] of boundary line segments at the 0/1 threshold.
export function marchingSquares(mask, ni, nj) {
  const segs = [];
  const EDGES = [
    [0.5, 0], [1, 0.5], [0.5, 1], [0, 0.5],  // top, right, bottom, left (offsets)
  ];
  const TABLE = [
    [],            // 0
    [3, 2],        // 1
    [2, 1],        // 2
    [3, 1],        // 3
    [1, 0],        // 4
    [3, 0, 2, 1],  // 5 (saddle)
    [2, 0],        // 6
    [3, 0],        // 7
    [0, 3],        // 8
    [0, 2],        // 9
    [0, 1, 2, 3],  // 10 (saddle)
    [0, 1],        // 11
    [3, 1],        // 12
    [1, 2],        // 13
    [3, 2],        // 14
    [],            // 15
  ];
  for (let j = 0; j < nj - 1; j++) {
    for (let i = 0; i < ni - 1; i++) {
      const a = mask[j * ni + i] ? 1 : 0;
      const b = mask[j * ni + i + 1] ? 1 : 0;
      const c = mask[(j + 1) * ni + i + 1] ? 1 : 0;
      const d = mask[(j + 1) * ni + i] ? 1 : 0;
      const idx = (a << 3) | (b << 2) | (c << 1) | d;
      const edges = TABLE[idx];
      for (let k = 0; k < edges.length; k += 2) {
        const e0 = EDGES[edges[k]];
        const e1 = EDGES[edges[k + 1]];
        segs.push(i + e0[0], j + e0[1], i + e1[0], j + e1[1]);
      }
    }
  }
  return segs;
}

// Generalized marching squares on a scalar field `values` (ni x nj) with linear
// interpolation at the `level` iso-value. Returns a flat array of line segments
// [x0,y0, x1,y1, ...] in (possibly fractional) voxel coordinates.
export function marchingSquaresIsoline(values, ni, nj, level) {
  const segs = [];
  const TABLE = [
    [],            // 0
    [3, 2],        // 1
    [2, 1],        // 2
    [3, 1],        // 3
    [1, 0],        // 4
    [3, 0, 2, 1],  // 5 (saddle)
    [2, 0],        // 6
    [3, 0],        // 7
    [0, 3],        // 8
    [0, 2],        // 9
    [0, 1, 2, 3],  // 10 (saddle)
    [0, 1],        // 11
    [3, 1],        // 12
    [1, 2],        // 13
    [3, 2],        // 14
    [],            // 15
  ];
  // Corner layout: a=(i,j) bottom-left, b=(i+1,j) bottom-right,
  // c=(i+1,j+1) top-right, d=(i,j+1) top-left.
  const interp = (edge, i, j, va, vb, vc, vd) => {
    let t, x, y;
    switch (edge) {
      case 0: { const d = vb - va; t = d !== 0 ? (level - va) / d : 0.5; x = i + t; y = j; break; }
      case 1: { const d = vc - vb; t = d !== 0 ? (level - vb) / d : 0.5; x = i + 1; y = j + t; break; }
      case 2: { const d = vd - vc; t = d !== 0 ? (level - vc) / d : 0.5; x = i + (1 - t); y = j + 1; break; }
      default: { const d = va - vd; t = d !== 0 ? (level - vd) / d : 0.5; x = i; y = j + (1 - t); break; }
    }
    return [x, y];
  };
  for (let j = 0; j < nj - 1; j++) {
    for (let i = 0; i < ni - 1; i++) {
      const va = values[j * ni + i];
      const vb = values[j * ni + i + 1];
      const vc = values[(j + 1) * ni + i + 1];
      const vd = values[(j + 1) * ni + i];
      const a = va >= level ? 1 : 0;
      const b = vb >= level ? 1 : 0;
      const c = vc >= level ? 1 : 0;
      const d = vd >= level ? 1 : 0;
      const idx = (a << 3) | (b << 2) | (c << 1) | d;
      const edges = TABLE[idx];
      for (let k = 0; k < edges.length; k += 2) {
        const p0 = interp(edges[k], i, j, va, vb, vc, vd);
        const p1 = interp(edges[k + 1], i, j, va, vb, vc, vd);
        segs.push(p0[0], p0[1], p1[0], p1[1]);
      }
    }
  }
  return segs;
}


export function injectCss() {
  if (cssInjected) return;
  cssInjected = true;
  const style = document.createElement('style');
  style.textContent = `
    .mpr-root { display:flex; flex-direction:column; width:100%; height:100%; min-height:0; }
    .mpr-grid { flex:1; min-height:0; display:grid; grid-template-columns:1fr 1fr; grid-template-rows:1fr 1fr; gap:4px; padding:4px; box-sizing:border-box; }
    .mpr-pane { display:flex; flex-direction:column; min-height:0; min-width:0; background:#0a0a0c; border:1px solid #2a2a2a; overflow:hidden; }
    .mpr-pane-head { display:flex; justify-content:space-between; align-items:center; padding:2px 8px; background:#141416; color:#ccc; font:11px sans-serif; border-bottom:1px solid #2a2a2a; flex:0 0 auto; }
    .mpr-pane-info { color:#777; font:10px monospace; }
    .mpr-canvas { flex:1; min-height:0; position:relative; }
    .mpr-canvas canvas { position:absolute; inset:0; width:100%; height:100%; }
    .mpr-placeholder { display:flex; align-items:center; justify-content:center; color:#555; text-align:center; font:12px sans-serif; line-height:1.6; }
    .mpr-placeholder span { color:#444; }
    .mpr-marker { position:absolute; color:#ffe066; font:bold 12px sans-serif; text-shadow:0 0 2px #000; pointer-events:none; z-index:3; }
    .mpr-root:not(.mpr-loaded) .mpr-marker,
    .mpr-root:not(.mpr-loaded) .mpr-ov,
    .mpr-root:not(.mpr-loaded) .mpr-layers { display:none; }
    .mpr-mk-top { top:4px; left:50%; transform:translateX(-50%); }
    .mpr-mk-bottom { bottom:4px; left:50%; transform:translateX(-50%); }
    .mpr-mk-left { left:5px; top:50%; transform:translateY(-50%); }
    .mpr-mk-right { right:5px; top:50%; transform:translateY(-50%); }
    .mpr-ov { position:absolute; color:#d6ff9a; font:10px sans-serif; line-height:1.35; text-shadow:0 0 2px #000, 0 0 2px #000; pointer-events:none; white-space:pre; z-index:3; }
    .mpr-ov-tl { top:4px; left:6px; text-align:left; }
    .mpr-ov-tr { top:4px; right:6px; text-align:right; }
    .mpr-ov-bl { bottom:4px; left:6px; text-align:left; }
    .mpr-ov-br { bottom:4px; right:6px; text-align:right; }
    .mpr-layers { position:absolute; right:8px; bottom:60px; background:rgba(20,20,24,0.7); color:#ccc; border:1px solid #444; border-radius:3px; cursor:pointer; padding:2px 6px; font:12px sans-serif; z-index:3; }
  `;
  document.head.appendChild(style);
}

/** Decode the engine's base64 little-endian voxel buffer into a typed array. */
export function decodeVolume(base64, valueType) {
  const bin = atob(base64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return (valueType === 'float32') ? new Float32Array(bytes.buffer) : new Int16Array(bytes.buffer);
}

/** Decode an int16 volume (kept for backward compatibility). */
export function decodeInt16Volume(base64) {
  return decodeVolume(base64, 'int16');
}