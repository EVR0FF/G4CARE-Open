//==============================================================================
// G4CARE - script-editor.js — SCRIPT (ExprTk) editor in the Input tab
//
// SCRIPT (ExprTk) editor in the Input tab
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { t, onLocaleChange } from './i18n/i18n.js';
import { icon, iconHtml } from './icons/icon.js';

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
const FUNC_CATS = [
  ['scr.cat.control', ['g4cout', 'g4cerr', 'abort_event', 'kill_track', 'format']],
  ['scr.cat.random', ['rand_gauss', 'rand_uniform', 'rand_poisson', 'rand_binomial', 'rand_exponential', 'rand_breitwigner', 'rand_breitwigner_cut', 'rand_landau', 'rand_gamma', 'rand_chisquare', 'rand_student_t', 'rand_lognormal', 'rand_rayleigh']],
  ['scr.cat.particles', ['pamela_accept', 'ion_mass', 'ion_charge', 'ion_spin', 'ion_magnetic_moment', 'ion_lifetime', 'ion_excitation_energy', 'species_name']],
  ['scr.cat.data', ['data_rows', 'data_cell', 'interp1d', 'interp2d']],
  ['scr.cat.math', ['sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh', 'exp', 'log', 'log10', 'sqrt', 'abs', 'ceil', 'floor', 'round', 'pow', 'min', 'max', 'sum', 'avg', 'clamp', 'tgamma', 'lgamma']],
];
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

const INP = 'background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 5px;font:12px Arial;';

function el(tag, text, css) {
    const n = document.createElement(tag);
    if (text != null) n.textContent = text;
    if (css) n.style.cssText = css;
    return n;
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

function analyzeScript(code, protectedSet) {
    const warnings = [];
    const locals = new Set();
    const lines = code.split(String.fromCharCode(10));
    lines.forEach((line, idx) => {
        const ln = idx + 1;
        let m;
        const varRe = /\bvar\s+([A-Za-z_][A-Za-z0-9_]*)/g;
        while ((m = varRe.exec(line))) {
            const name = m[1];
            if (protectedSet.has(name)) warnings.push({ line: ln, msg: t('scr.varShadow', { name }) });
            locals.add(name);
        }
        const fnRe = /([A-Za-z_][A-Za-z0-9_]*)\s*:=\(/g;
        while ((m = fnRe.exec(line))) {
            const name = m[1];
            if (FUNCS.includes(name)) warnings.push({ line: ln, msg: t('scr.fnShadow', { name }) });
            locals.add(name);
        }
        const asnRe = /([A-Za-z_][A-Za-z0-9_]*)\s*:=(?!\()/g;
        while ((m = asnRe.exec(line))) {
            const name = m[1];
            if (protectedSet.has(name) && !locals.has(name)) warnings.push({ line: ln, msg: t('scr.protectedAssign', { name }) });
            else locals.add(name);
        }
        const compRe = /([A-Za-z_][A-Za-z0-9_]*)\s*(\+=|-=|\*=|\/=|%=)/g;
        while ((m = compRe.exec(line))) {
            const name = m[1];
            if (protectedSet.has(name) && !locals.has(name)) warnings.push({ line: ln, msg: t('scr.protectedModify', { name }) });
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

function kvRow(rows, k, v) {
    const row = el('div', null, 'border:1px solid #343941;border-radius:4px;padding:4px;display:flex;flex-direction:column;gap:3px;');
    const top = el('div', null, 'display:flex;gap:4px;');
    const name = el('input', null, INP + 'flex:1;min-width:0;');
    name.placeholder = 'name'; name.value = k || '';
    const rm = el('button', null, 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;font:12px Arial;padding:0 8px;flex:none;');
    rm.textContent = '×';
    rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); row.remove(); const i = rows.indexOf(row); if (i >= 0) rows.splice(i, 1); });
    top.appendChild(name); top.appendChild(rm);
    const val = el('input', null, INP + 'width:100%;box-sizing:border-box;');
    val.placeholder = 'value'; val.value = (v == null) ? '' : String(v);
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
        const v = row._in.val.value.trim();
        out[k] = asString ? v : (v === '' ? '' : (Number.isFinite(Number(v)) ? Number(v) : v));
    });
    return out;
}

export async function initScriptEditor(monaco) {
    const host = document.getElementById('script-editor');
    if (!host) return;
    const tabsEl = document.getElementById('script-tabs');
    const checkBtn = document.getElementById('script-check');
    const addBtn = document.getElementById('script-add');
    const saveBtn = document.getElementById('script-save');
    const statusEl = document.getElementById('script-status');
    const sidebarEl = document.getElementById('script-sidebar');
    const metaEl = document.getElementById('script-meta');
    const propsHead = document.getElementById('script-props-head');
    if (propsHead) propsHead.onclick = () => {
        const m = document.getElementById('script-meta');
        if (!m) return;
        const on = m.style.display === 'none';
        m.style.display = on ? 'flex' : 'none';
        const c = document.getElementById('script-props-caret');
        if (c) c.textContent = on ? '▾' : '▸';
    };

    const editor = monaco.editor.create(host, {
        value: '',
        language: 'plaintext',
        theme: 'vs-dark',
        automaticLayout: true,
        minimap: { enabled: false },
        fontSize: 13,
        tabSize: 2,
    });

    let variables = {};
    let functions = {};
    let scripts = [];
    let current = 0;
    const varRows = [];
    const fnRows = [];

    monaco.languages.registerCompletionItemProvider('plaintext', {
        provideCompletionItems: (model, pos) => {
            const word = model.getWordUntilPosition(pos);
            const range = { startLineNumber: pos.lineNumber, endLineNumber: pos.lineNumber, startColumn: word.startColumn, endColumn: word.endColumn };
            const s = [];
            const add = (label, kind, detail) => s.push({ label, kind, insertText: label, range, detail });
            for (const f of FUNCS) add(f, monaco.languages.CompletionItemKind.Function, 'ExprTk');
            for (const k of KEYWORDS) add(k, monaco.languages.CompletionItemKind.Keyword, 'keyword');
            for (const v of VARS) add(v, monaco.languages.CompletionItemKind.Variable, 'variable');
            for (const v of Object.keys(variables)) add(v, monaco.languages.CompletionItemKind.Variable, 'variable');
            for (const f of Object.keys(functions)) add(f, monaco.languages.CompletionItemKind.Function, 'function');
            return { suggestions: s };
        },
    });

    try {
        const data = await window.g4care.getScript();
        variables = (data && data.variables) || {};
        functions = (data && data.functions) || {};
        scripts = (Array.isArray(data && data.scripts) ? data.scripts : []).map(toWorking);
    } catch (e) { /* ignore */ }
    if (!scripts.length) scripts = [toWorking({ NAME: 'script_1', SCRIPT: '' })];

    function insertAtCursor(text) {
        const sel = editor.getSelection();
        const id = { range: sel, text, forceMoveMarkers: true };
        editor.executeEdits('script', [id]);
        editor.focus();
        scripts[current].code = editor.getValue();
    }

    function renderTabs() {
        if (!tabsEl) return;
        tabsEl.innerHTML = '';
        scripts.forEach((sc, i) => {
            const tab = el('button', (i + 1) + '. ' + (sc.name || t('scr.scriptN', { n: i + 1 })), 'background:#131519;color:#969da8;border:1px solid #343941;border-radius:3px;padding:2px 8px;cursor:pointer;font:12px Arial;');
            if (i === current) { tab.style.background = '#e8752a'; tab.style.color = '#1a1a1a'; }
            tab.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); saveCurrent(); current = i; show(); });
            tabsEl.appendChild(tab);
            const up = el('button', null, 'background:#202329;color:#969da8;border:1px solid #343941;border-radius:3px;cursor:pointer;padding:2px 6px;display:inline-flex;align-items:center;justify-content:center;');
            { const u = icon('chevron-down', 12); u.style.transform = 'rotate(180deg)'; up.appendChild(u); }
            const dn = el('button', null, 'background:#202329;color:#969da8;border:1px solid #343941;border-radius:3px;cursor:pointer;padding:2px 6px;display:inline-flex;align-items:center;justify-content:center;');
            dn.appendChild(icon('chevron-down', 12));
            up.disabled = i === 0;
            dn.disabled = i === scripts.length - 1;
            up.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const tmp = scripts[i - 1]; scripts[i - 1] = scripts[i]; scripts[i] = tmp; current = i - 1; renderTabs(); });
            dn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); const tmp = scripts[i + 1]; scripts[i + 1] = scripts[i]; scripts[i] = tmp; current = i + 1; renderTabs(); });
            tabsEl.appendChild(up);
            tabsEl.appendChild(dn);
            const rm = el('button', null, 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:3px;cursor:pointer;padding:0 7px;display:inline-flex;align-items:center;justify-content:center;');
            rm.appendChild(icon('x', 12));
            rm.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); scripts.splice(i, 1); if (current >= scripts.length) current = scripts.length - 1; if (current < 0) current = 0; renderTabs(); show(); });
            tabsEl.appendChild(rm);
        });
    }

    function saveCurrent() { if (scripts[current]) scripts[current].code = editor.getValue(); }
    function renderMeta() {
        if (!metaEl) return;
        metaEl.innerHTML = '';
        const sc = scripts[current] || scripts[0];
        const lab = (txt) => { const s = el('span', txt, 'color:#e8752a;font:10px Arial;font-weight:bold;'); metaEl.appendChild(s); };
        const nameInp = el('input', null, INP + 'width:110px;'); nameInp.value = sc.name; nameInp.placeholder = t('scr.name');
        const volInp = el('input', null, INP + 'width:110px;'); volInp.value = sc.volume; volInp.placeholder = t('scr.volume');
        const excInp = el('input', null, INP + 'width:100px;'); excInp.value = sc.exclude; excInp.placeholder = t('scr.exclude');
        const trigSel = el('select', null, INP);
        [['', t('scr.trigger')], ['step', 'N_STEP'], ['event', 'N_EVENT'], ['track', 'N_TRACK'], ['run', 'N_RUN']].forEach(([v, l]) => { const o = el('option', l); o.value = v; trigSel.appendChild(o); });
        trigSel.value = sc.trigger;
        const nInp = el('input', null, INP + 'width:40px;'); nInp.value = sc.n; nInp.placeholder = 'N';
        const whenSel = el('select', null, INP + 'width:70px;');
        [['end', 'end'], ['start', 'start']].forEach(([v, l]) => { const o = el('option', l); o.value = v; whenSel.appendChild(o); });
        whenSel.value = sc.when;
        lab(t('scr.name')); metaEl.appendChild(nameInp);
        lab(t('scr.volume')); metaEl.appendChild(volInp);
        lab(t('scr.exclude')); metaEl.appendChild(excInp);
        lab(t('scr.trigger')); metaEl.appendChild(trigSel); metaEl.appendChild(nInp); lab(t('scr.when')); metaEl.appendChild(whenSel);
        nameInp.addEventListener('input', () => { sc.name = nameInp.value; });
        volInp.addEventListener('input', () => { sc.volume = volInp.value; });
        excInp.addEventListener('input', () => { sc.exclude = excInp.value; });
        trigSel.addEventListener('change', () => { sc.trigger = trigSel.value; });
        nInp.addEventListener('input', () => { sc.n = nInp.value; });
        whenSel.addEventListener('change', () => { sc.when = whenSel.value; });
    }
    function show() { const sc = scripts[current] || scripts[0]; editor.setValue(sc.code || ''); renderMeta(); }
    renderTabs();
    show();
    if (addBtn) addBtn.onclick = () => { scripts.push(toWorking({ NAME: '', SCRIPT: '' })); current = scripts.length - 1; renderTabs(); show(); };

    if (checkBtn) checkBtn.onclick = () => {
        saveCurrent();
        const protectedSet = new Set([...VARS, ...Object.keys(variables), ...Object.keys(functions)]);
        const { warnings } = analyzeScript(scripts[current].code, protectedSet);
        if (!warnings.length) statusEl && (statusEl.textContent = t('scr.ok'));
        else {
            statusEl && (statusEl.textContent = warnings.length + ' ' + (warnings.length === 1 ? 'warning' : 'warnings'));
            if (statusEl) statusEl.style.color = 'var(--danger)';
            if (sidebarEl) {
                const chk = document.getElementById('script-check-host');
                if (chk) { chk.innerHTML = ''; warnings.forEach((w) => chk.appendChild(el('div', t('scr.line') + w.line + ': ' + w.msg, 'color:#c66;font:12px Arial;margin:1px 0;'))); }
            }
        }
    };

    function renderSidebar() {
        if (!sidebarEl) return;
        if (varRows.length) variables = collectKv(varRows, false);
        if (fnRows.length) functions = collectKv(fnRows, true);
        varRows.length = 0; fnRows.length = 0;
        sidebarEl.innerHTML = '';
        const search = el('input', null, INP + 'width:100%;margin-bottom:6px;box-sizing:border-box;');
        search.placeholder = t('ph.search');
        sidebarEl.appendChild(search);
        const groups = [...FUNC_CATS.map(([k, names]) => [t(k), names]), [t('scr.variables'), VARS], [t('scr.keywords'), KEYWORDS]];
        const list = el('div', null, 'display:flex;flex-direction:column;gap:2px;');
        sidebarEl.appendChild(list);
        function renderTree() {
            list.innerHTML = '';
            const q = search.value.trim().toLowerCase();
            groups.forEach(([title, names]) => {
                const filtered = q ? names.filter((n) => n.toLowerCase().includes(q)) : names;
                if (q && !filtered.length) return;
                const sec = collapsibleSection(list, title + (filtered.length ? ' (' + filtered.length + ')' : ''), !q);
                filtered.forEach((n) => {
                    const item = el('div', n, 'padding:2px 6px;cursor:pointer;color:#e2e5ea;font:12px Arial;border-radius:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;');
                    item.addEventListener('mouseenter', () => { item.style.background = '#3a5f8a'; });
                    item.addEventListener('mouseleave', () => { item.style.background = 'transparent'; });
                    item.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); insertAtCursor(n); });
                    sec.body.appendChild(item);
                });
            });
        }
        search.addEventListener('input', renderTree);
        renderTree();

        const varSec = collapsibleSection(sidebarEl, t('scr.globalVars'), true);
        const varList = el('div', null, 'display:flex;flex-direction:column;gap:4px;');
        Object.entries(variables).forEach(([k, v]) => varList.appendChild(kvRow(varRows, k, v)));
        varSec.body.appendChild(varList);
        const addVar = el('button', t('scr.addVar'), 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;margin-top:4px;width:100%;');
        addVar.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); varList.appendChild(kvRow(varRows, '', '')); });
        varSec.body.appendChild(addVar);

        const fnSec = collapsibleSection(sidebarEl, t('scr.globalFns'), true);
        const fnList = el('div', null, 'display:flex;flex-direction:column;gap:4px;');
        Object.entries(functions).forEach(([k, v]) => fnList.appendChild(kvRow(fnRows, k, v)));
        fnSec.body.appendChild(fnList);
        const addFn = el('button', t('scr.addFn'), 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:3px;cursor:pointer;font:12px Arial;padding:2px 8px;margin-top:4px;width:100%;');
        addFn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); fnList.appendChild(kvRow(fnRows, '', '')); });
        fnSec.body.appendChild(addFn);

        const chk = el('div');
        chk.id = 'script-check-host';
        sidebarEl.appendChild(chk);
    }

    renderSidebar();
    onLocaleChange(() => { renderMeta(); renderTabs(); renderSidebar(); });

    if (saveBtn) saveBtn.onclick = async () => {
        saveCurrent();
        try {
            const r = await window.g4care.saveScript({ variables: collectKv(varRows || [], false), functions: collectKv(fnRows || [], true), scripts: scripts.map(toBlock) });
            if (statusEl) statusEl.textContent = (r && r.ok) ? 'OK' : ((r && r.error) || 'ERR');
            if (statusEl) statusEl.style.color = (r && r.ok) ? 'var(--ok)' : 'var(--danger)';
        } catch (e) {
            if (statusEl) statusEl.textContent = 'ERR';
        }
    };
}
