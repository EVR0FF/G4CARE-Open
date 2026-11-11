# G4CARE — ParaView/trame viewer

In-browser VTK viewer for G4CARE scoring meshes (`.vtu`/`.vtk`) — octree/voxel
grids with per-field coloring (flux, edep, dose, …). Embedded in the GUI
"ParaView" tab via an iframe.

## Setup

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## Run

```bash
.venv/bin/python server.py scoring.vtu --port 8788     # scoring mesh (voxels/octree)
.venv/bin/python geometry_server.py config.yaml --port 8787  # geometry viewer (vtk.js, local render)
```

Ports used by the GUI:
- `:8787` — **geometry viewer** (YAML `GEOMETRY.OBJECTS` → JSON → client-side vtk.js render).
- `:8788` — **scoring viewer** (`.vtu`/`.vtp`/`.vtk`).

Then open http://127.0.0.1:8787 (the GUI "Geometry" tab points here by default;
override with `G4CARE_GEOMETRY_URL`).

## Geometry viewer (client-side vtk.js)

The geometry viewer is no longer a trame `VtkRemoteView`. It is a tiny HTTP server
(`geometry_server.py`, stdlib only) that serves:

- `/` → `viewer.html` — an importmap + `<script type="module">` that loads the
  `@kitware/vtk.js` ESM build from a CDN (unpkg) and renders locally in WebGL.
- `/geometry.json` → `[{name, shape, dims, pos}, ...]` parsed from the YAML config.
- `/viewer.js` → the client that builds one vtk.js source/actor per object
  (`DIMENSIONS` conventions match `src/Geometry/ObjectManager.cc`) and offers a
  smooth trackball manipulator.

The scene is polled every second and rebuilt only when the geometry changes, so the
camera is preserved while the user rotates. vtk.js is fetched from the CDN for now;
vendor it locally (or bundle via Vite) to go offline.

## Details
- Loads `.vtu` (`vtkXMLUnstructuredGridReader`), `.vtp` (`vtkXMLPolyDataReader`)
  or `.vtk` (`vtkUnstructuredGridReader`). The engine's `exportSceneVtk` RPC
  writes a `.vtp` (Geant4 geometry via the G4Vtk driver) — point the server at it.
- Builds a `vtkDataSetMapper` → `vtkActor` → `vtkRenderer` → `vtkRenderWindow`,
  rendered server-side by trame's `VtkRemoteView` (offscreen).
- Dropdown to switch the active scalar field (point/cell data arrays).
