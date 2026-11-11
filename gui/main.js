//==============================================================================
// G4CARE - main.js — Electron main process: window, IPC handlers and geometry sidecar lifecycle
//
// Electron main process: window, IPC handlers and geometry sidecar lifecycle
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

'use strict';

const { app, BrowserWindow, ipcMain, Menu, dialog, shell } = require('electron');
const path = require('path');
const os = require('os');
const fs = require('fs');
const { Sidecar, ENGINE_PATH } = require('./sidecar');
const { spawn, spawnSync } = require('child_process');
const http = require('http');

let mainWindow = null;
const sidecar = new Sidecar();

// WebGL/GPU: the 3D geometry view (trame VTK remote view) needs WebGL in the
// client. Enable hardware WebGL (the NVIDIA GPU works); set G4CARE_SOFTWARE_GL=1
// to force software rendering (SwiftShader) if a specific setup has GL issues.
if (process.env.G4CARE_SOFTWARE_GL) {
    app.disableHardwareAcceleration();
}
app.commandLine.appendSwitch('ignore-gpu-blacklist');
app.commandLine.appendSwitch('enable-webgl');

const IS_DEV = process.env.G4CARE_GUI_DEV === '1';
const ROOT = path.join(__dirname, '..');
const SCHEMA_PATH = path.join(ROOT, '.vscode', 'config-schema.json');
const SNIPPETS_PATH = path.join(ROOT, '.vscode', 'g4.code-snippets');
const CLOUD_URL = process.env.G4CARE_CLOUD_URL || 'http://127.0.0.1:5173';
const TRAME_URL = process.env.G4CARE_TRAME_URL || 'http://127.0.0.1:8788';
const GEOMETRY_URL = process.env.G4CARE_GEOMETRY_URL || 'http://127.0.0.1:8787';
const GEOMETRY_PYTHON = process.env.G4CARE_GEOMETRY_PYTHON ||
    path.join(ROOT, 'trame', '.venv', 'bin', 'python');
const GEOMETRY_SERVER = path.join(ROOT, 'trame', 'geometry_server.py');
const GEOMETRY_YAML = process.env.G4CARE_GEOMETRY_YAML || '/tmp/g4care_gui_config.yaml';
const CFG_DIR_FILE = '/tmp/g4care_cfgdir'; // side-channel: directory of the opened YAML
const GDML_WORK = '/tmp/g4care_gui_work.gdml'; // working copy of the opened GDML (live preview)
const GEOMETRY_PORT = '8787';
const SCORING_PYTHON = process.env.G4CARE_SCORING_PYTHON ||
    path.join(ROOT, 'trame', '.venv', 'bin', 'python');
const SCORING_SERVER = path.join(ROOT, 'trame', 'server.py');
const SCORING_PORT = '8788';

const DEFAULT_CONFIG = `# G4CARE configuration
EVENTS: 10
VISUALIZATION: false

GEOMETRY:
  WORLD:
    shape: "box"
    material: "G4_AIR"
    DIMENSIONS: ["1*m", "1*m", "1*m"]
  OBJECTS:
    target:
      name: "target"
      shape: "box"
      material: "G4_WATER"
      DIMENSIONS: ["10*cm", "10*cm", "10*cm"]
      POSITION: [0, 0, 0]
      isSensitive: true
    sphere:
      name: "sphere"
      shape: "sphere"
      material: "G4_Al"
      DIMENSIONS: ["0*cm", "5*cm"]
      POSITION: ["25*cm", 0, 0]

SOURCE:
  gun:
    type: "particleGun"
    particle: "e-"
    energy: "1*MeV"
    direction: { x: 0, y: 0, z: 1 }
    pos: { x: 0, y: 0, z: "-40*cm" }

PHYSICS:
  BASIC:
    LIST: "QGSP_BERT_EMV"
`;

let geometryChild = null;
let currentFileDir = null; // directory of the YAML currently open in the editor
let currentGdmlPath = null; // if the open file is .gdml, its absolute path

function startGeometryServer() {
    try {
        // Ensure the YAML exists so the geometry server has a scene to render.
        if (!fs.existsSync(GEOMETRY_YAML)) {
            fs.writeFileSync(GEOMETRY_YAML, DEFAULT_CONFIG, 'utf8');
        }
    } catch (e) { /* geometry viewer is optional */ }
    try {
        // Kill any stale geometry server first. A STOPPED (Ctrl+Z) process keeps
        // the port bound but cannot serve, which makes the 3D iframe black.
        spawnSync('pkill', ['-9', '-f', 'geometry_server\\.py'], { stdio: 'ignore' });
    } catch (e) { /* ignore */ }
    try {
        const geomLog = fs.openSync('/tmp/g4care_geometry.log', 'a');
        geometryChild = spawn(GEOMETRY_PYTHON,
            [GEOMETRY_SERVER, GEOMETRY_YAML, '--port', GEOMETRY_PORT],
            { stdio: ['ignore', geomLog, geomLog] });
        geometryChild.on('error', () => { geometryChild = null; });
        geometryChild.on('exit', () => {
            try { fs.closeSync(geomLog); } catch (e) { /* ignore */ }
            geometryChild = null;
        });
    } catch (e) { /* geometry viewer is optional */ }
}

function stopGeometryServer() {
    if (geometryChild) {
        try { geometryChild.kill(); } catch (e) { /* ignore */ }
        geometryChild = null;
    }
}

let scoringChild = null;

// Spawn the trame scoring viewer (trame/server.py) on :8788 for a .vtu/.vtp file.
function startScoringViewer(file) {
    try {
        if (scoringChild) { try { scoringChild.kill(); } catch (e) { /* ignore */ } scoringChild = null; }
        spawnSync('pkill', ['-9', '-f', 'trame/server\\.py'], { stdio: 'ignore' });
        scoringChild = spawn(SCORING_PYTHON, [SCORING_SERVER, file, '--port', SCORING_PORT],
            { stdio: 'ignore' });
        scoringChild.on('error', () => { scoringChild = null; });
        scoringChild.on('exit', () => { scoringChild = null; });
        return { ok: true };
    } catch (e) {
        return { ok: false, error: e.message };
    }
}

function stopScoringServer() {
    if (scoringChild) {
        try { scoringChild.kill(); } catch (e) { /* ignore */ }
        scoringChild = null;
    }
}

function emit(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send(channel, payload);
    }
}

sidecar.on('progress', (p) => emit('progress', p));
sidecar.on('log', (line) => emit('log', line));
sidecar.on('status', (s) => emit('status', s));

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1280,
        height: 860,
        autoHideMenuBar: false,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
    });
    if (IS_DEV) {
        mainWindow.loadURL('http://localhost:5173');
    } else {
        mainWindow.loadFile(path.join(__dirname, 'dist', 'index.html'));
    }
    mainWindow.on('closed', () => { mainWindow = null; });
}

function readJson(file) {
    try { return JSON.parse(fs.readFileSync(file, 'utf8')); }
    catch (e) { return null; }
}

const FILE_FILTERS = [
    { name: 'G4CARE files', extensions: ['yaml', 'yml', 'mac', 'gdml', 'txt'] },
    { name: 'All files', extensions: ['*'] },
];

function buildMenu() {
    Menu.setApplicationMenu(Menu.buildFromTemplate([
        {
            label: 'File',
            submenu: [
                { label: 'Open...', accelerator: 'CmdOrCtrl+O', click: () => emit('menu-open') },
                { label: 'Save', accelerator: 'CmdOrCtrl+S', click: () => emit('menu-save') },
                { label: 'Save As...', accelerator: 'CmdOrCtrl+Shift+S', click: () => emit('menu-save-as') },
                { type: 'separator' },
                { label: 'Quit', accelerator: 'CmdOrCtrl+Q', role: 'quit' },
            ],
        },
        {
            label: 'Settings',
            click: () => emit('menu-settings'),
        },
        {
            label: 'Data',
            submenu: [
                {
                    label: 'Открыть локальное хранилище',
                    click: () => {
                        const dir = path.join(os.homedir(), 'Documents', 'g4care', 'materials');
                        try { fs.mkdirSync(dir, { recursive: true }); } catch (e) { /* ignore */ }
                        shell.openPath(dir);
                    },
                },
            ],
        },
    ]));
}

ipcMain.handle('fileOpen', async () => {
    const r = await dialog.showOpenDialog(mainWindow, {
        title: 'Open file', properties: ['openFile'], filters: FILE_FILTERS,
    });
    if (r.canceled || !r.filePaths.length) return null;
    const p = r.filePaths[0];
    currentFileDir = path.dirname(p);
    const raw = fs.readFileSync(p, 'utf8');
    let kind = 'other';
    if (/\.gdml$/i.test(p)) kind = 'gdml';
    else if (/\.mac$/i.test(p)) kind = 'mac';
    else if (/\.ya?ml$/i.test(p)) kind = 'yaml';
    if (kind === 'gdml') {
        // Honest GDML editing: the editor edits the XML directly. We keep a
        // working copy in /tmp for the geometry server to tessellate, and a YAML
        // wrapper (GEOMETRY.FILE -> working copy) so the server finds it.
        currentGdmlPath = p;
        fs.writeFileSync(GDML_WORK, raw, 'utf8');
        fs.writeFileSync(GEOMETRY_YAML, 'GEOMETRY:\n  FILE: ' + JSON.stringify(GDML_WORK) + '\n', 'utf8');
    } else if (kind === 'yaml') {
        currentGdmlPath = null;
        fs.writeFileSync(GEOMETRY_YAML, raw, 'utf8');
    } else {
        // .mac / other text: never touch the geometry config or working copy.
        currentGdmlPath = null;
    }
    fs.writeFileSync(CFG_DIR_FILE, currentFileDir, 'utf8');
    return { path: p, content: raw, isGdml: kind === 'gdml', kind };
});

ipcMain.handle('fileSave', async (_e, { content, filePath }) => {
    let p = filePath || null;
    if (!p) {
        const r = await dialog.showSaveDialog(mainWindow, {
            title: 'Save file', defaultPath: currentGdmlPath || 'config.yaml', filters: FILE_FILTERS,
        });
        if (r.canceled || !r.filePath) return null;
        p = r.filePath;
    }
    fs.writeFileSync(p, content, 'utf8');
    currentFileDir = path.dirname(p);
    fs.writeFileSync(CFG_DIR_FILE, currentFileDir, 'utf8');
    return { path: p };
});

ipcMain.handle('fileSaveAs', async (_e, { content, filePath }) => {
    const r = await dialog.showSaveDialog(mainWindow, {
        title: 'Save file as', defaultPath: filePath || (currentGdmlPath || 'config.yaml'), filters: FILE_FILTERS,
    });
    if (r.canceled || !r.filePath) return null;
    fs.writeFileSync(r.filePath, content, 'utf8');
    currentFileDir = path.dirname(r.filePath);
    fs.writeFileSync(CFG_DIR_FILE, currentFileDir, 'utf8');
    return { path: r.filePath };
});

// Pick a save path (returns the path only; the caller writes the file).
ipcMain.handle('saveDialog', async (_e, opts) => {
    const r = await dialog.showSaveDialog(mainWindow, {
        title: (opts && opts.title) || 'Save',
        defaultPath: (opts && opts.defaultPath) || 'export.gdml',
        filters: (opts && opts.filters) || FILE_FILTERS,
    });
    if (r.canceled || !r.filePath) return null;
    return r.filePath;
});

// Open an existing G4 macro file — returns the path only (no config side-effects).
ipcMain.handle('openMacroFile', async () => {
    const r = await dialog.showOpenDialog(mainWindow, {
        title: 'Выбрать макрос', properties: ['openFile'],
        filters: [{ name: 'G4 macro', extensions: ['mac', 'txt', 'in'] }, { name: 'All files', extensions: ['*'] }],
    });
    if (r.canceled || !r.filePaths.length) return null;
    return r.filePaths[0];
});

// List a directory for the Explorer sidebar (VSCode-style file browser).
ipcMain.handle('listDir', (_e, dir) => {
    try {
        const d = dir && typeof dir === 'string' ? dir : os.homedir();
        const entries = fs.readdirSync(d, { withFileTypes: true });
        const list = entries
            .filter((e) => !e.name.startsWith('.'))
            .map((e) => ({ name: e.name, dir: e.isDirectory() }))
            .sort((a, b) => (a.dir === b.dir ? a.name.localeCompare(b.name) : (a.dir ? -1 : 1)));
        return { ok: true, path: d, list };
    } catch (e) {
        return { ok: false, error: e.message };
    }
});

// Pick a path for a NEW G4 macro file — returns the path only.
ipcMain.handle('newMacroFile', async () => {
    const r = await dialog.showSaveDialog(mainWindow, {
        title: 'Создать макрос', defaultPath: 'source.mac',
        filters: [{ name: 'G4 macro', extensions: ['mac'] }, { name: 'All files', extensions: ['*'] }],
    });
    if (r.canceled || !r.filePath) return null;
    return r.filePath;
});

// Pick a directory (used by the DICOM tab to load a CT series).
ipcMain.handle('chooseDirectory', async () => {
    const r = await dialog.showOpenDialog(mainWindow, {
        title: 'Выберите папку', properties: ['openDirectory'],
    });
    if (r.canceled || !r.filePaths.length) return null;
    return r.filePaths[0];
});

// Pick a single file (used by the DICOM tab to load RTSTRUCT/RTDOSE/RTPLAN).
ipcMain.handle('chooseFile', async (_e, opts) => {
    const filters = (opts && opts.filters) || [
        { name: 'Медицинские данные', extensions: ['dcm', 'DCM', 'nrrd', 'NRRD', 'nii', 'nii.gz'] },
        { name: 'All files', extensions: ['*'] },
    ];
    const r = await dialog.showOpenDialog(mainWindow, {
        title: (opts && opts.title) || 'Выбрать файл', properties: ['openFile'],
        filters,
    });
    if (r.canceled || !r.filePaths.length) return null;
    return r.filePaths[0];
});

// Default save path for GDML exports, organised by mode into subfolders
// ~/Documents/g4care/gdml/{scenes,objects,arrays}/<name>.gdml
ipcMain.handle('gdmlSavePath', (_e, mode, name) => {
    const sub = mode === 'scene' ? 'scenes' : (mode === 'single' ? 'objects' : 'arrays');
    const dir = path.join(os.homedir(), 'Documents', 'g4care', 'gdml', sub);
    try { fs.mkdirSync(dir, { recursive: true }); } catch (e) { /* ignore */ }
    const safe = (name || mode || 'export').replace(/[^A-Za-z0-9_-]+/g, '_');
    return { dir, defaultPath: path.join(dir, safe + '.gdml') };
});

ipcMain.handle('readFile', async (_e, p) => {
    try { return fs.readFileSync(p, 'utf8'); } catch (e) { return null; }
});

ipcMain.handle('writeFile', async (_e, p, content) => {
    try { fs.writeFileSync(p, content, 'utf8'); return { ok: true }; } catch (e) { return { ok: false, error: e.message }; }
});

// Highlight an object in the 3D geometry view. The geometry server polls this
// file (side-channel, no HTTP route/threads) and recolors the matching actor.
ipcMain.handle('setHighlight', (_e, name) => {
    try {
        fs.writeFileSync('/tmp/g4care_highlight.json', JSON.stringify({ name: name || null }), 'utf8');
        return { ok: true };
    } catch (e) {
        return { ok: false, error: e.message };
    }
});

// Resolve relative `FILE:`/`file:` references in the YAML against the directory
// of the file the user opened. The GUI copies the editor text to /tmp, which
// would break relative GDML/material paths, so we make them absolute here.
function resolveRelativeFileRefs(text, baseDir) {
    if (!text || !baseDir) return text;
    return text.replace(/^([ \t]*[Ff][Ii][Ll][Ee]:[ \t]*["']?)([^"'\s#]+)/gm, (m, pre, p) => {
        if (p.includes('://') || /^(~|\/|\\|\$|\{)/.test(p) || /^[A-Za-z]:[\\/]/.test(p)) return m;
        try { return pre + path.resolve(baseDir, p); } catch (e) { return m; }
    });
}

// Live geometry preview: the renderer writes the editor text here on change so
// the geometry server (mtime-watch) rebuilds the 3D scene in real time.
// The text is written RAW — relative GDML/material paths are resolved by the
// geometry server via the CFG_DIR_FILE side-channel (so the editor is never
// mutated to absolute paths, which would fight the editor-sync poll).
ipcMain.handle('writeGeometryYaml', (_e, text) => {
    try {
        fs.writeFileSync(GEOMETRY_YAML, text, 'utf8');
        return { ok: true };
    } catch (e) {
        return { ok: false, error: e.message };
    }
});

// The geometry viewer can also edit the YAML (material/position/…). The editor
// re-reads the file here to stay in sync with those external edits.
ipcMain.handle('readGeometryYaml', () => {
    try {
        return fs.readFileSync('/tmp/g4care_gui_config.yaml', 'utf8');
    } catch (e) {
        return null;
    }
});

// GDML working-copy writer: the editor writes XML here; the geometry server
// tessellates it via GEOMETRY.FILE -> /tmp/g4care_gui_work.gdml.
ipcMain.handle('writeGeometryXml', (_e, text) => {
    try {
        fs.writeFileSync(GDML_WORK, text, 'utf8');
        return { ok: true };
    } catch (e) {
        return { ok: false, error: e.message };
    }
});

// The viewer can also edit the GDML (move/rotate/copy/delete via /api/gdml).
// The editor re-reads the working copy here to stay in sync.
ipcMain.handle('readGeometryXml', () => {
    try {
        return fs.readFileSync(GDML_WORK, 'utf8');
    } catch (e) {
        return null;
    }
});

// Settings (grid show/spacing/snap) — the geometry server polls this file.
ipcMain.handle('writeSettings', (_e, settings) => {
    try {
        fs.writeFileSync('/tmp/g4care_settings.json', JSON.stringify(settings || {}), 'utf8');
        return { ok: true };
    } catch (e) {
        return { ok: false, error: e.message };
    }
});

ipcMain.handle('rpc', async (_e, { method, params }) => {
    // The engine writes the text to /tmp and loads it; resolve relative
    // GDML/material paths against the opened file's directory first so both the
    // engine and the geometry viewer find them.
    if (method === 'loadConfigText' && params && typeof params.text === 'string') {
        params = { ...params, text: resolveRelativeFileRefs(params.text, currentFileDir) };
    }
    return sidecar.rpc(method, params || {});
});
ipcMain.handle('start', () => { sidecar.start(); return { ok: true }; });
ipcMain.handle('stop', () => { sidecar.stop(); return { ok: true }; });
ipcMain.handle('restart', () => { sidecar.restart(); return { ok: true }; });
ipcMain.handle('enginePath', () => ENGINE_PATH);
ipcMain.handle('getSchema', () => readJson(SCHEMA_PATH));
ipcMain.handle('getSnippets', () => readJson(SNIPPETS_PATH));
ipcMain.handle('startScoringViewer', (_e, file) => startScoringViewer(file || 'scoring.vtu'));
ipcMain.handle('getCloudUrl', () => CLOUD_URL);

ipcMain.handle('getTrameUrl', () => TRAME_URL);
ipcMain.handle('getGeometryUrl', () => GEOMETRY_URL);

// ── SCRIPT block (ExprTk) — proxied from the geometry server ──
function httpJson(method, url, body) {
    return new Promise((resolve, reject) => {
        const u = new URL(url);
        const data = body ? JSON.stringify(body) : null;
        const req = http.request({ hostname: u.hostname, port: u.port, path: u.pathname + u.search, method, headers: data ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(data) } : {} }, (res) => {
            let b = '';
            res.on('data', (c) => { b += c; });
            res.on('end', () => { try { resolve(JSON.parse(b)); } catch (e) { resolve(b); } });
        });
        req.on('error', reject);
        if (data) req.write(data);
        req.end();
    });
}

ipcMain.handle('getScript', async () => {
    try { return await httpJson('GET', GEOMETRY_URL + '/script.json'); }
    catch (e) { return { variables: {}, functions: {}, scripts: [] }; }
});

ipcMain.handle('saveScript', (_e, data) => {
    return httpJson('POST', GEOMETRY_URL + '/api/script', data).then((r) => ({ ok: !(r && r.error), error: (r && r.error) || '' }));
});
ipcMain.handle('getDefaultConfig', () => {
    // Return the last persisted config so a DICOM transfer (which writes
    // GEOMETRY.DICOM into GEOMETRY_YAML) survives an app restart. Without this
    // the editor always resets to the hard-coded sphere-only DEFAULT_CONFIG and
    // the phantom silently disappears from the next run.
    try {
        const persisted = fs.readFileSync(GEOMETRY_YAML, 'utf8');
        if (persisted &&
            persisted.includes('GEOMETRY:') &&
            (persisted.includes('EVENTS:') || persisted.includes('OBJECTS:') || persisted.includes('DICOM:'))) {
            return persisted;
        }
    } catch (e) { /* fall through to default */ }
    return DEFAULT_CONFIG;
});

// Open a URL in the system default browser (sidebar launchers).
ipcMain.handle('openExternal', (_e, url) => {
    if (!url) return { ok: false };
    shell.openExternal(url);
    return { ok: true };
});

// ── ROOT / ParaView discovery (auto-detect installed tools) ──
// Locate ROOT's browser binary (rootbrowse or root); returns absolute path or null.
function findRootBrowse() {
    if (process.env.G4CARE_ROOT_BIN) return process.env.G4CARE_ROOT_BIN;
    for (const c of ['rootbrowse', 'root']) {
        try { const p = spawnSync('which', [c]).stdout.toString().trim(); if (p && fs.existsSync(p)) return p; } catch (e) { /* ignore */ }
    }
    try {
        const rc = spawnSync('which', ['root-config']).stdout.toString().trim();
        if (rc) {
            const prefix = spawnSync(rc, ['--prefix']).stdout.toString().trim();
            for (const b of ['rootbrowse', 'root']) {
                const p = path.join(prefix, 'bin', b);
                if (fs.existsSync(p)) return p;
            }
        }
    } catch (e) { /* ignore */ }
    const prefixes = [
        '/opt/root', '/usr/local', '/usr',
        path.join(os.homedir(), 'software', 'ROOT', 'install'),
        path.join(os.homedir(), 'software', 'ROOT'),
        path.join(os.homedir(), 'ROOT'),
        path.join(os.homedir(), 'miniconda3'),
        path.join(os.homedir(), 'anaconda3'),
    ];
    for (const prefix of prefixes) {
        for (const b of ['bin/rootbrowse', 'bin/root']) {
            const p = path.join(prefix, b);
            if (fs.existsSync(p)) return p;
        }
    }
    return null;
}

// Locate the ParaView executable; returns absolute path or null.
function findParaview() {
    if (process.env.G4CARE_PARAVIEW_BIN) return process.env.G4CARE_PARAVIEW_BIN;
    try { const p = spawnSync('which', ['paraview']).stdout.toString().trim(); if (p && fs.existsSync(p)) return p; } catch (e) { /* ignore */ }
    // ParaView is often installed in a versioned directory (e.g. /opt/paraview-5.12/bin/paraview).
    for (const base of ['/opt', '/usr/local', path.join(os.homedir(), 'software')]) {
        try {
            for (const e of fs.readdirSync(base)) {
                if (/^paraview/i.test(e)) {
                    const p = path.join(base, e, 'bin', 'paraview');
                    if (fs.existsSync(p)) return p;
                }
            }
        } catch (err) { /* ignore */ }
    }
    const dirs = [
        '/usr/local/bin', '/usr/bin',
        path.join(os.homedir(), 'software', 'ParaView', 'bin'),
        path.join(os.homedir(), 'miniconda3', 'bin'),
        path.join(os.homedir(), 'anaconda3', 'bin'),
    ];
    for (const d of dirs) {
        const p = path.join(d, 'paraview');
        if (fs.existsSync(p)) return p;
    }
    return null;
}

function execVersion(bin) {
    try {
        const r = spawnSync(bin, ['--version'], { timeout: 4000 });
        return (r.stdout.toString().trim().split('\n')[0] || '').slice(0, 200) || null;
    } catch (e) { return null; }
}

ipcMain.handle('detectRoot', () => {
    const bin = findRootBrowse();
    if (!bin) return { found: false, path: null, version: null };
    return { found: true, path: bin, version: execVersion(bin) };
});

ipcMain.handle('detectParaview', () => {
    const bin = findParaview();
    if (!bin) return { found: false, path: null, version: null };
    return { found: true, path: bin, version: execVersion(bin) };
});

// Locate the main ROOT binary (for the web-based RBrowser).
function findRootBinary() {
    try { const p = spawnSync('which', ['root']).stdout.toString().trim(); if (p && fs.existsSync(p)) return p; } catch (e) { /* ignore */ }
    try {
        const rc = spawnSync('which', ['root-config']).stdout.toString().trim();
        if (rc) {
            const prefix = spawnSync(rc, ['--prefix']).stdout.toString().trim();
            const p = path.join(prefix, 'bin', 'root');
            if (fs.existsSync(p)) return p;
        }
    } catch (e) { /* ignore */ }
    for (const prefix of ['/opt/root', '/usr/local', '/usr', path.join(os.homedir(), 'software', 'ROOT', 'install'), path.join(os.homedir(), 'software', 'ROOT')]) {
        const p = path.join(prefix, 'bin', 'root');
        if (fs.existsSync(p)) return p;
    }
    return null;
}

// Open a .root file in ROOT's web-based RBrowser (same approach as the VSCode RBrowser extension).
ipcMain.handle('openRootBrowser', (_e, file) => {
    if (!file) return { ok: false };
    const root = findRootBinary();
    if (!root) return { ok: false, error: 'ROOT not found' };
    const port = 9500 + Math.floor(Math.random() * 400);
    const args = ['-l', '--web=server:' + port, file, '-e', 'new TBrowser'];
    try {
        const child = spawn(root, args, { detached: true, stdio: ['ignore', 'pipe', 'pipe'] });
        let out = '';
        child.stdout.on('data', (d) => { out += d.toString(); });
        child.stderr.on('data', (d) => { out += d.toString(); });
        child.unref();
        return new Promise((resolve) => {
            let tries = 0;
            const timer = setInterval(() => {
                const marker = 'http://localhost:';
                const p2 = out.indexOf(marker);
                let url = null;
                if (p2 >= 0) {
                    let end = out.length;
                    const ws = [' ', String.fromCharCode(10), String.fromCharCode(13), String.fromCharCode(9)];
                    for (const ch of ws) { const e = out.indexOf(ch, p2); if (e !== -1 && e < end) end = e; }
                    url = out.slice(p2, end).trim();
                }
                if (url) { clearInterval(timer); resolve({ ok: true, url }); }
                else if (++tries > 60) { clearInterval(timer); resolve({ ok: true, url: 'http://localhost:' + port + '/' }); }
            }, 250);
        });
    } catch (e) { return { ok: false, error: e.message }; }
});

// Launch ParaView (empty, or on a data file: scene VTP / scoring VTU).
ipcMain.handle('launchParaview', (_e, file) => {
    const args = file ? [file] : [];
    const bin = findParaview() || 'paraview';
    // ParaView bundles its own Qt; prepend its lib dir so the bundled Qt is used
    // (otherwise the system Qt plugins mismatch and the binary fails to start).
    const libDir = (bin !== 'paraview') ? path.join(path.dirname(bin), '..', 'lib') : null;
    const env = libDir ? { ...process.env, LD_LIBRARY_PATH: libDir + (process.env.LD_LIBRARY_PATH ? ':' + process.env.LD_LIBRARY_PATH : '') } : process.env;
    try { spawn(bin, args, { detached: true, stdio: 'ignore', env }).unref(); return { ok: true }; }
    catch (e) { return { ok: false, error: e.message }; }
});

// Check whether the geometry server (trame/VTK) is already serving. The renderer
// polls this before pointing the 3D iframe at :8787, because the server takes a
// few seconds to boot and a premature load shows a black/refused view.
ipcMain.handle('checkGeometry', () => new Promise((resolve) => {
    const req = http.get(GEOMETRY_URL + '/', (res) => {
        res.resume();
        resolve(res.statusCode < 500);
    });
    req.on('error', () => resolve(false));
    req.setTimeout(1500, () => { req.destroy(); resolve(false); });
}));

app.whenReady().then(() => {
    createWindow();
    buildMenu();
    sidecar.start();
    startGeometryServer();
    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
});

app.on('window-all-closed', () => {
    sidecar.stop();
    stopGeometryServer();
    stopScoringServer();
    if (process.platform !== 'darwin') app.quit();
});

app.on('will-quit', () => { stopGeometryServer(); stopScoringServer(); });
