//==============================================================================
// G4CARE - log.js — Batched and capped console logging (amortized O(1) appends)
//
// Batched and capped console logging (amortized O(1) appends)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

export function makeAppendLog(logEl, maxLines = 2000) {
    const logBuf = [];
    let logFlushPending = false;
    return function appendLog(t) {
        logBuf.push(t);
        if (logBuf.length > maxLines) logBuf.splice(0, logBuf.length - maxLines);
        if (!logFlushPending) {
            logFlushPending = true;
            requestAnimationFrame(() => {
                logEl.textContent = logBuf.join('\n');
                logEl.scrollTop = logEl.scrollHeight;
                logFlushPending = false;
            });
        }
    };
}
