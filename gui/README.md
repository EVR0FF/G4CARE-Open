# G4CARE GUI (Electron)

Desktop shell for G4CARE: config editor (Monaco), cloud tab, ROOT viewer (JSROOT)
and a native engine sidecar (`g4care-engine --rpc`).

## Prerequisites
- Node.js ≥ 18
- Built engine sidecar: `../build/g4care-engine` (see repo root `CMakeLists.txt`).
  Path is overridable via `G4CARE_ENGINE`.

## Install

```bash
npm install --ignore-scripts
```

> **Why `--ignore-scripts`:** `jsroot` declares `canvas` (a native module used
> only for *server-side* PNG rendering). Building it requires Cairo headers and
> a network download (`prebuild-install`). The GUI uses JSROOT only in the
> browser, so `canvas` is never loaded — skipping install scripts is safe.

## Build & run

```bash
npm run build      # bundle the renderer (Monaco + JSROOT) into dist/
npm start          # launch Electron (loads dist/index.html)
G4CARE_GUI_DEV=1 npm run dev   # dev mode: Vite dev server on :5173 + Electron
```

## Environment variables

| Var | Default | Purpose |
|---|---|---|
| `G4CARE_ENGINE` | `../build/g4care-engine` | path to the sidecar binary |
| `G4CARE_GDCM_LIB` | `/home/ever/software/gdcm-install/lib` | GDCM lib dir (added to `LD_LIBRARY_PATH`) |
| `G4CARE_CLOUD_URL` | `http://127.0.0.1:5174` | URL of the cloud frontend shown in the "Cloud" tab |
| `G4CARE_GUI_DEV` | `0` | load `http://localhost:5173` instead of `dist/index.html` |

## Tabs
- **Editor** — Monaco + YAML schema (`.vscode/config-schema.json`) + snippets
  (`.vscode/g4.code-snippets`). "Run" sends the editor text to the engine via
  `loadConfigText` → `init` → `run`.
- **Cloud** — iframe of the cloud frontend (reuse `cloud/frontend/`).
- **ROOT** — open a `.root` file and browse/draw its objects with JSROOT.

## Sidecar bridge
`sidecar.js` is an Electron-free module that spawns `g4care-engine --rpc` and
speaks JSON-RPC/NDJSON. Verify it standalone with:

```bash
node test-sidecar.js
```
