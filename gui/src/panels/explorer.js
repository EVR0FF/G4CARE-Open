//==============================================================================
// G4CARE - explorer.js — Sidebar: project/results tabs + Explorer (VSCode-style file browser)
//
// Sidebar: project/results tabs + Explorer (VSCode-style file browser)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// Sidebar: Project/Results tabs + the VSCode-style Explorer file browser.
export function initExplorer({ t, onLocaleChange }) {
    const sbExplorerEl = document.getElementById('side-explorer');
    const sbDirPathEl = document.getElementById('side-dir-path');
    let sbDir = '';
    const sbEsc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    const BS = String.fromCharCode(92);
    const sbParent = (p) => { const t = p.replace(new RegExp('[' + BS + '/]+$'), ''); const i = Math.max(t.lastIndexOf('/'), t.lastIndexOf(BS)); return i <= 0 ? '' : t.slice(0, i); };
    async function sbRefresh() {
        const res = await window.g4care.listDir(sbDir);
        if (!res || !res.ok) { sbExplorerEl.innerHTML = '<div style="color:var(--danger); padding:4px 8px;">' + sbEsc((res && res.error) || t('msg.error')) + '</div>'; return; }
        sbDir = res.path || '';
        if (sbDirPathEl) sbDirPathEl.textContent = sbDir || t('side.dirNotSelected');
        let html = '';
        if (sbDir) html += '<div class="side-item" data-nav="up">⬆ ..</div>';
        for (const it of res.list) html += '<div class="side-item" data-nav="' + sbEsc(it.name) + '"' + (it.dir ? ' data-isdir="1"' : '') + '>' + (it.dir ? '▸ ' : '· ') + sbEsc(it.name) + '</div>';
        sbExplorerEl.innerHTML = html;
        sbExplorerEl.querySelectorAll('[data-nav]').forEach((d) => { d.onclick = () => {
            if (d.dataset.nav === 'up') sbDir = sbParent(sbDir);
            else if (d.dataset.isdir) sbDir = (sbDir ? sbDir.replace(new RegExp('[' + BS + '/]+$'), '') + '/' : '') + d.dataset.nav;
            sbRefresh();
        }; });
    }
    const sbChoose = document.getElementById('side-choose-dir');
    onLocaleChange(() => { if (sbDirPathEl) sbDirPathEl.textContent = sbDir || t('side.dirNotSelected'); });
    if (sbChoose) sbChoose.onclick = async () => { const d = await window.g4care.chooseDirectory(); if (d) { sbDir = d; await sbRefresh(); } };

    const sideTabProject = document.getElementById('side-tab-project');
    const sideTabResults = document.getElementById('side-tab-results');
    const setSideTab = (tab) => {
        const proj = tab === 'project';
        const pp = document.getElementById('side-pane-project');
        const rp = document.getElementById('side-pane-results');
        if (pp) pp.style.display = proj ? 'flex' : 'none';
        if (rp) rp.style.display = proj ? 'none' : 'flex';
        if (sideTabProject) sideTabProject.classList.toggle('active', proj);
        if (sideTabResults) sideTabResults.classList.toggle('active', !proj);
    };
    if (sideTabProject) sideTabProject.onclick = () => setSideTab('project');
    if (sideTabResults) sideTabResults.onclick = () => setSideTab('results');
}
