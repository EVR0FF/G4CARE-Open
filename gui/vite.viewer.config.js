import { defineConfig } from 'vite';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Builds the vtk.js geometry viewer (trame/viewer.js -> trame/viewer.bundle.js).
// Lives in gui/ so that `vite` and `@kitware/vtk.js` resolve from gui/node_modules;
// the entry and output point into the trame/ directory.
export default defineConfig({
    root: __dirname,
    resolve: {
        alias: {
            // The viewer entry lives in trame/ (sibling of gui/), so Node-style
            // resolution can't reach gui/node_modules by walking up. Point the
            // vtk.js import explicitly at the GUI's installed copy.
            '@kitware/vtk.js': path.resolve(__dirname, 'node_modules', '@kitware', 'vtk.js'),
        },
    },
    build: {
        outDir: path.resolve(__dirname, '..', 'trame'),
        emptyOutDir: false, // keep gdml_loader.py, geometry_server.py, viewer.html, ...
        target: 'esnext',
        minify: false,
        rollupOptions: {
            input: { viewer: path.resolve(__dirname, '..', 'trame', 'src', 'viewer.js') },
            output: {
                entryFileNames: 'viewer.bundle.js',
                chunkFileNames: 'chunks/[name]-[hash].js',
                format: 'es',
            },
        },
    },
});
