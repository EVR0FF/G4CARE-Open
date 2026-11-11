//==============================================================================
// G4CARE - digitize.js — Digitize tool
//
// Digitize tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// DIGITIZE block editor — structured modal editor + props-panel summary.

import { fullScreen } from '../context.js';
import { showToast, btnStyle } from '../ui/dom.js';
import { icon } from '../../../gui/src/icons/icon.js';

const NUM = 'background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;width:130px;';
const INP = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;min-width:0;';
const UNIT = 'background:#111317;color:#e8752a;border:1px solid #343941;border-radius:3px;padding:3px 4px;font:12px Arial;';

function el(tag, text, css) {
  const n = document.createElement(tag);
  if (text != null) n.textContent = text;
  if (css) n.style.cssText = css;
  return n;
}

// Mirror viewer.js shieldUi(): stop pointer/key events from bubbling out of this
// UI root so <select> / checkboxes / inputs keep working over the vtk.js canvas.
function shield(root) {
  if (!root) return;
  for (const type of ['pointerdown', 'pointerup', 'wheel', 'contextmenu', 'dblclick', 'keydown', 'keypress', 'keyup']) {
    root.addEventListener(type, (e) => e.stopPropagation());
  }
}

function sectionHeader(text) {
  const h = el('div', text);
  h.style.cssText = 'font-weight:bold;color:#e8752a;font:12px Arial;margin:10px 0 4px;padding-bottom:2px;border-bottom:1px solid #3a3a3a;';
  return h;
}

function labelEl(f) {
  const l = el('div', null, 'display:flex;align-items:center;gap:5px;margin:4px 0 2px;');
  l.appendChild(el('span', f.label, 'color:#9ac;font:12px Arial;'));
  if (f.help) {
    const h = el('span', '?', 'color:#889;border:1px solid #556;border-radius:50%;width:13px;height:13px;display:inline-flex;align-items:center;justify-content:center;font:9px Arial;cursor:help;');
    h.title = f.help;
    l.appendChild(h);
  }
  return l;
}

// ── props-panel summary ──
export async function renderDigitizeSummary(name, container) {
  container.innerHTML = '<span style="color:var(--text-3);">' + t('msg.loading') + '</span>';
  let data;
  try {
    data = await (await fetch('/api/digitize?name=' + encodeURIComponent(name))).json();
  } catch (e) {
    container.innerHTML = '<span style="color:#a66;">' + t('msg.loadError') + '</span>';
    return;
  }
  if (!data || typeof data.digitize !== 'object') {
    container.innerHTML = '<span style="color:#a66;">' + t('msg.loadError') + '</span>';
    return;
  }
  const d = data.digitize || {};
  const parts = [t('dz.params') + Object.keys(d).length];
  if (data.is_sensitive === false) parts.push('⚠ isSensitive: false');
  const pipe = d.pipeline;
  if (Array.isArray(pipe) && pipe.length) parts.push('Pipeline: ' + pipe.join(' → '));
  container.innerHTML = '';
  container.appendChild(el('div', parts.join(' · '), 'color:#969da8;'));
}

// ── modal editor ──
export async function openDigitizeEditor(name) {
  if (!name) return;
  let data;
  try {
    data = await (await fetch('/api/digitize?name=' + encodeURIComponent(name))).json();
  } catch (e) { showToast(t('dz.loadFailed')); return; }
  if (!data || typeof data.digitize !== 'object') { showToast(t('dz.loadError')); return; }
  renderEditor(name, data);
}

function renderEditor(name, data) {
  const fields = data.fields || [];
  const pipelineSteps = data.pipeline_steps || [];
  const orig = data.digitize || {};

  const vals = {};
  for (const sec of fields) for (const f of sec.fields) {
    if (f.type === 'expr') {
      vals[f.use_key] = orig[f.use_key] === true;
      vals[f.key] = orig[f.key] != null ? String(orig[f.key]) : '';
    } else {
      vals[f.key] = orig[f.key];
    }
  }
  const pipeline = Array.isArray(orig.pipeline) ? orig.pipeline.slice() : [];

  const overlay = el('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10020;background:rgba(0,0,0,0.55);display:flex;align-items:center;justify-content:center;';
  const box = el('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;width:640px;max-width:92vw;max-height:88vh;display:flex;flex-direction:column;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';

  const head = el('div', t('dz.title') + name, 'padding:10px 14px;border-bottom:1px solid #343941;font-weight:bold;color:#e8752a;font:13px Arial;');
  box.appendChild(head);

  const body = el('div', null, 'padding:10px 14px;overflow:auto;flex:1;');
  if (data.is_sensitive === false) {
    body.appendChild(el('div', t('dz.notSensitive'), 'color:#c93;font:12px Arial;margin-bottom:6px;'));
  }

  body.appendChild(buildPipelineWidget(pipeline, pipelineSteps));
  for (const sec of fields) {
    body.appendChild(sectionHeader(sec.section));
    for (const f of sec.fields) body.appendChild(buildField(f, vals));
  }
  box.appendChild(body);

  const foot = el('div', null, 'padding:10px 14px;border-top:1px solid #343941;display:flex;gap:8px;justify-content:flex-end;');
  const cancelBtn = el('button', t('ctx.cancel'), btnStyle);
  const saveBtn = el('button', t('ctx.save'), 'background:#e8752a;color:#1a1a1a;border:1px solid #d97a3a;border-radius:4px;padding:5px 14px;cursor:pointer;font:12px Arial;');
  foot.appendChild(cancelBtn);
  foot.appendChild(saveBtn);
  box.appendChild(foot);

  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
  shield(overlay);

  const close = () => overlay.remove();
  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); });
  saveBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    saveDigitize(name, buildDigitize(orig, fields, vals, pipeline), close);
  });
}

function buildPipelineWidget(pipeline, pipelineSteps) {
  const wrap = el('div');
  wrap.appendChild(sectionHeader(t('dz.pipeline')));
  wrap.appendChild(el('div', t('dz.pipelineHint'), 'color:#777;font:10px Arial;margin-bottom:4px;'));

  const list = el('div', null, 'display:flex;flex-direction:column;gap:4px;margin:4px 0 6px;');
  const render = () => {
    list.innerHTML = '';
    if (!pipeline.length) {
      list.appendChild(el('div', t('dz.emptyPipeline'), 'color:#777;font:12px Arial;'));
    }
    pipeline.forEach((step, i) => {
      const chip = el('div', null, 'display:flex;align-items:center;gap:6px;');
      chip.appendChild(el('span', (i + 1) + '. ' + step, 'background:#e8752a;border:1px solid #d97a3a;border-radius:3px;padding:3px 8px;color:#1a1a1a;font:12px Arial;'));
      const up = el('button', '↑', btnStyle);
      const dn = el('button', '↓', btnStyle);
      const rm = el('button', null, 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 7px;display:inline-flex;align-items:center;justify-content:center;');
      rm.appendChild(icon('x', 11));
      up.disabled = i === 0;
      dn.disabled = i === pipeline.length - 1;
      up.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const t = pipeline[i - 1]; pipeline[i - 1] = pipeline[i]; pipeline[i] = t; render(); });
      dn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const t = pipeline[i + 1]; pipeline[i + 1] = pipeline[i]; pipeline[i] = t; render(); });
      rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); pipeline.splice(i, 1); render(); });
      chip.appendChild(up);
      chip.appendChild(dn);
      chip.appendChild(rm);
      list.appendChild(chip);
    });
  };
  render();
  wrap.appendChild(list);

  const addRow = el('div', null, 'display:flex;align-items:center;gap:6px;');
  const sel = el('select', null, INP + 'max-width:240px;');
  const addBtn = el('button', t('ctx.add'), btnStyle);
  const refreshOptions = () => {
    sel.innerHTML = '';
    sel.appendChild(el('option', t('dz.addStep'), ''));
    for (const s of pipelineSteps) if (!pipeline.includes(s)) sel.appendChild(el('option', s, ''));
  };
  refreshOptions();
  addBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    if (sel.value && !pipeline.includes(sel.value)) { pipeline.push(sel.value); render(); }
    refreshOptions();
  });
  addRow.appendChild(sel);
  addRow.appendChild(addBtn);
  wrap.appendChild(addRow);
  return wrap;
}

function buildField(f, vals) {
  const wrap = el('div', null, 'margin:1px 0;');
  wrap.appendChild(labelEl(f));

  if (f.type === 'number' || f.type === 'number_unit' || f.type === 'int') {
    const row = el('div', null, 'display:flex;align-items:center;gap:6px;');
    const inp = el('input');
    inp.type = 'text';
    const v = vals[f.key];
    inp.value = (v == null || v === '') ? '' : String(v);
    inp.style.cssText = NUM;
    inp.addEventListener('input', () => { vals[f.key] = inp.value.trim(); });
    row.appendChild(inp);
    if (f.type === 'number_unit' && f.units) {
      const sel = el('select', null, UNIT);
      for (const u of f.units) sel.appendChild(el('option', u));
      sel.value = f.unit || '';
      row.appendChild(sel);
    }
    wrap.appendChild(row);
    return wrap;
  }

  if (f.type === 'bool') {
    const cb = el('input');
    cb.type = 'checkbox';
    cb.checked = vals[f.key] === true;
    cb.style.cssText = 'width:16px;height:16px;margin:3px 0;';
    cb.addEventListener('change', () => { vals[f.key] = cb.checked; });
    wrap.appendChild(cb);
    return wrap;
  }

  if (f.type === 'select') {
    const sel = el('select', null, INP + 'max-width:280px;');
    sel.appendChild(el('option', t('dz.notSet'), ''));
    for (const o of (f.enum || [])) sel.appendChild(el('option', o, ''));
    const v = vals[f.key];
    sel.value = (v == null || v === '') ? '' : String(v);
    sel.addEventListener('change', () => { vals[f.key] = sel.value === '' ? undefined : sel.value; });
    wrap.appendChild(sel);
    return wrap;
  }

  if (f.type === 'list_int') {
    const inp = el('input');
    inp.type = 'text';
    inp.placeholder = t('ph.example');
    const v = vals[f.key];
    inp.value = Array.isArray(v) ? v.join(', ') : (v == null ? '' : String(v));
    inp.style.cssText = INP;
    inp.addEventListener('input', () => {
      const raw = inp.value.trim();
      if (raw === '') { vals[f.key] = undefined; return; }
      vals[f.key] = raw.split(',').map((s) => s.trim()).filter(Boolean).map(Number).filter((n) => !Number.isNaN(n));
    });
    wrap.appendChild(inp);
    return wrap;
  }

  if (f.type === 'expr') {
    const row = el('div', null, 'display:flex;align-items:center;gap:8px;');
    const cb = el('input');
    cb.type = 'checkbox';
    cb.checked = vals[f.use_key] === true;
    cb.style.cssText = 'width:16px;height:16px;';
    const inp = el('input');
    inp.type = 'text';
    inp.value = vals[f.key] || '';
    inp.placeholder = t('ph.exprtk');
    inp.style.cssText = INP;
    inp.disabled = !cb.checked;
    cb.addEventListener('change', () => { vals[f.use_key] = cb.checked; inp.disabled = !cb.checked; });
    inp.addEventListener('input', () => { vals[f.key] = inp.value; });
    row.appendChild(cb);
    row.appendChild(inp);
    wrap.appendChild(row);
    return wrap;
  }

  const inp = el('input');
  inp.type = 'text';
  const v = vals[f.key];
  inp.value = (v == null) ? '' : String(v);
  inp.style.cssText = INP;
  inp.addEventListener('input', () => { vals[f.key] = inp.value; });
  wrap.appendChild(inp);
  return wrap;
}

function buildDigitize(orig, fields, vals, pipeline) {
  const out = {};
  const known = new Set(['pipeline']);
  for (const sec of fields) for (const f of sec.fields) {
    known.add(f.key);
    if (f.type === 'expr') known.add(f.use_key);
  }
  for (const k of Object.keys(orig)) if (!known.has(k)) out[k] = orig[k];
  for (const sec of fields) for (const f of sec.fields) {
    if (f.type === 'expr') {
      if (vals[f.use_key] === true) out[f.use_key] = true;
      const v = vals[f.key];
      if (v != null && v !== '') out[f.key] = v;
    } else if (f.type === 'bool') {
      if (vals[f.key] === true) out[f.key] = true;
    } else {
      const v = vals[f.key];
      if (v != null && v !== '') out[f.key] = v;
    }
  }
  if (pipeline.length) out.pipeline = pipeline;
  return out;
}

async function saveDigitize(name, digitize, close) {
  try {
    const res = await fetch('/api/digitize', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, digitize }),
    });
    const j = await res.json().catch(() => ({}));
    if (res.ok && j.ok !== false) showToast(t('dz.saved'));
    else showToast(t('error.generic') + (j.error || ('HTTP ' + res.status)));
  } catch (e) {
    showToast(t('dz.saveFailed'));
  }
  close();
}
