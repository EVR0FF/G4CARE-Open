//==============================================================================
// G4CARE - object-tree.js — Sidebar object tree + per-object position properties
//
// Sidebar object tree + per-object position properties
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { objectsFromYaml, parsePosition, setObjectPosition } from './yaml-objects.js';

// Object tree (sidebar): parse GEOMETRY.OBJECTS from the YAML config and render
// clickable items with an inline POSITION editor. Returns updateObjectTree() so
// the geometry-preview listener can re-render on config changes.
export function initObjectTree({ geometryModel }) {
    const objectTreeEl = document.getElementById('object-tree');
    const propPanelEl = document.getElementById('prop-panel');

    function showProperties(obj) {
        const dimsStr = (obj.dims || []).join(', ');
        const comps = parsePosition(obj.pos);
        const hasPos = obj.pos !== null && obj.pos !== undefined;

        let html =
            '<div style="margin-bottom:6px;"><b style="color:var(--text)">' + obj.name + '</b> ' +
            '<span style="color:var(--text-3)">(' + obj.shape + ')</span></div>' +
            '<div style="color:var(--text-3)">dims: <span style="color:var(--text-2)">' + (dimsStr || '—') + '</span></div>';

        if (hasPos) {
            html += '<div style="color:var(--text-3); margin-top:6px;">position:</div>' +
                '<div style="display:flex; gap:4px; margin:4px 0;">' +
                comps.map((c, i) => {
                    const label = ['x', 'y', 'z'][i];
                    const unit = c.unit ? '<span style="color:var(--text-3)">' + c.unit + '</span>' : '<span style="color:var(--text-3)">mm</span>';
                    return '<label style="display:flex; flex-direction:column; font-size:11px; color:var(--text-3);">' + label +
                        '<input id="prop-' + label + '" type="number" step="0.1" value="' + c.value + '" style="width:56px; background:var(--inset); color:var(--text); border:1px solid var(--border); font:12px monospace;">' +
                        unit + '</label>';
                }).join('') +
                '</div>' +
                '<button id="prop-apply" style="width:100%; margin-top:2px;">Apply position</button>';
        } else {
            html += '<div style="color:var(--text-3); margin-top:6px;">position: <span style="color:var(--text-3)">—</span></div>';
        }

        propPanelEl.innerHTML = html;

        if (hasPos) {
            document.getElementById('prop-apply').onclick = () => {
                const newComps = ['x', 'y', 'z'].map((label, i) => {
                    const v = parseFloat(document.getElementById('prop-' + label).value);
                    return { value: isNaN(v) ? 0 : v, unit: comps[i].unit };
                });
                geometryModel.setValue(setObjectPosition(geometryModel.getValue(), obj.key, newComps));
            };
        }
    }

    function updateObjectTree() {
        const objs = objectsFromYaml(geometryModel.getValue());
        objectTreeEl.innerHTML = '';
        objs.forEach((obj) => {
            const div = document.createElement('div');
            div.className = 'side-item';
            div.textContent = obj.name;
            div.onclick = async () => {
                document.querySelectorAll('#object-tree .side-item').forEach((d) => d.classList.remove('active'));
                div.classList.add('active');
                showProperties(obj);
                try { await window.g4care.setHighlight(obj.name); } catch (e) { /* ignore */ }
            };
            objectTreeEl.appendChild(div);
        });
    }

    updateObjectTree();

    return { updateObjectTree };
}
