//==============================================================================
// G4CARE - state.js — Application state
//
// Application state
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

export const actors = new Map(); // name -> actor
export const actorNameMap = new Map(); // actor -> name
export const baseColors = new Map(); // name -> [r,g,b]
export const objectInfo = new Map(); // name -> { source, clipFilter, pointCloud }
export const objectMeta = new Map(); // name -> { shape, dims, pos, material, layer, color, opacity, visible, isSensitive }
export const planeActorMap = new Map(); // actor -> plane index (for clickable planes)
export const multiSel = new Set(); // multi-select (object names); drives tree highlight + batch/group/export
export const highlighted = new Set(); // names currently colored away from their base color
export const IDENTITY_MATRIX = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

// All GDML meshes are merged into a single actor (one draw call). Per-volume
// selection/highlight/visibility is done through per-cell RGBA colors.
export const mergedMesh = {
  actor: null,
  mapper: null,
  pd: null,
  clipFilter: null,
  cutPd: null,
  cutCellName: [],
  pointCloud: null,
  cellName: [],
  nameToCells: new Map(),
  cellColors: null,
  baseCellColors: null,
  defs: [], // raw {name, vertices, triangles, opacity} for rebuild on visibility change
};

export const gridState = { actor: null, show: false, spacing: 10, snap: false, halfSize: 200 };

// Material lists discovered from /materials.json (NIST + user folder + GDML).
export const materialsData = { nist: [], user: [], gdml: [] };

// Snapping state (vertices, centers, grid, auxiliary points).
export const snap = { enabled: true, grid: true, auxPoints: [], auxMeta: [], auxMode: false, deleteMode: false, mode: 'auto' };

// View / selection scalars. ES module imports are read-only bindings, so these
// reassigned scalars are grouped into a mutable object shared across modules.
export const view = {
  selectedName: null,
  hoveredName: null,
  repMode: 1,
  selectedPlaneIndex: -1,
  selectedAuxIndex: -1,
  selectedPrimIndex: -1,
  selectedAnnotationIndex: -1,
  lastSignature: '',
  globalLayers: [],
  labelMode: false,
  coordMode: false,
  coordPanelInitialized: false,
  coordEditing: false,
  angleMode: false,
  anglePts: [],
  mindistMode: false,
  mindistNames: [],
  thicknessMode: false,
  primMode: false,
  primType: 'line',
  primPts: [],
  primNormal: [0, 0, 1],
  moveMode: false,
  rotateMode: false,
  sourcePlaceMode: false,
  selectedSourceName: null,
  sourceType: 'particleGun',
  sourcePts: [],
  alignMode: null,
  alignRef: null,
  dimMode: false,
  dimPts: [],
  constraintMode: false,
  constraintType: 'coincident',
};

// Section / clipping-plane state.
export const section = {
  enabled: false,
  scope: 'global',        // 'global' | 'local'
  localTarget: null,      // object name for 'local' scope
  selective: false,
  cutSet: new Set(),
  pickMode: false,
  pickPoints: [],
  planes: [],             // array of vtkPlane (world space)
  active: 0,
};

// Actor -> primitive index (for clickable primitives).
export const primActorMap = new Map();

// Annotations (labels with callouts, persisted in YAML ANNOTATIONS).
export const annotations = [];
export const annotationActors = [];
export const annotationLabels = [];
export const annotationMarkerMap = new Map();

// Tool state (ruler, primitives, snapping, section plane build, layers).
export const measure = { active: false, pts: [], lineActor: null, markers: [] };
export const measureExtras = [];
export const auxMarkers = [];
export const localGrid = { enabled: false, actor: null, spacing: 10 };
export const prims = [];
export const primMarkers = [];
export const dimensions = [];
// Entity-move notification hook: dimensions.js registers a callback here; object
// and primitive move functions invoke it so persistent constraints re-solve.
export const moveNotifier = { fns: [] };
export const constraints = [];
// Object-move hook for primitive/point attachment: modules register callbacks
// that shift primitives/points placed on an object when the object translates.
export const objectMoveHook = { fns: [] };
export const planeBuild = {
  mode: null,
  pts: [],          // points for 'points' mode
  selPrims: [],     // primitive indices for 'prims' / 'perp' / 'angle'
  selPlanes: [],    // plane indices for 'parallel' / 'mid'
  refPoint: null,   // point picked on empty space (segment + point)
  refPlane: -1,     // reference plane for 'parallel' / 'angle'
  angle: 0,
};
export const expandedGroups = new Set();
export const customLayers = new Set();
export const groups = new Map(); // name -> { parent, children } (geometry scene-graph groups)
