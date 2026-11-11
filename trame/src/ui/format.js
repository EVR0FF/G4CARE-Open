//==============================================================================
// G4CARE - format.js — Formatting helpers
//
// Formatting helpers
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

export function rgbCss(c) {
  return 'rgb(' + Math.round(c[0] * 255) + ',' + Math.round(c[1] * 255) + ',' + Math.round(c[2] * 255) + ')';
}

export function fmtNum(v) {
  if (typeof v !== 'number') return v;
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a >= 1e4 || a < 1e-3) return v.toExponential(3);
  if (a >= 100) return v.toFixed(1);
  if (a >= 1) return v.toFixed(2);
  return v.toFixed(4);
}

export function fmtVec(v) {
  if (!v || !v.length) return '—';
  return v.map(fmtNum).join(', ');
}
