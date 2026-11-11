//==============================================================================
// G4CARE - preload.js — ContextBridge: minimal safe API surface for the renderer
//
// ContextBridge: minimal safe API surface for the renderer
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

'use strict';

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('g4care', {
    rpc: (method, params) => ipcRenderer.invoke('rpc', { method, params }),
    start: () => ipcRenderer.invoke('start'),
    stop: () => ipcRenderer.invoke('stop'),
    restart: () => ipcRenderer.invoke('restart'),
    enginePath: () => ipcRenderer.invoke('enginePath'),
    getSchema: () => ipcRenderer.invoke('getSchema'),
    getSnippets: () => ipcRenderer.invoke('getSnippets'),
    getCloudUrl: () => ipcRenderer.invoke('getCloudUrl'),
    getTrameUrl: () => ipcRenderer.invoke('getTrameUrl'),
    getGeometryUrl: () => ipcRenderer.invoke('getGeometryUrl'),
    getScript: () => ipcRenderer.invoke('getScript'),
    saveScript: (data) => ipcRenderer.invoke('saveScript', data),
    checkGeometry: () => ipcRenderer.invoke('checkGeometry'),
    getDefaultConfig: () => ipcRenderer.invoke('getDefaultConfig'),
    openFile: () => ipcRenderer.invoke('fileOpen'),
    openMacroFile: () => ipcRenderer.invoke('openMacroFile'),
    newMacroFile: () => ipcRenderer.invoke('newMacroFile'),
    chooseDirectory: () => ipcRenderer.invoke('chooseDirectory'),
    listDir: (p) => ipcRenderer.invoke('listDir', p),
    openExternal: (url) => ipcRenderer.invoke('openExternal', url),
    openRootBrowser: (file) => ipcRenderer.invoke('openRootBrowser', file),
    launchParaview: (file) => ipcRenderer.invoke('launchParaview', file),
    detectRoot: () => ipcRenderer.invoke('detectRoot'),
    detectParaview: () => ipcRenderer.invoke('detectParaview'),
    chooseFile: (opts) => ipcRenderer.invoke('chooseFile', opts),
    saveFile: (content, filePath) => ipcRenderer.invoke('fileSave', { content, filePath }),
    saveFileAs: (content, filePath) => ipcRenderer.invoke('fileSaveAs', { content, filePath }),
    saveDialog: (opts) => ipcRenderer.invoke('saveDialog', opts),
    gdmlSavePath: (mode, name) => ipcRenderer.invoke('gdmlSavePath', mode, name),
    readFile: (p) => ipcRenderer.invoke('readFile', p),
    writeFile: (p, content) => ipcRenderer.invoke('writeFile', p, content),
    setHighlight: (name) => ipcRenderer.invoke('setHighlight', name),
    writeGeometryYaml: (text) => ipcRenderer.invoke('writeGeometryYaml', text),
    readGeometryYaml: () => ipcRenderer.invoke('readGeometryYaml'),
    writeGeometryXml: (text) => ipcRenderer.invoke('writeGeometryXml', text),
    readGeometryXml: () => ipcRenderer.invoke('readGeometryXml'),
    writeSettings: (settings) => ipcRenderer.invoke('writeSettings', settings),
    startScoringViewer: (file) => ipcRenderer.invoke('startScoringViewer', file),
    onMenuSettings: (cb) => ipcRenderer.on('menu-settings', () => cb()),
    onMenuOpen: (cb) => ipcRenderer.on('menu-open', () => cb()),
    onMenuSave: (cb) => ipcRenderer.on('menu-save', () => cb()),
    onMenuSaveAs: (cb) => ipcRenderer.on('menu-save-as', () => cb()),
    onProgress: (cb) => ipcRenderer.on('progress', (_e, data) => cb(data)),
    onLog: (cb) => ipcRenderer.on('log', (_e, line) => cb(line)),
    onStatus: (cb) => ipcRenderer.on('status', (_e, s) => cb(s)),
});
