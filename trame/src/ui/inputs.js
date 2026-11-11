//==============================================================================
// G4CARE - inputs.js — Input widgets
//
// Input widgets
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { pushHistory } from '../history.js';

export function checkboxInput(label, val, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const btn = document.createElement('button');
  btn.textContent = val ? '☑' : '☐';
  btn.title = val ? t('ui.off') : t('ui.on');
  btn.style.cssText = 'background:none;border:1px solid #343941;border-radius:3px;color:' + (val ? '#8f8' : '#888') + ';cursor:pointer;font:13px Arial;padding:0 7px;';
  btn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); pushHistory(); onChange(!val); });
  wrap.appendChild(lbl);
  wrap.appendChild(btn);
  return wrap;
}

export function selectInput(label, options, val, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const sel = document.createElement('select');
  sel.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;max-width:150px;';
  options.forEach(([olabel, value]) => { const o = document.createElement('option'); o.value = value; o.textContent = olabel; sel.appendChild(o); });
  sel.value = val;
  sel.addEventListener('change', () => { pushHistory(); onChange(sel.value); });
  wrap.appendChild(lbl);
  wrap.appendChild(sel);
  return wrap;
}

export function textInput(label, val, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = val || '';
  inp.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;';
  inp.addEventListener('change', () => { pushHistory(); onChange(inp.value); });
  wrap.appendChild(lbl);
  wrap.appendChild(inp);
  return wrap;
}

export function vec3Input(label, vec, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:4px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  wrap.appendChild(lbl);
  const cur = vec.slice();
  ['X', 'Y', 'Z'].forEach((axis, i) => {
    const box = document.createElement('span');
    box.style.cssText = 'display:flex;align-items:center;gap:2px;';
    const al = document.createElement('span');
    al.textContent = axis;
    al.style.cssText = 'color:#777;width:10px;';
    const inp = document.createElement('input');
    inp.type = 'number';
    inp.step = 'any';
    inp.value = String(cur[i]);
    inp.style.cssText = 'width:56px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 3px;font:12px Arial;';
    inp.addEventListener('change', () => {
      const n = parseFloat(inp.value);
      if (Number.isFinite(n)) { pushHistory(); cur[i] = n; onChange(cur.slice()); }
    });
    box.appendChild(al);
    box.appendChild(inp);
    wrap.appendChild(box);
  });
  return wrap;
}

export function rgbInput(label, rgb, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'margin:2px 0;';
  const lbl = document.createElement('div');
  lbl.textContent = label;
  lbl.style.cssText = 'color:#9ac;margin-bottom:2px;';
  wrap.appendChild(lbl);
  const cur = rgb.slice();
  ['R', 'G', 'B'].forEach((c, i) => {
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:4px;';
    const cl = document.createElement('span');
    cl.textContent = c;
    cl.style.cssText = 'width:12px;color:#777;';
    const inp = document.createElement('input');
    inp.type = 'range';
    inp.min = '0';
    inp.max = '255';
    inp.step = '1';
    inp.value = Math.round(cur[i] * 255);
    inp.style.cssText = 'flex:1;';
    const num = document.createElement('input');
    num.type = 'number';
    num.min = '0';
    num.max = '255';
    num.value = Math.round(cur[i] * 255);
    num.style.cssText = 'width:44px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:1px 3px;font:12px Arial;';
    const apply = (v) => { cur[i] = Math.max(0, Math.min(255, v)) / 255; onChange(cur.slice()); };
    inp.addEventListener('pointerdown', () => pushHistory());
    inp.addEventListener('input', () => { num.value = inp.value; apply(parseInt(inp.value, 10)); });
    num.addEventListener('change', () => { const v = parseInt(num.value, 10); if (Number.isFinite(v)) { pushHistory(); inp.value = v; apply(v); } });
    row.appendChild(cl);
    row.appendChild(inp);
    row.appendChild(num);
    wrap.appendChild(row);
  });
  return wrap;
}

export function opacityInput(label, val, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const inp = document.createElement('input');
  inp.type = 'range';
  inp.min = '0';
  inp.max = '1';
  inp.step = '0.01';
  inp.value = val;
  inp.style.cssText = 'width:76px;';
  const num = document.createElement('input');
  num.type = 'number';
  num.min = '0';
  num.max = '1';
  num.step = '0.01';
  num.value = Number(val).toFixed(2);
  num.style.cssText = 'width:46px;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:1px 3px;font:12px Arial;';
  inp.addEventListener('pointerdown', () => pushHistory());
  inp.addEventListener('input', () => { const v = Number(inp.value); num.value = v.toFixed(2); onChange(v); });
  num.addEventListener('change', () => { const v = Number(num.value); if (Number.isFinite(v)) { pushHistory(); inp.value = v; onChange(Math.max(0, Math.min(1, v))); } });
  wrap.appendChild(lbl);
  wrap.appendChild(inp);
  wrap.appendChild(num);
  return wrap;
}

export function numInput(label, val, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const inp = document.createElement('input');
  inp.type = 'number';
  inp.step = 'any';
  inp.value = val;
  inp.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;max-width:80px;';
  inp.addEventListener('change', () => { const n = parseFloat(inp.value); if (Number.isFinite(n)) onChange(n); });
  wrap.appendChild(lbl);
  wrap.appendChild(inp);
  return wrap;
}
