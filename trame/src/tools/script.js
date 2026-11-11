//==============================================================================
// G4CARE - script.js — Script tool
//
// Script tool
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { bindI18n, t } from '../../../gui/src/i18n/i18n.js';
// SCRIPT block editor — code editor (ExprTk) with autocomplete, variables sidebar and check.

import { fullScreen } from '../context.js';
import { showToast, btnStyle } from '../ui/dom.js';
import { icon, iconHtml } from '../../../gui/src/icons/icon.js';

const INP = 'background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;';
const AREA = 'width:100%;box-sizing:border-box;background:#111317;color:#d7e8d7;border:1px solid #343941;border-radius:4px;padding:8px;font:13px/1.5 Consolas,Monaco,monospace;min-height:320px;resize:vertical;white-space:pre;tab-size:2;';

const FUNCS = [
  'g4cout', 'g4cerr', 'abort_event', 'kill_track', 'format',
  'rand_gauss', 'rand_uniform', 'rand_poisson', 'rand_binomial', 'rand_exponential',
  'rand_breitwigner', 'rand_breitwigner_cut', 'rand_landau', 'rand_gamma',
  'rand_chisquare', 'rand_student_t', 'rand_lognormal', 'rand_rayleigh',
  'pamela_accept', 'ion_mass', 'ion_charge', 'ion_spin', 'ion_magnetic_moment',
  'ion_lifetime', 'ion_excitation_energy', 'data_rows', 'data_cell',
  'interp1d', 'interp2d', 'species_name', 'tgamma', 'lgamma',
  'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh',
  'exp', 'log', 'log10', 'sqrt', 'abs', 'ceil', 'floor', 'round', 'pow',
  'min', 'max', 'sum', 'avg', 'clamp',
];

const KEYWORDS = ['if', 'else', 'while', 'for', 'repeat', 'until', 'var', 'switch', 'case', 'default', 'return', 'break', 'continue', 'true', 'false'];

const VARS = [
  'edep', 'kinetic_energy', 'delta_e', 'niel', 'dpa', 'dose_gy', 'let',
  'smeared_edep', 'visible_edep', 'energy', 'vertex_energy', 'weight',
  'track_length', 'step_length', 'step_grammage', 'n_secondaries',
  'n_events', 'n_interacting_events', 'event_id', 'run_id', 'track_id', 'parent_id',
  'step_number', 'volume_id', 'material_id', 'pdg_code', 'process_subtype',
  'particle_name', 'process_name', 'step_status', 'track_status', 'boundary_status',
  'x', 'y', 'z', 'pos_x', 'pos_y', 'pos_z', 'px', 'py', 'pz', 'pt', 'eta', 'beta',
  'global_time', 'mass', 'charge', 'Z', 'A', 'excitation', 'mean_free_path',
  'at_rest_rate', 'at_rest_lifetime', 'wavelength', 'safety',
  'is_first_step_in_volume', 'is_last_step_in_volume',
  'delta_position_x', 'delta_position_y', 'delta_position_z',
  'delta_momentum_x', 'delta_momentum_y', 'delta_momentum_z',
  'vertex_x', 'vertex_y', 'vertex_z', 'density', 'temperature', 'pressure', 'state',
  'rad_length', 'nuc_int_length', 'zeff', 'aeff', 'isomer_level', 'recoil_type',
  'particle_spin', 'particle_parity', 'particle_conjugation', 'particle_isospin',
  'particle_isospin3', 'particle_gparity', 'particle_lifetime', 'particle_width',
  'particle_lepton_number', 'particle_baryon_number',
  'volume_mass', 'Nc', 'Nt', 'Gc', 'Gt', 'radical_oh', 'radical_h', 'radical_eaq',
  'h2o2', 'h2', 'chem_n_species', 'chem_edep_eV', 'chem_end_ns', 'g_oh', 'g_h', 'g_eaq',
];

function el(tag, text, css) {
  const n = document.createElement(tag);
  if (text != null) n.textContent = text;
  if (css) n.style.cssText = css;
  return n;
}

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

const FUNC_SET = new Set(FUNCS);
const KW_SET = new Set(KEYWORDS);
const VAR_SET = new Set(VARS);

function escHtml(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function highlightExprtk(code) {
  let html = '';
  let last = 0;
  const re = /(\/\/[^\n]*|#[^\n]*)|('(?:[^'\n])*'|"(?:[^"\n])*")|(\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|([A-Za-z_][A-Za-z0-9_]*)/g;
  let m;
  while ((m = re.exec(code))) {
    if (m.index > last) html += escHtml(code.slice(last, m.index));
    const full = m[0];
    if (m[1]) html += '<span style="color:#6a9955">' + escHtml(m[1]) + '</span>';
    else if (m[2]) html += '<span style="color:#ce9178">' + escHtml(m[2]) + '</span>';
    else if (m[3]) html += '<span style="color:#b5cea8">' + escHtml(m[3]) + '</span>';
    else if (m[4]) {
      const id = m[4];
      if (KW_SET.has(id)) html += '<span style="color:#c586c0">' + escHtml(id) + '</span>';
      else if (FUNC_SET.has(id)) html += '<span style="color:#dcdcaa">' + escHtml(id) + '</span>';
      else if (VAR_SET.has(id)) html += '<span style="color:#9cdcfe">' + escHtml(id) + '</span>';
      else html += escHtml(id);
    } else html += escHtml(full);
    last = m.index + full.length;
  }
  html += escHtml(code.slice(last));
  return html;
}

function attachCodeHighlight(ta, host) {
  const pre = el('pre');
  pre.setAttribute('aria-hidden', 'true');
  pre.style.cssText = 'position:absolute;top:1px;left:1px;right:1px;bottom:1px;margin:0;padding:8px;overflow:hidden;white-space:pre;tab-size:2;font:13px/1.5 Consolas,Monaco,monospace;background:#111317;color:#d7e8d7;pointer-events:none;border-radius:3px;';
  // (overlay removed) keep the textarea text visible
  ta.style.background = '#111317';
  ta.style.color = '#d7e8d7';
  ta.style.caretColor = '#fff';
  ta.style.position = 'relative';
  ta.style.zIndex = '1';
  function update() {
    pre.innerHTML = highlightExprtk(ta.value) + '\n';
    pre.scrollTop = ta.scrollTop;
    pre.scrollLeft = ta.scrollLeft;
  }
  ta.addEventListener('input', update);
  ta.addEventListener('scroll', () => { pre.scrollTop = ta.scrollTop; pre.scrollLeft = ta.scrollLeft; });
  update();
}
export async function openScriptEditor() {
  let data = { variables: {}, functions: {}, scripts: [] };
  try { data = await (await fetch('/script.json')).json(); } catch (e) { showToast(t('scr.loadFailed')); return; }
  renderEditor(data);
}

function renderEditor(data) {
  const variables = (data.variables && typeof data.variables === 'object') ? data.variables : {};
  const functions = (data.functions && typeof data.functions === 'object') ? data.functions : {};
  const loaded = Array.isArray(data.scripts) ? data.scripts : [];
  const scripts = loaded.map((sc) => toWorking(sc));
  let current = scripts.length ? 0 : -1;
  let currentTa = null;

  const overlay = el('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10020;background:rgba(0,0,0,0.55);display:flex;align-items:center;justify-content:center;';
  const box = el('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;width:920px;max-width:96vw;max-height:92vh;display:flex;flex-direction:column;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';
  box.appendChild(el('div', t('scr.title'), 'padding:10px 14px;border-bottom:1px solid #343941;font-weight:bold;color:#e8752a;font:13px Arial;'));

  const body = el('div', null, 'display:flex;flex:1;min-height:0;');
  const left = el('div', null, 'flex:1;min-width:0;padding:10px 14px;overflow:auto;');
  const right = el('div', null, 'width:260px;flex:none;border-left:1px solid #343941;padding:10px;overflow:auto;background:#232323;');
  body.appendChild(left); body.appendChild(right);
  box.appendChild(body);

  const suggestions = () => {
    const names = new Set([...FUNCS, ...KEYWORDS, ...VARS]);
    Object.keys(variables).forEach((k) => names.add(k));
    Object.keys(functions).forEach((k) => names.add(k));
    return [...names].sort();
  };

  function insertAtCursor(text) {
    if (!currentTa) return;
    currentTa.setRangeText(text, currentTa.selectionStart, currentTa.selectionEnd, 'end');
    currentTa.focus();
    if (current >= 0) scripts[current].code = currentTa.value;
  }

  const tabsRow = el('div', null, 'display:flex;align-items:center;gap:4px;flex-wrap:wrap;margin-bottom:8px;');
  const renderTabs = () => {
    tabsRow.innerHTML = '';
    scripts.forEach((sc, i) => {
      const tab = el('button', (i + 1) + '. ' + (sc.name || (t('scr.name') + (i + 1))), 'background:#131519;color:#969da8;border:1px solid #343941;border-radius:3px;padding:3px 10px;cursor:pointer;font:12px Arial;');
      if (i === current) { tab.style.background = '#e8752a'; tab.style.color = '#1a1a1a'; }
      tab.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); current = i; renderTabs(); renderScriptArea(); });
      tabsRow.appendChild(tab);
      const up = el('button', '↑', btnStyle + 'padding:2px 6px;');
      const dn = el('button', '↓', btnStyle + 'padding:2px 6px;');
      up.disabled = i === 0;
      dn.disabled = i === scripts.length - 1;
      up.setAttribute('data-i18n-title', 'tip.moveUp'); up.title = t('tip.moveUp');
      dn.setAttribute('data-i18n-title', 'tip.moveDown'); dn.title = t('tip.moveDown');
      up.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const t = scripts[i - 1]; scripts[i - 1] = scripts[i]; scripts[i] = t; current = i - 1; renderTabs(); renderScriptArea(); });
      dn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const t = scripts[i + 1]; scripts[i + 1] = scripts[i]; scripts[i] = t; current = i + 1; renderTabs(); renderScriptArea(); });
      tabsRow.appendChild(up);
      tabsRow.appendChild(dn);
    });
    const add = el('button', t('scr.addScript'), btnStyle);
    add.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); scripts.push(toWorking(null)); current = scripts.length - 1; renderTabs(); renderScriptArea(); });
    tabsRow.appendChild(add);
  };
  left.appendChild(tabsRow);

  const scriptAreaHost = el('div');
  left.appendChild(scriptAreaHost);
  const checkHost = el('div');
  left.appendChild(checkHost);

  function renderScriptArea() {
    scriptAreaHost.innerHTML = '';
    if (current < 0) {
      scriptAreaHost.appendChild(el('div', t('scr.empty'), 'color:#777;font:12px Arial;'));
      currentTa = null;
      return;
    }
    const sc = scripts[current];
    const meta = el('div', null, 'display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin-bottom:6px;');
    const lab = (t) => el('span', t, 'color:#9ac;font:10px Arial;');
    const nameInp = el('input', null, INP + 'width:130px;'); nameInp.value = sc.name; nameInp.placeholder = 'NAME';
    const volInp = el('input', null, INP + 'width:130px;'); volInp.value = sc.volume; volInp.placeholder = 'VOLUME';
    const excInp = el('input', null, INP + 'width:110px;'); excInp.value = sc.exclude; excInp.placeholder = 'EXCLUDE';
    const trigSel = el('select', null, INP);
    [['', t('scr.trigger')], ['step', 'N_STEP'], ['event', 'N_EVENT'], ['track', 'N_TRACK'], ['run', 'N_RUN']].forEach(([v, l]) => { const o = el('option', l); o.value = v; trigSel.appendChild(o); });
    trigSel.value = sc.trigger;
    const nInp = el('input', null, INP + 'width:42px;'); nInp.value = sc.n; nInp.placeholder = 'N';
    const whenSel = el('select', null, INP + 'width:76px;');
    [['end', 'end'], ['start', 'start']].forEach(([v, l]) => { const o = el('option', l); o.value = v; whenSel.appendChild(o); });
    whenSel.value = sc.when;
    const delBtn = el('button', null, 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;margin-left:auto;display:inline-flex;align-items:center;justify-content:center;');
    delBtn.appendChild(icon('x', 11));
    delBtn.setAttribute('data-i18n-title', 'tip.deleteScript'); delBtn.title = t('tip.deleteScript');
    [lab('NAME'), nameInp, lab('VOLUME'), volInp, lab('EXCLUDE'), excInp, trigSel, nInp, whenSel, delBtn].forEach((x) => meta.appendChild(x));
    scriptAreaHost.appendChild(meta);

    const wrap = el('div', null, 'position:relative;');
    const ta = el('textarea', null, AREA);
    ta.value = sc.code;
    ta.spellcheck = false;
    ta.addEventListener('input', () => { sc.code = ta.value; });
    wrap.appendChild(ta);
    scriptAreaHost.appendChild(wrap);
    attachAutocomplete(ta, suggestions, wrap);
    attachCodeHighlight(ta, wrap);
    currentTa = ta;

    nameInp.addEventListener('input', () => { sc.name = nameInp.value; });
    volInp.addEventListener('input', () => { sc.volume = volInp.value; });
    excInp.addEventListener('input', () => { sc.exclude = excInp.value; });
    trigSel.addEventListener('change', () => { sc.trigger = trigSel.value; });
    nInp.addEventListener('input', () => { sc.n = nInp.value; });
    whenSel.addEventListener('change', () => { sc.when = whenSel.value; });
    delBtn.addEventListener('pointerdown', (e) => {
      e.stopPropagation(); e.preventDefault();
      scripts.splice(current, 1);
      if (current >= scripts.length) current = scripts.length - 1;
      renderTabs(); renderScriptArea();
    });

    scriptAreaHost.appendChild(el('div', t('scr.syntaxHint'), 'color:#777;font:10px Arial;margin-top:4px;'));
  }

  function runCheck() {
    checkHost.innerHTML = '';
    if (current < 0) return;
    const protectedSet = new Set([...VARS, ...Object.keys(variables), ...Object.keys(functions)]);
    const { warnings } = analyzeScript(scripts[current].code, protectedSet);
    if (!warnings.length) {
      checkHost.appendChild(el('div', t('scr.ok'), 'color:#8f8;font:12px Arial;margin-top:6px;'));
    } else {
      checkHost.appendChild(el('div', t('scr.protectedWarn'), 'color:#c66;font:12px Arial;font-weight:bold;margin-top:6px;'));
      warnings.forEach((w) => checkHost.appendChild(el('div', t('scr.line') + w.line + ': ' + w.msg, 'color:#c66;font:12px Arial;margin:1px 0;')));
    }
  }

  const varRows = [];
  const fnRows = [];
  buildSidebar(right, insertAtCursor, variables, functions, varRows, fnRows);

  const foot = el('div', null, 'padding:10px 14px;border-top:1px solid #343941;display:flex;gap:8px;justify-content:flex-end;');
  const checkBtn = el('button', t('scr.check'), btnStyle);
  const cancelBtn = el('button', t('ctx.cancel'), btnStyle);
  const saveBtn = el('button', t('ctx.save'), 'background:#e8752a;color:#1a1a1a;border:1px solid #d97a3a;border-radius:4px;padding:5px 14px;cursor:pointer;font:12px Arial;');
  foot.appendChild(checkBtn); foot.appendChild(cancelBtn); foot.appendChild(saveBtn);
  box.appendChild(foot);

  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
  shield(overlay);

  const close = () => overlay.remove();
  checkBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); runCheck(); });
  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); });
  saveBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    saveScript(collectKv(varRows, false), collectKv(fnRows, true), scripts.map(toBlock), close);
  });

  renderTabs();
  renderScriptArea();
}

function toWorking(sc) {
  const o = sc || {};
  const vol = Array.isArray(o.VOLUME) ? o.VOLUME.join(', ') : (o.VOLUME != null ? String(o.VOLUME) : '');
  const exc = Array.isArray(o.EXCLUDE) ? o.EXCLUDE.join(', ') : '';
  let trigger = '';
  let n = '';
  if (o.N_STEP != null) { trigger = 'step'; n = String(o.N_STEP); }
  else if (o.N_EVENT != null) { trigger = 'event'; n = String(o.N_EVENT); }
  else if (o.N_TRACK != null) { trigger = 'track'; n = String(o.N_TRACK); }
  else if (o.N_RUN != null) { trigger = 'run'; n = String(o.N_RUN); }
  return {
    _orig: o,
    name: o.NAME ? String(o.NAME) : '',
    volume: vol,
    exclude: exc,
    trigger,
    n,
    when: o.WHEN || 'end',
    code: o.SCRIPT ? String(o.SCRIPT) : '',
  };
}

function toBlock(sc) {
  const block = { ...sc._orig };
  if (sc.name.trim()) block.NAME = sc.name.trim(); else delete block.NAME;
  const vol = sc.volume.trim();
  if (vol) { const parts = vol.split(',').map((s) => s.trim()).filter(Boolean); block.VOLUME = parts.length > 1 ? parts : parts[0]; }
  else delete block.VOLUME;
  const exc = sc.exclude.trim();
  if (exc) block.EXCLUDE = exc.split(',').map((s) => s.trim()).filter(Boolean);
  else delete block.EXCLUDE;
  delete block.N_STEP; delete block.N_EVENT; delete block.N_TRACK; delete block.N_RUN;
  if (sc.trigger && sc.n.trim() !== '') {
    const key = { step: 'N_STEP', event: 'N_EVENT', track: 'N_TRACK', run: 'N_RUN' }[sc.trigger];
    if (key) block[key] = Math.round(Number(sc.n));
  }
  if (sc.when !== 'end') block.WHEN = sc.when; else delete block.WHEN;
  if (sc.code.trim()) block.SCRIPT = sc.code; else delete block.SCRIPT;
  return block;
}

function kvRow(rows, k, v) {
  const row = el('div', null, 'border:1px solid #343941;border-radius:4px;padding:4px;display:flex;flex-direction:column;gap:3px;');
  const top = el('div', null, 'display:flex;gap:4px;');
  const name = el('input', null, INP + 'flex:1;min-width:0;');
  name.placeholder = t('ph.name'); name.value = k || '';
  const rm = el('button', null, 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 8px;flex:none;display:inline-flex;align-items:center;justify-content:center;');
  rm.appendChild(icon('x', 11));
  rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const i = rows.indexOf(row); if (i >= 0) rows.splice(i, 1); });
  top.appendChild(name); top.appendChild(rm);
  const val = el('input', null, INP + 'width:100%;box-sizing:border-box;');
  val.placeholder = t('ph.value'); val.value = (v == null) ? '' : String(v);
  row.appendChild(top); row.appendChild(val);
  row._in = { name, val };
  rows.push(row);
  return row;
}

function collectKv(rows, asString) {
  const out = {};
  rows.forEach((row) => {
    const k = row._in.name.value.trim();
    if (!k) return;
    const v = row._in.val.value;
    if (asString) { out[k] = v; return; }
    const num = Number(v);
    out[k] = (v !== '' && !Number.isNaN(num) && String(num) === v.trim()) ? num : v;
  });
  return out;
}

function attachAutocomplete(ta, provider, host) {
  const dd = el('div', null, 'position:absolute;left:0;right:0;z-index:10030;background:#131519;border:1px solid #343941;border-radius:4px;max-height:180px;overflow:auto;display:none;font:12px/1.6 Consolas,Monaco,monospace;');
  host.appendChild(dd);
  let items = [];
  let active = -1;

  function token() {
    const pos = ta.selectionStart;
    const m = ta.value.slice(0, pos).match(/([A-Za-z_][A-Za-z0-9_]*)$/);
    return m ? { t: m[1], s: pos - m[1].length } : null;
  }
  function hide() { dd.style.display = 'none'; items = []; active = -1; }
  function render() {
    if (!items.length) { hide(); return; }
    dd.innerHTML = '';
    items.forEach((s, i) => {
      const r = el('div', s, 'padding:2px 8px;cursor:pointer;color:#e2e5ea;white-space:nowrap;');
      if (i === active) r.style.background = '#3a5f8a';
      r.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); insert(s); });
      dd.appendChild(r);
    });
    dd.style.display = 'block';
  }
  function insert(s) {
    const tk = token();
    const start = tk ? tk.s : ta.selectionStart;
    ta.setRangeText(s, start, ta.selectionStart, 'end');
    hide();
    ta.focus();
  }
  function insertTab() { ta.setRangeText('  ', ta.selectionStart, ta.selectionEnd, 'end'); }
  function update() {
    const tk = token();
    if (!tk || tk.t.length < 1) { hide(); return; }
    const sug = provider();
    items = sug.filter((s) => s.toLowerCase().startsWith(tk.t.toLowerCase()) && s !== tk.t);
    active = -1;
    render();
  }
  ta.addEventListener('input', update);
  ta.addEventListener('keydown', (e) => {
    const open = dd.style.display === 'block';
    if (open) {
      if (e.key === 'ArrowDown') { e.preventDefault(); active = (active + 1) % items.length; render(); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); active = (active - 1 + items.length) % items.length; render(); }
      else if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); if (items[active] != null) insert(items[active]); }
      else if (e.key === 'Escape') { hide(); }
    } else if (e.key === 'Tab') { e.preventDefault(); insertTab(); }
  });
  ta.addEventListener('blur', () => setTimeout(hide, 200));
}

function analyzeScript(code, protectedSet) {
  const warnings = [];
  const locals = new Set();
  const lines = code.split('\n');
  lines.forEach((line, idx) => {
    const ln = idx + 1;
    let m;
    const varRe = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    while ((m = varRe.exec(line))) {
      const name = m[1];
      if (protectedSet.has(name)) warnings.push({ line: ln, msg: t('scr.varShadow', {name}) });
      locals.add(name);
    }
    const fnRe = /([A-Za-z_][A-Za-z0-9_]*)\s*:=\(/g;
    while ((m = fnRe.exec(line))) {
      const name = m[1];
      if (FUNCS.includes(name)) warnings.push({ line: ln, msg: t('scr.fnShadow', {name}) });
      locals.add(name);
    }
    const asnRe = /([A-Za-z_][A-Za-z0-9_]*)\s*:=(?!\()/g;
    while ((m = asnRe.exec(line))) {
      const name = m[1];
      if (protectedSet.has(name) && !locals.has(name)) warnings.push({ line: ln, msg: t('scr.protectedAssign', {name}) });
      else locals.add(name);
    }
    const compRe = /([A-Za-z_][A-Za-z0-9_]*)\s*(\+=|-=|\*=|\/=|%=)/g;
    while ((m = compRe.exec(line))) {
      const name = m[1];
      if (protectedSet.has(name) && !locals.has(name)) warnings.push({ line: ln, msg: t('scr.protectedModify', {name}) });
    }
  });
  return { warnings, locals };
}

function collapsibleSection(parent, title, open) {
  const wrap = el('div');
  const header = el('div', null, 'display:flex;align-items:center;gap:4px;cursor:pointer;color:#e8752a;font:10px Arial;font-weight:bold;margin:6px 0 2px;user-select:none;');
  const arrow = el('span', null, 'width:12px;display:inline-flex;align-items:center;');
  arrow.innerHTML = iconHtml(open ? 'chevron-down' : 'chevron-right', 10);
  header.appendChild(arrow);
  header.appendChild(el('span', title));
  const body = el('div', null, 'display:' + (open ? 'block' : 'none') + ';');
  header.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const on = body.style.display !== 'none';
    body.style.display = on ? 'none' : 'block';
    arrow.innerHTML = iconHtml(on ? 'chevron-right' : 'chevron-down', 10);
  });
  wrap.appendChild(header);
  wrap.appendChild(body);
  parent.appendChild(wrap);
  return { header, body, arrow };
}

function buildSidebar(container, insertFn, variables, functions, varRows, fnRows) {
  container.innerHTML = '';
  const search = el('input', null, INP + 'width:100%;margin-bottom:6px;box-sizing:border-box;');
  search.placeholder = t('ph.search');
  container.appendChild(search);

  const groups = [
    [t('scr.functions'), FUNCS],
    [t('scr.variables'), VARS],
    [t('scr.keywords'), KEYWORDS],
  ];
  const list = el('div', null, 'display:flex;flex-direction:column;gap:2px;');
  container.appendChild(list);

  function render() {
    list.innerHTML = '';
    const q = search.value.trim().toLowerCase();
    groups.forEach(([title, names]) => {
      const filtered = q ? names.filter((n) => n.toLowerCase().includes(q)) : names;
      if (q && !filtered.length) return;
      const sec = collapsibleSection(list, title + (filtered.length ? ' (' + filtered.length + ')' : ''), !q);
      filtered.forEach((n) => {
        const item = el('div', n, 'padding:2px 6px;cursor:pointer;color:#e2e5ea;font:12px Arial;border-radius:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;');
        item.setAttribute('data-i18n-title', 'tip.insertCode'); item.title = t('tip.insertCode');
        item.addEventListener('mouseenter', () => { item.style.background = '#3a5f8a'; });
        item.addEventListener('mouseleave', () => { item.style.background = 'transparent'; });
        item.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); insertFn(n); });
        sec.body.appendChild(item);
      });
    });
  }
  search.addEventListener('input', render);
  render();

  const varSec = collapsibleSection(container, t('scr.globalVars'), true);
  const varList = el('div', null, 'display:flex;flex-direction:column;gap:4px;');
  Object.entries(variables).forEach(([k, v]) => varList.appendChild(kvRow(varRows, k, v)));
  varSec.body.appendChild(varList);
  const addVar = el('button', t('scr.addVar'), btnStyle + 'margin-top:4px;width:100%;');
  addVar.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); varList.appendChild(kvRow(varRows, '')); });
  varSec.body.appendChild(addVar);

  const fnSec = collapsibleSection(container, t('scr.globalFns'), true);
  const fnList = el('div', null, 'display:flex;flex-direction:column;gap:4px;');
  Object.entries(functions).forEach(([k, v]) => fnList.appendChild(kvRow(fnRows, k, v)));
  fnSec.body.appendChild(fnList);
  const addFn = el('button', t('scr.addFn'), btnStyle + 'margin-top:4px;width:100%;');
  addFn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); fnList.appendChild(kvRow(fnRows, '', '')); });
  fnSec.body.appendChild(addFn);
}
async function saveScript(variables, functions, scripts, close) {
  try {
    const res = await fetch('/api/script', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ variables, functions, scripts }),
    });
    const j = await res.json().catch(() => ({}));
    if (res.ok && j.ok !== false) showToast(t('scr.saved'));
    else showToast(t('error.generic') + (j.error || ('HTTP ' + res.status)));
  } catch (e) {
    showToast(t('scr.saveFailed'));
  }
  close();
}
