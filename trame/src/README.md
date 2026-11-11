# trame/src — vtk.js geometry viewer (client)

The client is built by Vite (`gui/vite.viewer.config.js`) into `../viewer.bundle.js`,
which `viewer.html` loads. The entry is `viewer.js`; it is being split into
domain modules so it stops being a ~5000-line monolith.

## Target layout

```
src/
├── viewer.js            # entry / bootstrap (fullScreen, renderer, interactor)
├── context.js           # vtk bootstrap singletons (fullScreen, renderer, ...)
├── math3d.js            # quaternion/euler/vector/closest-point helpers
├── state.js             # shared scene collections (actors, objectMeta, mergedMesh)
├── api.js               # fetch wrappers + /api/update postUpdate
├── ui/                  # DOM helpers (toolbar, inputs, toast, prompt, context menu)
│   ├── format.js        # rgbCss / fmtNum / fmtVec
│   ├── dom.js           # btnStyle / infoRow / promptText / confirmAction / showToast
│   └── inputs.js        # checkboxInput / selectInput / textInput / vec3Input / rgbInput / opacityInput / numInput
├── render/
│   ├── sources.js       # parametric shape sources (makeSource) + color constants
│   ├── mesh.js          # polydata / point-cloud / mesh builders
│   ├── actors.js        # markers / grid / axes / visibility
│   ├── picking.js       # pick / highlight / select / hover
│   ├── mesh-measure.js  # closest point / surface sampling / ray cast
│   └── mesh-merged.js   # merged GDML mesh (single actor + per-cell colors)
├── scene/
│   ├── materials.js     # material discovery / export / create dialog
│   └── io.js            # loadGeometry / poll
├── panels/
│   └── objects.js       # object property mutations (position/rotation/material/...)
├── tools/
│   ├── measure.js       # ruler / angle / min-distance / thickness
│   ├── snap.js          # snapping / aux points
│   ├── primitives.js    # lines / circles / planes / figures
│   ├── section.js       # clipping planes
│   ├── annotations.js   # labels
│   └── view.js          # representation / camera / pan / fit
├── history.js           # undo / redo
├── sources.js           # (planned) source panel + solid-angle visuals
└── constraints.js       # (planned) KOMPAS-like geometric constraints
```

Extracted so far: `context.js`, `math3d.js`, `state.js`, `api.js`, `history.js`,
`render/sources.js`, `render/mesh.js`, `render/actors.js`, `render/picking.js`,
`render/mesh-measure.js`, `render/mesh-merged.js`, `ui/format.js`, `ui/dom.js`,
`ui/inputs.js`, `scene/materials.js`, `scene/io.js`, `tools/annotations.js`,
`tools/primitives.js`, `tools/snap.js`, `tools/measure.js`, `tools/section.js`,
`tools/view.js`, `panels/objects.js`.

Remaining in `viewer.js`: the tree/layers/properties panel UI, context menu, coord
panel, and interactor bootstrap (kept together because they share the DOM toolbar).

## Functional roadmap (after refactoring)

Done: A, B, C, F. Next: E (sources), then D (constraints).

- **A. Direct GDML open** — DONE: `.gdml` opens as honest XML editing (the editor
  edits the XML itself; a working copy feeds the geometry server), Save writes the
  original file.
- **B. Single-object edit** — DONE: copy / delete via `geom/updates.py`
  `duplicate` / `delete` ops + viewer context menu (YAML objects only).
- **C. Batch ops** — DONE: `updateMultiSelBar()` copy/delete buttons +
  `POST /api/batch` (atomic, single save).
- **F. GDML editing + full object interaction** — DONE:
  - `geom/gdml_edit.py` edits `<physvol>` placement/duplicate/delete; `POST /api/gdml`.
  - `gdml_loader.py` caches per-solid tessellation — placement edits never re-tessellate.
  - viewer: GDML volumes copy/delete (context menu) + position/rotation (properties
    panel), move/rotate toolbar buttons (YAML live-drag, GDML on-release), cut/paste
    (YAML via `insert` op). GDML cut/paste = copy + delete (paste-from-name).
- **E. Source panel** — IN PROGRESS: `geom/sources.py` → `/sources.json` +
  `POST /api/source` (edit a source's fields); viewer `tools/sources.js` (list +
  static visuals + «Сэмплировать» rays with footprint + «✎» settings editor down
  to ISOTOPES + SOURCE_MODE). DONE: engine `sampleSource` RPC (C++), GUI bridge.
  Next: gauss-beam explicit view, energy-colored rays, VERTEX_GENERATOR/VERTICES
  editing.
- **D. Constraints** — `GEOMETRY.CONSTRAINTS` (lock/coaxial/parallel/face/distance),
  interactive projection solver reusing `surfaceNormalAt` / `closestPointOnMesh`.

