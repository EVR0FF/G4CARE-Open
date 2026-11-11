import { defineConfig } from 'vite';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
    base: './',
    resolve: {
        alias: {
            // monaco-worker-manager (dep of monaco-yaml) hardcodes the
            // "esm/vs/" path, which no longer resolves under monaco-editor 0.57's
            // "exports" map. Point it at the real file.
            'monaco-editor/esm/vs': path.resolve(__dirname, 'node_modules/monaco-editor/esm/vs'),
        },
    },
    build: {
        outDir: 'dist',
        emptyOutDir: true,
        target: 'esnext',
    },
    server: {
        port: 5173,
        strictPort: true,
    },
});
