//==============================================================================
// G4CARE - status.js — Command line, geometry-server status + editor sync
//
// Command line, geometry-server status + editor sync
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// Geant4-style command line, the geometry-server status poll and the 1.5 s
// editor <-> 3D-viewer sync loop.
export function initStatus({ appendConsole, appendLog, tabs, geometryModel, editor }) {
    // ── Geant4-style command line ──
    const cmdInput = document.getElementById('cmd-input');
    cmdInput.addEventListener('keydown', async (e) => {
        if (e.key !== 'Enter') return;
        const cmd = cmdInput.value.trim();
        cmdInput.value = '';
        if (!cmd) return;
        appendConsole('Idle> ' + cmd);
        try {
            const res = await window.g4care.rpc('executeCommand', { command: cmd });
            appendConsole('  ' + JSON.stringify(res));
        } catch (err) {
            appendConsole('  ERROR: ' + err.message);
        }
    });

    window.g4care.enginePath().then((p) => appendLog('[gui] engine: ' + p));

    // Geometry server status indicator (poll every few seconds).
    async function updateGeomStatus() {
        try {
            const ok = await window.g4care.checkGeometry();
            const el = document.getElementById('geom-status');
            el.textContent = '3D: ' + (ok ? 'up' : 'down');
            el.style.color = ok ? '#8f8' : '#f88';
        } catch (e) { /* ignore */ }
    }
    updateGeomStatus();
    setInterval(updateGeomStatus, 5000);

    // Keep the editor in sync with edits made from the 3D geometry viewer
    // (material / position / color / …). Only overwrite when the editor is not
    // focused, so we never clobber the user's own typing.
    setInterval(async () => {
        try {
            const content = tabs.kind === 'gdml'
                ? await window.g4care.readGeometryXml()
                : await window.g4care.readGeometryYaml();
            if (content && content !== geometryModel.getValue() && !editor.hasTextFocus()) {
                geometryModel.setValue(content);
            }
        } catch (e) { /* ignore */ }
    }, 1500);
}
