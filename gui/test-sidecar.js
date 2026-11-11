// G4CARE GUI — standalone bridge test (no Electron). Exercises the full
// RPC cycle against the real sidecar binary. Run with `node test-sidecar.js`.
'use strict';

const path = require('path');
const { Sidecar, ENGINE_PATH } = require('./sidecar');

const CONFIG = path.join(__dirname, '..', 'examples', 'tests', 'test_gdml_export.yaml');

function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

async function main() {
    console.log('engine:', ENGINE_PATH);
    const s = new Sidecar();
    s.on('log', (line) => console.log('  [stderr]', line));
    s.on('status', (st) => console.log('  [status]', st));
    s.on('progress', (p) => console.log('  [progress]', JSON.stringify(p)));

    if (!s.start()) { process.exit(1); }
    await sleep(400); // let the engine boot

    console.log('ping        =>', JSON.stringify(await s.rpc('ping')));
    console.log('getStatus   =>', JSON.stringify(await s.rpc('getStatus')));
    console.log('loadConfig  =>', JSON.stringify(await s.rpc('loadConfig',
        { path: CONFIG })));
    console.log('init        =>', JSON.stringify(await s.rpc('init')));
    console.log('run(5)      =>', JSON.stringify(await s.rpc('run', { nEvents: 5 })));
    console.log('getStatus   =>', JSON.stringify(await s.rpc('getStatus')));
    console.log('shutdown    =>', JSON.stringify(await s.rpc('shutdown')));

    s.stop();
    console.log('OK');
    process.exit(0);
}

main().catch((e) => { console.error('FAIL:', e.message); process.exit(1); });
