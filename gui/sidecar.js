//==============================================================================
// G4CARE - sidecar.js — Engine sidecar bridge: spawns g4care-engine --rpc, JSON-RPC/NDJSON over stdio
//
// Engine sidecar bridge: spawns g4care-engine --rpc, JSON-RPC/NDJSON over stdio
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

'use strict';

const { spawn } = require('child_process');
const { EventEmitter } = require('events');
const path = require('path');
const readline = require('readline');
const fs = require('fs');

const ENGINE_PATH = process.env.G4CARE_ENGINE ||
    path.join(__dirname, '..', 'build', 'g4care-engine');
// The engine dir also carries the HDF5 stub (libhdf5_serial.so.103) that
// satisfies libG4analysis.so's old HDF5 ABI (G4CARE output is ROOT-only).
const ENGINE_DIR = path.dirname(ENGINE_PATH);
const GDCM_LIB = process.env.G4CARE_GDCM_LIB || '/home/ever/software/gdcm-install/lib';
const ROOT_LIB = process.env.G4CARE_ROOT_LIB || '/home/ever/software/ROOT/install/lib';
const GEANT4_LIB = process.env.G4CARE_GEANT4_LIB ||
    '/home/ever/software/geant4-11.4.0/geant4-v11.4.0-install/lib';

class Sidecar extends EventEmitter {
    constructor() {
        super();
        this.child = null;
        this.nextId = 1;
        this.pending = new Map(); // id -> {resolve, reject}
    }

    get running() { return this.child !== null; }

    start() {
        if (this.child) return true;
        if (!fs.existsSync(ENGINE_PATH)) {
            this.emit('log', `[gui] engine not found: ${ENGINE_PATH}`);
            return false;
        }
        const env = {
            ...process.env,
            LD_LIBRARY_PATH: [ENGINE_DIR, GDCM_LIB, ROOT_LIB, GEANT4_LIB, process.env.LD_LIBRARY_PATH]
                .filter(Boolean).join(':'),
            // Use the software Z-buffer driver instead of the GLES (TSG) driver:
            // the GLES shader path hangs on GL 3.2/GLSL 150 contexts (llvmpipe).
            G4VIS_DEFAULT_DRIVER: process.env.G4VIS_DEFAULT_DRIVER || 'TSGZB',
        };
        // The Geant4 Qt/OGL window (VISUALIZATION: true) needs a working OpenGL
        // context. On machines/remote sessions where GLX is broken (e.g. no GPU
        // driver, X forwarding without GLX) Qt crashes with "QOpenGLWidget:
        // Failed to create context". G4CARE_SOFTWARE_GL=1 forces the engine to
        // use software OpenGL (Mesa llvmpipe) so the Geant4 GUI still opens.
        if (process.env.G4CARE_SOFTWARE_GL) {
            env.LIBGL_ALWAYS_SOFTWARE = '1';
            env.QT_OPENGL = 'software';
            env.QT_QUICK_BACKEND = 'software';
        }
        this.child = spawn(ENGINE_PATH, ['--rpc'], { env, stdio: ['pipe', 'pipe', 'pipe'] });

        const outRl = readline.createInterface({ input: this.child.stdout });
        outRl.on('line', (line) => this._onLine(line));

        const errRl = readline.createInterface({ input: this.child.stderr });
        errRl.on('line', (line) => this.emit('log', line));

        this.child.on('error', (err) => this.emit('log', `[gui] engine error: ${err.message}`));
        this.child.on('exit', (code) => {
            // Ignore exits of stale children (e.g. killed by restart()): only the
            // CURRENT child's exit should reject pending requests.
            if (this.child !== child) return;
            for (const [, p] of this.pending) p.reject(new Error(`engine exited (code ${code})`));
            this.pending.clear();
            this.child = null;
            this.emit('status', 'idle');
        });

        this.emit('status', 'running');
        return true;
    }

    stop() {
        if (!this.child) return;
        this.child.kill();
        this.child = null;
    }

    restart() {
        this.stop();
        return this.start();
    }

    rpc(method, params = {}) {
        if (!this.child) return Promise.reject(new Error('engine not running'));
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            try {
                this.child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
            } catch (e) {
                this.pending.delete(id);
                reject(e);
            }
        });
    }

    _onLine(line) {
        if (!line.trim()) return;
        let msg;
        try { msg = JSON.parse(line); } catch (e) { return; }
        if (msg && msg.method === 'progress') {
            this.emit('progress', msg.params);
        } else if (msg && msg.id !== undefined && msg.id !== null) {
            const p = this.pending.get(msg.id);
            if (p) {
                this.pending.delete(msg.id);
                if (msg.error) p.reject(new Error(msg.error.message || 'rpc error'));
                else p.resolve(msg.result);
            }
        }
    }
}

module.exports = { Sidecar, ENGINE_PATH, GDCM_LIB };
