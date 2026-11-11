//==============================================================================
// G4CARE - console.js — Console tabs (log/console/errors) + collapse toggle
//
// Console tabs (log/console/errors) + collapse toggle
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// Console tabs (журнал / консоль / ошибки) + collapse toggle. Exposes
// window.__g4careShowErrors so the log handler can auto-open the Errors pane.
export function initConsole({ logEl, consolePaneEl, errorsEl }) {
    const body = document.getElementById('console-body');
    const collapseBtn = document.getElementById('console-collapse');
    const tabs = Array.from(document.querySelectorAll('.ctab'));
    const panes = { log: logEl, console: consolePaneEl, errors: errorsEl };
    tabs.forEach((btn) => {
        btn.addEventListener('click', () => {
            tabs.forEach((b) => b.classList.remove('active'));
            btn.classList.add('active');
            const key = btn.dataset.ctab;
            Object.keys(panes).forEach((k) => panes[k].classList.toggle('active', k === key));
        });
    });
    if (collapseBtn && body) {
        collapseBtn.addEventListener('click', () => body.classList.toggle('collapsed'));
    }
    window.__g4careShowErrors = () => {
        tabs.forEach((b) => b.classList.remove('active'));
        const errTab = tabs.find((b) => b.dataset.ctab === 'errors');
        if (errTab) errTab.classList.add('active');
        Object.keys(panes).forEach((k) => panes[k].classList.toggle('active', k === 'errors'));
        if (body) body.classList.remove('collapsed');
    };
}
