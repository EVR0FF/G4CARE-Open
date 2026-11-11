//==============================================================================
// G4CARE - settings.js — Settings modal (grid + simulation + RNTuple output options)
//
// Settings modal (grid + simulation + RNTuple output options)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { parse, stringify } from 'yaml';

// Settings modal: populate the simulation fields from the geometry config, then
// apply edits back into the config (and to the engine) on "Apply".
export function initSettings({ geometryModel, appendLog }) {
    const settingsModal = document.getElementById('settings-modal');

    window.g4care.onMenuSettings(() => {
        // Populate the simulation fields from the current geometry config.
        try {
            const doc = parse(geometryModel.getValue()) || {};
            document.getElementById('set-verbosity').value = doc.VERBOSITY ?? 0;
            document.getElementById('set-nthreads').value = doc.NTHREADS ?? 1;
            const seed = Array.isArray(doc.SEED) && doc.SEED.length >= 2 ? doc.SEED : [123, 2004];
            document.getElementById('set-seed1').value = seed[0] ?? 123;
            document.getElementById('set-seed2').value = seed[1] ?? 2004;
            const outFile = (doc.OUTPUT && typeof doc.OUTPUT === 'object' && doc.OUTPUT.file)
                ? doc.OUTPUT.file : (doc.OUTPUT_FILE ?? 'output.root');
            document.getElementById('set-output-file').value = outFile;
            const comp = (doc.OUTPUT && doc.OUTPUT.compression) || {};
            document.getElementById('set-compression-algo').value = comp.algorithm || 'zstd';
            document.getElementById('set-compression-level').value = comp.level ?? 5;
            document.getElementById('set-cluster-size').value = (doc.OUTPUT && doc.OUTPUT.cluster_size_mb) || 128;
            document.getElementById('set-page-size').value = (doc.OUTPUT && doc.OUTPUT.page_size_kb) || 1024;
        } catch (e) { /* ignore */ }
        settingsModal.style.display = 'block';
    });

    document.getElementById('set-cancel').onclick = () => { settingsModal.style.display = 'none'; };

    document.getElementById('set-apply').onclick = async () => {
        const settings = {
            axes_size: parseFloat(document.getElementById('set-axes-size').value) || 20,
        };
        try { await window.g4care.writeSettings(settings); } catch (e) { /* ignore */ }
        // Update the simulation keys in the geometry config and push to the engine.
        try {
            const doc = parse(geometryModel.getValue()) || {};
            doc.VERBOSITY = parseInt(document.getElementById('set-verbosity').value, 10) || 0;
            doc.NTHREADS = parseInt(document.getElementById('set-nthreads').value, 10) || 1;
            doc.SEED = [
                parseInt(document.getElementById('set-seed1').value, 10) || 123,
                parseInt(document.getElementById('set-seed2').value, 10) || 2004,
            ];
            const outFile = (document.getElementById('set-output-file').value || '').trim() || 'output.root';
            const compAlgo = document.getElementById('set-compression-algo').value || 'zstd';
            const compLevel = parseInt(document.getElementById('set-compression-level').value, 10) || 5;
            const clusterMb = parseInt(document.getElementById('set-cluster-size').value, 10) || 128;
            const pageKb = parseInt(document.getElementById('set-page-size').value, 10) || 1024;
            if (doc.OUTPUT && typeof doc.OUTPUT === 'object') {
                doc.OUTPUT.file = outFile;
                doc.OUTPUT.compression = { algorithm: compAlgo, level: compLevel };
                doc.OUTPUT.cluster_size_mb = clusterMb;
                doc.OUTPUT.page_size_kb = pageKb;
            } else {
                doc.OUTPUT = { file: outFile, compression: { algorithm: compAlgo, level: compLevel }, cluster_size_mb: clusterMb, page_size_kb: pageKb };
            }
            delete doc.OUTPUT_FILE;
            const text = stringify(doc);
            geometryModel.setValue(text);
            window.g4care.writeGeometryYaml(text).catch(() => {});
        } catch (e) { appendLog('  settings ERROR: ' + e.message); }
        settingsModal.style.display = 'none';
        appendLog('[gui] settings: ' + JSON.stringify(settings));
    };

    // Push default settings so the grid state is known to the geometry server.
    window.g4care.writeSettings({ grid: { show: false, spacing: 10, snap: false }, axes_size: 20 }).catch(() => {});
}
