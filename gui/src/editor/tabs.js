//==============================================================================
// G4CARE - tabs.js — VSCode-style editor tab strip (permanent geometry + closable tabs)
//
// VSCode-style editor tab strip (permanent geometry + closable tabs)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// Maps a file path to its Monaco language id.
export function languageFor(p) {
    if (/\.gdml$/i.test(p)) return 'xml';
    if (/\.mac$/i.test(p)) return 'g4macro';
    return /\.ya?ml$/i.test(p) ? 'yaml' : 'plaintext';
}

// One permanent, non-closable "Geometry" tab (YAML/GDML, bound to the 3D
// preview) plus closable tabs for .mac / other text files. A single Monaco
// editor switches between models; geometry stays bound to geometryModel.
export function createEditorTabs({ editor, geometryModel, monaco, t, appendLog, sideConfigEl }) {
    let geometryKind = 'yaml';     // 'yaml' | 'gdml'
    let geometryPath = null;
    const textTabs = [];           // { id, path, model, dirty }
    let tabSeq = 0;
    let activeTabId = 'geometry';
    const tabsBar = document.getElementById('editor-tabs');

    function geometryLabel() {
        if (geometryPath) return geometryPath.split('/').pop();
        return geometryKind === 'gdml' ? 'geometry.gdml' : 'config.yaml';
    }

    function renderTabs() {
        if (!tabsBar) return;
        tabsBar.innerHTML = '';
        const g = document.createElement('div');
        g.className = 'edtab' + (activeTabId === 'geometry' ? ' active' : '');
        g.title = t('tabs.geometry');
        g.onclick = () => activateTab('geometry');
        const gKind = document.createElement('span');
        gKind.className = 'edtab-kind';
        gKind.textContent = geometryKind === 'gdml' ? 'xml' : 'yaml';
        const gLbl = document.createElement('span');
        gLbl.className = 'edtab-label';
        gLbl.textContent = geometryLabel();
        g.appendChild(gKind);
        g.appendChild(gLbl);
        tabsBar.appendChild(g);
        for (const tab of textTabs) {
            const d = document.createElement('div');
            d.className = 'edtab' + (activeTabId === tab.id ? ' active' : '');
            d.onclick = () => activateTab(tab.id);
            const lbl = document.createElement('span');
            lbl.className = 'edtab-label';
            lbl.textContent = tab.path ? tab.path.split('/').pop() : t('tabs.untitled');
            const dirty = document.createElement('span');
            dirty.className = 'edtab-dirty';
            dirty.textContent = tab.dirty ? '●' : '';
            const x = document.createElement('span');
            x.className = 'edtab-close';
            x.textContent = '✕';
            x.title = t('tabs.close');
            x.onclick = (e) => { e.stopPropagation(); closeTextTab(tab.id); };
            d.appendChild(lbl);
            d.appendChild(dirty);
            d.appendChild(x);
            tabsBar.appendChild(d);
        }
        const plus = document.createElement('button');
        plus.className = 'edtab-plus';
        plus.textContent = '+';
        plus.title = t('tabs.newMacro');
        plus.onclick = () => newMacroTab();
        tabsBar.appendChild(plus);
    }

    function activateTab(id) {
        activeTabId = id;
        if (id === 'geometry') editor.setModel(geometryModel);
        else {
            const tab = textTabs.find((tb) => tb.id === id);
            if (tab) editor.setModel(tab.model);
        }
        renderTabs();
        editor.focus();
    }

    function openTextTab(path, content, lang) {
        const existing = textTabs.find((tb) => tb.path && tb.path === path);
        if (existing) { activateTab(existing.id); return; }
        const id = 'text-' + (++tabSeq);
        const model = monaco.editor.createModel(content || '', lang, monaco.Uri.file(path));
        const tab = { id, path, model, dirty: false };
        model.onDidChangeContent(() => {
            if (!tab.dirty) { tab.dirty = true; renderTabs(); }
        });
        textTabs.push(tab);
        activateTab(id);
        appendLog('[file] opened ' + path);
    }

    function newMacroTab() {
        const id = 'text-' + (++tabSeq);
        const model = monaco.editor.createModel(
            '# G4CARE macro\n/run/initialize\n/run/beamOn 10\n',
            'g4macro',
            monaco.Uri.parse('inmemory://g4care/macro/untitled-' + id + '.mac'),
        );
        const tab = { id, path: null, model, dirty: true };
        model.onDidChangeContent(() => {
            if (!tab.dirty) { tab.dirty = true; renderTabs(); }
        });
        textTabs.push(tab);
        activateTab(id);
    }

    function closeTextTab(id) {
        const i = textTabs.findIndex((tb) => tb.id === id);
        if (i < 0) return;
        const tab = textTabs[i];
        textTabs.splice(i, 1);
        try { tab.model.dispose(); } catch (e) { /* ignore */ }
        if (activeTabId === id) activateTab('geometry');
        else renderTabs();
    }

    function activeModelInfo() {
        if (activeTabId === 'geometry') return { model: geometryModel, path: geometryPath, kind: geometryKind };
        const tab = textTabs.find((tb) => tb.id === activeTabId);
        if (tab) return { model: tab.model, path: tab.path, kind: 'mac' };
        return { model: geometryModel, path: geometryPath, kind: geometryKind };
    }

    // Load a geometry file (YAML or GDML) into the permanent geometry tab.
    function loadGeometry(path, content, kind) {
        geometryPath = path;
        geometryKind = kind === 'gdml' ? 'gdml' : 'yaml';
        geometryModel.setValue(content);
        monaco.editor.setModelLanguage(geometryModel, languageFor(path));
        if (sideConfigEl) sideConfigEl.textContent = path.split('/').pop();
        activateTab('geometry');
        appendLog('[file] opened ' + path);
    }

    // After a successful save: update the active tab's path/dirty marker.
    function markSaved(path) {
        if (activeTabId !== 'geometry') {
            const tab = textTabs.find((tb) => tb.id === activeTabId);
            if (tab) { tab.path = path; tab.dirty = false; }
        } else {
            geometryPath = path;
        }
    }

    renderTabs();

    return {
        renderTabs,
        activateTab,
        openTextTab,
        newMacroTab,
        closeTextTab,
        activeModelInfo,
        loadGeometry,
        markSaved,
        get kind() { return geometryKind; },
    };
}
