//==============================================================================
// G4CARE - renderer.js — Main renderer: Monaco editor (YAML/GDML/mac tabs), panels, DICOM, run/export
//
// Main renderer: Monaco editor (YAML/GDML/mac tabs), panels, DICOM, run/export
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import './styles/theme.css';
import { icon, iconHtml, injectIconSprite } from './icons/icon.js';
import { initI18n, t, setLocale, getLocale, applyI18n, bindI18n, onLocaleChange } from './i18n/i18n.js';
import ru from './i18n/ru.json';
import en from './i18n/en.json';
import fr from './i18n/fr.json';
import de from './i18n/de.json';
import * as monaco from 'monaco-editor';
import EditorWorker from 'monaco-editor/editor/editor.worker.js?worker';
import YamlWorker from 'monaco-yaml/yaml.worker.js?worker';
import { parse, stringify } from 'yaml';
import { makeAppendLog } from './ui/log.js';
import { initCustomSelects } from './ui/csel.js';
import { initConsole } from './ui/console.js';
import { initStatus } from './panels/status.js';
import { configureYaml, registerSnippets } from './editor/monaco.js';
import { registerG4Macro } from './editor/g4macro.js';
import { createEditorTabs, languageFor } from './editor/tabs.js';
import { initExplorer } from './panels/explorer.js';
import { initSettings } from './panels/settings.js';
import { initObjectTree } from './objects/object-tree.js';
import { initEngine } from './engine.js';
import { initRootViewer } from './root-viewer.js';
import { initScriptEditor } from './script-editor.js';
import { createMprViewer, decodeVolume } from './dicom/mpr.js';
import { createGraphCfg, plotlyLayout, attachGraphMenu, renderDvh, PLOTLY_CONFIG } from './dicom/charts.js';
import Plotly from 'plotly.js-dist-min';

self.MonacoEnvironment = {
    getWorker(_workerId, label) {
        if (label === 'yaml') return new YamlWorker();
        return new EditorWorker();
    },
};

// monaco-editor 0.57 changed monaco.editor.createWebWorker to accept { worker },
// but monaco-worker-manager (dep of monaco-yaml) still calls the old signature
// { moduleId, label, createData }. Restore the old contract so the YAML language
// service worker actually starts and receives the schema (createData).
(function shimCreateWebWorker() {
    const orig = monaco.editor.createWebWorker.bind(monaco.editor);
    monaco.editor.createWebWorker = (opts) => {
        if (opts && typeof opts === 'object' && opts.moduleId !== undefined && opts.worker === undefined) {
            const env = self.MonacoEnvironment || {};
            const getWorker = typeof env.getWorker === 'function' ? env.getWorker.bind(env) : null;
            const getWorkerUrl = typeof env.getWorkerUrl === 'function' ? env.getWorkerUrl.bind(env) : null;
            let worker;
            if (getWorker) worker = getWorker(opts.moduleId, opts.label);
            else if (getWorkerUrl) worker = new Worker(getWorkerUrl(opts.moduleId, opts.label), { type: 'module' });
            else worker = Promise.reject(new Error('MonacoEnvironment.getWorker is not defined'));
            const ready = Promise.resolve(worker).then((w) => {
                w.postMessage('ignore');
                w.postMessage(opts.createData);
                return w;
            });
            return orig({ worker: ready, host: opts.host, keepIdleModels: opts.keepIdleModels });
        }
        return orig(opts);
    };
})();

const logEl = document.getElementById('log');
const consolePaneEl = document.getElementById('console-pane');
const errorsEl = document.getElementById('errors-pane');
const bar = document.getElementById('bar');
const statusEl = document.getElementById('status');
const appendLog = makeAppendLog(logEl);
const appendConsole = makeAppendLog(consolePaneEl);
const appendErrors = makeAppendLog(errorsEl);

// Inline <use href="#i-..."> references in index.html need the sprite present.
injectIconSprite();

// i18n: load dictionaries and wire the language switcher.
initI18n({ ru, en, fr, de });

initCustomSelects();

const langSel = document.getElementById('set-language');
if (langSel) {
    langSel.value = getLocale();
    langSel.onchange = () => { setLocale(langSel.value); const vf = document.getElementById('viewer-frame'); if (vf && vf.contentWindow) vf.contentWindow.postMessage({ type: 'g4care-locale', locale: langSel.value }, '*'); };
}

window.g4care.onLog((l) => {
    appendLog(l);
    if (/error|warning|fatal|fail|exception|segmentation|abort/i.test(l)) {
        appendErrors(l);
        if (/error|fatal|fail|exception|segmentation|abort/i.test(l) && window.__g4careShowErrors) window.__g4careShowErrors();
    }
});
window.g4care.onStatus((s) => { statusEl.textContent = s; });
window.g4care.onProgress((p) => {
    const total = p.total || 1;
    bar.style.width = Math.min(100, Math.max(0, Math.round((p.done / total) * 100))) + '%';
});

initConsole({ logEl, consolePaneEl, errorsEl });

async function main() {
    let schema = null;
    let snippets = null;
    let defaultConfig = '# G4CARE configuration\nEVENTS: 10\nVISUALIZATION: false\n';
    try {
        schema = await window.g4care.getSchema();
        snippets = await window.g4care.getSnippets();
        defaultConfig = await window.g4care.getDefaultConfig();
    } catch (e) {
        appendLog('[gui] failed to load schema/snippets: ' + e.message);
    }

    if (schema) {
        configureYaml(monaco, schema);
        appendLog('[gui] YAML schema loaded');
    }

    registerG4Macro(monaco);

    const geometryModel = monaco.editor.createModel(
        defaultConfig,
        'yaml',
        monaco.Uri.parse('inmemory://g4care/geometry/config.yaml'),
    );
    const editor = monaco.editor.create(document.getElementById('editor'), {
        model: geometryModel,
        theme: 'vs-dark',
        automaticLayout: true,
        minimap: { enabled: false },
        fontSize: 13,
        tabSize: 2,
    });
    initScriptEditor(monaco);
    const scriptToggle = document.getElementById('btn-script-toggle');
    if (scriptToggle) scriptToggle.onclick = () => {
        const sp = document.getElementById('script-pane');
        if (sp) {
            const on = sp.style.display !== 'flex';
            sp.style.display = on ? 'flex' : 'none';
            scriptToggle.classList.toggle('active', on);
        }
    };

    // WebGL diagnostic — the 3D geometry view (trame/VTK) needs a WebGL context.
    try {
        const c = document.createElement('canvas');
        const gl = c.getContext('webgl2') || c.getContext('webgl') || c.getContext('experimental-webgl');
        appendLog('[gui] WebGL: ' + (gl ? 'OK (' + (gl.getParameter(gl.RENDERER) || 'unknown') + ')' : 'UNAVAILABLE'));
    } catch (e) {
        appendLog('[gui] WebGL: check failed - ' + e.message);
    }

    if (snippets) registerSnippets(monaco, snippets);

    const sideConfigEl = document.getElementById('side-config');
    let treeTimer = null;
    let geoWriteTimer = null;

    // Object tree (sidebar) + per-object position properties live in ./objects/object-tree.js.
    const objectTree = initObjectTree({ geometryModel });

    // ── Editor tabs (VSCode-style): one permanent "Geometry" tab (YAML/GDML)
    //    plus closable tabs for .mac / other text files. ──
    const tabs = createEditorTabs({ editor, geometryModel, monaco, t, appendLog, sideConfigEl });

    // Push the editor's initial content to the geometry server so the 3D view
    // matches the editor at startup (not a stale /tmp config).
    window.g4care.writeGeometryYaml(geometryModel.getValue()).catch(() => {});
    geometryModel.onDidChangeContent(() => {
        if (tabs.kind === 'yaml') {
            clearTimeout(treeTimer);
            treeTimer = setTimeout(objectTree.updateObjectTree, 600);
        }
        // Live geometry preview: push the geometry text to the geometry server so
        // the 3D view rebuilds while typing (debounced). For a GDML the text is
        // XML and goes to the working copy; for YAML it goes to the config.
        clearTimeout(geoWriteTimer);
        geoWriteTimer = setTimeout(() => {
            if (tabs.kind === 'gdml') window.g4care.writeGeometryXml(geometryModel.getValue()).catch(() => {});
            else window.g4care.writeGeometryYaml(geometryModel.getValue()).catch(() => {});
        }, 800);
    });

    // ── File menu (Open / Save / Save As) ──
    async function openFile() {
        try {
            const res = await window.g4care.openFile();
            if (!res) return;
            if (res.kind === 'mac' || res.kind === 'other') {
                tabs.openTextTab(res.path, res.content, languageFor(res.path));
                return;
            }
            // Geometry file (YAML or GDML): load into the permanent geometry tab.
            tabs.loadGeometry(res.path, res.content, res.kind);
        } catch (e) { appendLog('  open ERROR: ' + e.message); }
    }

    async function saveFile() {
        try {
            const info = tabs.activeModelInfo();
            if (info.kind === 'mac' && !info.path) return saveFileAs();
            const res = await window.g4care.saveFile(info.model.getValue(), info.path);
            if (res && res.path) {
                tabs.markSaved(res.path);
                tabs.renderTabs();
                appendLog('[file] saved ' + res.path);
            }
        } catch (e) { appendLog('  save ERROR: ' + e.message); }
    }

    async function saveFileAs() {
        try {
            const info = tabs.activeModelInfo();
            const def = info.path || (info.kind === 'mac' ? 'macro.mac' : 'config.yaml');
            const res = await window.g4care.saveFileAs(info.model.getValue(), def);
            if (res && res.path) {
                tabs.markSaved(res.path);
                tabs.renderTabs();
                appendLog('[file] saved ' + res.path);
            }
        } catch (e) { appendLog('  save ERROR: ' + e.message); }
    }

    window.g4care.onMenuOpen(openFile);
    window.g4care.onMenuSave(saveFile);
    window.g4care.onMenuSaveAs(saveFileAs);



    const engine = initEngine({ geometryModel, appendLog, t });
    // The 3D viewer (iframe) posts these messages: "Export GDML" button + the
    // per-object "Сохранить как GDML" context-menu item + the multi-select bar.
    window.addEventListener('message', (ev) => {
        const d = ev.data;
        if (!d) return;
        if (d.type === 'g4care-export-gdml') engine.exportGdmlScene();
        else if (d.type === 'g4care-export-gdml-single') engine.exportGdml('single', d.volume || '');
        else if (d.type === 'g4care-export-gdml-array') engine.exportGdml('array', null, d.volumes || []);
        else if (d.type === 'g4care-save-yaml-array') engine.saveYamlArray(d.volumes || []);
        else if (d.type === 'g4care-sample-source') sampleSource(ev.source, d.name, d.n || 100);
        else if (d.type === 'g4care-phantom-position') engine.setPhantomPosition(d.position);
        else if (d.type === 'g4care-open-macro') pickMacro(ev.source, false);
        else if (d.type === 'g4care-new-macro') pickMacro(ev.source, true);
    });

    function setupDicomTab() {
        // Toolbar tab groups (Weasis-style): toggle the visible group of tools.
        document.querySelectorAll('.dcm-tgroup').forEach((btn) => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.dcm-tgroup').forEach((b) => b.classList.remove('active'));
                document.querySelectorAll('.dcm-tgroup-pane').forEach((p) => p.classList.remove('active'));
                btn.classList.add('active');
                const pane = document.getElementById('dcm-group-' + btn.dataset.group);
                if (pane) pane.classList.add('active');
            });
        });

        const loadBtn = document.getElementById('dcm-load');
        const wlSel = document.getElementById('dcm-wl');
        const chk = document.getElementById('dcm-crosshair');
        const fitBtn = document.getElementById('dcm-fit');
        const projSel = document.getElementById('dcm-proj');
        const zoomInBtn = document.getElementById('dcm-zoom-in');
        const zoomOutBtn = document.getElementById('dcm-zoom-out');
        const panBtn = document.getElementById('dcm-pan');
        const invertBtn = document.getElementById('dcm-invert');
        const magnifierBtn = document.getElementById('dcm-magnifier');
        const layoutSel = document.getElementById('dcm-layout');
        const resetWlBtn = document.getElementById('dcm-reset-wl');
        const finishBtn = document.getElementById('dcm-measure-done');
        const slabInput = document.getElementById('dcm-slab');
        const oblAz = document.getElementById('dcm-obl-az');
        const oblEl = document.getElementById('dcm-obl-el');
        const info = document.getElementById('dcm-info');
        const host = document.getElementById('dcm-mpr');
        const rtLoadBtn = document.getElementById('dcm-rtstruct-load');
        const rtSaveBtn = document.getElementById('dcm-rtstruct-save');
        rtSaveBtn.onclick = async () => {
            const data = viewer.buildRtStructExport();
            if (!data || !data.rois || !data.rois.length) { info.textContent = t('msg.noContours'); return; }
            const path = await window.g4care.saveDialog({ title: t('msg.saveRtstruct'), defaultPath: 'rtstruct.dcm', filters: [{ name: 'DICOM', extensions: ['dcm'] }] });
            if (!path) return;
            try {
                const res = await window.g4care.rpc('dicom.rtstruct.save', { file: path, rois: data.rois });
                info.textContent = (res && res.ok) ? (t('msg.rtstructSaved') + path) : (t('msg.exportError') + ': ' + ((res && res.error) || t('msg.noResponse')));
            } catch (err) {
                info.textContent = t('msg.exportError') + ': ' + (err && err.message ? err.message : err);
            }
        };
        const nrrdBtn = document.getElementById('dcm-load-nrrd');
        const resetBtn = document.getElementById('dcm-reset');
        const roiList = document.getElementById('dcm-roi-list');
        const segList = document.getElementById('dcm-seg-list');
        const segBtn = document.getElementById('dcm-segment');
        const seg5Btn = document.getElementById('dcm-segment5');
        const growModeBtn = document.getElementById('dcm-grow-mode');
        const growRunBtn = document.getElementById('dcm-grow-run');
        const growClearBtn = document.getElementById('dcm-grow-clear');
        const growTol = document.getElementById('dcm-grow-tol');
        const measureClearBtn = document.getElementById('dcm-measure-clear');
        const measureValue = document.getElementById('dcm-measure-value');
        const measureList = document.getElementById('dcm-measure-list');
        const volumeList = document.getElementById('dcm-volume-list');
        const volumeValue = document.getElementById('dcm-volume-value');
        const beamsVisible = document.getElementById('dcm-beams-visible');
        if (beamsVisible) beamsVisible.onchange = () => viewer.setBeamsVisible(beamsVisible.checked);
        const trFrom = document.getElementById('dcm-tr-from');
        const trTo = document.getElementById('dcm-tr-to');
        const trMode = document.getElementById('dcm-tr-mode');
        const trPosX = document.getElementById('dcm-tr-pos-x');
        const trPosY = document.getElementById('dcm-tr-pos-y');
        const trPosZ = document.getElementById('dcm-tr-pos-z');
        const transferBtn = document.getElementById('dcm-transfer');
        const trResult = document.getElementById('dcm-tr-result');
        let currentCtDir = null;
        let currentBeams = [];
        // HU → material table: pick a file and write GEOMETRY.DICOM.HU_TABLE so the
        // engine uses it when the voxel phantom is built (assign before transfer/run).
        const huBtn = document.getElementById('dcm-hu-table-btn');
        const huVal = document.getElementById('dcm-hu-table-value');
        if (huBtn) huBtn.onclick = async () => {
            try {
                const path = await window.g4care.chooseFile({
                    title: t('msg.huMaterialTitle'),
                    filters: [
                        { name: t('msg.huTableTitle'), extensions: ['dat', 'txt'] },
                        { name: 'All files', extensions: ['*'] },
                    ],
                });
                if (!path) return;
                const doc = parse(geometryModel.getValue()) || {};
                doc.GEOMETRY = doc.GEOMETRY || {};
                doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
                doc.GEOMETRY.DICOM.HU_TABLE = path;
                const yaml = stringify(doc);
                geometryModel.setValue(yaml);
                await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                if (huVal) { huVal.textContent = path; huVal.style.color = '#9cf'; }
                info.textContent = t('label.huTable') + ': ' + path;
            } catch (e) {
                appendLog('[gui] hu table: ' + (e && e.message ? e.message : e));
            }
        };
        // HU → density curve: pick a file and write GEOMETRY.DICOM.DENSITY_TABLE.
        const densBtn = document.getElementById('dcm-density-table-btn');
        const densVal = document.getElementById('dcm-density-table-value');
        if (densBtn) densBtn.onclick = async () => {
            try {
                const path = await window.g4care.chooseFile({
                    title: t('msg.huDensityTitle'),
                    filters: [
                        { name: t('msg.densityTableTitle'), extensions: ['dat', 'txt'] },
                        { name: 'All files', extensions: ['*'] },
                    ],
                });
                if (!path) return;
                const doc = parse(geometryModel.getValue()) || {};
                doc.GEOMETRY = doc.GEOMETRY || {};
                doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
                doc.GEOMETRY.DICOM.DENSITY_TABLE = path;
                const yaml = stringify(doc);
                geometryModel.setValue(yaml);
                await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                if (densVal) { densVal.textContent = path; densVal.style.color = '#9cf'; }
                info.textContent = t('label.density') + ': ' + path;
            } catch (e) {
                appendLog('[gui] density table: ' + (e && e.message ? e.message : e));
            }
        };
        // PARAMETERISATION toggle (regular | nested) -> GEOMETRY.DICOM.PARAMETERISATION.
        const paramSel = document.getElementById('dcm-parametrisation');
        if (paramSel) paramSel.onchange = async () => {
            try {
                const doc = parse(geometryModel.getValue()) || {};
                doc.GEOMETRY = doc.GEOMETRY || {};
                doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
                doc.GEOMETRY.DICOM.PARAMETERISATION = paramSel.value;
                const yaml = stringify(doc);
                geometryModel.setValue(yaml);
                await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                info.textContent = t('label.parametrization') + ': ' + paramSel.value;
            } catch (e) {
                appendLog('[gui] parametrisation: ' + (e && e.message ? e.message : e));
            }
        };
        if (transferBtn) transferBtn.onclick = async () => {
            const mode = parseInt(trMode.value, 10) || 0;
            const payload = viewer.buildTransferPayload(mode, parseInt(trFrom.value, 10), parseInt(trTo.value, 10));
            if (!payload || !payload.volumes.length) { trResult.textContent = t('msg.noVolumesTransfer'); return; }
            const posX = (trPosX && trPosX.value !== '') ? parseFloat(trPosX.value) : NaN;
            const posY = (trPosY && trPosY.value !== '') ? parseFloat(trPosY.value) : NaN;
            const posZ = (trPosZ && trPosZ.value !== '') ? parseFloat(trPosZ.value) : NaN;
            const hasPosition = Number.isFinite(posX) && Number.isFinite(posY) && Number.isFinite(posZ);
            if (hasPosition) payload.position = { x: posX, y: posY, z: posZ };
            transferBtn.disabled = true;
            try {
                const res = await window.g4care.rpc('dicom.transfer', payload);
                if (!res || !res.ok) { trResult.textContent = t('msg.error') + ': ' + ((res && res.error) || t('msg.noResponse')); return; }
                let txt = t('msg.transferred') + res.nx + '×' + res.ny + '×' + res.nz + t('msg.voxels') + '\n';
                for (const v of res.volumes) {
                    txt += '• ' + v.name + ' (' + v.totalVoxels + t('msg.voxelsParen');
                    if (v.material) txt += ' → ' + v.material;
                    else txt += '\n' + v.groups.map((g) => '    ' + g.material + ': ' + g.voxelCount).join('\n');
                    txt += '\n';
                }
                trResult.textContent = txt;
                const ctDir = (res && res.ctDir) || currentCtDir;
                if (ctDir) {
                    try {
                        const doc = parse(geometryModel.getValue()) || {};
                        doc.GEOMETRY = doc.GEOMETRY || {};
                        doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
                        doc.GEOMETRY.DICOM.ENABLE = true;
                        doc.GEOMETRY.DICOM.CT_DIR = ctDir;
                        doc.GEOMETRY.DICOM.SLICE_FROM = parseInt(trFrom.value, 10) || 0;
                        doc.GEOMETRY.DICOM.SLICE_TO = parseInt(trTo.value, 10) || 0;
                        doc.GEOMETRY.DICOM.MASKS_FILE = '/tmp/g4care_transfer_masks.json';
                        if (hasPosition) doc.GEOMETRY.DICOM.POSITION = { x: posX, y: posY, z: posZ };
                        const yaml = stringify(doc);
                        geometryModel.setValue(yaml);
                        await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                        txt += '\n' + t('msg.configUpdated', { dir: ctDir });
                        trResult.textContent = txt;
                    } catch (e3) {
                        txt += '\n' + t('msg.configUpdateFailed', { err: (e3 && e3.message ? e3.message : e3) });
                        trResult.textContent = txt;
                    }
                } else {
                    txt += '\n' + t('msg.unknownCtDir');
                    trResult.textContent = txt;
                }
                try {
                    const mm = await window.g4care.rpc('dicom.transfer.meshes', {});
                    if (mm && mm.ok) txt += '\n' + t('msg.meshesForGeometry') + mm.meshCount + t('msg.volumesTo') + mm.file;
                    else txt += '\n' + t('msg.meshes') + ((mm && mm.error) || t('msg.error'));
                    trResult.textContent = txt;
                } catch (e2) { /* ignore */ }
            } catch (err) {
                trResult.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                transferBtn.disabled = false;
            }
        };
        const COLOR_PRESETS = ['#ffe066', '#ff5a5a', '#59ff59', '#6699ff', '#4dffff', '#ff66ff', '#ff9933', '#ffffff'];
        const histBtn = document.getElementById('dcm-histogram');
        const histModal = document.getElementById('hist-modal');
        const histCanvas = document.getElementById('hist-plot');
        const histClose = document.getElementById('hist-close');
        const histSavePng = document.getElementById('hist-save-png');
        const volPreset = document.getElementById('dcm-vol-preset');
        const rtDoseBtn = document.getElementById('dcm-rtdose-load');
        const doseList = document.getElementById('dcm-dose-list');
        const rtPlanBtn = document.getElementById('dcm-rtplan-load');
        const dvhBtn = document.getElementById('dcm-dvh-btn');
        const dvhModal = document.getElementById('dvh-modal');
        const dvhPlot = document.getElementById('dvh-plot');
        const dvhMetrics = document.getElementById('dvh-metrics');
        const dvhClose = document.getElementById('dvh-close');
        const dvhSavePng = document.getElementById('dvh-save-png');
        const dvhExportCsv = document.getElementById('dvh-export-csv');
        const dvhLogx = document.getElementById('dvh-logx');
        let lastDvhResults = [];
        let dvhLogX = false;
        let currentRois = [];
        const roiChecked = {};
        const planSel = document.getElementById('dcm-plan-sel');
        const planDose = document.getElementById('dcm-plan-dose');
        const structInfo = document.getElementById('dcm-struct-info');
        const opacity = document.getElementById('dcm-opacity');
        const opacityVal = document.getElementById('dcm-opacity-val');
        const tabStruct = document.getElementById('dcm-tab-struct');
        const tabIsodose = document.getElementById('dcm-tab-isodose');
        const tabDraw = document.getElementById('dcm-tab-draw');
        const tabRt = document.getElementById('dcm-tab-rt');
        const paneDraw = document.getElementById('dcm-pane-draw');
        const paneRt = document.getElementById('dcm-pane-rt');
        let currentPlan = null;
        const ROI_HEX = ['#ff5a5a', '#59ff59', '#6699ff', '#ffff4d', '#ff66ff', '#4dffff', '#ff9933', '#b366ff'];
        const thumbCanvas = document.getElementById('dcm-thumb');
        const seriesInfo = document.getElementById('dcm-series-info');
        const rtBadge = document.getElementById('dcm-rt-badge');
        const rtFlags = { struct: false, dose: false, plan: false };

        let wlFromPreset = false;
        const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        const renderMeasureTable = (v) => {
            if (!v) { measureValue.innerHTML = ''; return; }
            let html = '<table style="width:100%; border-collapse:collapse; font:12px monospace;">';
            for (const line of String(v).split('\n')) {
                const i = line.indexOf(': ');
                const k = i < 0 ? line : line.slice(0, i);
                const val = i < 0 ? '' : line.slice(i + 2);
                html += '<tr><td style="padding:1px 4px 1px 0; color:var(--text-2); vertical-align:top; white-space:nowrap;">' + esc(k) + '</td>' +
                    '<td style="padding:1px 0; text-align:right; color:#ffd966; vertical-align:top;">' + esc(val) + '</td></tr>';
            }
            html += '</table>';
            measureValue.innerHTML = html;
        };
        let selectedMeasureId = null;
        let selectedMeasureValue = '';
        const collapsedGroups = new Set();
        const renderMeasureList = (list) => {
            if (!measureList) return;
            measureList.innerHTML = '';
            const labels = { length: t('measure.length'), polyline: t('measure.polyline'), angle: t('measure.angle'), cobb: t('measure.cobb'), rectangle: t('measure.rectangle'), ellipse: t('measure.ellipse'), polygon: t('measure.polygon'), spline: t('measure.spline'), curve: t('measure.curve'), freehand: t('measure.freehand'), perpendicular: t('measure.perpendicular'), parallel: t('measure.parallel') };
            const paneLabels = { axial: t('label.axial'), coronal: t('label.coronal'), sagittal: t('label.sagittal') };
            const buildRow = (m, inGroup) => {
                const row = document.createElement('div');
                row.style.cssText = 'display:flex; align-items:center; gap:6px; padding:2px 0; cursor:pointer; border-radius:3px;' + (m.id === selectedMeasureId ? ' background:#243244;' : '');
                const cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.checked = m.visible !== false;
                cb.onchange = (e) => { e.stopPropagation(); viewer.setMeasurementVisible(m.id, cb.checked); };
                const swatch = document.createElement('button');
                swatch.style.cssText = 'width:12px; height:12px; border-radius:2px; border:1px solid var(--border); cursor:pointer; flex:0 0 auto; background:' + (m.color || '#ffe066') + ';';
                swatch.title = t('tip.colorNext');
                swatch.onclick = (e) => {
                    e.stopPropagation();
                    const cur = m.color || '#ffe066';
                    const i = COLOR_PRESETS.indexOf(cur);
                    viewer.setMeasurementColor(m.id, COLOR_PRESETS[(i + 1) % COLOR_PRESETS.length]);
                };
                const nm = document.createElement('span');
                nm.style.cssText = 'color:var(--text-3); font:11px sans-serif; flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;';
                nm.textContent = (labels[m.type] || m.type) + ' · ' + (paneLabels[m.pane] || m.pane) + (m.slice != null ? t('label.sliceOf') + (m.slice + 1) : '');
                const del = document.createElement('button');
                del.textContent = '✕';
                del.style.cssText = 'background:none; border:1px solid var(--border); color:var(--text-3); cursor:pointer; font:11px sans-serif; border-radius:3px;';
                del.onclick = (e) => { e.stopPropagation(); viewer.deleteMeasurement(m.id); };
                row.onclick = () => viewer.selectMeasurement(m.id);
                row.appendChild(cb);
                if (!inGroup) {
                    const nameInput = document.createElement('input');
                    nameInput.value = m.name || '';
                    nameInput.placeholder = t('vol.name');
                    nameInput.style.cssText = 'background:var(--inset); color:var(--text-2); border:1px solid var(--border); padding:1px 4px; font:11px sans-serif; width:80px; flex:0 0 auto; border-radius:2px;';
                    nameInput.onclick = (e) => e.stopPropagation();
                    nameInput.onchange = () => viewer.setMeasurementName(m.id, nameInput.value);
                    row.appendChild(nameInput);
                }
                row.appendChild(swatch);
                row.appendChild(nm);
                row.appendChild(del);
                return row;
            };
            const groups = new Map();
            const ungrouped = [];
            (list || []).forEach((m) => {
                if (m.name) { if (!groups.has(m.name)) groups.set(m.name, []); groups.get(m.name).push(m); }
                else ungrouped.push(m);
            });
            ungrouped.forEach((m) => measureList.appendChild(buildRow(m, false)));
            for (const [name, members] of groups) {
                const collapsed = collapsedGroups.has(name);
                const head = document.createElement('div');
                head.style.cssText = 'display:flex; align-items:center; gap:6px; padding:3px 0; cursor:pointer; border-top:1px solid var(--border); margin-top:2px;';
                const arrow = document.createElement('span');
                arrow.textContent = collapsed ? '▸' : '▾';
                arrow.style.cssText = 'color:var(--text-2); width:10px; flex:0 0 auto;';
                const gcb = document.createElement('input');
                gcb.type = 'checkbox';
                gcb.checked = members.some((m) => m.visible !== false);
                gcb.onchange = (e) => { e.stopPropagation(); members.forEach((m) => viewer.setMeasurementVisible(m.id, gcb.checked)); renderMeasureList(list); };
                const gsw = document.createElement('button');
                gsw.style.cssText = 'width:12px; height:12px; border-radius:2px; border:1px solid var(--border); cursor:pointer; flex:0 0 auto; background:' + (members[0].color || '#ff66ff') + ';';
                gsw.title = t('tip.groupColor');
                gsw.onclick = (e) => { e.stopPropagation(); const cur = members[0].color || '#ff66ff'; const i = COLOR_PRESETS.indexOf(cur); members.forEach((m) => viewer.setMeasurementColor(m.id, COLOR_PRESETS[(i + 1) % COLOR_PRESETS.length])); };
                const gname = document.createElement('span');
                gname.textContent = name;
                gname.style.cssText = 'color:var(--text); font:12px sans-serif; font-weight:bold; flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;';
                const cnt = document.createElement('span');
                cnt.textContent = members.length;
                cnt.style.cssText = 'color:var(--text-3); font:11px sans-serif; flex:0 0 auto;';
                const canBuild = members.some((m) => m.type === 'rectangle' || m.type === 'ellipse' || m.type === 'polygon' || m.type === 'curve' || m.type === 'freehand');
                const gbuild = document.createElement('button');
                gbuild.textContent = '3D';
                gbuild.title = t('tip.buildVolume');
                gbuild.style.cssText = 'background:none; border:1px solid var(--border); color:#9cf; cursor:pointer; font:11px sans-serif; border-radius:3px;';
                gbuild.onclick = (e) => { e.stopPropagation(); const err = viewer.buildTumorFromContours(name); info.textContent = err || ('«' + name + '» ' + t('vol.built')); };
                const gdel = document.createElement('button');
                gdel.textContent = '✕';
                gdel.style.cssText = 'background:none; border:1px solid var(--border); color:var(--text-3); cursor:pointer; font:11px sans-serif; border-radius:3px;';
                gdel.title = t('tip.deleteGroup');
                gdel.onclick = (e) => { e.stopPropagation(); members.forEach((m) => viewer.deleteMeasurement(m.id)); };
                head.onclick = () => { if (collapsedGroups.has(name)) collapsedGroups.delete(name); else collapsedGroups.add(name); renderMeasureList(list); };
                head.appendChild(arrow);
                head.appendChild(gcb);
                head.appendChild(gsw);
                head.appendChild(gname);
                head.appendChild(cnt);
                if (canBuild) head.appendChild(gbuild);
                head.appendChild(gdel);
                measureList.appendChild(head);
                if (!collapsed) {
                    const body = document.createElement('div');
                    body.style.cssText = 'padding-left:10px;';
                    members.forEach((m) => body.appendChild(buildRow(m, true)));
                    measureList.appendChild(body);
                }
            }
        };
        const MATERIAL_PRESETS = ['', 'G4_AIR', 'G4_WATER', 'G4_LUNG_ICRP', 'G4_ADIPOSE_TISSUE_ICRP', 'G4_MUSCLE_SKELETAL_ICRP', 'G4_TISSUE_SOFT_ICRP', 'G4_BONE_COMPACT_ICRU', 'G4_BONE_CORTICAL_ICRP'];
        let selectedVolumeId = null;
        const renderVolumeInfo = (v) => {
            if (!volumeValue) return;
            if (!v) { volumeValue.innerHTML = ''; return; }
            const st = v.stats;
            const lines = [t('vol.name') + ': ' + (v.name || '')];
            if (st) {
                lines.push(t('vol.volume') + ': ' + st.volumeCc.toFixed(2) + ' cm³');
                lines.push(t('label.voxelCount') + st.count);
                lines.push('Mean: ' + st.mean.toFixed(1) + ' HU');
                lines.push('SD: ' + st.sd.toFixed(1) + ' HU');
                lines.push('Min: ' + st.min.toFixed(0) + ' HU');
                lines.push('Max: ' + st.max.toFixed(0) + ' HU');
            }
            let html = '<table style="width:100%; border-collapse:collapse; font:12px monospace;">';
            for (const line of lines) {
                const i = line.indexOf(': ');
                const k = i < 0 ? line : line.slice(0, i);
                const val = i < 0 ? '' : line.slice(i + 2);
                html += '<tr><td style="padding:1px 4px 1px 0; color:var(--text-2); vertical-align:top; white-space:nowrap;">' + esc(k) + '</td>' +
                    '<td style="padding:1px 0; text-align:right; color:#ffd966; vertical-align:top;">' + esc(val) + '</td></tr>';
            }
            html += '</table>';
            volumeValue.innerHTML = html;
        };
        const VOL_COLORS = [[1, 0.45, 0.25], [0.25, 0.6, 1.0], [0.25, 1.0, 0.4], [1.0, 0.85, 0.2], [0.9, 0.3, 0.9], [0.2, 1.0, 0.9]];
        const renderVolumeList = (vols) => {
            if (!volumeList) return;
            volumeList.innerHTML = '';
            const selGone = selectedVolumeId != null && !(vols || []).some((v) => v.id === selectedVolumeId);
            (vols || []).forEach((v) => {
                const rgb = v.color || [1, 0.45, 0.25];
                const row = document.createElement('div');
                row.style.cssText = 'display:flex; align-items:center; gap:6px; padding:2px 0; cursor:pointer; border-radius:3px;' + (v.id === selectedVolumeId ? ' background:#243244;' : '');
                row.onclick = () => { selectedVolumeId = v.id; renderVolumeList(viewer.getVolumes()); renderVolumeInfo(v); };
                const cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.checked = v.visible !== false;
                cb.onclick = (e) => e.stopPropagation();
                cb.onchange = () => viewer.setVolumeVisible(v.id, cb.checked);
                const swatch = document.createElement('button');
                swatch.style.cssText = 'width:12px; height:12px; border-radius:2px; border:1px solid var(--border); cursor:pointer; flex:0 0 auto; background:rgb(' + Math.round(rgb[0] * 255) + ',' + Math.round(rgb[1] * 255) + ',' + Math.round(rgb[2] * 255) + ');';
                swatch.title = t('tip.color');
                swatch.onclick = (e) => {
                    e.stopPropagation();
                    const cur = v.color || [1, 0.45, 0.25];
                    const idx = VOL_COLORS.findIndex((c) => Math.abs(c[0] - cur[0]) < 0.01 && Math.abs(c[1] - cur[1]) < 0.01 && Math.abs(c[2] - cur[2]) < 0.01);
                    viewer.setVolumeColor(v.id, VOL_COLORS[(idx + 1) % VOL_COLORS.length]);
                };
                const nameInput = document.createElement('input');
                nameInput.value = v.name || '';
                nameInput.style.cssText = 'background:var(--inset); color:var(--text-2); border:1px solid var(--border); padding:1px 4px; font:11px sans-serif; width:68px; flex:0 0 auto; border-radius:2px;';
                nameInput.onclick = (e) => e.stopPropagation();
                nameInput.onchange = () => viewer.setVolumeName(v.id, nameInput.value);
                const stats = document.createElement('span');
                stats.style.cssText = 'color:var(--text-3); font:11px sans-serif; flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;';
                const st = v.stats;
                stats.textContent = st ? (st.volumeCc.toFixed(1) + t('label.cm3') + Math.round(st.mean) + ' HU') : '';
                stats.title = st ? (t('vol.volume') + ': ' + st.volumeCc.toFixed(2) + t('label.cm3Mean') + st.mean.toFixed(1) + ' HU  SD: ' + st.sd.toFixed(1) + '\nMin: ' + st.min.toFixed(0) + '  Max: ' + st.max.toFixed(0)) : '';
                const del = document.createElement('button');
                del.textContent = '✕';
                del.style.cssText = 'background:none; border:1px solid var(--border); color:var(--text-3); cursor:pointer; font:11px sans-serif; border-radius:3px;';
                del.onclick = (e) => { e.stopPropagation(); viewer.deleteVolume(v.id); };
                row.appendChild(cb);
                row.appendChild(swatch);
                row.appendChild(nameInput);
                row.appendChild(stats);
                row.appendChild(del);
                volumeList.appendChild(row);
            });
            if (selGone) { selectedVolumeId = null; renderVolumeInfo(null); }
        };
        const viewer = createMprViewer(host, {
            onMeasureChange: (v) => { if (v) renderMeasureTable(v); else renderMeasureTable(selectedMeasureValue); },
            onMeasurementsChange: (list) => { renderMeasureList(list); saveState(); },
            onVolumesChange: (vols) => { renderVolumeList(vols); saveState(); },
            onSelectionChange: (m) => {
                selectedMeasureId = m ? m.id : null;
                selectedMeasureValue = m ? m.value : '';
                renderMeasureList(viewer.getMeasurements());
                renderMeasureTable(selectedMeasureValue);
            },
            onToggleLayers: () => {
                const p = document.getElementById('dcm-rt-panel');
                if (p) p.style.display = (p.style.display === 'none') ? 'flex' : 'none';
            },
            onSlabChange: (v) => { slabInput.value = v; },
            onWindowLevel: (o) => {
                if (wlFromPreset) return;
                let sel = '';
                for (const pv of ['400,40', '1500,-600', '2000,500']) {
                    const [w, l] = pv.split(',').map(parseFloat);
                    if (Math.abs(w - o.window) < 1 && Math.abs(l - o.level) < 1) { sel = pv; break; }
                }
                wlSel.value = sel;
            },
        });

        function updateSeriesInfo(meta) {
            const patientName = document.getElementById('dcm-patient-name');
            const patientId = document.getElementById('dcm-patient-id');
            const studyLine = document.getElementById('dcm-study-line');
            if (patientName) patientName.textContent = (meta.patientName ? meta.patientName.trim() : t('dicom.patientEmpty'));
            if (patientId) patientId.textContent = meta.patientId ? ('ID: ' + meta.patientId.trim()) : '';
            let study = t('label.studyPrefix') + (meta.studyId ? meta.studyId.trim() : '—');
            if (meta.acquisitionDate) study += ' · ' + meta.acquisitionDate.trim();
            if (meta.institutionName) study += '<br>' + meta.institutionName.trim();
            if (studyLine) studyLine.innerHTML = study;
            let html = '';
            if (meta.seriesNumber) html += t('label.seriesPrefix') + meta.seriesNumber.trim();
            if (meta.modality) html += (html ? '<br>' : '') + meta.modality;
            html += (html ? '<br>' : '') + meta.nx + '×' + meta.ny + '×' + meta.nz + ' (' + meta.nz + t('label.slicesSuffix');
            html += '<br>' + (+meta.dx).toFixed(2) + '×' + (+meta.dy).toFixed(2) + '×' + (+meta.dz).toFixed(2) + t('label.mm');
            seriesInfo.innerHTML = html;
        }

        function renderThumbnail() {
            const t = viewer.getThumbnail();
            if (!t) return;
            const ctx = thumbCanvas.getContext('2d');
            const W = thumbCanvas.width, H = thumbCanvas.height;
            const img = ctx.createImageData(W, H);
            const st = viewer.getState();
            const lo = st.level - st.window / 2;
            const hi = st.level + st.window / 2;
            const w = t.w, h = t.h;
            for (let y = 0; y < H; y++) {
                const sy = Math.min(h - 1, Math.floor(y / H * h));
                for (let x = 0; x < W; x++) {
                    const sx = Math.min(w - 1, Math.floor(x / W * w));
                    const v = t.data[sy * w + sx];
                    const g = Math.max(0, Math.min(255, Math.round((v - lo) / (hi - lo) * 255)));
                    const idx = (y * W + x) * 4;
                    img.data[idx] = g; img.data[idx + 1] = g; img.data[idx + 2] = g; img.data[idx + 3] = 255;
                }
            }
            ctx.putImageData(img, 0, 0);
        }

        function updateRtBadge() {
            rtBadge.style.display = (rtFlags.struct || rtFlags.dose || rtFlags.plan) ? 'flex' : 'none';
        }

        // Load the engine's current volume (already loaded via dicom.load or
        // volume.load) into the viewer. Returns null on success or an error string.
        async function applyLoaded(meta) {
            if (!meta || !meta.ok) return t('msg.error') + ': ' + ((meta && meta.error) || t('msg.noData'));
            const vol = await window.g4care.rpc('dicom.volume', {});
            if (!vol || !vol.ok) return t('msg.volumeError') + ((vol && vol.error) || t('msg.noData'));
            const valueType = vol.valueType || meta.valueType || 'int16';
            viewer.load(meta, decodeVolume(vol.data, valueType));
            viewer.resize();
            const emptyEl = document.getElementById('dcm-empty');
            if (emptyEl) emptyEl.style.display = 'none';
            if (meta.modality !== 'CT') viewer.autoWindowLevel();
            updateSeriesInfo(meta);
            renderThumbnail();
            viewer.setOverlayInfo(meta);
            currentMetaKey = meta.nx + '|' + meta.ny + '|' + meta.nz + '|' + (+meta.dx).toFixed(3) + '|' + (+meta.dy).toFixed(3) + '|' + (+meta.dz).toFixed(3);
            currentMeta = meta;
            restoreSavedState();
            if (trFrom) trFrom.value = 0;
            if (trTo) trTo.value = meta.nz ? (meta.nz - 1) : 0;
            info.textContent = meta.nx + '×' + meta.ny + '×' + meta.nz +
                '  ' + (+meta.dx).toFixed(2) + '×' + (+meta.dy).toFixed(2) + '×' + (+meta.dz).toFixed(2) + t('label.mmSep') + (meta.modality || '?');
            return null;
        }

        loadBtn.onclick = async () => {
            const dir = await window.g4care.chooseDirectory();
            if (!dir) return;
            currentCtDir = dir;
            loadBtn.disabled = true;
            info.textContent = t('msg.loadingCt');
            try {
                const e = await applyLoaded(await window.g4care.rpc('dicom.load', { directory: dir }));
                if (e) info.textContent = e;
            } catch (err) {
                info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                loadBtn.disabled = false;
            }
        };

        nrrdBtn.onclick = async () => {
            const file = await window.g4care.chooseFile({ title: t('msg.chooseNrrd') });
            if (!file) return;
            nrrdBtn.disabled = true;
            info.textContent = t('msg.loadingVolume');
            try {
                const e = await applyLoaded(await window.g4care.rpc('volume.load', { file }));
                if (e) info.textContent = e;
            } catch (err) {
                info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                nrrdBtn.disabled = false;
            }
        };

        // Reset the whole DICOM session (volume + RT overlays + UI lists) so a
        // different patient / study can be loaded cleanly.
        let currentMetaKey = null;
        let currentMeta = null;
        const resetDicomUi = () => {
            currentMetaKey = null;
            currentMeta = null;
            currentRois = [];
            currentPlan = null;
            rtFlags.struct = rtFlags.dose = rtFlags.plan = false;
            for (const k of Object.keys(roiChecked)) delete roiChecked[k];
            updateRtBadge();
            roiList.innerHTML = '';
            doseList.innerHTML = '';
            segList.innerHTML = '';
            planSel.innerHTML = '';
            bindI18n(planDose, 'dicom.doseEmpty');
            structInfo.innerHTML = '';
            bindI18n(seriesInfo, 'label.seriesEmpty');
            const pn = document.getElementById('dcm-patient-name');
            if (pn) bindI18n(pn, 'dicom.patientEmpty');
            const pid = document.getElementById('dcm-patient-id');
            if (pid) pid.textContent = '';
            const sl = document.getElementById('dcm-study-line');
            if (sl) bindI18n(sl, 'dicom.studyEmpty');
            const tc = thumbCanvas.getContext('2d');
            tc.clearRect(0, 0, thumbCanvas.width, thumbCanvas.height);
            bindI18n(info, 'label.ctNotLoaded');
            const emptyEl2 = document.getElementById('dcm-empty');
            if (emptyEl2) emptyEl2.style.display = 'flex';
            measureValue.textContent = '';
            lastDvhResults = [];
            dvhMetrics.innerHTML = '';
            wlSel.value = '400,40';
            slabInput.value = '0';
            oblAz.value = '0';
            oblEl.value = '0';
            document.querySelectorAll('.dcm-tool-btn').forEach((b) => b.classList.remove('active'));
            if (layoutSel) {
                const mprBtn = layoutSel.querySelector('[data-layout="mpr"]');
                if (mprBtn) mprBtn.classList.add('active');
            }
            growModeBtn.classList.remove('active');
            growModeBtn.textContent = t('label.seeds');
        };
        resetBtn.onclick = () => {
            viewer.reset();
            try { localStorage.removeItem(SAVE_KEY); } catch (e) { /* ignore */ }
            resetDicomUi();
        };

        // Re-apply dynamic DICOM labels when the locale changes (empty state or loaded state).
        onLocaleChange(() => {
            if (currentMeta) {
                updateSeriesInfo(currentMeta);
                info.textContent = currentMeta.nx + '×' + currentMeta.ny + '×' + currentMeta.nz +
                    '  ' + (+currentMeta.dx).toFixed(2) + '×' + (+currentMeta.dy).toFixed(2) + '×' + (+currentMeta.dz).toFixed(2) + t('label.mmSep') + (currentMeta.modality || '?');
            } else {
                resetDicomUi();
            }
        });
        resetDicomUi();

        wlSel.onchange = () => {
            if (!wlSel.value) return;
            const [w, l] = wlSel.value.split(',').map(parseFloat);
            wlFromPreset = true;
            viewer.setWindowLevel(w, l);
            wlFromPreset = false;
        };

        chk.onchange = () => viewer.setCrosshairsVisible(chk.checked);

        fitBtn.onclick = () => viewer.fit();
        projSel.onchange = () => viewer.setProjectionMode(projSel.value);
        zoomInBtn.onclick = () => viewer.setZoom(0.8);
        zoomOutBtn.onclick = () => viewer.setZoom(1.25);
        panBtn.onclick = () => {
            const on = panBtn.classList.toggle('active');
            viewer.setPanMode(on);
            panBtn.textContent = on ? t('msg.panOn') : t('dicom.pan');
        };
        invertBtn.onclick = () => { const on = invertBtn.classList.toggle('active'); viewer.setInvert(on); };
        magnifierBtn.onclick = () => { const on = magnifierBtn.classList.toggle('active'); viewer.setMagnifier(on); magnifierBtn.textContent = on ? t('msg.magnifierOn') : t('dicom.magnifier'); };
        Array.from(layoutSel.querySelectorAll('[data-layout]')).forEach((btn) => {
            btn.onclick = () => {
                layoutSel.querySelectorAll('[data-layout]').forEach((b) => b.classList.remove('active'));
                btn.classList.add('active');
                viewer.setLayout(btn.dataset.layout);
            };
        });
        resetWlBtn.onclick = () => viewer.resetWindowLevel();
        finishBtn.onclick = () => viewer.finishMeasurement();

        slabInput.oninput = () => viewer.setSlabThickness(parseFloat(slabInput.value) || 0);

        const applyOblique = () => viewer.setOblique(parseFloat(oblAz.value) || 0, parseFloat(oblEl.value) || 0);
        oblAz.oninput = applyOblique;
        oblEl.oninput = applyOblique;

        rtLoadBtn.onclick = async () => {
            const file = await window.g4care.chooseFile({ title: t('msg.chooseRtstruct') });
            if (!file) return;
            rtLoadBtn.disabled = true;
            try {
                const res = await window.g4care.rpc('dicom.rtstruct.load', { file });
                if (!res || !res.ok) { info.textContent = t('msg.rtstructError') + ': ' + ((res && res.error) || t('msg.noData')); return; }
                viewer.setRois(res.rois || []);
                currentRois = res.rois || [];
                rtFlags.struct = true;
                updateRtBadge();
                // Build the structure tree (Weasis-style) with type tags.
                roiList.innerHTML = '';
                currentRois.forEach((roi, idx) => {
                    roiChecked[roi.number] = true;
                    const row = document.createElement('div');
                    row.style.cssText = 'display:flex; align-items:center; gap:6px; padding:3px 4px; cursor:pointer; border-radius:3px;';
                    const cb = document.createElement('input');
                    cb.type = 'checkbox';
                    cb.checked = true;
                    cb.onchange = (e) => { e.stopPropagation(); roiChecked[roi.number] = cb.checked; viewer.setRoiVisible(roi.number, cb.checked); };
                    const dot = document.createElement('span');
                    dot.style.cssText = 'width:9px; height:9px; border-radius:2px; display:inline-block; background:' + ROI_HEX[idx % ROI_HEX.length] + ';';
                    const nm = document.createElement('span');
                    nm.style.cssText = 'color:var(--text-2); font:12px sans-serif; flex:1;';
                    nm.textContent = roi.name || ('ROI ' + roi.number);
                    const tp = document.createElement('span');
                    tp.style.cssText = 'color:var(--text-3); font:10px sans-serif;';
                    tp.textContent = roi.type ? '[' + roi.type + ']' : '';
                    row.appendChild(cb);
                    row.appendChild(dot);
                    row.appendChild(nm);
                    row.appendChild(tp);
                    row.onclick = () => showStructInfo(roi);
                    roiList.appendChild(row);
                });
                info.textContent = 'RTSTRUCT: ' + res.rois.length + ' ROI';
            } catch (err) {
                info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                rtLoadBtn.disabled = false;
            }
        };

        // Load an RTDOSE (explicit path — no dialog) and show the isodose
        // overlay. Shared by the DICOM "Загрузить RTDOSE" button and by the
        // Geometry "Экспорт RTDOSE" flow (auto-open after export).
        async function loadRtDose(file) {
            if (!file) return;
            rtDoseBtn.disabled = true;
            info.textContent = t('msg.loadingRtdose');
            try {
                const res = await window.g4care.rpc('dicom.rtdose.load', { file });
                if (!res || !res.ok) { info.textContent = t('msg.rtdoseError') + ': ' + ((res && res.error) || t('msg.noData')); return; }
                const data = decodeVolume(res.data || '', 'float32');
                const st = viewer.getState();
                const ctMatch = st && st.loaded && Array.isArray(st.dims)
                    && st.dims[0] === res.nx && st.dims[1] === res.ny && st.dims[2] === res.nz;
                if (!ctMatch) {
                    // No matching CT loaded: show the dose as the main volume
                    // (legacy behaviour — a standalone RTDOSE opens like an image).
                    const meta = {
                        nx: res.nx, ny: res.ny, nz: res.nz,
                        dx: res.dx || 1, dy: res.dy || 1, dz: res.dz || 1,
                        origin: (res.origin && res.origin.length >= 3) ? res.origin.slice(0, 3) : [0, 0, 0],
                        direction: res.direction,
                        valueType: 'float32',
                        modality: 'RTDOSE',
                    };
                    viewer.load(meta, data);
                    viewer.autoWindowLevel();
                    updateSeriesInfo(meta);
                    renderThumbnail();
                }
                // Overlay/refresh the isodose on the current volume.
                const levels = viewer.setDoseGrid({ doseGy: data });
                rtFlags.dose = true;
                updateRtBadge();
                // Re-rasterize any already-loaded structures onto the new dose
                // grid so contours and DVH stay consistent regardless of order.
                if (currentRois.length) {
                    viewer.setRois(currentRois);
                    currentRois.forEach((roi) => viewer.setRoiVisible(roi.number, roiChecked[roi.number] !== false));
                }
                doseList.innerHTML = '';
                (levels || []).forEach((lv, i) => {
                    const lbl = document.createElement('label');
                    lbl.style.cssText = 'display:flex; align-items:center; gap:4px; color:var(--text-2); font:12px sans-serif; cursor:pointer;';
                    const cb = document.createElement('input');
                    cb.type = 'checkbox';
                    cb.checked = true;
                    cb.onchange = () => viewer.setDoseLevelVisible(i, cb.checked);
                    const dot = document.createElement('span');
                    dot.style.cssText = 'width:9px; height:9px; border-radius:2px; display:inline-block; background:rgb(' +
                        Math.round(lv.color[0] * 255) + ',' + Math.round(lv.color[1] * 255) + ',' + Math.round(lv.color[2] * 255) + ');';
                    const span = document.createElement('span');
                    span.textContent = Math.round(lv.fraction * 100) + '% (' + lv.gy.toFixed(1) + ' Gy)';
                    lbl.appendChild(cb);
                    lbl.appendChild(dot);
                    lbl.appendChild(span);
                    doseList.appendChild(lbl);
                });
                info.textContent = 'RTDOSE: ' + res.nx + '×' + res.ny + '×' + res.nz + ' (' + res.doseUnits + ')';
            } catch (err) {
                info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                rtDoseBtn.disabled = false;
            }
        }

        rtDoseBtn.onclick = async () => {
            const file = await window.g4care.chooseFile({ title: t('msg.chooseRtdose') });
            if (file) await loadRtDose(file);
        };

        // Экспорт RTDOSE: save the simulated dose grid, then open it in DICOM.
        async function exportRtDose() {
            const geomInfo = document.getElementById('geom-export-info');
            const setInfo = (t) => { if (geomInfo) geomInfo.textContent = t; info.textContent = t; };
            try {
                const path = await window.g4care.saveDialog({ title: t('msg.saveRtdose'), defaultPath: 'rtdose.dcm', filters: [{ name: 'DICOM', extensions: ['dcm'] }] });
                if (!path) return;
                const res = await window.g4care.rpc('dicom.rtdose.save', { file: path });
                if (res && res.ok) {
                    setInfo(t('msg.rtdoseSaved') + path + ' (' + res.nx + '×' + res.ny + '×' + res.nz + ')');
                    switchTab('dicom');
                    await loadRtDose(path);
                } else {
                    setInfo(t('msg.rtdoseExportError') + ((res && res.error) || t('msg.noResponse')));
                }
            } catch (err) {
                setInfo(t('msg.rtdoseExportError') + (err && err.message ? err.message : err));
            }
        }

        // Load an RTPLAN (explicit path — no dialog) and show its beams in the
        // MPR 3D view. Shared by the DICOM "Загрузить RTPLAN" button and by the
        // Geometry "Экспорт RTPLAN" flow (auto-open after export).
        async function loadRtPlan(file) {
            if (!file) return;
            rtPlanBtn.disabled = true;
            try {
                const res = await window.g4care.rpc('dicom.rtplan.load', { file });
                if (!res || !res.ok) { info.textContent = t('msg.rtplanError') + ': ' + ((res && res.error) || t('msg.noData')); return; }
                viewer.setBeams(res.beams || []);
                currentBeams = res.beams || [];
                // Persist RTPLAN_FILE for engine logging / export reference.
                try {
                    const doc = parse(geometryModel.getValue()) || {};
                    doc.GEOMETRY = doc.GEOMETRY || {};
                    doc.GEOMETRY.DICOM = doc.GEOMETRY.DICOM || {};
                    doc.GEOMETRY.DICOM.RTPLAN_FILE = file;
                    const yaml = stringify(doc);
                    geometryModel.setValue(yaml);
                    await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                } catch (e) { /* ignore */ }
                currentPlan = { label: res.label || '', rxCGy: res.prescriptionDoseCGy || 0 };
                rtFlags.plan = true;
                updateRtBadge();
                planSel.innerHTML = '';
                const pOpt = document.createElement('option');
                pOpt.value = '0';
                pOpt.textContent = currentPlan.label ? (currentPlan.label + ' (Rx ' + currentPlan.rxCGy + ' cGy)') : 'Plan';
                planSel.appendChild(pOpt);
                planDose.textContent = currentPlan.rxCGy > 0 ? (t('label.dose') + ': ' + currentPlan.rxCGy + t('msg.cGy')) : t('dicom.doseEmpty');
                info.textContent = 'RTPLAN: ' + (res.beams || []).length + t('msg.beams');
            } catch (err) {
                info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
            } finally {
                rtPlanBtn.disabled = false;
            }
        }

        rtPlanBtn.onclick = async () => {
            const file = await window.g4care.chooseFile({ title: t('msg.chooseRtplan') });
            if (file) await loadRtPlan(file);
        };

        // Import loaded RTPLAN beams into SOURCE as editable particleGun sources.
        const importBtn = document.getElementById('dcm-rtplan-import');
        if (importBtn) importBtn.onclick = async () => {
            if (!currentBeams.length) { info.textContent = t('msg.loadRtplanFirst'); return; }
            try {
                const doc = parse(geometryModel.getValue()) || {};
                doc.SOURCE = doc.SOURCE || {};
                const rtParticle = (rt) => rt === 'ELECTRON' ? 'e-' : rt === 'PROTON' ? 'proton' : rt === 'NEUTRON' ? 'neutron' : 'gamma';
                const rnd = (v) => Math.round(v * 1e6) / 1e6;
                let added = 0;
                for (const beam of currentBeams) {
                    if (!beam.controlPoints || !beam.controlPoints.length) continue;
                    const cp = beam.controlPoints[0];
                    const g = (cp.gantryAngleDeg || 0) * Math.PI / 180;
                    const c = (cp.couchAngleDeg || 0) * Math.PI / 180;
                    const dir = [Math.sin(g) * Math.cos(c), Math.sin(g) * Math.sin(c), -Math.cos(g)];
                    const iso = cp.isocenter || [0, 0, 0];
                    const sad = (beam.sadMm > 0) ? beam.sadMm : 1000;
                    const pos = [iso[0] - dir[0] * sad, iso[1] - dir[1] * sad, iso[2] - dir[2] * sad];
                    doc.SOURCE['beam_' + beam.number] = {
                        type: 'particleGun',
                        particle: rtParticle(beam.radiationType),
                        energy: cp.energyMeV + '*MeV',
                        direction: { x: rnd(dir[0]), y: rnd(dir[1]), z: rnd(dir[2]) },
                        isocenter: { x: iso[0], y: iso[1], z: iso[2] },
                        pos: { x: rnd(pos[0]), y: rnd(pos[1]), z: rnd(pos[2]) },
                    };
                    added++;
                }
                const yaml = stringify(doc);
                geometryModel.setValue(yaml);
                await window.g4care.writeGeometryYaml(yaml).catch(() => {});
                info.textContent = t('msg.importedBeams') + ': ' + added;
            } catch (e) {
                appendLog('[gui] rtplan import: ' + (e && e.message ? e.message : e));
            }
        };

        // Экспорт RTPLAN из пучков, расставленных в Geometry (SOURCE.<name>).
        // After a successful save, switch to the DICOM tab and open the plan.
        async function exportRtPlan() {
            const geomInfo = document.getElementById('geom-export-info');
            const toNum = (v) => { const x = parseFloat(v); return Number.isFinite(x) ? x : 0; };
            const toMeV = (s) => {
                if (s == null) return 0;
                const m = String(s).trim().match(/^([-+0-9.eE]+)\s*\*?\s*([A-Za-z]*)$/);
                if (!m) return toNum(s);
                let v = parseFloat(m[1]);
                const u = m[2].toLowerCase();
                if (u === 'kev') v /= 1000; else if (u === 'gev') v *= 1000;
                return v;
            };
            const setInfo = (t) => { if (geomInfo) geomInfo.textContent = t; info.textContent = t; };
            try {
                // Read the freshest geometry YAML (the Geometry viewer writes
                // SOURCE.<name> there) instead of the possibly-stale editor.
                let text = null;
                try { text = await window.g4care.readGeometryYaml(); } catch (e) { /* ignore */ }
                if (!text) text = geometryModel.getValue();
                const doc = parse(text) || {};
                const src = doc.SOURCE || {};
                const beams = [];
                let num = 0;
                for (const [name, s] of Object.entries(src)) {
                    if (!s || typeof s !== 'object' || name === 'SOURCE_MODE') continue;
                    if (!s.particle) continue;
                    num++;
                    // Isocenter: explicit RT beam field wins; otherwise use the
                    // gun's emission point (pos) so the drawn beam passes through
                    // where the source actually sits instead of [0,0,0].
                    const iso = s.isocenter ? [toNum(s.isocenter.x), toNum(s.isocenter.y), toNum(s.isocenter.z)]
                        : (s.pos ? [toNum(s.pos.x), toNum(s.pos.y), toNum(s.pos.z)] : [0, 0, 0]);
                    const cp = { energyMeV: toMeV(s.energy), gantryAngleDeg: 0, couchAngleDeg: 0, isocenter: iso };
                    if (s.direction) {
                        const dx = toNum(s.direction.x), dy = toNum(s.direction.y), dz = toNum(s.direction.z);
                        // Inverse of IEC 61217: dir = (sin g cos c, sin g sin c, -cos g).
                        cp.gantryAngleDeg = Math.atan2(Math.hypot(dx, dy), -dz) * 180 / Math.PI;
                        cp.couchAngleDeg = Math.atan2(dy, dx) * 180 / Math.PI;
                    }
                    beams.push({ number: num, name, particle: s.particle, controlPoints: [cp] });
                }
                if (!beams.length) { setInfo(t('msg.noBeamsInSource')); return; }
                const path = await window.g4care.saveDialog({ title: t('msg.saveRtplan'), defaultPath: 'rtplan.dcm', filters: [{ name: 'DICOM', extensions: ['dcm'] }] });
                if (!path) return;
                const res = await window.g4care.rpc('dicom.rtplan.save', { file: path, label: 'G4CARE Plan', prescriptionDoseCGy: 0, beams });
                if (res && res.ok) {
                    setInfo(t('msg.rtplanSaved') + path + ' (' + beams.length + t('msg.rtplanBeams'));
                    switchTab('dicom');
                    await loadRtPlan(path);
                } else {
                    setInfo(t('msg.rtplanExportError') + ((res && res.error) || t('msg.noResponse')));
                }
            } catch (err) {
                setInfo(t('msg.rtplanExportError') + (err && err.message ? err.message : err));
            }
        }

        const DVH_COLORS = ['#ff5a5a', '#59ff59', '#6699ff', '#ffff4d', '#ff66ff', '#4dffff', '#ff9933', '#b366ff'];

        // Plotly graph settings (configurable via right-click context menu).
        const graphCfg = createGraphCfg();
        dvhClose.onclick = () => { dvhModal.style.display = 'none'; };

        dvhLogx.onchange = () => {
            dvhLogX = dvhLogx.checked;
            graphCfg.xScale = dvhLogX ? 'log' : 'linear';
            if (lastDvhResults.length) renderDvh(lastDvhResults, graphCfg);
        };

        dvhSavePng.onclick = () => { Plotly.downloadImage('dvh-plot', { format: 'png', filename: 'dvh', width: 1000, height: 560 }); };

        dvhExportCsv.onclick = async () => {
            if (!lastDvhResults.length) return;
            const lines = ['Structure,Volume (cm3),Min (Gy),Max (Gy),Mean (Gy),D98 (Gy),D95 (Gy),D50 (Gy),D2cc (Gy)'];
            for (const r of lastDvhResults) {
                const d = r.dvh;
                lines.push([r.name, d.volumeCc.toFixed(4), d.minGy.toFixed(4), d.maxGy.toFixed(4),
                    d.meanGy.toFixed(4), d.d98.toFixed(4), d.d95.toFixed(4), d.d50.toFixed(4), d.d2cc.toFixed(4)].join(','));
            }
            await window.g4care.saveFileAs(lines.join('\n'), null);
        };

        dvhBtn.onclick = () => {
            if (!currentRois.length) { info.textContent = t('msg.loadRtstructFirst'); return; }
            const results = [];
            currentRois.forEach((roi, idx) => {
                if (roiChecked[roi.number] === false) return;
                const dvh = viewer.getDvh(roi.number);
                if (!dvh) return;
                results.push({
                    name: roi.name || ('ROI ' + roi.number),
                    color: DVH_COLORS[idx % DVH_COLORS.length],
                    dvh,
                });
            });
            if (!results.length) { info.textContent = t('msg.noDvh'); return; }
            renderDvh(results);
            lastDvhResults = results;
            dvhMetrics.innerHTML = results.map((r) => {
                const d = r.dvh;
                return '<div style="color:' + r.color + ';">' + r.name +
                    ': V=' + d.volumeCc.toFixed(1) + ' cc, D98=' + d.d98.toFixed(2) +
                    ' Gy, D95=' + d.d95.toFixed(2) + ' Gy, D50=' + d.d50.toFixed(2) +
                    ' Gy, D2cc=' + d.d2cc.toFixed(2) + ' Gy, mean=' + d.meanGy.toFixed(2) + ' Gy</div>';
            }).join('');
            dvhModal.style.display = 'block';
        };

        attachGraphMenu(dvhPlot, () => { if (lastDvhResults.length) renderDvh(lastDvhResults); });

        function showStructInfo(roi) {
            const st = viewer.getState();
            const dz = (st.spacing && st.spacing[2]) || null;
            const stats = viewer.getRoiStats(roi.number);
            const dvh = viewer.getDvh(roi.number);
            const rxGy = (currentPlan && currentPlan.rxCGy > 0) ? (currentPlan.rxCGy / 100) : null;
            let html = '<b>' + (roi.name || ('ROI ' + roi.number)) + '</b>';
            if (roi.type) html += ' <span style="color:var(--text-3)">[' + roi.type + ']</span>';
            html += '<br>';
            if (dz != null) html += 'Thickness: ' + dz.toFixed(2) + ' mm<br>';
            if (stats) {
                html += 'Volume: ' + stats.volumeCc.toFixed(4) + ' cm³ (' + stats.count + ' vox)<br>';
                const unit = st.doseLoaded ? 'Gy' : 'HU';
                html += 'Min/Mean/Max: ' + stats.min.toFixed(1) + ' / ' + stats.mean.toFixed(1) + ' / ' + stats.max.toFixed(1) + ' ' + unit +
                    ' (σ ' + stats.stdev.toFixed(1) + ')<br>';
            }
            if (dvh && rxGy) {
                html += t('msg.rxDose') + (dvh.minGy / rxGy * 100).toFixed(2) + '% / ' +
                    (dvh.maxGy / rxGy * 100).toFixed(2) + '% / ' + (dvh.meanGy / rxGy * 100).toFixed(2) + '%';
            }
            structInfo.innerHTML = html;
        }

        opacity.oninput = () => {
            const v = parseInt(opacity.value, 10) / 100;
            opacityVal.textContent = opacity.value + '%';
            for (const roi of currentRois) viewer.setRoiOpacity(roi.number, v);
        };

        tabStruct.onclick = () => {
            tabStruct.style.background = 'var(--panel)'; tabStruct.style.color = 'var(--text)';
            tabIsodose.style.background = 'var(--panel-2)'; tabIsodose.style.color = 'var(--text-3)';
            roiList.style.display = 'flex';
            doseList.style.display = 'none';
        };
        tabIsodose.onclick = () => {
            tabIsodose.style.background = 'var(--panel)'; tabIsodose.style.color = 'var(--text)';
            tabStruct.style.background = 'var(--panel-2)'; tabStruct.style.color = 'var(--text-3)';
            roiList.style.display = 'none';
            doseList.style.display = 'flex';
        };

        

        // ── Grow-from-seeds (region growing) UI ──
        const SAVE_KEY = 'g4care_contours_v1';
        const saveState = (() => {
            let timer = null;
            return () => {
                if (timer) clearTimeout(timer);
                timer = setTimeout(() => {
                    if (!currentMetaKey) return;
                    try {
                        const payload = viewer.serializeState();
                        localStorage.setItem(SAVE_KEY, JSON.stringify({ key: currentMetaKey, measurements: payload.measurements, volumes: payload.volumes }));
                    } catch (e) { /* ignore quota errors */ }
                }, 400);
            };
        })();
        const restoreSavedState = () => {
            try {
                const raw = localStorage.getItem(SAVE_KEY);
                if (!raw) return;
                const parsed = JSON.parse(raw);
                if (parsed.key !== currentMetaKey) return;
                viewer.restoreState({ measurements: parsed.measurements, volumes: parsed.volumes });
            } catch (e) { /* ignore */ }
        };
        if (seg5Btn) seg5Btn.onclick = () => {
            const classes = [
                { name: t('material.bone'), min: 300, max: 4000, color: [1.0, 0.85, 0.3] },
                { name: t('material.soft'), min: -50, max: 300, color: [0.3, 1.0, 0.4] },
                { name: t('material.fat'), min: -100, max: -50, color: [0.9, 0.75, 0.5] },
                { name: t('material.lung'), min: -900, max: -400, color: [0.3, 0.6, 1.0] },
                { name: t('material.air'), min: -4000, max: -900, color: [1.0, 0.35, 0.35] },
            ];
            seg5Btn.disabled = true;
            info.textContent = t('msg.segmentation');
            setTimeout(() => {
                try {
                    const built = viewer.segmentVolumes(classes);
                    info.textContent = built > 0
                        ? (t('msg.autoSeg') + built + t('msg.tissuesAdded'))
                        : (built < 0 ? t('msg.segCtOnly') : t('msg.segNotFound'));
                } catch (err) {
                    info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
                } finally {
                    seg5Btn.disabled = false;
                }
            }, 30);
        };

        if (segBtn) segBtn.onclick = () => {
            segBtn.disabled = true;
            info.textContent = t('msg.bodyMask');
            setTimeout(() => {
                try {
                    const built = viewer.segmentVolumes([{ name: t('dicom.segmentBody'), min: -500, max: 4000, color: [0.7, 0.72, 0.78] }]);
                    info.textContent = built > 0 ? t('msg.bodyAdded') : (built < 0 ? t('msg.bodyMaskCtOnly') : t('msg.bodyNotFound'));
                } catch (err) {
                    info.textContent = t('msg.error') + ': ' + (err && err.message ? err.message : err);
                } finally {
                    segBtn.disabled = false;
                }
            }, 30);
        };

        growModeBtn.onclick = () => {
            const on = growModeBtn.classList.toggle('active');
            viewer.setSeedMode(on);
            growModeBtn.textContent = on ? t('msg.seedsOn') : t('label.seeds');
            info.textContent = on ? t('msg.seedHint') : '';
        };
        growRunBtn.onclick = () => {
            const tol = parseFloat(growTol.value);
            const tolVal = Number.isFinite(tol) ? tol : 30;
            const count = viewer.runGrow(tolVal);
            info.textContent = count > 0
                ? t('msg.growDone', { n: count, tol: tolVal })
                : t('msg.noSeeds');
        };
        growClearBtn.onclick = () => {
            viewer.clearGrow();
            info.textContent = t('msg.growCleared');
        };

        document.querySelectorAll('.dcm-tool-btn').forEach((btn) => {
            btn.onclick = () => {
                const t = btn.dataset.type;
                document.querySelectorAll('.dcm-tool-btn').forEach((b) => b.classList.remove('active'));
                if (t === 'none') {
                    viewer.setMeasureMode(false);
                } else {
                    btn.classList.add('active');
                    viewer.setMeasureMode(true, t);
                }
            };
        });
        measureClearBtn.onclick = () => {
            viewer.clearMeasurements();
            measureValue.textContent = '';
        };
        tabDraw.onclick = () => {
            tabDraw.classList.add('active');
            tabRt.classList.remove('active');
            paneDraw.style.display = 'flex';
            paneRt.style.display = 'none';
        };
        tabRt.onclick = () => {
            tabRt.classList.add('active');
            tabDraw.classList.remove('active');
            paneDraw.style.display = 'none';
            paneRt.style.display = 'flex';
        };
        document.querySelectorAll('.dcm-sec-head').forEach((head) => {
            const caret = head.querySelector('.dcm-sec-caret');
            head.onclick = () => {
                const body = head.nextElementSibling;
                if (!body) return;
                const collapsed = body.classList.toggle('collapsed');
                if (caret) caret.textContent = collapsed ? '▸' : '▾';
            };
        });

        histClose.onclick = () => { histModal.style.display = 'none'; };

        histBtn.onclick = () => {
            const h = viewer.getHistogram();
            if (!h) { info.textContent = t('msg.noHistogram'); return; }
            const binW = (h.max - h.min) / h.bins.length;
            const xs = h.bins.map((_, i) => h.min + (i + 0.5) * binW);
            const trace = { x: xs, y: h.bins, type: 'bar', marker: { color: '#4da6ff' } };
            Plotly.react('hist-plot', [trace], plotlyLayout(graphCfg, t('plot.histogram'), t('plot.value'), 'N'), PLOTLY_CONFIG);
            histModal.style.display = 'block';
        };

        histSavePng.onclick = () => { Plotly.downloadImage('hist-plot', { format: 'png', filename: 'histogram', width: 1000, height: 560 }); };

        attachGraphMenu(histCanvas, () => histBtn.onclick());

        volPreset.onchange = () => {
            if (volPreset.value) viewer.setVolumePreset(volPreset.value);
        };

        // Expose the DICOM tab's plan/dose actions to the Geometry toolbar.
        dicomActions = { loadRtPlan, exportRtPlan, loadRtDose, exportRtDose };
        return viewer;
    }

    // Open / create a G4 macro file for a source's `macro:` field, then reply
    // to the viewer iframe with the chosen path.
    async function pickMacro(target, isNew) {
        const reply = (payload) => {
            try { target.postMessage({ type: 'g4care-macro-path', ...payload }, '*'); } catch (e) { /* ignore */ }
        };
        try {
            const p = isNew ? await window.g4care.newMacroFile() : await window.g4care.openMacroFile();
            if (!p) { reply({ ok: false, canceled: true }); return; }
            if (isNew) {
                const template = [
                    '# G4CARE macro — ' + p.split('/').pop(),
                    '/run/initialize',
                    '/run/beamOn 10',
                    '',
                ].join('\n');
                await window.g4care.writeFile(p, template);
            }
            reply({ ok: true, path: p });
        } catch (err) {
            reply({ ok: false, error: String(err && err.message || err) });
        }
    }

    // Sample a source via the engine (loadConfigText -> init -> sampleSource) and
    // reply to the 3D viewer iframe so it can draw the real sampled distribution.
    async function sampleSource(target, name, n) {
        const reply = (payload) => {
            try { target.postMessage({ type: 'g4care-sample-source-result', ...payload }, '*'); } catch (e) { /* ignore */ }
        };
        if (!name) { reply({ ok: false, error: 'no source name' }); return; }
        try {
            await window.g4care.restart();
            const lc = await window.g4care.rpc('loadConfigText', { text: geometryModel.getValue() });
            if (!lc.ok) { reply({ ok: false, error: lc.error || 'loadConfigText failed' }); return; }
            const ini = await window.g4care.rpc('init');
            if (!ini.ok) { reply({ ok: false, error: ini.error || 'init failed' }); return; }
            const res = await window.g4care.rpc('sampleSource', { name, n });
            reply({ ok: !!res.ok, error: res.error || '', samples: res.samples || [], count: res.count || 0 });
        } catch (err) {
            reply({ ok: false, error: String(err && err.message || err) });
        }
    }

    // ── Tabs (Input / Geometry / Results) ──
    const viewerFrame = document.getElementById('viewer-frame');
    if (viewerFrame) {
        // Keep the 3D viewer's locale in sync with the GUI on every (re)load;
        // the viewer runs on a different origin so it cannot read our localStorage.
        viewerFrame.addEventListener('load', () => { try { viewerFrame.contentWindow.postMessage({ type: 'g4care-locale', locale: getLocale() }, '*'); } catch (e) { /* ignore */ } });
    }
    try {
        const geomUrl = await window.g4care.getGeometryUrl();
        if (geomUrl) {
            // The geometry server (trame/VTK) takes a few seconds to boot; poll
            // until it responds before pointing the iframe at it, otherwise a
            // premature load gets "connection refused" -> black 3D view.
            for (let i = 0; i < 40; i++) {
                let ok = false;
                try { ok = await window.g4care.checkGeometry(); } catch (e) { /* ignore */ }
                if (ok) break;
                await new Promise((r) => setTimeout(r, 1000));
            }
            viewerFrame.src = geomUrl;
        }
    } catch (e) { /* ignore */ }

    // The MPR viewer (vtk.js) is created lazily on first DICOM-tab open, and
    // any failure is isolated so it can never break the rest of the UI (e.g.
    // the other tabs' click handlers).
    let mprViewer = null;
    let dicomSetupFailed = false;
    let dicomActions = null;  // { loadRtPlan, exportRtPlan } — set by setupDicomTab()
    function ensureDicomViewer() {
        if (mprViewer || dicomSetupFailed) return mprViewer;
        try {
            mprViewer = setupDicomTab();
        } catch (e) {
            dicomSetupFailed = true;
            appendLog('[dicom] init failed: ' + (e && e.message ? e.message : e));
        }
        return mprViewer;
    }

    function switchTab(name) {
        document.querySelectorAll('.tab').forEach((b) => b.classList.remove('active'));
        document.querySelectorAll('.pane').forEach((p) => p.classList.remove('active'));
        const btn = document.querySelector('.tab[data-tab="' + name + '"]');
        if (btn) btn.classList.add('active');
        const pane = document.getElementById('tab-' + name);
        if (pane) pane.classList.add('active');
        if (name === 'input') editor.layout();
        // The MPR canvases are display:none until the tab is shown; resize the
        // vtk.js viewports once the DICOM tab becomes visible.
        if (name === 'dicom') {
            const v = ensureDicomViewer();
            if (v) v.resize();
        }
    }

    document.querySelectorAll('.tab').forEach((btn) => {
        btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    // Geometry-tab "Экспорт RTPLAN": reuse the DICOM tab's export logic
    // (exportRtPlan/loadRtPlan live inside setupDicomTab, created lazily).
    const geomExportRtplanBtn = document.getElementById('geom-export-rtplan');
    if (geomExportRtplanBtn) geomExportRtplanBtn.onclick = async () => {
        ensureDicomViewer();               // ensure setupDicomTab() has run
        if (dicomActions) await dicomActions.exportRtPlan();
        else {
            const el = document.getElementById('geom-export-info');
            if (el) el.textContent = t('msg.dicomNotInit');
        }
    };

    // Geometry-tab "Экспорт RTDOSE": save the simulated dose grid and open it.
    const geomExportRtdoseBtn = document.getElementById('geom-export-rtdose');
    if (geomExportRtdoseBtn) geomExportRtdoseBtn.onclick = async () => {
        ensureDicomViewer();               // ensure setupDicomTab() has run
        if (dicomActions) await dicomActions.exportRtDose();
        else {
            const el = document.getElementById('geom-export-info');
            if (el) el.textContent = t('msg.dicomNotInit');
        }
    };

    initExplorer({ t, onLocaleChange });

    const sbRoot = document.getElementById('side-root');
    const sbPara = document.getElementById('side-paraview');
    const sbCloud = document.getElementById('side-cloud');
    if (sbRoot) sbRoot.onclick = async () => { const file = await window.g4care.chooseFile({ title: t('msg.openRootFile'), filters: [{ name: 'ROOT', extensions: ['root'] }] }); if (file) { const r = await window.g4care.openRootBrowser(file); if (r && r.url) window.g4care.openExternal(r.url); } };
    if (sbPara) sbPara.onclick = () => window.g4care.launchParaview('');
    if (sbCloud) sbCloud.onclick = async () => { const u = await window.g4care.getCloudUrl(); window.g4care.openExternal(u); };

    // ROOT opens the bundled JSROOT viewer (always works). ParaView needs the
    // external binary, so disable its launcher when it is not installed.
    initRootViewer();
    const rootModalClose = document.getElementById('root-modal-close');
    if (rootModalClose) rootModalClose.onclick = () => { const m = document.getElementById('root-modal'); if (m) m.style.display = 'none'; };
    window.g4care.detectParaview().then((info) => { if (sbPara && !info.found) { sbPara.disabled = true; sbPara.title = t('side.paraviewNotFound'); } }).catch(() => {});

    initStatus({ appendConsole, appendLog, tabs, geometryModel, editor });

    initSettings({ geometryModel, appendLog });

}

main();
