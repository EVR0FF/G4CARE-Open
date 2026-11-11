//==============================================================================
// G4CARE - engine.js — Engine run/export: run flow, ParaView scene, GDML export, phantom
//
// Engine run/export: run flow, ParaView scene, GDML export, phantom
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { parse, stringify } from 'yaml';

// Engine run/export: run flow, ParaView scene export, GDML export, phantom position.
export function initEngine({ geometryModel, appendLog, t }) {
    async function runFlow(visualize) {
        let text = geometryModel.getValue();
        let n = 10;
        try { n = parseInt((parse(geometryModel.getValue()) || {}).EVENTS, 10) || 10; } catch (e) { /* default */ }
        if (visualize) {
            try {
                const doc = parse(text) || {};
                doc.VISUALIZATION = true;
                text = stringify(doc);
            } catch (e) { /* ignore */ }
        }
        appendLog(`> ${visualize ? 'visualize' : 'run'} (${n} events)`);
        try {
            // Fresh engine process → clean state (Geant4 can't fully re-init in-process).
            await window.g4care.restart();
            const lc = await window.g4care.rpc('loadConfigText', { text });
            if (!lc.ok) { appendLog('  loadConfig FAILED: ' + (lc.error || '')); return; }
            const ini = await window.g4care.rpc('init');
            if (!ini.ok) { appendLog('  init FAILED: ' + (ini.error || '')); return; }
            const run = await window.g4care.rpc('run', { nEvents: n });
            appendLog('  run: ' + JSON.stringify(run));
        } catch (e) {
            appendLog('  ERROR: ' + e.message);
        }
    }

    document.getElementById('btn-run').onclick = () => { const sb = document.getElementById('btn-stop'); if (sb) sb.disabled = false; runFlow(false); };
    async function exportAndLaunchParaview() {
        const text = geometryModel.getValue();
        appendLog('> export scene');
        try {
            await window.g4care.restart();
            const lc = await window.g4care.rpc('loadConfigText', { text });
            if (!lc.ok) { appendLog('  loadConfig FAILED: ' + (lc.error || '')); return; }
            const ini = await window.g4care.rpc('init');
            if (!ini.ok) { appendLog('  init FAILED: ' + (ini.error || '')); return; }
            const res = await window.g4care.rpc('exportSceneVtk', { file: '/tmp/g4care_scene.vtp' });
            if (res && res.ok) {
                await window.g4care.launchParaview('/tmp/g4care_scene.vtp');
                appendLog('  -> ' + t('msg.paraviewStarted'));
            }
        } catch (e) {
            appendLog('  ERROR: ' + e.message);
        }
    }
    document.getElementById('btn-export-scene').onclick = exportAndLaunchParaview;
    const exportMenuBtn = document.getElementById('btn-export-menu');
    const exportMenu = document.getElementById('export-menu');
    if (exportMenuBtn && exportMenu) {
        exportMenuBtn.onclick = (e) => { e.stopPropagation(); exportMenu.style.display = exportMenu.style.display === 'none' ? 'block' : 'none'; };
        document.addEventListener('click', () => { exportMenu.style.display = 'none'; });
        exportMenu.addEventListener('click', () => { exportMenu.style.display = 'none'; });
    }
    const stopBtn = document.getElementById('btn-stop');
    stopBtn.disabled = true;
    document.getElementById('btn-stop').onclick = () => { window.g4care.stop(); stopBtn.disabled = true; };
    document.getElementById('btn-restart').onclick = async () => {
        await window.g4care.restart();
        appendLog('[gui] engine restarted');
    };

    // Inject <auxiliary> color/opacity/visible tags into an exported GDML.
    function injectAuxTags(gdml, appearance) {
        let out = gdml;
        for (const [name, a] of Object.entries(appearance || {})) {
            if (!a || typeof a !== 'object') continue;
            const tags = [];
            if (Array.isArray(a.color) && a.color.length >= 3) {
                tags.push(`<auxiliary auxtype="Color" auxvalue="${a.color.map((x) => Number(x).toFixed(4)).join(' ')}"/>`);
            }
            if (a.opacity !== undefined && a.opacity !== null) {
                tags.push(`<auxiliary auxtype="Opacity" auxvalue="${a.opacity}"/>`);
            }
            if (a.visible !== undefined) {
                tags.push(`<auxiliary auxtype="Visible" auxvalue="${a.visible ? 'true' : 'false'}"/>`);
            }
            if (!tags.length) continue;
            const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            const re = new RegExp(`(<volume\\s+name="${escaped}"[^>]*>)([\\s\\S]*?)(</volume>)`);
            if (re.test(out)) {
                out = out.replace(re, `$1$2\n  ${tags.join('\n  ')}\n$3`);
            }
        }
        return out;
    }

    // Tiny Promise-based text prompt (Electron disables window.prompt).
    function promptText(message, defaultValue) {
        return new Promise((resolve) => {
            const overlay = document.createElement('div');
            overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;z-index:99999;';
            const box = document.createElement('div');
            box.style.cssText = 'background:#262626;border:1px solid var(--border);border-radius:8px;padding:14px;width:360px;color:var(--text);font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';
            const msg = document.createElement('div');
            msg.textContent = message;
            msg.style.cssText = 'margin-bottom:8px;';
            const inp = document.createElement('input');
            inp.type = 'text';
            inp.value = defaultValue || '';
            inp.style.cssText = 'width:100%;box-sizing:border-box;background:var(--inset);color:var(--text);border:1px solid var(--border);border-radius:4px;padding:6px;margin-bottom:10px;font:12px Arial;';
            const row = document.createElement('div');
            row.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;';
            const ok = document.createElement('button'); ok.textContent = 'OK';
            const cancel = document.createElement('button'); cancel.textContent = t('settings.cancel');
            [ok, cancel].forEach((b) => { b.style.cssText = 'background:#3a3a3a;color:var(--text);border:1px solid var(--border);border-radius:4px;padding:4px 12px;cursor:pointer;font:12px Arial;'; });
            const close = (val) => { overlay.remove(); resolve(val); };
            ok.onclick = () => close(inp.value.trim());
            cancel.onclick = () => close(null);
            inp.onkeydown = (e) => { if (e.key === 'Enter') close(inp.value.trim()); if (e.key === 'Escape') close(null); };
            row.appendChild(ok); row.appendChild(cancel);
            box.appendChild(msg); box.appendChild(inp); box.appendChild(row);
            overlay.appendChild(box);
            document.body.appendChild(overlay);
            inp.focus();
        });
    }

    // Export the current scene/geometry as a GDML file (engine GEOMETRY.EXPORT).
    // When outPath is provided the save dialog is skipped (batch export); otherwise
    // the default path is ~/Documents/g4care/gdml/{scenes,objects,arrays}/.
    async function exportGdml(mode, volume, objects, outPath) {
        const label = mode === 'scene' ? 'scene' : (mode === 'single' ? ('single ' + volume) : ('array ' + (objects || []).length));
        let out = outPath || null;
        if (!out) {
            const def = await window.g4care.gdmlSavePath(mode, volume || (mode === 'array' ? ('array_' + (objects || []).length) : 'scene'));
            out = await window.g4care.saveDialog({ title: 'Export GDML', defaultPath: def.defaultPath });
            if (!out) return null;
        }
        appendLog('> export GDML (' + label + ')');
        try {
            const text = geometryModel.getValue();
            let cfg = {};
            try { cfg = parse(text) || {}; } catch (e) { cfg = {}; }
            cfg.GEOMETRY = cfg.GEOMETRY || {};
            cfg.GEOMETRY.EXPORT = { ENABLE: true, FILE: out, MODE: mode };
            if (mode === 'single' && volume) cfg.GEOMETRY.EXPORT.VOLUME = volume;
            if (mode === 'array' && objects && objects.length) cfg.GEOMETRY.EXPORT.OBJECTS = objects;
            await window.g4care.restart();
            const lc = await window.g4care.rpc('loadConfigText', { text: stringify(cfg) });
            if (!lc.ok) { appendLog('  loadConfig FAILED: ' + (lc.error || '')); return null; }
            const ini = await window.g4care.rpc('init');
            if (!ini.ok) { appendLog('  init FAILED: ' + (ini.error || '')); return null; }
            const res = await window.g4care.rpc('exportGdml', {});
            appendLog('  export: ' + JSON.stringify(res));
            if (res && res.ok) {
                const gdml = await window.g4care.readFile(out);
                if (gdml) {
                    await window.g4care.writeFile(out, injectAuxTags(gdml, cfg.GEOMETRY.APPEARANCE));
                    appendLog('  -> ' + out + ' (aux tags injected)');
                } else {
                    appendLog('  -> ' + out);
                }
            }
            return out;
        } catch (e) {
            appendLog('  ERROR: ' + e.message);
            return null;
        }
    }
    const exportGdmlScene = () => exportGdml('scene');

    // Export each selected volume as a single GDML into ~/Documents/g4care/gdml/objects/,
    // then generate a YAML that loads them via GEOMETRY.OBJECTS.<id>.file.
    async function saveYamlArray(volumes) {
        if (!volumes || !volumes.length) return;
        appendLog('> save YAML array (' + volumes.length + ' volumes)');
        const objsDir = await window.g4care.gdmlSavePath('single', volumes[0]);
        const exported = [];
        for (const vol of volumes) {
            const safe = vol.replace(/[^A-Za-z0-9_-]+/g, '_');
            const file = objsDir.dir + '/' + safe + '.gdml';
            const got = await exportGdml('single', vol, null, file);
            if (got) exported.push({ id: safe, name: vol, file: got });
        }
        if (!exported.length) { appendLog('  no volumes exported'); return; }
        // Build the array YAML referencing the exported files.
        let doc = {};
        try { doc = parse(geometryModel.getValue()) || {}; } catch (e) { doc = {}; }
        doc.GEOMETRY = doc.GEOMETRY || {};
        doc.GEOMETRY.OBJECTS = doc.GEOMETRY.OBJECTS || {};
        for (const o of exported) {
            if (doc.GEOMETRY.OBJECTS[o.id]) continue;
            doc.GEOMETRY.OBJECTS[o.id] = { file: o.file };
        }
        const def = await window.g4care.gdmlSavePath('array', 'array_' + exported.length);
        const out = await window.g4care.saveDialog({ title: 'Save YAML (array)', defaultPath: def.defaultPath.replace(/\.gdml$/, '.yaml') });
        if (!out) return;
        await window.g4care.writeFile(out, stringify(doc));
        appendLog('  -> ' + out + ' (' + exported.length + ' OBJECTS.file refs)');
    }

    async function setPhantomPosition(position) {
        if (!position || !Array.isArray(position) || position.length < 3) return;
        try {
            const doc = parse(geometryModel.getValue()) || {};
            doc.GEOMETRY = doc.GEOMETRY || {};
            doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
            doc.GEOMETRY.DICOM.POSITION = { x: position[0], y: position[1], z: position[2] };
            const yaml = stringify(doc);
            geometryModel.setValue(yaml);
            await window.g4care.writeGeometryYaml(yaml).catch(() => {});
            await window.g4care.rpc('dicom.transfer.meshes', { position: { x: position[0], y: position[1], z: position[2] } });
        } catch (e) {
            appendLog('[gui] phantom position: ' + (e && e.message ? e.message : e));
        }
    }

    document.getElementById('btn-export-gdml').onclick = exportGdmlScene;
    return { exportGdmlScene, exportGdml, saveYamlArray, setPhantomPosition };
}
