//==============================================================================
// G4CARE - root-viewer.js — ROOT file viewer (JSROOT): hierarchical key tree + drawing
//
// ROOT file viewer (JSROOT): hierarchical key tree + drawing
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { openFile, draw, cleanup } from 'jsroot';
import { t } from './i18n/i18n.js';

const DIR_RE = /TDirectory/i;

export function initRootViewer() {
    const btn = document.getElementById('btn-open-root');
    const fileInput = document.getElementById('root-file');
    const keyList = document.getElementById('root-keys');
    const drawArea = document.getElementById('root-draw');
    const fnameEl = document.getElementById('root-fname');

    let currentFile = null;

    btn.addEventListener('click', () => fileInput.click());

    fileInput.addEventListener('change', async () => {
        const f = fileInput.files[0];
        if (!f) return;
        keyList.innerHTML = '<div>' + t('root.opening') + '</div>';
        fnameEl.textContent = f.name;
        try {
            currentFile = await openFile(f);
            await renderKeys(currentFile, keyList, 0);
        } catch (e) {
            keyList.innerHTML = '<div>' + t('error.generic') + e.message + '</div>';
        }
    });

    async function listKeys(node) {
        const res = await node.readKeys();
        const arr = Array.isArray(res) ? res : (res.fKeys || []);
        return [...arr].sort((a, b) => {
            const ad = DIR_RE.test(a.fClassName || ''), bd = DIR_RE.test(b.fClassName || '');
            if (ad !== bd) return ad ? -1 : 1;
            return String(a.fName || a.name || '').localeCompare(String(b.fName || b.name || ''));
        });
    }

    async function renderKeys(node, container, depth) {
        let keys;
        try { keys = await listKeys(node); } catch (e) { keys = []; }
        if (!keys.length) {
            const empty = document.createElement('div');
            empty.textContent = t('root.noKeys');
            empty.style.cssText = 'color:#777;padding:4px 8px;font:12px sans-serif;';
            container.appendChild(empty);
            return;
        }
        for (const k of keys) {
            const name = k.fName || k.name || '';
            const cls = k.fClassName || '';
            const isDir = DIR_RE.test(cls);
            const row = document.createElement('div');
            row.style.cssText = 'display:flex;align-items:center;gap:4px;padding:2px 6px;cursor:pointer;border-bottom:1px solid #222;white-space:nowrap;';
            row.style.paddingLeft = (8 + depth * 14) + 'px';
            if (isDir) {
                const tw = document.createElement('span');
                tw.textContent = '▸';
                tw.style.cssText = 'width:12px;color:#9ab;flex:none;';
                row.appendChild(tw);
            }
            const lbl = document.createElement('span');
            lbl.textContent = (isDir ? name + '/' : name) + (isDir ? '' : '  (' + cls + ')');
            lbl.style.cssText = 'flex:1;overflow:hidden;text-overflow:ellipsis;';
            row.appendChild(lbl);
            row.addEventListener('click', () => {
                if (!isDir) { drawKey(node, name); return; }
                const toggle = row.querySelector('span');
                const next = row.nextElementSibling;
                if (next && next.dataset && next.dataset.subdir === name) {
                    next.remove();
                    if (toggle) toggle.textContent = '▸';
                    return;
                }
                if (toggle) toggle.textContent = '▾';
                const sub = document.createElement('div');
                sub.dataset.subdir = name;
                sub.style.cssText = 'padding-left:' + ((depth + 1) * 14) + 'px;';
                row.after(sub);
                (async () => {
                    try {
                        const dirObj = await node.readObject(name);
                        await renderKeys(dirObj, sub, depth + 1);
                    } catch (e) {
                        const err = document.createElement('div');
                        err.textContent = t('error.generic') + e.message;
                        err.style.cssText = 'color:#f66;padding:2px 6px;';
                        sub.appendChild(err);
                    }
                })();
            });
            container.appendChild(row);
        }
    }

    function showObjectFallback(dom, obj, name) {
        const cls = (obj && obj.fClassName) || 'object';
        let html = '<div style="padding:12px;color:#e2e5ea;font:13px sans-serif;">';
        html += '<div style="color:#e8752a;font-weight:bold;">' + name + '</div>';
        html += '<div style="color:#9ab;margin-top:4px;">' + cls + '</div>';
        const branches = (obj && obj.fBranches) ? obj.fBranches : null;
        if (Array.isArray(branches) && branches.length) {
            html += '<div style="margin-top:8px;color:#9ab;">Branches:</div>';
            for (const b of branches) {
                const bn = b.fName || b.name || '';
                html += '<div style="padding-left:12px;color:#d7e8d7;">' + bn + '</div>';
            }
        }
        html += '</div>';
        dom.innerHTML = html;
    }

    async function drawKey(node, name) {
        try {
            cleanup(drawArea);
            const obj = await node.readObject(name);
            try {
                await draw(drawArea, obj, '');
            } catch (e) {
                // Some objects (e.g. TTree) need a draw expression; show info instead.
                showObjectFallback(drawArea, obj, name);
            }
        } catch (e) {
            drawArea.innerHTML = '<div style="color:#f66;padding:8px">' + t('error.generic') + e.message + '</div>';
        }
    }
}
