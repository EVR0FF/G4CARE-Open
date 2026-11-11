//==============================================================================
// G4CARE - mpr.js — Multi-planar reconstruction (MPR) viewer for the DICOM tab
//
// Multi-planar reconstruction (MPR) viewer for the DICOM tab
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

// Register the vtk.js OpenGL/WebGPU view-node factories (Renderer, ImageMapper,
// ImageSlice, Actor, PolyDataMapper, ...). Without this the MPR panes throw
// "No vtkOpenGLViewNodeFactory implementation found for vtkRenderer".
import '@kitware/vtk.js/Rendering/Profiles/All';

import vtkGenericRenderWindow from '@kitware/vtk.js/Rendering/Misc/GenericRenderWindow';
import vtkInteractorStyleImage from '@kitware/vtk.js/Interaction/Style/InteractorStyleImage';
import vtkImageData from '@kitware/vtk.js/Common/DataModel/ImageData';
import vtkDataArray from '@kitware/vtk.js/Common/Core/DataArray';
import vtkImageMapper from '@kitware/vtk.js/Rendering/Core/ImageMapper';
import vtkImageSlice from '@kitware/vtk.js/Rendering/Core/ImageSlice';
import vtkImageProperty from '@kitware/vtk.js/Rendering/Core/ImageProperty';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkPolyData from '@kitware/vtk.js/Common/DataModel/PolyData';
import vtkPoints from '@kitware/vtk.js/Common/Core/Points';
import vtkCellArray from '@kitware/vtk.js/Common/Core/CellArray';
import vtkVolume from '@kitware/vtk.js/Rendering/Core/Volume';
import vtkVolumeMapper from '@kitware/vtk.js/Rendering/Core/VolumeMapper';
import vtkVolumeProperty from '@kitware/vtk.js/Rendering/Core/VolumeProperty';
import vtkColorTransferFunction from '@kitware/vtk.js/Rendering/Core/ColorTransferFunction';
import vtkPiecewiseFunction from '@kitware/vtk.js/Common/DataModel/PiecewiseFunction';
import vtkInteractorStyleTrackballCamera from '@kitware/vtk.js/Interaction/Style/InteractorStyleTrackballCamera';
import vtkImageMarchingCubes from '@kitware/vtk.js/Filters/General/ImageMarchingCubes';
import { computeDvh } from './dvh.mjs';
import { t } from '../i18n/i18n.js';

// Each anatomical plane is defined by which voxel axis is the slice normal (k)
// and which two are the in-plane column (i) and row (j) axes.
// For identity direction (LPS): axis 0 = X (LR), 1 = Y (PA), 2 = Z (IS).
import { VIEWS, DEFAULT_WINDOW, DEFAULT_LEVEL, marchingSquares, marchingSquaresIsoline, injectCss } from './mpr-utils.js';

// Build a ruler as a single vtkPolyData with a main line + tick marks.
// `horizontal` = true for a horizontal ruler; ticks every `tickMm`, with a
// longer tick every 5 divisions.
function makeRuler(horizontal, lengthMm, tickMm) {
  const pts = [];
  const cells = [];
  let p = 0;
  const seg = (x0, y0, x1, y1) => {
    pts.push(x0, y0, 0.7, x1, y1, 0.7);
    cells.push(2, p, p + 1);
    p += 2;
  };
  if (horizontal) {
    seg(0, 0, lengthMm, 0);
    for (let x = 0; x <= lengthMm + 1e-6; x += tickMm) {
      const h = (Math.round(x / tickMm) % 5) === 0 ? 3.5 : 2;
      seg(x, -h, x, h);
    }
  } else {
    seg(0, 0, 0, lengthMm);
    for (let y = 0; y <= lengthMm + 1e-6; y += tickMm) {
      const h = (Math.round(y / tickMm) % 5) === 0 ? 3.5 : 2;
      seg(-h, y, h, y);
    }
  }
  const polyData = vtkPolyData.newInstance();
  const vpts = vtkPoints.newInstance();
  vpts.setData(Float32Array.from(pts), 3);
  polyData.setPoints(vpts);
  const ca = vtkCellArray.newInstance();
  ca.setData(Uint32Array.from(cells));
  polyData.setLines(ca);
  const mapper = vtkMapper.newInstance();
  mapper.setInputData(polyData);
  const actor = vtkActor.newInstance();
  actor.setMapper(mapper);
  actor.getProperty().setColor(0.98, 0.8, 0.15);
  actor.getProperty().setLineWidth(1.5);
  actor.getProperty().setLighting(false);
  return actor;
}

/**
 * Create an MPR viewer inside `container`.
 * @param {HTMLElement} container
 * @param {object} [hooks] { onSliceChange({view,index}), onWindowLevel({window,level}) }
 * @returns viewer API { load, setSlice, setWindowLevel, setCrosshairsVisible, getState, resize, destroy }
 */
export function createMprViewer(container, hooks = {}) {
  injectCss();

  const state = {
    dims: [0, 0, 0],        // nx, ny, nz
    spacing: [1, 1, 1],     // dx, dy, dz
    data: null,             // Int16Array or Float32Array of voxel values
    doseData: null,         // Float32Array dose grid (Gy), aligned with the CT
    floatData: false,       // true when data is Float32Array (PET SUV etc.)
    loaded: false,
    window: DEFAULT_WINDOW,
    level: DEFAULT_LEVEL,
    slabMm: 0,              // projection slab thickness in mm (0 = single slice)
    projectionMode: 'max',  // slab projection type: 'max' | 'min' | 'mean'
    oblique: { azimuthDeg: 0, elevationDeg: 0 },  // oblique axial plane tilt
    rois: [],               // RTSTRUCT ROIs: {number, name, contours:[{x,y,z}]}
    origin: [0, 0, 0],      // CT origin (mm LPS), for mapping contours to panes
    direction: [1, 0, 0, 0, 1, 0, 0, 0, 1],  // row-major 3x3 DICOM direction (LPS)
    segClasses: [],         // auto-segmentation classes: {name, min, max, color}
    growSeeds: [],          // region-growing seeds: [ix, iy, iz]
    volumes: [],            // named 3D masks: { id, name, color:[r,g,b], mask, visible, stats }
    volumeIdSeq: 0,
    seedMode: false,        // click-to-place-seed mode
    measureMode: false,     // click-to-place-measurement mode
    measureType: 'length',  // length|polyline|angle|cobb|rectangle|ellipse|polygon
    measurePoints: { axial: [], coronal: [], sagittal: [] },  // per-pane [{x, y}] world mm
    measurePane: null,      // pane that received the last measurement click
    measureValue: '',       // formatted measurement value
    measureOpen: false,    // true while a polyline/polygon/spline is being drawn
    measureSlice: null,    // slice index the in-progress measurement was started on
    measurements: [],       // committed measurements: {id, type, pane, points, value, visible}
    measureId: 0,
    selectedId: null,       // id of the measurement selected in the tree
    panMode: false,        // left-drag pans the image instead of adjusting W/L
    panStart: null,        // {x, y, fx, fy} while pan-dragging
    freehandDrawing: false, // true while drawing a freehand region
    freehandPoints: [],     // temporary points during freehand drawing
    invert: false,          // invert grayscale
    lens: { fx: 0.5, fy: 0.5, size: 180, zoom: 2 },  // shared lens: fx,fy = fraction of pane
    layout: 'mpr',          // 'mpr' (2x2) | 'axial'|'coronal'|'sagittal'|'3d' (single view)
    crosshairsVisible: true,
  };

  const panes = {};
  let activePaneKey = 'axial';   // pane under the cursor (for keyboard shortcuts)
  let volumePane = null;
  let lensDrag = null;          // active magnifier-lens drag state
  let crosshairDrag = null;     // pane currently dragging the crosshair
  const sharedProperty = vtkImageProperty.newInstance();
  sharedProperty.setColorWindow(state.window);
  sharedProperty.setColorLevel(state.level);
  sharedProperty.setInterpolationTypeToLinear();

  container.classList.add('mpr-root');
  container.innerHTML = '';

  const grid = document.createElement('div');
  grid.className = 'mpr-grid';
  container.appendChild(grid);

  // WebGL context-loss recovery: a lost GPU context would otherwise leave the
  // panes blank (white) forever. preventDefault() lets the browser restore the
  // context; we surface a hint instead of a silent white screen.
  function watchWebGLContext(canvasHost) {
    const cv = canvasHost.querySelector('canvas');
    if (!cv) return;
    cv.addEventListener('webglcontextlost', (e) => {
      e.preventDefault();
      if (!canvasHost.querySelector('.mpr-gl-lost')) {
        const note = document.createElement('div');
        note.className = 'mpr-gl-lost';
        note.style.cssText = 'position:absolute; left:0; right:0; bottom:0; background:rgba(30,30,30,0.92); color:#ffd27f; font:12px sans-serif; padding:4px 8px; z-index:9; pointer-events:none;';
        note.textContent = t('msg.gpuLost');
        canvasHost.appendChild(note);
      }
    });
    cv.addEventListener('webglcontextrestored', () => {
      const note = canvasHost.querySelector('.mpr-gl-lost');
      if (note) note.remove();
      renderAll();
    });
  }

  // 4th slot (top-left): 3D volume reconstruction (ray casting).
  {
    const paneDiv = document.createElement('div');
    paneDiv.className = 'mpr-pane';
    const head = document.createElement('div');
    head.className = 'mpr-pane-head';
    const label = document.createElement('span');
    label.textContent = '3D';
    const vinfo = document.createElement('span');
    vinfo.className = 'mpr-pane-info';
    head.appendChild(label);
    head.appendChild(vinfo);
    paneDiv.appendChild(head);
    const canvasHost = document.createElement('div');
    canvasHost.className = 'mpr-canvas';
    paneDiv.appendChild(canvasHost);
    grid.appendChild(paneDiv);

    const glw = vtkGenericRenderWindow.newInstance({ background: [0.05, 0.05, 0.07], listenWindowResize: true });
    glw.setContainer(canvasHost);
    watchWebGLContext(canvasHost);
    glw.getInteractor().setInteractorStyle(vtkInteractorStyleTrackballCamera.newInstance());
    const renderer = glw.getRenderer();
    const renderWindow = glw.getRenderWindow();

    const volImageData = vtkImageData.newInstance();
    const volMapper = vtkVolumeMapper.newInstance();
    volMapper.setInputData(volImageData);
    volMapper.setSampleDistance(1.0);
    // Valid placeholder so the first render (before any CT is loaded) never
    // crashes the volume mapper on a null scalar array.
    volImageData.setDimensions(1, 1, 1);
    volImageData.setSpacing(1, 1, 1);
    volImageData.setOrigin(0, 0, 0);
    volImageData.getPointData().setScalars(vtkDataArray.newInstance({
      name: 'HU', values: new Float32Array([0]), numberOfComponents: 1,
    }));
    volImageData.modified();
    const volume = vtkVolume.newInstance();
    volume.setMapper(volMapper);
    const volProperty = vtkVolumeProperty.newInstance();
    volume.setProperty(volProperty);
    renderer.addVolume(volume);

    const volCtf = vtkColorTransferFunction.newInstance();
    const volOfun = vtkPiecewiseFunction.newInstance();
    // Typical CT opacity curve: air/fat transparent, soft tissue translucent, bone opaque.
    volOfun.addPoint(-1000, 0.0);
    volOfun.addPoint(-200, 0.0);
    volOfun.addPoint(0, 0.0);
    volOfun.addPoint(100, 0.1);
    volOfun.addPoint(300, 0.4);
    volOfun.addPoint(700, 0.8);
    volOfun.addPoint(1500, 1.0);
    volOfun.addPoint(3071, 1.0);
    volProperty.setScalarOpacity(0, volOfun);
    volProperty.setScalarOpacityUnitDistance(0, 3.0);
    volProperty.setInterpolationTypeToLinear();
    volProperty.setShade(true);
    volProperty.setAmbient(0.1);
    volProperty.setDiffuse(0.9);
    volProperty.setSpecular(0.2);

    volumePane = {
      key: '3d', glw, renderer, renderWindow,
      volImageData, volMapper, volume, volProperty, volCtf, info: vinfo,
      paneDiv, head,
    };
  }

  for (const key of Object.keys(VIEWS)) {
    const view = VIEWS[key];

    const paneDiv = document.createElement('div');
    paneDiv.className = 'mpr-pane';

    const head = document.createElement('div');
    head.className = 'mpr-pane-head';
    const label = document.createElement('span');
    label.textContent = view.label;
    const info = document.createElement('span');
    info.className = 'mpr-pane-info';
    info.textContent = '—';
    const lensBtn = document.createElement('button');
    lensBtn.className = 'mpr-lens-btn';
    lensBtn.textContent = '◉';
    lensBtn.title = t('tip.magnifier');
    lensBtn.style.cssText = 'background:none; border:1px solid #444; color:#888; cursor:pointer; font:12px sans-serif; border-radius:3px; padding:0 5px; line-height:14px; margin-left:6px;';
    lensBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      const p = panes[key];
      p.lensOn = !p.lensOn;
      syncLens();
    });
    const spacer = document.createElement('span');
    spacer.style.flex = '1';
    head.appendChild(label);
    head.appendChild(spacer);
    head.appendChild(info);
    head.appendChild(lensBtn);
    paneDiv.appendChild(head);

    const canvasHost = document.createElement('div');
    canvasHost.className = 'mpr-canvas';
    paneDiv.appendChild(canvasHost);

    // Orientation markers (mirrored radiology convention: R left).
    const MK = { axial: ['A', 'P', 'R', 'L'], coronal: ['S', 'I', 'R', 'L'], sagittal: ['S', 'I', 'A', 'P'] }[key];
    const mk = {};
    const mkPos = [['mpr-mk-top', MK[0]], ['mpr-mk-bottom', MK[1]], ['mpr-mk-left', MK[2]], ['mpr-mk-right', MK[3]]];
    for (const [cls, letter] of mkPos) {
      const s = document.createElement('span');
      s.className = 'mpr-marker ' + cls;
      s.textContent = letter;
      canvasHost.appendChild(s);
      mk[cls.replace('mpr-mk-', '')] = s;
    }

    // Four-corner metadata overlays (rendered inside the viewport).
    const ovTL = document.createElement('div');
    ovTL.className = 'mpr-ov mpr-ov-tl';
    canvasHost.appendChild(ovTL);
    const ovTR = document.createElement('div');
    ovTR.className = 'mpr-ov mpr-ov-tr';
    canvasHost.appendChild(ovTR);
    const ovBL = document.createElement('div');
    ovBL.className = 'mpr-ov mpr-ov-bl';
    canvasHost.appendChild(ovBL);
    const ovBR = document.createElement('div');
    ovBR.className = 'mpr-ov mpr-ov-br';
    canvasHost.appendChild(ovBR);

    // Floating layers button (toggles the RT panel from fullscreen view).
    const layersBtn = document.createElement('button');
    layersBtn.className = 'mpr-layers';
    layersBtn.textContent = '☰';
    layersBtn.title = t('tip.rtPanel');
    layersBtn.onclick = (e) => { e.stopPropagation(); if (hooks.onToggleLayers) hooks.onToggleLayers(); };
    canvasHost.appendChild(layersBtn);

    // Two screen-space scale bars (fixed to the window, not the image): their
    // physical-length labels are recomputed from the zoom in updateRulers().
    const rulerHCanvas = document.createElement('canvas');
    rulerHCanvas.width = 140; rulerHCanvas.height = 22;
    rulerHCanvas.style.cssText = 'position:absolute; width:140px; height:22px; left:50%; top:22px; transform:translateX(-50%); pointer-events:none; z-index:3;';
    canvasHost.appendChild(rulerHCanvas);
    const rulerVCanvas = document.createElement('canvas');
    rulerVCanvas.width = 22; rulerVCanvas.height = 140;
    rulerVCanvas.style.cssText = 'position:absolute; width:22px; height:140px; left:6px; top:50%; transform:translateY(-50%); pointer-events:none; z-index:3;';
    canvasHost.appendChild(rulerVCanvas);
    const magCanvas = document.createElement('canvas');
    magCanvas.width = 180; magCanvas.height = 180;
    magCanvas.style.cssText = 'position:absolute; left:0; top:0; width:180px; height:180px; pointer-events:none; z-index:6; display:none;';
    canvasHost.appendChild(magCanvas);

    grid.appendChild(paneDiv);

    const glw = vtkGenericRenderWindow.newInstance({ background: [0.05, 0.05, 0.07], listenWindowResize: true });
    glw.setContainer(canvasHost);
    watchWebGLContext(canvasHost);
    const style = vtkInteractorStyleImage.newInstance();
    glw.getInteractor().setInteractorStyle(style);
    style.onEndInteractionEvent(() => { refreshPaneInfo(panes[key]); updateRulers(panes[key]); syncWindowLevelFromProperty(); });

    const renderer = glw.getRenderer();
    const renderWindow = glw.getRenderWindow();

    const imageData = vtkImageData.newInstance();
    const mapper = vtkImageMapper.newInstance();
    mapper.setInputData(imageData);
    const sliceActor = vtkImageSlice.newInstance();
    sliceActor.setMapper(mapper);
    sliceActor.setProperty(sharedProperty);
    renderer.addActor(sliceActor);

    // Give the mapper valid scalar data immediately so the first render (before
    // any CT is loaded) does not crash in vtkOpenGLImageMapper::renderPieceDraw
    // ("Cannot read properties of null (reading 'activate')").
    imageData.setDimensions(1, 1, 1);
    imageData.setSpacing(1, 1, 1);
    imageData.setOrigin(0, 0, 0);
    imageData.getPointData().setScalars(vtkDataArray.newInstance({
      name: 'HU', values: new Float32Array([0]), numberOfComponents: 1,
    }));
    imageData.modified();

    const vLine = vtkLineSource.newInstance();
    const vMap = vtkMapper.newInstance();
    vMap.setInputConnection(vLine.getOutputPort());
    const vActor = vtkActor.newInstance();
    vActor.setMapper(vMap);
    vActor.getProperty().setColor(0.98, 0.8, 0.15);
    vActor.getProperty().setLineWidth(1);
    renderer.addActor(vActor);

    const hLine = vtkLineSource.newInstance();
    const hMap = vtkMapper.newInstance();
    hMap.setInputConnection(hLine.getOutputPort());
    const hActor = vtkActor.newInstance();
    hActor.setMapper(hMap);
    hActor.getProperty().setColor(0.98, 0.8, 0.15);
    hActor.getProperty().setLineWidth(1);
    renderer.addActor(hActor);

    // Scale rulers with tick marks (10 cm horizontal + 20 cm vertical).
    const rulerHActor = makeRuler(true, 100, 10);
    renderer.addActor(rulerHActor);
    const rulerVActor = makeRuler(false, 200, 20);
    renderer.addActor(rulerVActor);

    panes[key] = {
      key, view, index: 0, glw, renderer, renderWindow, canvasHost,
      imageData, sliceActor, vLine, vActor, hLine, hActor, info,
      ovTL, ovTR, ovBL, ovBR, layersBtn, rulerHActor, rulerVActor, rulerHCanvas, rulerVCanvas, magCanvas, mk, huText: '',
      lensOn: false, lensBtn,
      paneDiv, head,
    };

    // Magnifier lens (Weasis-like square lens): drag the body to move it, drag
    // the bottom-right handle to resize (50..500 px), mouse wheel changes the
    // zoom factor, double-click resets the zoom to 2x. The lens state is shared
    // across panes (position kept as a fraction of the pane), so it follows the
    // same relative point in every projection.
    const lensCorner = (rect) => {
      const L = state.lens;
      const half = L.size / 2;
      return { x: L.fx * rect.width + half, y: L.fy * rect.height + half };
    };
    magCanvas.addEventListener('pointerdown', (e) => {
      if (!panes[key].lensOn) return;
      e.stopPropagation();
      const rect = canvasHost.getBoundingClientRect();
      const ex = e.clientX - rect.left;
      const ey = e.clientY - rect.top;
      const c = lensCorner(rect);
      const corner = Math.hypot(ex - c.x, ey - c.y) <= 16;
      lensDrag = { resize: corner, startX: ex, startY: ey, size: state.lens.size, fx: state.lens.fx, fy: state.lens.fy };
      try { magCanvas.setPointerCapture(e.pointerId); } catch (_) {}
    });
    magCanvas.addEventListener('pointermove', (e) => {
      if (!panes[key].lensOn) return;
      if (state.measureMode) { magCanvas.style.cursor = 'crosshair'; return; }
      const rect = canvasHost.getBoundingClientRect();
      const ex = e.clientX - rect.left;
      const ey = e.clientY - rect.top;
      if (!lensDrag) {
        const c = lensCorner(rect);
        magCanvas.style.cursor = (Math.hypot(ex - c.x, ey - c.y) <= 16) ? 'nwse-resize' : 'move';
        return;
      }
      e.stopPropagation();
      const L = state.lens;
      if (lensDrag.resize) {
        L.size = Math.max(50, Math.min(500, Math.round(Math.hypot(ex - L.fx * rect.width, ey - L.fy * rect.height) * 2)));
      } else {
        L.fx = Math.max(0, Math.min(1, lensDrag.fx + (ex - lensDrag.startX) / rect.width));
        L.fy = Math.max(0, Math.min(1, lensDrag.fy + (ey - lensDrag.startY) / rect.height));
      }
      syncLens();
    });
    magCanvas.addEventListener('pointerup', (e) => {
      lensDrag = null;
      try { magCanvas.releasePointerCapture(e.pointerId); } catch (_) {}
    });
    magCanvas.addEventListener('dblclick', (e) => {
      if (!panes[key].lensOn) return;
      e.stopPropagation();
      state.lens.zoom = 2;
      syncLens();
    });

    // Draw precisely through the magnifier: a click on the lens maps to the
    // magnified image coordinate (sub-pixel on the normal view) and places a
    // measurement point there, so a tiny structure (e.g. a capillary) can be
    // traced on the enlarged view.
    magCanvas.addEventListener('click', (e) => {
      if (!panes[key].lensOn || !state.measureMode || !state.loaded) return;
      if (state.measureType === 'freehand') return;
      e.stopPropagation();
      const w = lensScreenToWorld(key, e.clientX, e.clientY);
      if (!w) return;
      const wx = w.x, wy = w.y;
      state.measurePane = key;
      const pts = state.measurePoints[key];
      const maxPts = measureMaxPoints(state.measureType);
      if (!state.measureOpen || pts.length >= maxPts) { state.measureOpen = true; pts.length = 0; state.measureSlice = panes[key].index; }
      pts.push({ x: wx, y: wy });
      updateMeasureOverlay(key);
      renderAll();
      if (Number.isFinite(maxPts) && pts.length >= maxPts) commitMeasurement();
    });

    // Track the hovered pane so keyboard shortcuts act on the right slice.
    canvasHost.addEventListener('mouseenter', () => { activePaneKey = key; });

    // Wheel scrolls this pane's slice; when the pointer is over the magnifier
    // lens it changes the lens zoom instead. Capture phase + stopPropagation so
    // the interactor's default wheel-to-zoom never fires.
    canvasHost.addEventListener('wheel', (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (panes[key].lensOn && (e.target === magCanvas || magCanvas.contains(e.target))) {
        const L = state.lens;
        L.zoom = Math.max(1, Math.min(10, (L.zoom || 2) + (e.deltaY > 0 ? -0.5 : 0.5)));
        syncLens();
        return;
      }
      const dir = e.deltaY > 0 ? 1 : -1;
      setSlice(key, panes[key].index + dir);
    }, { capture: true, passive: false });

    // Left button: freehand measurement / pan. Intercept pointerdown so the vtk
    // interactor never starts window/level while a tool is active.
    canvasHost.addEventListener('pointerdown', (e) => {
      if (e.button !== 0 || !state.loaded) return;
      if (state.measureMode) {
        if (state.measureType === 'freehand') {
          const rect = canvasHost.getBoundingClientRect();
          if (rect.width < 1 || rect.height < 1) return;
          const cam = renderer.getActiveCamera();
          const fp = cam.getFocalPoint();
          const visibleH = 2 * cam.getParallelScale();
          const visibleW = visibleH * (rect.width / rect.height);
          state.measurePane = key;
          state.freehandDrawing = true;
          state.measureOpen = true;
          state.measureSlice = panes[key].index;
          const lw = lensScreenToWorld(key, e.clientX, e.clientY);
          state.freehandPoints = [lw || { x: ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0], y: (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1] }];
          state.measurePoints[key] = state.freehandPoints;
        }
        e.stopPropagation();
        return;
      }
      if (!state.panMode) {
        // Cross-reference lines: drag the crosshair intersection to move the
        // slices in the other views.
        if (state.crosshairsVisible && !state.seedMode && isNearCrosshair(key, e)) {
          crosshairDrag = key;
          e.stopPropagation();
        }
        return;
      }
      const cam = renderer.getActiveCamera();
      state.panStart = { x: e.clientX, y: e.clientY, fx: cam.getFocalPoint()[0], fy: cam.getFocalPoint()[1] };
      e.stopPropagation();
    }, { capture: true });

    // HU-under-cursor readout (parallel camera: map CSS px -> world -> voxel).
    canvasHost.addEventListener('pointermove', (e) => {
      const p = panes[key];
      if (state.panMode && state.panStart && state.loaded) {
        const rect = canvasHost.getBoundingClientRect();
        if (rect.height > 0) {
          const cam = renderer.getActiveCamera();
          const mmPerPx = (2 * cam.getParallelScale()) / rect.height;
          const dx = -(e.clientX - state.panStart.x) * mmPerPx;
          const dy = (e.clientY - state.panStart.y) * mmPerPx;
          cam.setFocalPoint(state.panStart.fx + dx, state.panStart.fy + dy, 0);
          cam.setPosition(state.panStart.fx + dx, state.panStart.fy + dy, 1000);
          renderAll();
        }
        return;
      }
      if (state.freehandDrawing && state.measureMode) {
        const rect = canvasHost.getBoundingClientRect();
        if (rect.width >= 1 && rect.height >= 1) {
          const cam = renderer.getActiveCamera();
          const fp = cam.getFocalPoint();
          const visibleH = 2 * cam.getParallelScale();
          const visibleW = visibleH * (rect.width / rect.height);
          const lw = lensScreenToWorld(key, e.clientX, e.clientY);
          const wx = lw ? lw.x : ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0];
          const wy = lw ? lw.y : (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1];
          const last = state.freehandPoints[state.freehandPoints.length - 1];
          if (!last || Math.hypot(wx - last.x, wy - last.y) > 3) {
            state.freehandPoints.push({ x: wx, y: wy });
            state.measurePoints[key] = state.freehandPoints;
            updateMeasureOverlay(key);
            renderAll();
          }
        }
        return;
      }
      if (crosshairDrag === key) {
        const rect = canvasHost.getBoundingClientRect();
        if (rect.width >= 1 && rect.height >= 1) {
          const cam = renderer.getActiveCamera();
          const fp = cam.getFocalPoint();
          const visibleH = 2 * cam.getParallelScale();
          const visibleW = visibleH * (rect.width / rect.height);
          const wx = ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0];
          const wy = (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1];
          const slices = crosshairDragToSlices(key, wx, wy);
          for (const [k2, idx] of Object.entries(slices)) setSlice(k2, idx);
        }
        return;
      }
      if (!state.loaded || !p.sliceData) return;
      const rect = canvasHost.getBoundingClientRect();
      if (rect.width < 1 || rect.height < 1) return;
      const v = p.view;
      const cam = renderer.getActiveCamera();
      const fp = cam.getFocalPoint();
      const scale = cam.getParallelScale();
      const visibleH = 2 * scale;
      const visibleW = visibleH * (rect.width / rect.height);
      const wx = ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0];
      const wy = (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1];
      const col = Math.floor(wx / state.spacing[v.i]);
      const row = Math.floor(wy / state.spacing[v.j]);
      const ni = state.dims[v.i];
      const nj = state.dims[v.j];
      if (col >= 0 && col < ni && row >= 0 && row < nj) {
        const val = p.sliceData[row * ni + col];
        const px = (col * state.spacing[v.i]).toFixed(1);
        const py = (row * state.spacing[v.j]).toFixed(1);
        p.huText = 'Value: ' + val + ' (' + col + ',' + row + ') Pos: ' + px + ',' + py + ' mm';
        setOvBL(p);
      }
    });
    canvasHost.addEventListener('mouseleave', () => {
      const p = panes[key];
      p.huText = '';
      setOvBL(p);
      state.panStart = null;
    });
    canvasHost.addEventListener('pointerup', () => {
      state.panStart = null;
      crosshairDrag = null;
      if (state.freehandDrawing) {
        state.freehandDrawing = false;
        state.measureOpen = false;
        commitMeasurement();
      }
    });

    // Measurement mode: a click drops a measurement point on whichever pane was
    // clicked. Points are stored per pane in world mm; the lines are drawn in the
    // same (unflipped) world frame as the image actor, so they land exactly under
    // the cursor.
    canvasHost.addEventListener('click', (e) => {
      if (!state.measureMode || !state.loaded) return;
      if (state.measureType === 'freehand') return;   // drawn by drag, not click
      const p = panes[key];
      const rect = canvasHost.getBoundingClientRect();
      if (rect.width < 1 || rect.height < 1) return;
      const v = p.view;
      const cam = renderer.getActiveCamera();
      const fp = cam.getFocalPoint();
      const scale = cam.getParallelScale();
      const visibleH = 2 * scale;
      const visibleW = visibleH * (rect.width / rect.height);
      const wx = ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0];
      const wy = (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1];
      state.measurePane = key;
      const pts = state.measurePoints[key];
      const maxPts = measureMaxPoints(state.measureType);
      if (!state.measureOpen || pts.length >= maxPts) { state.measureOpen = true; pts.length = 0; state.measureSlice = panes[key].index; }
      pts.push({ x: wx, y: wy });
      updateMeasureOverlay(key);
      renderAll();
      if (Number.isFinite(maxPts) && pts.length >= maxPts) commitMeasurement();
    });

    // Double-click completes an open polyline/polygon/spline/curve (Weasis).
    // The two clicks of a double-click each add a point, so drop the second one
    // and close the path at the first (the double-click position).
    canvasHost.addEventListener('dblclick', (e) => {
      if (!state.measureMode || !state.loaded) return;
      const t = state.measureType;
      if (t !== 'polyline' && t !== 'polygon' && t !== 'spline' && t !== 'curve') return;
      if (!state.measureOpen) return;
      e.stopPropagation();
      const pts = state.measurePoints[key] || [];
      if (pts.length > 1) pts.pop();
      commitMeasurement();
    });

    // Seed placement for "Grow from Seeds": a click drops a seed voxel on the
    // axial pane (only while seed mode is active).
    if (key === 'axial') {
      canvasHost.addEventListener('click', (e) => {
        if (!state.loaded || !state.seedMode) return;
        const p = panes.axial;
        const rect = canvasHost.getBoundingClientRect();
        if (rect.width < 1 || rect.height < 1) return;
        const v = p.view;
        const cam = renderer.getActiveCamera();
        const fp = cam.getFocalPoint();
        const scale = cam.getParallelScale();
        const visibleH = 2 * scale;
        const visibleW = visibleH * (rect.width / rect.height);
        const wx = ((e.clientX - rect.left) / rect.width - 0.5) * visibleW + fp[0];
        const wy = (0.5 - (e.clientY - rect.top) / rect.height) * visibleH + fp[1];
        let col = Math.floor(wx / state.spacing[v.i]);
        let row = Math.floor(wy / state.spacing[v.j]);
        const ni = state.dims[v.i];
        const nj = state.dims[v.j];
        if (v.flipH) col = ni - 1 - col;
        if (v.flipV) row = nj - 1 - row;
        if (col >= 0 && col < ni && row >= 0 && row < nj) {
          toggleSeed(col, row, panes.axial.index);
        }
      });
    }
  }
  function renderAll() {
    for (const key of Object.keys(VIEWS)) panes[key].renderWindow.render();
    if (volumePane) volumePane.renderWindow.render();
    syncLens();
  }

  function updateVolumeColorTF() {
    if (!volumePane) return;
    const lo = state.level - state.window / 2;
    const hi = state.level + state.window / 2;
    volumePane.volCtf.removeAllPoints();
    if (state.invert) {
      volumePane.volCtf.addRGBPoint(lo, 1, 1, 1);
      volumePane.volCtf.addRGBPoint(hi, 0, 0, 0);
    } else {
      volumePane.volCtf.addRGBPoint(lo, 0, 0, 0);
      volumePane.volCtf.addRGBPoint(hi, 1, 1, 1);
    }
    volumePane.volProperty.setRGBTransferFunction(0, volumePane.volCtf);
  }

  function setVolumePreset(name) {
    if (!volumePane) return;
    const presets = {
      [t('material.soft')]: {
        color: [[-1000, 0, 0, 0], [0, 0.4, 0.4, 0.4], [300, 1, 1, 1], [1500, 1, 1, 1]],
        opacity: [[-1000, 0], [-200, 0], [0, 0.05], [100, 0.2], [300, 0.5], [700, 0.8], [1500, 1]],
      },
      [t('material.bone')]: {
        color: [[-1000, 0, 0, 0], [300, 0.9, 0.85, 0.75], [700, 1, 1, 1], [2000, 1, 1, 1]],
        opacity: [[-1000, 0], [300, 0], [400, 0.4], [700, 0.9], [1200, 1], [2000, 1]],
      },
      [t('material.lung')]: {
        color: [[-1000, 0.4, 0.5, 0.7], [-800, 0.7, 0.7, 0.8], [-500, 1, 1, 1], [0, 0, 0, 0]],
        opacity: [[-1000, 0.15], [-900, 0.5], [-800, 0.7], [-500, 0.15], [0, 0]],
      },
    };
    const p = presets[name] || presets[t('material.soft')];
    const ctf = volumePane.volCtf;
    ctf.removeAllPoints();
    for (const [x, r, g, b] of p.color) ctf.addRGBPoint(x, r, g, b);
    volumePane.volProperty.setRGBTransferFunction(0, ctf);
    const ofun = volumePane.volProperty.getScalarOpacity(0);
    ofun.removeAllPoints();
    for (const [x, y] of p.opacity) ofun.addPoint(x, y);
    volumePane.volProperty.setScalarOpacity(0, ofun);
    renderAll();
  }

  function buildVolumeImageData() {
    if (!volumePane) return;
    const [nx, ny, nz] = state.dims;
    // Downsample the 3D preview so ray-casting stays light (<=256 per axis).
    let f = 1;
    const big = Math.max(nx, ny, nz);
    while (big / f > 256) f++;
    if (f === 1) {
      volumePane.volImageData.setDimensions(nx, ny, nz);
      volumePane.volImageData.setSpacing(state.spacing[0], state.spacing[1], state.spacing[2]);
      volumePane.volImageData.setOrigin(0, 0, 0);
      volumePane.volImageData.getPointData().setScalars(vtkDataArray.newInstance({
        name: 'HU', values: Float32Array.from(state.data), numberOfComponents: 1,
      }));
    } else {
      const cnx = Math.ceil(nx / f), cny = Math.ceil(ny / f), cnz = Math.ceil(nz / f);
      const src = state.data;
      const out = new Float32Array(cnx * cny * cnz);
      for (let k = 0; k < cnz; k++) {
        const z0 = k * f;
        for (let j = 0; j < cny; j++) {
          const y0 = j * f;
          for (let i = 0; i < cnx; i++) {
            const x0 = i * f;
            let sum = 0, cnt = 0;
            const z1 = Math.min(z0 + f, nz), y1 = Math.min(y0 + f, ny), x1 = Math.min(x0 + f, nx);
            for (let z = z0; z < z1; z++) for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) { sum += src[(z * ny + y) * nx + x]; cnt++; }
            out[(k * cny + j) * cnx + i] = cnt ? (sum / cnt) : 0;
          }
        }
      }
      volumePane.volImageData.setDimensions(cnx, cny, cnz);
      volumePane.volImageData.setSpacing(state.spacing[0] * f, state.spacing[1] * f, state.spacing[2] * f);
      volumePane.volImageData.setOrigin(0, 0, 0);
      volumePane.volImageData.getPointData().setScalars(vtkDataArray.newInstance({
        name: 'HU', values: out, numberOfComponents: 1,
      }));
    }
    volumePane.volImageData.modified();
    volumePane.volMapper.setInputData(volumePane.volImageData);
    volumePane.volume.setVisibility(true);
  }

  function orientVolumeCamera() {
    if (!volumePane) return;
    volumePane.renderer.resetCamera();
    const cam = volumePane.renderer.getActiveCamera();
    cam.azimuth(30);
    cam.elevation(20);
    volumePane.renderer.resetCameraClippingRange();
  }

  // Parallel-camera scale that fits the whole image in the pane (no cropping),
  // as large as possible, using the pane's current aspect ratio.
  function fitScaleFor(pane) {
    const v = pane.view;
    const W = state.dims[v.i] * state.spacing[v.i];
    const H = state.dims[v.j] * state.spacing[v.j];
    const rect = pane.canvasHost.getBoundingClientRect();
    const aspect = (rect.width > 1 && rect.height > 1) ? rect.width / rect.height : 1;
    return Math.max(W / (2 * aspect), H / 2);
  }

  function orientCamera(pane) {
    const v = pane.view;
    const W = state.dims[v.i] * state.spacing[v.i];
    const H = state.dims[v.j] * state.spacing[v.j];
    const cam = pane.renderer.getActiveCamera();
    cam.setParallelProjection(true);
    cam.setFocalPoint(W / 2, H / 2, 0);
    cam.setPosition(W / 2, H / 2, 1000);
    cam.setViewUp(0, 1, 0);
    cam.setParallelScale(fitScaleFor(pane));
    cam.setClippingRange(1, 4000);
  }

  function sampleTrilinear(px, py, pz) {
    const [nx, ny, nz] = state.dims;
    const src = state.data;
    if (px < 0 || py < 0 || pz < 0 || px > nx - 1 || py > ny - 1 || pz > nz - 1) return -1024;  // air
    const x0 = Math.floor(px), y0 = Math.floor(py), z0 = Math.floor(pz);
    const x1 = Math.min(nx - 1, x0 + 1), y1 = Math.min(ny - 1, y0 + 1), z1 = Math.min(nz - 1, z0 + 1);
    const fx = px - x0, fy = py - y0, fz = pz - z0;
    const v000 = src[(z0 * ny + y0) * nx + x0], v100 = src[(z0 * ny + y0) * nx + x1];
    const v010 = src[(z0 * ny + y1) * nx + x0], v110 = src[(z0 * ny + y1) * nx + x1];
    const v001 = src[(z1 * ny + y0) * nx + x0], v101 = src[(z1 * ny + y0) * nx + x1];
    const v011 = src[(z1 * ny + y1) * nx + x0], v111 = src[(z1 * ny + y1) * nx + x1];
    const c00 = v000 + (v100 - v000) * fx;
    const c10 = v010 + (v110 - v010) * fx;
    const c01 = v001 + (v101 - v001) * fx;
    const c11 = v011 + (v111 - v011) * fx;
    const c0 = c00 + (c10 - c00) * fy;
    const c1 = c01 + (c11 - c01) * fy;
    return c0 + (c1 - c0) * fz;
  }

  // Oblique axial resampling: rotate the axial plane by azimuth (about Y) and
  // elevation (about X), then trilinear-sample the volume onto the output grid.
  function extractObliqueAxial(index) {
    const [nx, ny] = state.dims;
    const dx = state.spacing[0], dy = state.spacing[1], dz = state.spacing[2];
    const ni = nx, nj = ny;
    const out = state.floatData ? new Float32Array(ni * nj) : new Int16Array(ni * nj);
    const az = state.oblique.azimuthDeg * Math.PI / 180;
    const el = state.oblique.elevationDeg * Math.PI / 180;
    const cosA = Math.cos(az), sinA = Math.sin(az);
    const cosE = Math.cos(el), sinE = Math.sin(el);
    // Rotated in-plane axes (unit vectors): u = right, v = up.
    const ux = cosA, uy = 0, uz = -sinA;
    const vx = sinA * sinE, vy = cosE, vz = cosA * sinE;
    const cx = (nx / 2) * dx, cy = (ny / 2) * dy, cz = index * dz;
    for (let j = 0; j < nj; j++) {
      const dyv = (j - nj / 2) * dy;
      for (let i = 0; i < ni; i++) {
        const dxu = (i - ni / 2) * dx;
        const wx = cx + dxu * ux + dyv * vx;
        const wy = cy + dxu * uy + dyv * vy;
        const wz = cz + dxu * uz + dyv * vz;
        const s = sampleTrilinear(wx / dx, wy / dy, wz / dz);
        out[j * ni + i] = state.floatData ? s : Math.round(s);
      }
    }
    return out;
  }

  function extractSlice(view, index) {
    const ni = state.dims[view.i];
    const nj = state.dims[view.j];
    let out;
    if (view.k === 2 && (state.oblique.azimuthDeg !== 0 || state.oblique.elevationDeg !== 0)) {
      out = extractObliqueAxial(index);
    } else {
      const [nx, ny] = state.dims;
      const nk = state.dims[view.k];
      out = state.floatData ? new Float32Array(ni * nj) : new Int16Array(ni * nj);
      const src = state.data;
      const xyz = [0, 0, 0];
      // Slab (MIP): composite `nSlices` around `index` along the slice normal.
      let k0 = index;
      let k1 = index;
      const sk = state.spacing[view.k];
      if (state.slabMm > 0 && sk > 0) {
        const nSlices = Math.max(1, Math.round(state.slabMm / sk));
        k0 = Math.max(0, index - Math.floor(nSlices / 2));
        k1 = Math.min(nk - 1, index + Math.floor((nSlices - 1) / 2));
      }
      const mode = (state.projectionMode === 'min') ? 'min' : (state.projectionMode === 'mean') ? 'mean' : 'max';
      for (let j = 0; j < nj; j++) {
        xyz[view.j] = j;
        for (let i = 0; i < ni; i++) {
          xyz[view.i] = i;
          let acc = (mode === 'min') ? Infinity : (mode === 'mean') ? 0 : (state.floatData ? -Infinity : -32768);
          let cnt = 0;
          for (let k = k0; k <= k1; k++) {
            xyz[view.k] = k;
            const v = src[(xyz[2] * ny + xyz[1]) * nx + xyz[0]];
            if (mode === 'min') { if (v < acc) acc = v; }
            else if (mode === 'mean') { acc += v; cnt++; }
            else { if (v > acc) acc = v; }
          }
          out[j * ni + i] = (mode === 'mean' && cnt) ? (state.floatData ? (acc / cnt) : Math.round(acc / cnt)) : acc;
        }
      }
    }
    if (view.flipH || view.flipV) applyMirror(out, ni, nj, view.flipH, view.flipV);
    return out;
  }

  // Mirror a row-major 2D slice in place (flipH: columns, flipV: rows).
  function applyMirror(arr, ni, nj, flipH, flipV) {
    if (flipH) {
      for (let j = 0; j < nj; j++) {
        const rowOff = j * ni;
        for (let i = 0; i < ni >> 1; i++) {
          const a = arr[rowOff + i];
          arr[rowOff + i] = arr[rowOff + ni - 1 - i];
          arr[rowOff + ni - 1 - i] = a;
        }
      }
    }
    if (flipV) {
      for (let i = 0; i < ni; i++) {
        for (let j = 0; j < nj >> 1; j++) {
          const a = arr[j * ni + i];
          arr[j * ni + i] = arr[(nj - 1 - j) * ni + i];
          arr[(nj - 1 - j) * ni + i] = a;
        }
      }
    }
  }

  function setOvBL(pane) {
    if (!state.loaded) return;
    const v = pane.view;
    const cam = pane.renderer.getActiveCamera();
    const zoom = fitScaleFor(pane) / cam.getParallelScale() * 100;
    let t = 'Frame: ' + (pane.index + 1) + ' / ' + state.dims[v.k] +
        '\nZoom: ' + zoom.toFixed(1) + '%' +
        '\nW/L: ' + Math.round(state.window) + ' / ' + Math.round(state.level);
    if (pane.huText) t += '\n' + pane.huText;
    const m = state.overlayMeta || {};
    const mod = (m.modality || 'CT').trim();
    const plane = pane.key === 'axial' ? 'AXIAL' : pane.key === 'coronal' ? 'CORONAL' : 'SAGITTAL';
    t += '\n' + mod + ' (' + state.dims[v.i] + 'x' + state.dims[v.j] + ') - ' + plane;
    pane.ovBL.textContent = t;
  }

  function updateOverlayStatic(pane) {
    const m = state.overlayMeta || {};
    let tl = '';
    if (m.patientName) tl += (m.patientName || '').trim();
    if (m.patientId) tl += (tl ? '\n' : '') + (m.patientId || '').trim();
    pane.ovTL.textContent = tl;
    let tr = '';
    if (m.institutionName) tr += (m.institutionName || '').trim();
    if (m.studyId) tr += (tr ? '\n' : '') + 'Study: ' + (m.studyId || '').trim();
    if (m.acquisitionDate || m.acquisitionTime) tr += (tr ? '\n' : '') + (m.acquisitionDate || '').trim() + ' ' + (m.acquisitionTime || '').trim();
    pane.ovTR.textContent = tr;
  }

  function setOverlayInfo(meta) {
    state.overlayMeta = meta || {};
    for (const key of Object.keys(VIEWS)) updateOverlayStatic(panes[key]);
  }

  function worldToScreen(pane, wx, wy) {
    const rect = pane.canvasHost.getBoundingClientRect();
    const cam = pane.renderer.getActiveCamera();
    const fp = cam.getFocalPoint();
    const visibleH = 2 * cam.getParallelScale();
    const visibleW = visibleH * (rect.width / rect.height);
    const sx = ((wx - fp[0]) / visibleW + 0.5) * rect.width;
    const sy = (0.5 - (wy - fp[1]) / visibleH) * rect.height;
    return [sx, sy];
  }

  function syncLens() {
    for (const key of Object.keys(VIEWS)) {
      const p = panes[key];
      const c = p.magCanvas;
      if (p.lensBtn) {
        p.lensBtn.style.color = p.lensOn ? '#ffe066' : '#888';
        p.lensBtn.style.borderColor = p.lensOn ? '#ffe066' : '#444';
      }
      if (!c) continue;
      if (p.lensOn) {
        c.style.pointerEvents = 'auto';
        drawLens(p);
      } else {
        c.style.pointerEvents = 'none';
        c.style.display = 'none';
      }
    }
  }

  function lensScreenToWorld(key, clientX, clientY) {
    const p = panes[key];
    if (!p || !p.lensOn) return null;
    const L = state.lens;
    const rect = p.canvasHost.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return null;
    const half = L.size / 2;
    const lx = clientX - rect.left - L.fx * rect.width;
    const ly = clientY - rect.top - L.fy * rect.height;
    if (Math.abs(lx) > half || Math.abs(ly) > half) return null;
    const cam = p.renderer.getActiveCamera();
    const fp = cam.getFocalPoint();
    const visibleH = 2 * cam.getParallelScale();
    const visibleW = visibleH * (rect.width / rect.height);
    const mmPerLensPx = (visibleW / rect.width) / (L.zoom || 2);
    const wx0 = (L.fx - 0.5) * visibleW + fp[0];
    const wy0 = (0.5 - L.fy) * visibleH + fp[1];
    return { x: wx0 + lx * mmPerLensPx, y: wy0 - ly * mmPerLensPx };
  }

  function drawLens(pane) {
    const canvas = pane.magCanvas;
    if (!canvas) return;
    const rect = pane.canvasHost.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return;
    const L = state.lens;
    const v = pane.view;
    const cam = pane.renderer.getActiveCamera();
    const fp = cam.getFocalPoint();
    const visibleH = 2 * cam.getParallelScale();
    const visibleW = visibleH * (rect.width / rect.height);
    const wx = (L.fx - 0.5) * visibleW + fp[0];
    const wy = (0.5 - L.fy) * visibleH + fp[1];
    const ni = state.dims[v.i], nj = state.dims[v.j];
    const data = pane.sliceData;
    if (!data || data.length !== ni * nj) { canvas.style.display = 'none'; return; }
    const size = Math.round(L.size);
    if (canvas.width !== size) {
      canvas.width = size; canvas.height = size;
      canvas.style.width = size + 'px'; canvas.style.height = size + 'px';
    }
    const zoom = L.zoom || 2;
    const di = state.spacing[v.i];
    const dj = state.spacing[v.j];
    // Voxel under the lens centre. The slice data is already mirrored to match
    // the displayed image (extractSlice applies flipH/flipV), so world coords
    // map to data indices directly — do NOT mirror again.
    const cx = wx / di;
    const cy = wy / dj;
    // Zoom is relative to the current display scale, so 2x means the same
    // physical magnification on every projection regardless of pane size.
    const mmPerPx = visibleW / rect.width;
    const stepX = (mmPerPx / zoom) / di;
    const stepY = (mmPerPx / zoom) / dj;
    const lo = state.level - state.window / 2;
    const win = Math.abs(state.window) || 1;
    const ctx = canvas.getContext('2d');
    const img = ctx.createImageData(size, size);
    const half = size / 2;
    for (let yy = 0; yy < size; yy++) {
      // Minus sign: canvas y is top-down while vtk's j axis is bottom-up.
      const iy = Math.round(cy - (yy - half) * stepY);
      const iyOk = iy >= 0 && iy < nj;
      for (let xx = 0; xx < size; xx++) {
        const ix = Math.round(cx + (xx - half) * stepX);
        const idx = (yy * size + xx) * 4;
        let val = 0;
        if (iyOk && ix >= 0 && ix < ni) val = data[iy * ni + ix];
        let g = (val - lo) / win * 255;
        if (state.invert) g = 255 - g;
        g = Math.max(0, Math.min(255, g));
        img.data[idx] = img.data[idx + 1] = img.data[idx + 2] = g;
        img.data[idx + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    // Square border + bottom-right resize handle.
    ctx.strokeStyle = '#ffe066';
    ctx.lineWidth = 2;
    ctx.strokeRect(1, 1, size - 2, size - 2);
    const h = 14;
    ctx.fillStyle = '#ffe066';
    ctx.beginPath();
    ctx.moveTo(size - h, size - 1);
    ctx.lineTo(size - 1, size - 1);
    ctx.lineTo(size - 1, size - h);
    ctx.closePath();
    ctx.fill();
    // Draw all measurements (committed + in-progress) with their true shapes
    // inside the lens, so rectangle/ellipse/polygon outlines are visible.
    const mmPerLensPx = mmPerPx / zoom;
    const toLens = (wxv, wyv) => [half + (wxv - wx) / mmPerLensPx, half - (wyv - wy) / mmPerLensPx];
    const drawM = (t, pts, color) => {
      if (!pts || pts.length === 0) return;
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      const seg = (a, b) => { const [x1, y1] = toLens(a.x, a.y); const [x2, y2] = toLens(b.x, b.y); ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); };
      if (t === 'rectangle' && pts.length >= 2) {
        const x0 = Math.min(pts[0].x, pts[1].x), x1 = Math.max(pts[0].x, pts[1].x);
        const y0 = Math.min(pts[0].y, pts[1].y), y1 = Math.max(pts[0].y, pts[1].y);
        seg({x:x0,y:y0},{x:x1,y:y0}); seg({x:x1,y:y0},{x:x1,y:y1}); seg({x:x1,y:y1},{x:x0,y:y1}); seg({x:x0,y:y1},{x:x0,y:y0});
      } else if (t === 'ellipse' && pts.length >= 2) {
        const cx = (pts[0].x + pts[1].x) / 2, cy = (pts[0].y + pts[1].y) / 2;
        const ra = Math.abs(pts[0].x - pts[1].x) / 2, rb = Math.abs(pts[0].y - pts[1].y) / 2;
        let prev = null, first = null;
        for (let i = 0; i <= 48; i++) { const th = i / 48 * Math.PI * 2; const q = { x: cx + ra * Math.cos(th), y: cy + rb * Math.sin(th) }; if (prev) seg(prev, q); else first = q; prev = q; }
        seg(prev, first);
      } else {
        for (let i = 0; i + 1 < pts.length; i++) seg(pts[i], pts[i + 1]);
        if ((t === 'polygon' || t === 'freehand') && pts.length >= 3) seg(pts[pts.length - 1], pts[0]);
      }
      for (const p of pts) { const [x, y] = toLens(p.x, p.y); ctx.beginPath(); ctx.moveTo(x - 3, y); ctx.lineTo(x + 3, y); ctx.stroke(); ctx.beginPath(); ctx.moveTo(x, y - 3); ctx.lineTo(x, y + 3); ctx.stroke(); }
    };
    for (const m of state.measurements) {
      if (m.pane !== pane.key || m.slice !== pane.index || m.visible === false) continue;
      drawM(m.type, m.points, m.name ? (m.color || '#ff66ff') : '#ffe066');
    }
    const curPts = (state.measureSlice == null || state.measureSlice === pane.index) ? (state.measurePoints[pane.key] || []) : [];
    if (curPts.length > 0) drawM(state.measureType, curPts, '#00e5ff');
    canvas.style.left = (L.fx * rect.width - half) + 'px';
    canvas.style.top = (L.fy * rect.height - half) + 'px';
    canvas.style.display = 'block';
  }

  function drawHScaleBar(canvas, niceMm, px) {
    const W = Math.max(20, Math.ceil(px) + 6);
    const H = 22;
    canvas.width = W; canvas.height = H;
    canvas.style.width = W + 'px'; canvas.style.height = H + 'px';
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#ffe066';
    ctx.strokeStyle = '#ffe066';
    ctx.lineWidth = 1.5;
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText((niceMm >= 10) ? (niceMm / 10 + ' cm') : (niceMm.toFixed(0) + ' mm'), W / 2, 10);
    const y = 18;
    ctx.beginPath(); ctx.moveTo(3, y); ctx.lineTo(3 + px, y); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(3, y - 3); ctx.lineTo(3, y + 3); ctx.moveTo(3 + px, y - 3); ctx.lineTo(3 + px, y + 3); ctx.stroke();
    for (let i = 1; i < 5; i++) { const x = 3 + px * i / 5; ctx.beginPath(); ctx.moveTo(x, y - 2); ctx.lineTo(x, y + 2); ctx.stroke(); }
  }

  function drawVScaleBar(canvas, niceMm, px) {
    const W = 22;
    const H = Math.max(30, Math.ceil(px) + 19);
    canvas.width = W; canvas.height = H;
    canvas.style.width = W + 'px'; canvas.style.height = H + 'px';
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#ffe066';
    ctx.strokeStyle = '#ffe066';
    ctx.lineWidth = 1.5;
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText((niceMm >= 10) ? (niceMm / 10 + ' cm') : (niceMm.toFixed(0) + ' mm'), 3, 11);
    const x = W - 6;
    ctx.beginPath(); ctx.moveTo(x, 16); ctx.lineTo(x, 16 + px); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(x - 3, 16); ctx.lineTo(x + 3, 16); ctx.moveTo(x - 3, 16 + px); ctx.lineTo(x + 3, 16 + px); ctx.stroke();
    for (let i = 1; i < 5; i++) { const yy = 16 + px * i / 5; ctx.beginPath(); ctx.moveTo(x - 2, yy); ctx.lineTo(x + 2, yy); ctx.stroke(); }
  }

  function updateRulers(pane) {
    if (!state.loaded) {
      pane.rulerHActor.setVisibility(false);
      pane.rulerVActor.setVisibility(false);
      const ch = pane.rulerHCanvas.getContext('2d');
      ch.clearRect(0, 0, pane.rulerHCanvas.width, pane.rulerHCanvas.height);
      const cv = pane.rulerVCanvas.getContext('2d');
      cv.clearRect(0, 0, pane.rulerVCanvas.width, pane.rulerVCanvas.height);
      return;
    }
    pane.rulerHActor.setVisibility(false);
    pane.rulerVActor.setVisibility(false);
    const rect = pane.canvasHost.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return;
    const mmPerPx = (2 * pane.renderer.getActiveCamera().getParallelScale()) / rect.height;
    if (!(mmPerPx > 0)) return;
    // Nice physical length (1/2/5 × 10^k mm) for a ~110 px bar.
    const niceLen = (availPx) => {
      const t = mmPerPx * availPx;
      const pow = Math.pow(10, Math.floor(Math.log10(t)));
      let n = pow;
      if (t / pow >= 5) n = 5 * pow;
      else if (t / pow >= 2) n = 2 * pow;
      return { nice: n, px: n / mmPerPx };
    };

    // Top scale: strictly centered, below the letter "A".
    const h = niceLen(120);
    pane.rulerHCanvas.style.left = '50%';
    pane.rulerHCanvas.style.top = '22px';
    pane.rulerHCanvas.style.bottom = 'auto';
    pane.rulerHCanvas.style.transform = 'translateX(-50%)';
    drawHScaleBar(pane.rulerHCanvas, h.nice, h.px);

    // Left scale: vertically centered, normal readable size.
    const availH = Math.max(80, rect.height * 0.35);
    const v = niceLen(availH);
    pane.rulerVCanvas.style.left = '6px';
    pane.rulerVCanvas.style.top = '50%';
    pane.rulerVCanvas.style.transform = 'translateY(-50%)';
    drawVScaleBar(pane.rulerVCanvas, v.nice, v.px);
  }

  function refreshPaneInfo(pane) {
    if (!state.loaded) return;
    const v = pane.view;
    const loc = state.origin[v.k] + pane.index * state.spacing[v.k];
    const m = state.overlayMeta || {};
    setOvBL(pane);
    let br = '';
    if (m.seriesNumber) br += 'Series: ' + (m.seriesNumber || '').trim();
    br += (br ? '\n' : '') + 'Thickness: ' + state.spacing[v.k].toFixed(2) + ' mm';
    br += '\nLocation: ' + loc.toFixed(2) + ' mm';
    pane.ovBR.textContent = br;
    // The header strip keeps only a short frame indicator.
    pane.info.textContent = (pane.index + 1) + '/' + state.dims[v.k];
  }

  function updatePane(pane) {
    const v = pane.view;
    const ni = state.dims[v.i];
    const nj = state.dims[v.j];
    const slice = extractSlice(v, pane.index);
    pane.sliceData = slice;  // Int16Array, kept for HU-under-cursor readout
    pane.imageData.setDimensions(ni, nj, 1);
    pane.imageData.setSpacing(state.spacing[v.i], state.spacing[v.j], 1);
    pane.imageData.setOrigin(0, 0, 0);
    const arr = vtkDataArray.newInstance({
      name: 'HU', values: Float32Array.from(slice), numberOfComponents: 1,
    });
    pane.imageData.getPointData().setScalars(arr);
    pane.imageData.modified();
    refreshPaneInfo(pane);
  }

  // World position of the crosshair intersection in pane `key` (mirrored to
  // match the displayed slice image).
  function crosshairWorldPos(key) {
    const p = panes[key];
    const v = p.view;
    const ni = state.dims[v.i];
    const nj = state.dims[v.j];
    const si = state.spacing[v.i];
    const sj = state.spacing[v.j];
    let vertIdx = 0, horizIdx = 0;
    for (const k2 of Object.keys(VIEWS)) {
      if (VIEWS[k2].k === v.i) vertIdx = panes[k2].index;
      if (VIEWS[k2].k === v.j) horizIdx = panes[k2].index;
    }
    let x = (vertIdx + 0.5) * si;
    let y = (horizIdx + 0.5) * sj;
    if (v.flipH) x = ni * si - x;
    if (v.flipV) y = nj * sj - y;
    return { x, y };
  }

  // Map a dragged world position in pane `key` back to the two slice indices
  // (the views whose slice normal is this pane's i / j axis).
  function crosshairDragToSlices(key, wx, wy) {
    const p = panes[key];
    const v = p.view;
    const ni = state.dims[v.i];
    const nj = state.dims[v.j];
    const si = state.spacing[v.i];
    const sj = state.spacing[v.j];
    let x = wx, y = wy;
    if (v.flipH) x = ni * si - x;
    if (v.flipV) y = nj * sj - y;
    const vertIdx = Math.max(0, Math.min(ni - 1, Math.round(x / si - 0.5)));
    const horizIdx = Math.max(0, Math.min(nj - 1, Math.round(y / sj - 0.5)));
    const out = {};
    for (const k2 of Object.keys(VIEWS)) {
      if (VIEWS[k2].k === v.i) out[k2] = vertIdx;
      if (VIEWS[k2].k === v.j) out[k2] = horizIdx;
    }
    return out;
  }

  function isNearCrosshair(key, e) {
    const p = panes[key];
    const { x, y } = crosshairWorldPos(key);
    const [sx, sy] = worldToScreen(p, x, y);
    const rect = p.canvasHost.getBoundingClientRect();
    return Math.hypot((e.clientX - rect.left) - sx, (e.clientY - rect.top) - sy) < 14;
  }

  function updateCrosshairs() {
    for (const key of Object.keys(VIEWS)) {
      const p = panes[key];
      const v = p.view;
      const ni = state.dims[v.i];
      const nj = state.dims[v.j];
      const si = state.spacing[v.i];
      const sj = state.spacing[v.j];
      let vertIdx = 0;
      let horizIdx = 0;
      for (const k2 of Object.keys(VIEWS)) {
        if (VIEWS[k2].k === v.i) vertIdx = panes[k2].index;
        if (VIEWS[k2].k === v.j) horizIdx = panes[k2].index;
      }
      let x = (vertIdx + 0.5) * si;
      let y = (horizIdx + 0.5) * sj;
      // The slice image may be mirrored (radiology convention), so the crosshair
      // must be mirrored the same way — otherwise it lands on mirrored anatomy
      // instead of the true slice position.
      if (v.flipH) x = ni * si - x;
      if (v.flipV) y = nj * sj - y;
      // Offset the crosshair lines slightly toward the camera (+Z) so they never
      // z-fight with the coplanar slice image.
      const z = 0.5;
      p.vLine.setPoint1(x, 0, z);
      p.vLine.setPoint2(x, nj * sj, z);
      p.hLine.setPoint1(0, y, z);
      p.hLine.setPoint2(ni * si, y, z);
    }
  }

  function setSlice(key, index) {
    if (!state.loaded) return;
    const p = panes[key];
    const max = state.dims[p.view.k] - 1;
    if (max < 0) return;
    const clamped = Math.max(0, Math.min(index, max));
    if (clamped === p.index) return;
    p.index = clamped;
    updatePane(p);
    updateCrosshairs();
    updateMeasureOverlay(key);
    updateRegionOverlay(key);
    updateContourOverlay(key);
    updateSegOverlay(key);
    updateVolumeOverlay(key);
    updateDoseOverlay(key);
    renderAll();
    if (hooks.onSliceChange) hooks.onSliceChange({ view: key, index: clamped });
  }

  function load(meta, data) {
    state.dims = [meta.nx, meta.ny, meta.nz];
    state.spacing = [meta.dx || 1, meta.dy || 1, meta.dz || 1];
    state.origin = (meta.origin && meta.origin.length >= 3) ? meta.origin.slice(0, 3) : [0, 0, 0];
    state.direction = (meta.direction && meta.direction.length >= 9) ? meta.direction.slice(0, 9) : [1, 0, 0, 0, 1, 0, 0, 0, 1];
    state.data = data;
    state.floatData = (meta.valueType === 'float32') || (data instanceof Float32Array);
    state.loaded = true;
    container.classList.add('mpr-loaded');
    // A new volume invalidates any previous measurements / seeds / region.
    clearMeasurements();
    state.growSeeds = [];
    for (const id of Object.keys(volumeActors)) removeVolumeEntry(id);
    state.volumes = [];
    for (const k of Object.keys(VIEWS)) {
      seedActors[k].actor.setVisibility(false);
    }
    // A new volume invalidates any isodose overlay (levels belong to the old
    // volume's value range); updateDoseOverlay() hides the actors when levels
    // are null.
    state.isodoseLevels = null;
    state.isodoseFractions = null;
    state.doseLoaded = false;
    for (const entry of beamActors) {
      try { if (volumePane) volumePane.renderer.removeActor(entry.actor); } catch (e) { /* ignore */ }
    }
    beamActors.length = 0;
    state.roiMasks = {};
    for (const num of Object.keys(roiActors)) {
      for (const key of Object.keys(VIEWS)) {
        try { roiActors[num].panes[key].actor.setVisibility(false); } catch (e) { /* ignore */ }
        try { roiActors[num].panes[key].fillActor.setVisibility(false); } catch (e) { /* ignore */ }
      }
    }
    if (volumePane) {
      buildVolumeImageData();
      updateVolumeColorTF();
      orientVolumeCamera();
    }
    const center = {
      axial: Math.floor(meta.nz / 2),
      coronal: Math.floor(meta.ny / 2),
      sagittal: Math.floor(meta.nx / 2),
    };
    for (const key of Object.keys(VIEWS)) orientCamera(panes[key]);
    for (const key of Object.keys(VIEWS)) {
      panes[key].index = -1;
      setSlice(key, center[key]);
    }
    for (const key of Object.keys(VIEWS)) updateRulers(panes[key]);
    renderAll();
  }

  // Fully clear the viewer: volume + all RT overlays, ready for a new dataset.
  function reset() {
    setRois([]);
    setSegmentation([]);
    setDoseGrid(null);
    setBeams([]);
    clearMeasurements();
    state.growSeeds = [];
    state.seedMode = false;
    for (const id of Object.keys(volumeActors)) removeVolumeEntry(id);
    state.volumes = [];
    for (const key of Object.keys(VIEWS)) {
      seedActors[key].actor.setVisibility(false);
    }
    state.loaded = false;
    for (const key of Object.keys(VIEWS)) updateRulers(panes[key]);
    container.classList.remove('mpr-loaded');
    state.data = null;
    state.dims = [0, 0, 0];
    state.spacing = [1, 1, 1];
    state.origin = [0, 0, 0];
    state.floatData = false;
    state.roiMasks = {};
    state.isodoseLevels = null;
    state.isodoseFractions = null;
    state.doseLoaded = false;
    state.doseMaxGy = 0;
    state.overlayMeta = {};
    state.window = DEFAULT_WINDOW;
    state.level = DEFAULT_LEVEL;
    sharedProperty.setColorWindow(state.window);
    sharedProperty.setColorLevel(state.level);
    for (const key of Object.keys(VIEWS)) {
      const p = panes[key];
      p.sliceData = null;
      p.index = -1;
      p.imageData.setDimensions(1, 1, 1);
      p.imageData.setSpacing(1, 1, 1);
      p.imageData.setOrigin(0, 0, 0);
      p.imageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'HU', values: new Float32Array([0]), numberOfComponents: 1 }));
      p.imageData.modified();
      p.info.textContent = '—';
      p.ovTL.textContent = '';
      p.ovTR.textContent = '';
      p.ovBL.textContent = '';
      p.ovBR.textContent = '';
      p.huText = '';
    }
    if (volumePane) {
      volumePane.volImageData.setDimensions(1, 1, 1);
      volumePane.volImageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'HU', values: new Float32Array([0]), numberOfComponents: 1 }));
      volumePane.volImageData.modified();
      volumePane.volume.setVisibility(false);
    }
    renderAll();
  }

  function setWindowLevel(w, l) {
    state.window = w;
    state.level = l;
    sharedProperty.setColorWindow(state.invert ? -Math.abs(w) : Math.abs(w));
    sharedProperty.setColorLevel(l);
    updateVolumeColorTF();
    for (const key of Object.keys(VIEWS)) refreshPaneInfo(panes[key]);
    renderAll();
    if (hooks.onWindowLevel) hooks.onWindowLevel({ window: w, level: l });
  }

  function resetWindowLevel() {
    if (!state.loaded) return;
    if (looksLikeHU()) setWindowLevel(DEFAULT_WINDOW, DEFAULT_LEVEL);
    else autoWindowLevel();
  }

  // Read the W/L that vtkInteractorStyleImage set during a left-drag and mirror
  // it into our state + the 3D volume so the overlay and volume stay in sync.
  function syncWindowLevelFromProperty() {
    const w = sharedProperty.getColorWindow();
    const l = sharedProperty.getColorLevel();
    if (!Number.isFinite(w) || !Number.isFinite(l)) return;
    if (Math.abs(w - state.window) < 1e-3 && Math.abs(l - state.level) < 1e-3) return;
    state.window = w;
    state.level = l;
    updateVolumeColorTF();
    for (const key of Object.keys(VIEWS)) refreshPaneInfo(panes[key]);
    renderAll();
    if (hooks.onWindowLevel) hooks.onWindowLevel({ window: w, level: l });
  }

  // Auto window/level from the 2%–98% intensity percentiles (for MR/PET/any
  // non-CT modality where there are no fixed HU presets).
  function autoWindowLevel() {
    if (!state.loaded || !state.data) return;
    const n = state.data.length;
    if (n === 0) return;
    // Sample (cap to keep it fast for large volumes).
    const step = Math.max(1, Math.floor(n / 200000));
    const vals = [];
    for (let i = 0; i < n; i += step) vals.push(state.data[i]);
    vals.sort((a, b) => a - b);
    const lo = vals[Math.floor(vals.length * 0.02)];
    const hi = vals[Math.floor(vals.length * 0.98)];
    const level = (lo + hi) / 2;
    const window = Math.max(1e-3, hi - lo);
    setWindowLevel(window, level);
  }

  function setCrosshairsVisible(visible) {
    state.crosshairsVisible = !!visible;
    for (const key of Object.keys(VIEWS)) {
      panes[key].vActor.setVisibility(visible);
      panes[key].hActor.setVisibility(visible);
    }
    renderAll();
  }

  function setSlabThickness(mm) {
    state.slabMm = Math.max(0, mm || 0);
    if (hooks.onSlabChange) hooks.onSlabChange(state.slabMm);
    if (!state.loaded) return;
    for (const key of Object.keys(VIEWS)) updatePane(panes[key]);
    updateCrosshairs();
    renderAll();
  }

  function setProjectionMode(mode) {
    state.projectionMode = (mode === 'min' || mode === 'mean') ? mode : 'max';
    if (state.slabMm === 0) {
      state.slabMm = 20;   // projection needs a slab; auto-enable a default thickness
      if (hooks.onSlabChange) hooks.onSlabChange(state.slabMm);
    }
    console.log('[mpr] projection mode=', state.projectionMode, 'slab=', state.slabMm);
    if (!state.loaded) return;
    for (const key of Object.keys(VIEWS)) updatePane(panes[key]);
    renderAll();
  }

  function setZoom(factor) {
    if (!state.loaded) return;
    console.log('[mpr] zoom factor=', factor);
    for (const key of Object.keys(VIEWS)) {
      const cam = panes[key].renderer.getActiveCamera();
      cam.setParallelScale(Math.max(0.01, Math.min(100000, cam.getParallelScale() * factor)));
    }
    renderAll();
    refreshOverlayLabels();
  }

  function setOblique(azimuthDeg, elevationDeg) {
    state.oblique.azimuthDeg = azimuthDeg || 0;
    state.oblique.elevationDeg = elevationDeg || 0;
    if (!state.loaded) return;
    updatePane(panes.axial);
    updateCrosshairs();
    renderAll();
  }

  // ── RTSTRUCT contour overlay (rasterized to 3D masks, drawn on all panes) ──
  const ROI_COLORS = [
    [1.0, 0.35, 0.35], [0.35, 1.0, 0.35], [0.4, 0.6, 1.0], [1.0, 1.0, 0.3],
    [1.0, 0.4, 1.0], [0.3, 1.0, 1.0], [1.0, 0.6, 0.2], [0.7, 0.4, 1.0],
  ];
  const roiActors = {};  // roiNumber -> { panes: {key: {actor, polyData}}, visible }

  // Even-odd scanline fill of a closed 2D polygon into `mask2d` (nx x ny).
  // `poly` is an array of [ix, iy] voxel coordinates (floats).
  function rasterizePolygon2D(mask2d, nx, ny, poly) {
    const n = poly.length;
    if (n < 3) return;
    let minY = Infinity, maxY = -Infinity;
    for (const p of poly) { if (p[1] < minY) minY = p[1]; if (p[1] > maxY) maxY = p[1]; }
    const y0 = Math.max(0, Math.floor(minY));
    const y1 = Math.min(ny - 1, Math.ceil(maxY));
    for (let y = y0; y <= y1; y++) {
      const xs = [];
      for (let i = 0, j = n - 1; i < n; j = i++) {
        const yi = poly[i][1], yj = poly[j][1];
        if ((yi <= y + 0.5 && yj > y + 0.5) || (yj <= y + 0.5 && yi > y + 0.5)) {
          const xi = poly[i][0], xj = poly[j][0];
          const t = (y + 0.5 - yi) / (yj - yi);
          xs.push(xi + t * (xj - xi));
        }
      }
      xs.sort((a, b) => a - b);
      for (let k = 0; k + 1 < xs.length; k += 2) {
        const xStart = Math.max(0, Math.ceil(xs[k]));
        const xEnd = Math.min(nx - 1, Math.floor(xs[k + 1]));
        for (let x = xStart; x <= xEnd; x++) mask2d[y * nx + x] = 1;
      }
    }
  }

  function rasterizeRoi(roi) {
    const nx = state.dims[0], ny = state.dims[1], nz = state.dims[2];
    const dx = state.spacing[0], dy = state.spacing[1], dz = state.spacing[2];
    const [ox, oy, oz] = state.origin;
    const mask = new Uint8Array(nx * ny * nz);
    if (!nx || !ny || !nz) return mask;
    for (const c of roi.contours) {
      if (!c || !c.x || c.x.length < 3) continue;
      const n = c.x.length;
      let avgZ = 0;
      for (let i = 0; i < n; i++) avgZ += c.z[i];
      avgZ /= n;
      const k = Math.round((avgZ - oz) / dz);
      if (k < 0 || k >= nz) continue;
      const poly = [];
      for (let i = 0; i < n; i++) poly.push([(c.x[i] - ox) / dx, (c.y[i] - oy) / dy]);
      const slice = mask.subarray(k * nx * ny, (k + 1) * nx * ny);
      rasterizePolygon2D(slice, nx, ny, poly);
    }
    return mask;
  }

  function setRois(rois) {
    // Remove old contour + fill actors (one per ROI per pane).
    for (const num of Object.keys(roiActors)) {
      for (const key of Object.keys(VIEWS)) {
        const e = roiActors[num].panes[key];
        try { panes[key].renderer.removeActor(e.actor); } catch (err) { /* ignore */ }
        try { panes[key].renderer.removeActor(e.fillActor); } catch (err) { /* ignore */ }
      }
      delete roiActors[num];
    }
    state.rois = rois || [];
    state.roiMasks = {};
    state.rois.forEach((roi, idx) => {
      if (state.loaded) state.roiMasks[roi.number] = rasterizeRoi(roi);
      const color = ROI_COLORS[idx % ROI_COLORS.length];
      const panesMap = {};
      for (const key of Object.keys(VIEWS)) {
        // Semi-transparent fill overlay (binary mask -> color at fill opacity).
        const fillImageData = vtkImageData.newInstance();
        fillImageData.setDimensions(1, 1, 1);
        fillImageData.setSpacing(1, 1, 1);
        fillImageData.setOrigin(0, 0, 0.3);
        fillImageData.getPointData().setScalars(vtkDataArray.newInstance({
          name: 'mask', values: new Float32Array([0]), numberOfComponents: 1,
        }));
        fillImageData.modified();
        const fillMapper = vtkImageMapper.newInstance();
        fillMapper.setInputData(fillImageData);
        const fillActor = vtkImageSlice.newInstance();
        fillActor.setMapper(fillMapper);
        const fillCtf = vtkColorTransferFunction.newInstance();
        fillCtf.addRGBPoint(0, 0, 0, 0);
        fillCtf.addRGBPoint(1, color[0], color[1], color[2]);
        const fillPwf = vtkPiecewiseFunction.newInstance();
        fillPwf.addPoint(0, 0.0);
        fillPwf.addPoint(1, 0.35);
        const fillProp = vtkImageProperty.newInstance();
        fillProp.setRGBTransferFunction(fillCtf);
        fillProp.setScalarOpacity(fillPwf);
        fillProp.setUseLookupTableScalarRange(true);
        fillProp.setInterpolationTypeToNearest();
        fillActor.setProperty(fillProp);
        panes[key].renderer.addActor(fillActor);

        // Contour line (drawn on top of the fill).
        const polyData = vtkPolyData.newInstance();
        const mapper = vtkMapper.newInstance();
        mapper.setInputData(polyData);
        const actor = vtkActor.newInstance();
        actor.setMapper(mapper);
        actor.getProperty().setColor(color[0], color[1], color[2]);
        actor.getProperty().setLineWidth(2);
        actor.getProperty().setLighting(false);
        panes[key].renderer.addActor(actor);

        panesMap[key] = { actor, polyData, fillActor, fillImageData, fillCtf, fillPwf };
      }
      roiActors[roi.number] = { panes: panesMap, visible: true, lineOpacity: 1, fillOpacity: 0.35 };
    });
    updateContourOverlay();
    renderAll();
  }

  function updateContourOverlay(keyArg) {
    if (!state.loaded) return;
    try {
      updateContourOverlayUnsafe(keyArg);
    } catch (e) {
      console.error('[mpr] contour overlay error', e);
    }
  }

  function updateContourOverlayUnsafe(keyArg) {
    const keys = keyArg ? [keyArg] : Object.keys(VIEWS);
    for (const roi of state.rois) {
      const entry = roiActors[roi.number];
      const mask = state.roiMasks[roi.number];
      if (!entry || !mask) continue;
      for (const key of keys) {
        const p = panes[key];
        const v = p.view;
        const ni = state.dims[v.i];
        const nj = state.dims[v.j];
        const di = state.spacing[v.i];
        const dj = state.spacing[v.j];
        const mask2d = extractSliceMask(mask, v, p.index, (m) => m);
        const paneEntry = entry.panes[key];
        const has = setContourLines(paneEntry.polyData, mask2d, ni, nj, di, dj, 0.5);
        paneEntry.actor.setVisibility(entry.visible && has);
        // Update the semi-transparent fill overlay.
        paneEntry.fillImageData.setDimensions(ni, nj, 1);
        paneEntry.fillImageData.setSpacing(di, dj, 1);
        paneEntry.fillImageData.setOrigin(0, 0, 0.3);
        paneEntry.fillImageData.getPointData().setScalars(vtkDataArray.newInstance({
          name: 'mask', values: Float32Array.from(mask2d), numberOfComponents: 1,
        }));
        paneEntry.fillImageData.modified();
        paneEntry.fillPwf.removeAllPoints();
        paneEntry.fillPwf.addPoint(0, 0.0);
        paneEntry.fillPwf.addPoint(1, entry.fillOpacity);
        paneEntry.fillPwf.modified();
        paneEntry.fillActor.setVisibility(entry.visible);
      }
    }
  }

  function setRoiVisible(number, visible) {
    const entry = roiActors[number];
    if (entry) {
      entry.visible = !!visible;
      updateContourOverlay();
      renderAll();
    }
  }

  function setRoiOpacity(number, opacity) {
    const entry = roiActors[number];
    if (entry) {
      entry.lineOpacity = Math.max(0, Math.min(1, opacity));
      entry.fillOpacity = Math.max(0, Math.min(1, opacity));
      for (const key of Object.keys(VIEWS)) {
        const e = entry.panes[key];
        e.actor.getProperty().setOpacity(entry.lineOpacity);
        e.fillPwf.removeAllPoints();
        e.fillPwf.addPoint(0, 0.0);
        e.fillPwf.addPoint(1, entry.fillOpacity);
        e.fillPwf.modified();
      }
      renderAll();
    }
  }

  // Build a 2D binary mask (row-major by the view's j axis) for one pane's
  // current slice from `src` (full-volume flat array), marking 1 where
  // predicate(value) is truthy. view = { k, i, j }.
  function extractSliceMask(src, view, kIndex, predicate) {
    const nx = state.dims[0], ny = state.dims[1];
    const ni = state.dims[view.i];
    const nj = state.dims[view.j];
    const out = new Uint8Array(ni * nj);
    const xyz = [0, 0, 0];
    xyz[view.k] = kIndex;
    for (let j = 0; j < nj; j++) {
      xyz[view.j] = j;
      const rowOff = j * ni;
      for (let i = 0; i < ni; i++) {
        xyz[view.i] = i;
        const val = src[(xyz[2] * ny + xyz[1]) * nx + xyz[0]];
        out[rowOff + i] = predicate(val) ? 1 : 0;
      }
    }
    if (view.flipH || view.flipV) applyMirror(out, ni, nj, view.flipH, view.flipV);
    return out;
  }

  // Render a marching-squares contour of a 2D binary mask into `polyData`.
  // (di, dj) are the in-plane voxel spacings, z the camera-side offset.
  // Returns true when there is at least one boundary segment.
  function setContourLines(polyData, mask2d, ni, nj, di, dj, z) {
    const segs = marchingSquares(mask2d, ni, nj);
    if (segs.length === 0) return false;
    const points = [];
    const cells = [];
    let off = 0;
    for (let k = 0; k < segs.length; k += 4) {
      points.push(segs[k] * di, segs[k + 1] * dj, z);
      points.push(segs[k + 2] * di, segs[k + 3] * dj, z);
      cells.push(2, off, off + 1);
      off += 2;
    }
    const pts = vtkPoints.newInstance();
    pts.setData(Float32Array.from(points), 3);
    polyData.setPoints(pts);
    const ca = vtkCellArray.newInstance();
    ca.setData(Uint32Array.from(cells));
    polyData.setLines(ca);
    polyData.modified();
    return true;
  }

  // ── Grow-from-seeds (3D region growing, 6-connected) ──
  // The user drops seed voxels, then the connected component whose intensity is
  // within ±tolerance of the seed range is extracted. Seeds and the grown-region
  // contour are drawn on every orthogonal pane, so the region is visible on the
  // axial, coronal and sagittal slices at once.
  const seedActors = {};   // pane key -> { polyData, actor }
  const volumeActors = {}; // volume id -> { panes: {key: {polyData, actor, fillActor, fillImageData, fillCtf}}, threeD: {actor, mc, imageData} }
  const VOLUME_COLORS = [[1, 0.45, 0.25], [0.25, 0.6, 1.0], [0.25, 1.0, 0.4], [1.0, 0.85, 0.2], [0.9, 0.3, 0.9], [0.2, 1.0, 0.9]];

  function ensureVolumeEntry(vol) {
    if (volumeActors[vol.id]) return volumeActors[vol.id];
    const rgb = vol.color || [1, 0.45, 0.25];
    const entry = { panes: {}, threeD: null };
    for (const key of Object.keys(VIEWS)) {
      const polyData = vtkPolyData.newInstance();
      const mapper = vtkMapper.newInstance();
      mapper.setInputData(polyData);
      const actor = vtkActor.newInstance();
      actor.setMapper(mapper);
      actor.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
      actor.getProperty().setLineWidth(2);
      actor.getProperty().setLighting(false);
      actor.setVisibility(false);
      panes[key].renderer.addActor(actor);

      const fillImageData = vtkImageData.newInstance();
      fillImageData.setDimensions(1, 1, 1);
      fillImageData.setSpacing(1, 1, 1);
      fillImageData.setOrigin(0, 0, 0.3);
      fillImageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'mask', values: new Float32Array([0]), numberOfComponents: 1 }));
      fillImageData.modified();
      const fillMapper = vtkImageMapper.newInstance();
      fillMapper.setInputData(fillImageData);
      const fillActor = vtkImageSlice.newInstance();
      fillActor.setMapper(fillMapper);
      const fillCtf = vtkColorTransferFunction.newInstance();
      fillCtf.addRGBPoint(0, 0, 0, 0);
      fillCtf.addRGBPoint(1, rgb[0], rgb[1], rgb[2]);
      const fillPwf = vtkPiecewiseFunction.newInstance();
      fillPwf.addPoint(0, 0.0);
      fillPwf.addPoint(1, 0.3);
      const fillProp = vtkImageProperty.newInstance();
      fillProp.setRGBTransferFunction(fillCtf);
      fillProp.setScalarOpacity(fillPwf);
      fillProp.setUseLookupTableScalarRange(true);
      fillProp.setInterpolationTypeToNearest();
      fillActor.setProperty(fillProp);
      fillActor.setVisibility(false);
      panes[key].renderer.addActor(fillActor);
      entry.panes[key] = { polyData, actor, fillActor, fillImageData, fillCtf };
    }
    if (volumePane) {
      const imageData = vtkImageData.newInstance();
      const mc = vtkImageMarchingCubes.newInstance({ contourValue: 0.5, computeNormals: true, mergePoints: true });
      mc.setInputData(imageData);
      const mapper = vtkMapper.newInstance();
      mapper.setInputConnection(mc.getOutputPort());
      const actor = vtkActor.newInstance();
      actor.setMapper(mapper);
      actor.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
      actor.getProperty().setOpacity(0.7);
      actor.getProperty().setLighting(true);
      actor.getProperty().setAmbient(0.2);
      actor.getProperty().setDiffuse(0.8);
      actor.getProperty().setSpecular(0.3);
      actor.getProperty().setSpecularPower(20);
      actor.setVisibility(false);
      volumePane.renderer.addActor(actor);
      entry.threeD = { actor, mc, imageData };
    }
    volumeActors[vol.id] = entry;
    return entry;
  }

  function removeVolumeEntry(id) {
    const entry = volumeActors[id];
    if (!entry) return;
    for (const key of Object.keys(VIEWS)) {
      const e = entry.panes[key];
      try { panes[key].renderer.removeActor(e.actor); } catch (err) { /* ignore */ }
      try { panes[key].renderer.removeActor(e.fillActor); } catch (err) { /* ignore */ }
    }
    if (entry.threeD && volumePane) { try { volumePane.renderer.removeActor(entry.threeD.actor); } catch (err) { /* ignore */ } }
    delete volumeActors[id];
  }

  for (const key of Object.keys(VIEWS)) {
    const seedPolyData = vtkPolyData.newInstance();
    const seedMapper = vtkMapper.newInstance();
    seedMapper.setInputData(seedPolyData);
    const seedActor = vtkActor.newInstance();
    seedActor.setMapper(seedMapper);
    seedActor.getProperty().setColor(1.0, 0.9, 0.2);
    seedActor.getProperty().setLineWidth(2);
    seedActor.getProperty().setLighting(false);
    seedActor.setVisibility(false);
    panes[key].renderer.addActor(seedActor);
    seedActors[key] = { polyData: seedPolyData, actor: seedActor };
  }

  // 3D surfaces (marching cubes) are created per volume in ensureVolumeEntry().

  // Rebuild the 3D surface from the grown-region mask. The mask is cropped to
  // its bounding box (plus one voxel of padding) so marching cubes stays fast
  // even on large 512x512xN CT volumes.
  function updateVolume3D() {
    if (!volumePane) return;
    const [nx, ny, nz] = state.dims;
    for (const vol of state.volumes) {
      const entry = ensureVolumeEntry(vol);
      if (!entry.threeD) continue;
      if (vol.visible === false) { entry.threeD.actor.setVisibility(false); continue; }
      const mask = vol.mask;
      let x0 = nx, x1 = -1, y0 = ny, y1 = -1, z0 = nz, z1 = -1;
      for (let idx = 0; idx < mask.length; idx++) {
        if (!mask[idx]) continue;
        const x = idx % nx;
        const t = (idx - x) / nx;
        const y = t % ny;
        const z = (t - y) / ny;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
        if (z < z0) z0 = z;
        if (z > z1) z1 = z;
      }
      if (x1 < 0) { entry.threeD.actor.setVisibility(false); continue; }
      x0 = Math.max(0, x0 - 1); y0 = Math.max(0, y0 - 1); z0 = Math.max(0, z0 - 1);
      x1 = Math.min(nx - 1, x1 + 1); y1 = Math.min(ny - 1, y1 + 1); z1 = Math.min(nz - 1, z1 + 1);
      const bw = x1 - x0 + 1, bh = y1 - y0 + 1, bd = z1 - z0 + 1;
      // Bound the marching-cubes cost: subsample masks whose bounding box is
      // larger than the cap so the 3D surface stays light. The 2D overlays
      // remain full resolution. This prevents GPU/CPU exhaustion (white screen)
      // when segmenting large structures such as the whole patient body.
      const MC_MAX_VOXELS = 4 * 1024 * 1024;
      let stride = 1;
      while ((bw * bh * bd) / (stride * stride * stride) > MC_MAX_VOXELS) stride *= 2;
      const sw = Math.ceil(bw / stride), sh = Math.ceil(bh / stride), sd = Math.ceil(bd / stride);
      const crop = new Uint8Array(sw * sh * sd);
      for (let z = 0; z < sd; z++) {
        const sBaseZ = (z0 + z * stride) * nx * ny;
        const dBase = z * sw * sh;
        for (let y = 0; y < sh; y++) {
          const sRow = sBaseZ + (y0 + y * stride) * nx;
          const dRow = dBase + y * sw;
          for (let x = 0; x < sw; x++) crop[dRow + x] = mask[sRow + x0 + x * stride];
        }
      }
      entry.threeD.imageData.setDimensions(sw, sh, sd);
      entry.threeD.imageData.setSpacing(state.spacing[0] * stride, state.spacing[1] * stride, state.spacing[2] * stride);
      entry.threeD.imageData.setOrigin(x0 * state.spacing[0], y0 * state.spacing[1], z0 * state.spacing[2]);
      entry.threeD.imageData.getPointData().setScalars(vtkDataArray.newInstance({
        name: 'mask', values: crop, numberOfComponents: 1,
      }));
      entry.threeD.imageData.modified();
      entry.threeD.mc.update();
      entry.threeD.actor.setVisibility(true);
    }
  }

  function toggleSeed(col, row, iz) {
    const idx = state.growSeeds.findIndex((s) => s[0] === col && s[1] === row && s[2] === iz);
    if (idx >= 0) state.growSeeds.splice(idx, 1);
    else state.growSeeds.push([col, row, iz]);
    updateVolumeOverlay();
    renderAll();
  }

  function setSeedMode(on) {
    state.seedMode = !!on;
    panes.axial.canvasHost.style.cursor = state.seedMode ? 'crosshair' : '';
  }

  function setPanMode(on) {
    state.panMode = !!on;
    state.panStart = null;
    for (const key of Object.keys(VIEWS)) {
      panes[key].canvasHost.style.cursor = state.panMode ? 'grab' : '';
    }
  }

  function setInvert(on) {
    state.invert = !!on;
    sharedProperty.setColorWindow(state.invert ? -Math.abs(state.window) : Math.abs(state.window));
    updateVolumeColorTF();
    renderAll();
  }

  function setMagnifier(on) {
    for (const key of Object.keys(VIEWS)) panes[key].lensOn = !!on;
    syncLens();
  }

  // ── Measurements (length / angle / area, any pane) ──
  const measureActors = {};  // pane key -> { polyData, actor }

  function ensureMeasureActor(key) {
    if (measureActors[key]) return;
    const polyData = vtkPolyData.newInstance();
    const mapper = vtkMapper.newInstance();
    mapper.setInputData(polyData);
    const actor = vtkActor.newInstance();
    actor.setMapper(mapper);
    actor.getProperty().setColor(1.0, 0.9, 0.2);
    actor.getProperty().setLineWidth(2);
    actor.getProperty().setLighting(false);
    panes[key].renderer.addActor(actor);
    measureActors[key] = { polyData, actor };
  }

  const selectedMeasureActors = {};  // pane key -> { polyData, actor } (highlight)
  function ensureSelectedMeasureActor(key) {
    if (selectedMeasureActors[key]) return;
    const polyData = vtkPolyData.newInstance();
    const mapper = vtkMapper.newInstance();
    mapper.setInputData(polyData);
    const actor = vtkActor.newInstance();
    actor.setMapper(mapper);
    actor.getProperty().setColor(0.2, 0.85, 1.0);   // cyan highlight
    actor.getProperty().setLineWidth(3);
    actor.getProperty().setLighting(false);
    panes[key].renderer.addActor(actor);
    selectedMeasureActors[key] = { polyData, actor };
  }

  const colorActors = {};  // pane key -> { hex -> { polyData, actor } }
  function hexToRgb(hex) {
    const h = (hex || '#ff66ff').replace('#', '');
    return [parseInt(h.slice(0, 2), 16) / 255, parseInt(h.slice(2, 4), 16) / 255, parseInt(h.slice(4, 6), 16) / 255];
  }
  function ensureColorActor(key, hex) {
    if (!colorActors[key]) colorActors[key] = {};
    if (colorActors[key][hex]) return colorActors[key][hex];
    const polyData = vtkPolyData.newInstance();
    const mapper = vtkMapper.newInstance();
    mapper.setInputData(polyData);
    const actor = vtkActor.newInstance();
    actor.setMapper(mapper);
    const [r, g, b] = hexToRgb(hex);
    actor.getProperty().setColor(r, g, b);
    actor.getProperty().setLineWidth(2.5);
    actor.getProperty().setLighting(false);
    panes[key].renderer.addActor(actor);
    colorActors[key][hex] = { polyData, actor };
    return colorActors[key][hex];
  }

  // Draw named primitives in their own colour on their slice.
  function updateRegionOverlay(key) {
    const idx = panes[key].index;
    const byColor = {};
    for (const m of state.measurements) {
      if (!m.name || m.pane !== key || m.slice !== idx || m.visible === false || m.id === state.selectedId) continue;
      const closed = m.type === 'polygon' || m.type === 'curve' || m.type === 'freehand';
      if (m.points.length < (closed ? 3 : 2)) continue;
      const hex = m.color || '#ff66ff';
      (byColor[hex] = byColor[hex] || []).push(m);
    }
    if (colorActors[key]) for (const hex of Object.keys(colorActors[key])) colorActors[key][hex].actor.setVisibility(false);
    for (const hex of Object.keys(byColor)) {
      const entry = ensureColorActor(key, hex);
      const points = [];
      const cells = [];
      const P = (x, y, z) => { points.push(x, y, z); return (points.length / 3) - 1; };
      const L = (a, b) => cells.push(2, a, b);
      for (const m of byColor[hex]) {
        appendMeasureGeometry(m.type, m.points, P, L, 0.85, 0.85, false);
      }
      const vpts = vtkPoints.newInstance();
      vpts.setData(Float32Array.from(points), 3);
      entry.polyData.setPoints(vpts);
      const ca = vtkCellArray.newInstance();
      ca.setData(Uint32Array.from(cells));
      entry.polyData.setLines(ca);
      entry.polyData.modified();
      entry.actor.setVisibility(true);
    }
  }

  // Number of clicks needed to complete a measurement (Infinity = until done).
  function measureMaxPoints(t) {
    if (t === 'length' || t === 'rectangle' || t === 'ellipse') return 2;
    if (t === 'angle' || t === 'perpendicular' || t === 'parallel') return 3;
    if (t === 'cobb') return 4;
    return Infinity;
  }

  function pointLineDistance(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) return { dist: Math.hypot(px - ax, py - ay), foot: { x: ax, y: ay } };
    const t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / len2));
    const fx = ax + t * dx, fy = ay + t * dy;
    return { dist: Math.hypot(px - fx, py - fy), foot: { x: fx, y: fy } };
  }

  function catmullRom(pts, samples) {
    if (pts.length < 2) return [];
    const out = [];
    const n = pts.length;
    for (let i = 0; i < n - 1; i++) {
      const p0 = pts[Math.max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1], p3 = pts[Math.min(n - 1, i + 2)];
      for (let s = 0; s < samples; s++) {
        const t = s / samples, t2 = t * t, t3 = t2 * t;
        const x = 0.5 * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
        const y = 0.5 * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
        out.push({ x, y });
      }
    }
    out.push({ x: pts[n - 1].x, y: pts[n - 1].y });
    return out;
  }

  function catmullRomClosed(pts, samples) {
    if (pts.length < 3) return [];
    const n = pts.length;
    const out = [];
    for (let i = 0; i < n; i++) {
      const p0 = pts[(i - 1 + n) % n], p1 = pts[i], p2 = pts[(i + 1) % n], p3 = pts[(i + 2) % n];
      for (let s = 0; s < samples; s++) {
        const t = s / samples, t2 = t * t, t3 = t2 * t;
        const x = 0.5 * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
        const y = 0.5 * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
        out.push({ x, y });
      }
    }
    return out;
  }

  function pointInPolygon(poly, x, y) {
    let inside = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const xi = poly[i].x, yi = poly[i].y, xj = poly[j].x, yj = poly[j].y;
      if ((yi > y) !== (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  }

  function splineLength(pts) {
    const sp = catmullRom(pts, 16);
    let s = 0;
    for (let i = 0; i + 1 < sp.length; i++) s += Math.hypot(sp[i + 1].x - sp[i].x, sp[i + 1].y - sp[i].y);
    return s;
  }

  function polygonArea(pts) {
    let s = 0;
    for (let i = 0; i < pts.length; i++) {
      const j = (i + 1) % pts.length;
      s += pts[i].x * pts[j].y - pts[j].x * pts[i].y;
    }
    return Math.abs(s) / 2;
  }

  function polygonPerimeter(pts) {
    let p = 0;
    for (let i = 0; i < pts.length; i++) {
      const j = (i + 1) % pts.length;
      p += Math.hypot(pts[j].x - pts[i].x, pts[j].y - pts[i].y);
    }
    return p;
  }

  function ellipsePerimeter(a, b) {
    // Ramanujan approximation.
    return Math.PI * (3 * (a + b) - Math.sqrt((3 * a + b) * (a + 3 * b)));
  }

  function makeInsideTest(t, pts) {
    if (t === 'rectangle' && pts.length >= 2) {
      const x0 = Math.min(pts[0].x, pts[1].x), x1 = Math.max(pts[0].x, pts[1].x);
      const y0 = Math.min(pts[0].y, pts[1].y), y1 = Math.max(pts[0].y, pts[1].y);
      return (x, y) => x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
    if (t === 'ellipse' && pts.length >= 2) {
      const cx = (pts[0].x + pts[1].x) / 2, cy = (pts[0].y + pts[1].y) / 2;
      const a = Math.abs(pts[0].x - pts[1].x) / 2, b = Math.abs(pts[0].y - pts[1].y) / 2;
      if (a < 1e-9 || b < 1e-9) return () => false;
      return (x, y) => ((x - cx) / a) * ((x - cx) / a) + ((y - cy) / b) * ((y - cy) / b) <= 1;
    }
    if ((t === 'polygon' || t === 'freehand') && pts.length >= 3) {
      return (x, y) => pointInPolygon(pts, x, y);
    }
    if (t === 'curve' && pts.length >= 3) {
      const poly = catmullRomClosed(pts, 16);
      return (x, y) => pointInPolygon(poly, x, y);
    }
    return () => false;
  }

  function computeRegionStats(pane, t, pts) {
    const v = pane.view;
    const ni = state.dims[v.i];
    const nj = state.dims[v.j];
    const di = state.spacing[v.i];
    const dj = state.spacing[v.j];
    const data = pane.sliceData;
    if (!data || data.length !== ni * nj) return null;
    const inside = makeInsideTest(t, pts);
    const vals = [];
    for (let r = 0; r < nj; r++) {
      for (let c = 0; c < ni; c++) {
        if (inside((c + 0.5) * di, (r + 0.5) * dj)) vals.push(data[r * ni + c]);
      }
    }
    const count = vals.length;
    if (count === 0) return null;
    let sum = 0, min = Infinity, max = -Infinity;
    for (const val of vals) { sum += val; if (val < min) min = val; if (val > max) max = val; }
    const mean = sum / count;
    vals.sort((a, b) => a - b);
    const median = (count % 2) ? vals[(count - 1) >> 1] : (vals[(count >> 1) - 1] + vals[count >> 1]) / 2;
    let s2 = 0, s3 = 0, s4 = 0;
    for (const val of vals) { const d = val - mean; s2 += d * d; s3 += d * d * d; s4 += d * d * d * d; }
    const variance = s2 / count;
    const stdev = Math.sqrt(variance);
    const skew = variance > 1e-12 ? (s3 / count) / Math.pow(variance, 1.5) : 0;
    const kurt = variance > 1e-12 ? (s4 / count) / (variance * variance) - 3 : 0;
    const hist = new Map();
    for (const val of vals) hist.set(val, (hist.get(val) || 0) + 1);
    let entropy = 0;
    for (const c of hist.values()) { const pr = c / count; entropy -= pr * Math.log2(pr); }
    return { count, min, max, median, mean, stdev, skew, kurt, entropy };
  }

  function measureStatsText() {
    const t = state.measureType;
    if (t !== 'rectangle' && t !== 'ellipse' && t !== 'polygon' && t !== 'curve' && t !== 'freehand') return '';
    const pane = panes[state.measurePane];
    if (!pane) return '';
    const pts = state.measurePoints[state.measurePane] || [];
    const st = computeRegionStats(pane, t, pts);
    if (!st) return '';
    return '\nN: ' + st.count + '  Min: ' + st.min.toFixed(1) + '  Max: ' + st.max.toFixed(1) +
      '\nMean: ' + st.mean.toFixed(2) + '  SD: ' + st.stdev.toFixed(2) +
      '\nMedian: ' + st.median.toFixed(2) + '  Skew: ' + st.skew.toFixed(2) + '  Kurt: ' + st.kurt.toFixed(2) +
      '\nEntropy: ' + st.entropy.toFixed(2) + ' bit';
  }

  function computeMeasure() {
    const pts = state.measurePoints[state.measurePane] || [];
    const mtype = state.measureType;
    if (mtype === 'length') {
      if (pts.length < 2) return '';
      return t('label.length') + ': ' + Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y).toFixed(1) + t('label.mm');
    }
    if (mtype === 'polyline') {
      if (pts.length < 2) return '';
      let s = 0;
      for (let i = 0; i + 1 < pts.length; i++) s += Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
      return t('label.length') + ': ' + s.toFixed(1) + t('label.mm');
    }
    if (mtype === 'spline') {
      if (pts.length < 2) return '';
      return t('label.length') + ': ' + splineLength(pts).toFixed(1) + t('label.mm');
    }
    if (mtype === 'curve') {
      if (pts.length < 3) return '';
      const poly = catmullRomClosed(pts, 16);
      let bx0 = Infinity, bx1 = -Infinity, by0 = Infinity, by1 = -Infinity;
      for (const p of poly) { if (p.x < bx0) bx0 = p.x; if (p.x > bx1) bx1 = p.x; if (p.y < by0) by0 = p.y; if (p.y > by1) by1 = p.y; }
      return 'W: ' + (bx1 - bx0).toFixed(1) + ' × H: ' + (by1 - by0).toFixed(1) + t('label.mm') + '\n' + t('label.area') + ': ' + (polygonArea(poly) / 100).toFixed(2) + ' cm²\n' + t('label.perimeter') + ': ' + polygonPerimeter(poly).toFixed(1) + t('label.mm') + measureStatsText();
    }
    if (mtype === 'angle' || mtype === 'cobb') {
      const need = (mtype === 'angle') ? 3 : 4;
      if (pts.length < need) return '';
      const a = (mtype === 'angle') ? [pts[0].x - pts[1].x, pts[0].y - pts[1].y] : [pts[1].x - pts[0].x, pts[1].y - pts[0].y];
      const b = (mtype === 'angle') ? [pts[2].x - pts[1].x, pts[2].y - pts[1].y] : [pts[3].x - pts[2].x, pts[3].y - pts[2].y];
      const na = Math.hypot(a[0], a[1]), nb = Math.hypot(b[0], b[1]);
      if (na < 1e-6 || nb < 1e-6) return '';
      const deg = Math.acos(Math.max(-1, Math.min(1, (a[0] * b[0] + a[1] * b[1]) / (na * nb)))) * 180 / Math.PI;
      return (mtype === 'angle' ? t('label.angleVal') + ': ' : t('label.cobbVal') + ': ') + deg.toFixed(1) + '°';
    }
    if (mtype === 'perpendicular') {
      if (pts.length < 3) return '';
      const r = pointLineDistance(pts[2].x, pts[2].y, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
      const base = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
      return t('label.perpendicularVal') + ': ' + r.dist.toFixed(1) + t('label.mm') + '\n' + t('label.base') + ': ' + base.toFixed(1) + t('label.mm');
    }
    if (mtype === 'parallel') {
      if (pts.length < 3) return '';
      const d = pointLineDistance(pts[2].x, pts[2].y, pts[0].x, pts[0].y, pts[1].x, pts[1].y).dist;
      const base = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
      return t('label.distance') + ': ' + d.toFixed(1) + t('label.mm') + '\n' + t('label.length') + ': ' + base.toFixed(1) + t('label.mm');
    }
    if (mtype === 'rectangle') {
      if (pts.length < 2) return '';
      const w = Math.abs(pts[1].x - pts[0].x), h = Math.abs(pts[1].y - pts[0].y);
      return 'W: ' + w.toFixed(1) + ' × H: ' + h.toFixed(1) + t('label.mm') + '\n' + t('label.area') + ': ' + (w * h / 100).toFixed(2) + ' cm²\n' + t('label.perimeter') + ': ' + (2 * (w + h)).toFixed(1) + t('label.mm') + measureStatsText();
    }
    if (mtype === 'ellipse') {
      if (pts.length < 2) return '';
      const a = Math.abs(pts[1].x - pts[0].x) / 2, b = Math.abs(pts[1].y - pts[0].y) / 2;
      return 'a: ' + a.toFixed(1) + ' × b: ' + b.toFixed(1) + t('label.mm') + '\n' + t('label.area') + ': ' + (Math.PI * a * b / 100).toFixed(2) + ' cm²\n' + t('label.perimeter') + ': ' + ellipsePerimeter(a, b).toFixed(1) + t('label.mm') + measureStatsText();
    }
    if (mtype === 'polygon' || mtype === 'freehand') {
      if (pts.length < 3) return '';
      let bx0 = Infinity, bx1 = -Infinity, by0 = Infinity, by1 = -Infinity;
      for (const p of pts) { if (p.x < bx0) bx0 = p.x; if (p.x > bx1) bx1 = p.x; if (p.y < by0) by0 = p.y; if (p.y > by1) by1 = p.y; }
      return 'W: ' + (bx1 - bx0).toFixed(1) + ' × H: ' + (by1 - by0).toFixed(1) + t('label.mm') + '\n' + t('label.area') + ': ' + (polygonArea(pts) / 100).toFixed(2) + ' cm²\n' + t('label.perimeter') + ': ' + polygonPerimeter(pts).toFixed(1) + t('label.mm') + measureStatsText();
    }
    return '';
  }

  // Append the line geometry (main shape + vertex crosses) for one measurement
  // into the shared points/cells arrays.
  function appendMeasureGeometry(t, pts, P, L, zm, zc, drawCrosses) {
    zm = zm == null ? 0.6 : zm;
    zc = zc == null ? 0.7 : zc;
    if (t === 'rectangle' && pts.length >= 2) {
      const x0 = Math.min(pts[0].x, pts[1].x), x1 = Math.max(pts[0].x, pts[1].x);
      const y0 = Math.min(pts[0].y, pts[1].y), y1 = Math.max(pts[0].y, pts[1].y);
      const a = P(x0, y0, zm), b = P(x1, y0, zm), c = P(x1, y1, zm), d = P(x0, y1, zm);
      L(a, b); L(b, c); L(c, d); L(d, a);
    } else if (t === 'ellipse' && pts.length >= 2) {
      const cx = (pts[0].x + pts[1].x) / 2, cy = (pts[0].y + pts[1].y) / 2;
      const ra = Math.abs(pts[0].x - pts[1].x) / 2, rb = Math.abs(pts[0].y - pts[1].y) / 2;
      const N = 72;
      let prev = -1, first = -1;
      for (let i = 0; i <= N; i++) {
        const th = (i / N) * Math.PI * 2;
        const idx = P(cx + ra * Math.cos(th), cy + rb * Math.sin(th), zm);
        if (prev >= 0) L(prev, idx); else first = idx;
        prev = idx;
      }
      L(prev, first);
    } else if (t === 'perpendicular' && pts.length >= 3) {
      const a = P(pts[0].x, pts[0].y, zm); const b = P(pts[1].x, pts[1].y, zm); L(a, b);
      const r = pointLineDistance(pts[2].x, pts[2].y, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
      const f = P(r.foot.x, r.foot.y, zm); const c = P(pts[2].x, pts[2].y, zm); L(f, c);
    } else if (t === 'parallel' && pts.length >= 3) {
      const a = P(pts[0].x, pts[0].y, zm); const b = P(pts[1].x, pts[1].y, zm); L(a, b);
      const dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y;
      const c = P(pts[2].x - dx / 2, pts[2].y - dy / 2, zm); const d = P(pts[2].x + dx / 2, pts[2].y + dy / 2, zm); L(c, d);
    } else if (t === 'cobb' && pts.length >= 2) {
      if (pts.length >= 2) { const a = P(pts[0].x, pts[0].y, zm); const b = P(pts[1].x, pts[1].y, zm); L(a, b); }
      if (pts.length >= 4) { const c = P(pts[2].x, pts[2].y, zm); const d = P(pts[3].x, pts[3].y, zm); L(c, d); }
    } else if (t === 'spline' && pts.length >= 2) {
      const sp = catmullRom(pts, 16);
      const idxs = sp.map((q) => P(q.x, q.y, zm));
      for (let i = 0; i + 1 < idxs.length; i++) L(idxs[i], idxs[i + 1]);
    } else if (t === 'curve' && pts.length >= 3) {
      const poly = catmullRomClosed(pts, 16);
      const idxs = poly.map((q) => P(q.x, q.y, zm));
      for (let i = 0; i < idxs.length; i++) L(idxs[i], idxs[(i + 1) % idxs.length]);
    } else {
      let start = -1;
      for (let i = 0; i < pts.length; i++) { const idx = P(pts[i].x, pts[i].y, zm); if (i === 0) start = idx; }
      for (let i = 0; i + 1 < pts.length; i++) L(start + i, start + i + 1);
      if ((t === 'polygon' || t === 'freehand') && pts.length >= 3) L(start + pts.length - 1, start);
    }

    // Cross markers at each click point.
    if (drawCrosses !== false) {
      const R = Math.max(state.spacing[0], state.spacing[1]) * 2;
      for (let i = 0; i < pts.length; i++) {
        const cx = pts[i].x, cy = pts[i].y;
        const a = P(cx - R, cy, zc), b = P(cx + R, cy, zc); L(a, b);
        const c = P(cx, cy - R, zc), d = P(cx, cy + R, zc); L(c, d);
      }
    }
  }

  function updateMeasureOverlay(key) {
    key = key || state.measurePane;
    if (!key) return;
    ensureMeasureActor(key);
    const entry = measureActors[key];
    const points = [];
    const cells = [];
    const P = (x, y, z) => { points.push(x, y, z); return (points.length / 3) - 1; };
    const L = (a, b) => cells.push(2, a, b);

    // Committed measurements on this pane (skip hidden and the selected one,
    // which is drawn separately in a brighter colour).
    for (const m of state.measurements) {
      if (m.pane === key && m.slice === panes[key].index && m.visible !== false && m.id !== state.selectedId && !m.name && m.points.length > 0) {
        appendMeasureGeometry(m.type, m.points, P, L);
      }
    }
    // Current in-progress measurement on this pane (only on its slice).
    const cur = (state.measureSlice == null || state.measureSlice === panes[key].index) ? (state.measurePoints[key] || []) : [];
    if (cur.length > 0) appendMeasureGeometry(state.measureType, cur, P, L);

    // Selected measurement — drawn on top in a brighter colour.
    let selM = null;
    if (state.selectedId != null) selM = state.measurements.find((m) => m.id === state.selectedId);
    if (selM && selM.pane === key && selM.slice === panes[key].index && selM.visible !== false && selM.points.length > 0) {
      ensureSelectedMeasureActor(key);
      const sentry = selectedMeasureActors[key];
      const spoints = [], scells = [];
      const sP = (x, y, z) => { spoints.push(x, y, z); return (spoints.length / 3) - 1; };
      const sL = (a, b) => scells.push(2, a, b);
      appendMeasureGeometry(selM.type, selM.points, sP, sL, 0.9, 0.95);
      const svpts = vtkPoints.newInstance();
      svpts.setData(Float32Array.from(spoints), 3);
      sentry.polyData.setPoints(svpts);
      const sca = vtkCellArray.newInstance();
      sca.setData(Uint32Array.from(scells));
      sentry.polyData.setLines(sca);
      sentry.polyData.modified();
      sentry.actor.setVisibility(true);
    } else if (selectedMeasureActors[key]) {
      selectedMeasureActors[key].actor.setVisibility(false);
    }

    if (points.length === 0) { entry.actor.setVisibility(false); return; }
    const vpts = vtkPoints.newInstance();
    vpts.setData(Float32Array.from(points), 3);
    entry.polyData.setPoints(vpts);
    const ca = vtkCellArray.newInstance();
    ca.setData(Uint32Array.from(cells));
    entry.polyData.setLines(ca);
    entry.polyData.modified();
    entry.actor.setVisibility(true);

    // Live value for the current in-progress measurement only.
    state.measureValue = computeMeasure();
    if (!state.measureValue && cur.length > 0) {
      const need = measureMaxPoints(state.measureType);
      const labels = { length: t('measure.length'), polyline: t('measure.polyline'), angle: t('measure.angle'), cobb: t('measure.cobb'), rectangle: t('measure.rectangle'), ellipse: t('measure.ellipse'), polygon: t('measure.polygon'), spline: t('measure.spline'), curve: t('measure.curve'), freehand: t('measure.freehand'), perpendicular: t('measure.perpendicular'), parallel: t('measure.parallel') };
      state.measureValue = (labels[state.measureType] || t('msg.measurement')) + ': ' + cur.length + (Number.isFinite(need) ? '/' + need + ' ' + t('msg.point') : ' ' + t('msg.pointEnter'));
    }
    if (hooks.onMeasureChange) hooks.onMeasureChange(state.measureValue);
  }

  function setMeasureMode(on, type) {
    state.measureMode = !!on;
    if (type) state.measureType = type;
    state.measurePane = null;
    for (const key of Object.keys(VIEWS)) state.measurePoints[key] = [];
    state.measureValue = '';
    state.measureSlice = null;
    for (const key of Object.keys(VIEWS)) {
      panes[key].canvasHost.style.cursor = state.measureMode ? 'crosshair' : '';
      updateMeasureOverlay(key);
    }
    renderAll();
  }

  function commitMeasurement() {
    const key = state.measurePane;
    if (!key) return;
    const pts = state.measurePoints[key] || [];
    if (pts.length === 0) return;
    state.measurements.push({
      id: ++state.measureId,
      type: state.measureType,
      pane: key,
      slice: state.measureSlice,
      points: pts.map((p) => ({ x: p.x, y: p.y })),
      value: state.measureValue,
      visible: true,
      name: '',
      color: null,
    });
    state.measurePoints[key] = [];
    state.measureValue = '';
    state.measureOpen = false;
    state.measureSlice = null;
    updateMeasureOverlay(key);
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
  }

  function deleteMeasurement(id) {
    const idx = state.measurements.findIndex((m) => m.id === id);
    if (idx < 0) return;
    state.measurements.splice(idx, 1);
    if (state.selectedId === id) { state.selectedId = null; if (hooks.onSelectionChange) hooks.onSelectionChange(null); }
    for (const key of Object.keys(VIEWS)) updateMeasureOverlay(key);
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
  }

  function setMeasurementVisible(id, visible) {
    const m = state.measurements.find((mm) => mm.id === id);
    if (!m) return;
    m.visible = !!visible;
    for (const key of Object.keys(VIEWS)) updateMeasureOverlay(key);
    renderAll();
  }

  function selectMeasurement(id) {
    state.selectedId = id;
    for (const key of Object.keys(VIEWS)) updateMeasureOverlay(key);
    renderAll();
    const m = state.measurements.find((mm) => mm.id === id) || null;
    if (hooks.onSelectionChange) hooks.onSelectionChange(m);
  }

  function clearMeasurements() {
    state.measurePane = null;
    for (const key of Object.keys(VIEWS)) state.measurePoints[key] = [];
    state.measureValue = '';
    state.measureOpen = false;
    state.measureSlice = null;
    state.measurements = [];
    state.selectedId = null;
    for (const key of Object.keys(VIEWS)) updateMeasureOverlay(key);
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
    if (hooks.onSelectionChange) hooks.onSelectionChange(null);
  }

  function setMeasurementName(id, name) {
    const m = state.measurements.find((mm) => mm.id === id);
    if (!m) return;
    m.name = (name || '').trim();
    for (const key of Object.keys(VIEWS)) { updateMeasureOverlay(key); updateRegionOverlay(key); }
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
    if (hooks.onSelectionChange) hooks.onSelectionChange(m);
  }

  function setMeasurementColor(id, color) {
    const m = state.measurements.find((mm) => mm.id === id);
    if (!m) return;
    m.color = color;
    for (const key of Object.keys(VIEWS)) updateRegionOverlay(key);
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
  }

  function finishMeasurement() {
    if (state.measureOpen) commitMeasurement();
    else state.measureOpen = false;
  }

  // Weasis: Backspace drops the last placed vertex; on the first one it cancels.
  function removeLastVertex() {
    const key = state.measurePane;
    if (!key) return;
    const pts = state.measurePoints[key] || [];
    if (pts.length <= 1) { cancelMeasurement(); return; }
    pts.pop();
    updateMeasureOverlay(key);
    renderAll();
  }

  // Weasis: Escape cancels the drawing in progress (keeps the tool selected).
  function cancelMeasurement() {
    const key = state.measurePane;
    if (key) state.measurePoints[key] = [];
    state.measureOpen = false;
    state.measureSlice = null;
    if (key) { updateMeasureOverlay(key); renderAll(); }
  }

  // ── Tumour volume: interpolation between named contours (like RTSTRUCT) ──
  function pointInPolygon(px, py, poly) {
    let inside = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const [xi, yi] = poly[i], [xj, yj] = poly[j];
      if ((yi > py) !== (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  }

  // Resample a closed polygon to n vertices by arc length (for vertex correspondence).
  function resamplePolygon(pts, n) {
    if (pts.length < 3) return pts;
    const seg = [];
    let total = 0;
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], b = pts[(i + 1) % pts.length];
      const d = Math.hypot(b.x - a.x, b.y - a.y);
      seg.push(d); total += d;
    }
    if (total < 1e-9) return pts;
    const out = [];
    const step = total / n;
    let acc = 0, k = 0;
    for (let i = 0; i < n; i++) {
      const target = i * step;
      while (k < pts.length && acc + seg[k] < target) { acc += seg[k]; k++; }
      const t = seg[k] ? (target - acc) / seg[k] : 0;
      const a = pts[k % pts.length], b = pts[(k + 1) % pts.length];
      out.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t });
    }
    return out;
  }

  // Build a 3D voxel mask by linear interpolation between named axial contours.
  // Returns null on success, or an error string. Reuses the grow overlay + 3D view.
  function computeMaskStats(mask) {
    const src = state.data;
    let count = 0, sum = 0, min = Infinity, max = -Infinity;
    for (let i = 0; i < mask.length; i++) {
      if (!mask[i]) continue;
      const v = src[i];
      sum += v; count++;
      if (v < min) min = v;
      if (v > max) max = v;
    }
    if (count === 0) return null;
    const mean = sum / count;
    let s2 = 0;
    for (let i = 0; i < mask.length; i++) if (mask[i]) { const d = src[i] - mean; s2 += d * d; }
    const sd = Math.sqrt(s2 / count);
    const volumeCc = count * state.spacing[0] * state.spacing[1] * state.spacing[2] / 1000;
    return { count, volumeCc, min, max, mean, sd };
  }

  // Convert a measurement to a closed contour polygon (rectangle -> 4 corners,
  // ellipse -> sampled ellipse, curve -> smooth closed spline). Returns null for
  // non-region primitives.
  function regionPolygon(m) {
    const t = m.type, pts = m.points;
    if (t === 'rectangle' && pts.length >= 2) {
      const x0 = Math.min(pts[0].x, pts[1].x), x1 = Math.max(pts[0].x, pts[1].x);
      const y0 = Math.min(pts[0].y, pts[1].y), y1 = Math.max(pts[0].y, pts[1].y);
      return [{ x: x0, y: y0 }, { x: x1, y: y0 }, { x: x1, y: y1 }, { x: x0, y: y1 }];
    }
    if (t === 'ellipse' && pts.length >= 2) {
      const cx = (pts[0].x + pts[1].x) / 2, cy = (pts[0].y + pts[1].y) / 2;
      const a = Math.abs(pts[0].x - pts[1].x) / 2, b = Math.abs(pts[0].y - pts[1].y) / 2;
      const N = 64, out = [];
      for (let i = 0; i < N; i++) { const th = (i / N) * Math.PI * 2; out.push({ x: cx + a * Math.cos(th), y: cy + b * Math.sin(th) }); }
      return out;
    }
    if (t === 'curve' && pts.length >= 3) return catmullRomClosed(pts, 16);
    if ((t === 'polygon' || t === 'freehand') && pts.length >= 3) return pts;
    return null;
  }

  function buildTumorFromContours(name) {
    if (!state.loaded) return t('label.ctNotLoaded');
    const key = 'axial';
    const v = VIEWS[key];
    const ni = state.dims[v.i], nj = state.dims[v.j];
    const di = state.spacing[v.i], dj = state.spacing[v.j];
    const byName = new Map();
    for (const m of state.measurements) {
      if (!m.name || m.pane !== key || m.slice == null) continue;
      const poly = regionPolygon(m);
      if (!poly) continue;
      if (name && m.name !== name) continue;
      if (!byName.has(m.name)) byName.set(m.name, []);
      byName.get(m.name).push({ slice: m.slice, points: poly });
    }
    if (byName.size === 0) return name ? (t('msg.noContoursNamed') + name + '»') : t('msg.noNamedContours');
    const N = 64;
    const [nx, ny, nz] = state.dims;
    const built = [];
    for (const [nm, contours] of byName) {
      if (contours.length < 2) continue;
      contours.sort((a, b) => a.slice - b.slice);
      const z0 = contours[0].slice, z1 = contours[contours.length - 1].slice;
      if (z0 === z1) continue;
      const resampled = contours.map((c) => resamplePolygon(c.points, N));
      const mask = new Uint8Array(nx * ny * nz);
      for (let z = z0; z <= z1; z++) {
        let lo = contours[0], hi = contours[contours.length - 1], loI = 0, hiI = contours.length - 1;
        for (let i = 0; i < contours.length; i++) {
          if (contours[i].slice <= z) { lo = contours[i]; loI = i; }
          if (contours[i].slice >= z) { hi = contours[i]; hiI = i; break; }
        }
        let poly;
        if (lo.slice === z) poly = resampled[loI];
        else if (hi.slice === z) poly = resampled[hiI];
        else {
          const t = (z - lo.slice) / (hi.slice - lo.slice);
          poly = resampled[loI].map((p, k) => ({ x: p.x + (resampled[hiI][k].x - p.x) * t, y: p.y + (resampled[hiI][k].y - p.y) * t }));
        }
        const vpoly = poly.map((p) => [
          p.x / di - 0.5,
          v.flipV ? nj - p.y / dj - 0.5 : p.y / dj - 0.5,
        ]);
        let minI = Infinity, maxI = -Infinity, minJ = Infinity, maxJ = -Infinity;
        for (const [i, j] of vpoly) { minI = Math.min(minI, i); maxI = Math.max(maxI, i); minJ = Math.min(minJ, j); maxJ = Math.max(maxJ, j); }
        minI = Math.max(0, Math.floor(minI)); maxI = Math.min(ni - 1, Math.ceil(maxI));
        minJ = Math.max(0, Math.floor(minJ)); maxJ = Math.min(nj - 1, Math.ceil(maxJ));
        for (let j = minJ; j <= maxJ; j++) {
          for (let i = minI; i <= maxI; i++) {
            if (pointInPolygon(i + 0.5, j + 0.5, vpoly)) mask[(z * nj + j) * ni + i] = 1;
          }
        }
      }
      const stats = computeMaskStats(mask);
      const existing = state.volumes.find((vv) => vv.name === nm);
      if (existing) {
        existing.mask = mask;
        existing.stats = stats;
        existing.visible = true;
      } else {
        state.volumes.push({
          id: ++state.volumeIdSeq,
          name: nm,
          color: VOLUME_COLORS[state.volumes.length % VOLUME_COLORS.length],
          mask,
          material: '',
          visible: true,
          stats,
        });
      }
      built.push(nm);
    }
    if (built.length === 0) return t('msg.needTwoContours');
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
    return null;
  }

  function runGrow(tolerance) {
    if (!state.loaded || state.growSeeds.length === 0) return 0;
    const [nx, ny, nz] = state.dims;
    const src = state.data;
    let refMin = Infinity, refMax = -Infinity;
    for (const s of state.growSeeds) {
      const v = src[(s[2] * ny + s[1]) * nx + s[0]];
      if (v < refMin) refMin = v;
      if (v > refMax) refMax = v;
    }
    refMin -= tolerance;
    refMax += tolerance;
    const mask = new Uint8Array(nx * ny * nz);
    const queue = [];
    const push = (i) => { if (!mask[i]) { mask[i] = 1; queue.push(i); } };
    for (const s of state.growSeeds) push((s[2] * ny + s[1]) * nx + s[0]);
    const NXY = nx * ny;
    while (queue.length) {
      const idx = queue.pop();
      const iz = (idx / NXY) | 0;
      const rem = idx - iz * NXY;
      const iy = (rem / nx) | 0;
      const ix = rem - iy * nx;
      const inRange = (i) => { const v = src[i]; return v >= refMin && v <= refMax; };
      if (ix > 0 && inRange(idx - 1)) push(idx - 1);
      if (ix < nx - 1 && inRange(idx + 1)) push(idx + 1);
      if (iy > 0 && inRange(idx - nx)) push(idx - nx);
      if (iy < ny - 1 && inRange(idx + nx)) push(idx + nx);
      if (iz > 0 && inRange(idx - NXY)) push(idx - NXY);
      if (iz < nz - 1 && inRange(idx + NXY)) push(idx + NXY);
    }
    let count = 0;
    for (let i = 0; i < mask.length; i++) count += mask[i];
    const stats = computeMaskStats(mask);
    let nm = t('msg.grown'), seq = 1;
    while (state.volumes.some((vv) => vv.name === nm)) { seq++; nm = t('msg.grownX') + seq; }
    state.volumes.push({
      id: ++state.volumeIdSeq,
      name: nm,
      color: VOLUME_COLORS[state.volumes.length % VOLUME_COLORS.length],
      mask,
      material: '',
      visible: true,
      stats,
    });
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
    return count;
  }

  // Chain marching-squares boundary segments into closed rings (for RTSTRUCT).
  function maskRings(mask, ni, nj) {
    const segs = marchingSquares(mask, ni, nj);
    const key = (x, y) => x + ',' + y;
    const adj = new Map();
    const add = (a, b) => {
      const ka = key(a[0], a[1]), kb = key(b[0], b[1]);
      if (!adj.has(ka)) adj.set(ka, []);
      adj.get(ka).push([b[0], b[1], kb]);
    };
    for (let i = 0; i < segs.length; i += 4) {
      add([segs[i], segs[i + 1]], [segs[i + 2], segs[i + 3]]);
      add([segs[i + 2], segs[i + 3]], [segs[i], segs[i + 1]]);
    }
    const visited = new Set();
    const rings = [];
    for (const startK of adj.keys()) {
      if (visited.has(startK)) continue;
      const ring = [];
      let curK = startK;
      while (curK && !visited.has(curK)) {
        visited.add(curK);
        const c = curK.split(',').map(Number);
        ring.push(c);
        const nbrs = adj.get(curK) || [];
        let next = null;
        for (const nb of nbrs) if (!visited.has(nb[2])) { next = nb; break; }
        curK = next ? next[2] : null;
      }
      if (ring.length >= 3) rings.push(ring);
    }
    return rings;
  }

  // Set / clear the material override of a volume ('' = HU-based).
  function setVolumeMaterial(id, material) {
    const vol = state.volumes.find((v) => v.id === id);
    if (!vol) return;
    vol.material = material || '';
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  // Build the payload for the dicom.transfer RPC: a slice range + volume masks
  // (cropped to that range, RLE-encoded).
  function buildTransferPayload(mode, sliceFrom, sliceTo) {
    if (!state.loaded) return null;
    const [nx, ny, nz] = state.dims;
    const from = Math.max(0, Math.min(sliceFrom | 0, nz - 1));
    const to = Math.max(0, Math.min(sliceTo | 0, nz - 1));
    if (from > to) return null;
    const nz2 = to - from + 1;
    const plane = nx * ny;
    const volumes = [];
    for (const v of state.volumes) {
      if (v.visible === false) continue;  // transfer only visible volumes
      const crop = new Uint8Array(plane * nz2);
      for (let z = 0; z < nz2; z++) {
        crop.set(v.mask.subarray((from + z) * plane, (from + z + 1) * plane), z * plane);
      }
      volumes.push({ name: v.name, material: v.material || '', len: crop.length, rle: rleEncode(crop) });
    }
    return { mode, sliceFrom: from, sliceTo: to, volumes };
  }

  // Collect named contours + volume masks into an RTSTRUCT ROI list (LPS mm).
  function buildRtStructExport() {
    if (!state.loaded) return null;
    const dir = state.direction;
    const [ox, oy, oz] = state.origin;
    const voxelToLps = (i, j, k) => [
      ox + dir[0] * i + dir[3] * j + dir[6] * k,
      oy + dir[1] * i + dir[4] * j + dir[7] * k,
      oz + dir[2] * i + dir[5] * j + dir[8] * k,
    ];
    const rois = [];
    let num = 1;
    const ni = state.dims[0], nj = state.dims[1];
    const di = state.spacing[0], dj = state.spacing[1];
    const flipV = VIEWS.axial.flipV;
    // named closed measurement contours (axial)
    for (const m of state.measurements) {
      if (!m.name) continue;
      const poly = regionPolygon(m);
      if (!poly || poly.length < 3) continue;
      const xs = [], ys = [], zs = [];
      for (const p of poly) {
        const i = p.x / di - 0.5;
        const j = flipV ? (nj - p.y / dj - 0.5) : (p.y / dj - 0.5);
        const l = voxelToLps(i, j, m.slice == null ? 0 : m.slice);
        xs.push(l[0]); ys.push(l[1]); zs.push(l[2]);
      }
      rois.push({ number: num++, name: m.name, type: 'ORGAN', contours: [{ x: xs, y: ys, z: zs }] });
    }
    // volumes -> per-slice closed rings
    const [nx, ny, nz] = state.dims;
    for (const v of state.volumes) {
      const contours = [];
      for (let z = 0; z < nz; z++) {
        const mask2d = new Uint8Array(nx * ny);
        for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) mask2d[j * nx + i] = v.mask[(z * ny + j) * nx + i];
        const rings = maskRings(mask2d, nx, ny);
        for (const ring of rings) {
          const xs = [], ys = [], zs = [];
          for (const [i, j] of ring) { const l = voxelToLps(i, j, z); xs.push(l[0]); ys.push(l[1]); zs.push(l[2]); }
          contours.push({ x: xs, y: ys, z: zs });
        }
      }
      rois.push({ number: num++, name: v.name, type: 'ORGAN', contours });
    }
    return { rois };
  }

  function rleEncode(mask) {
    const runs = [];
    let cur = 0, count = 0;
    for (let i = 0; i < mask.length; i++) {
      if (mask[i] === cur) count++;
      else { runs.push(count); cur = mask[i]; count = 1; }
    }
    runs.push(count);
    return runs;
  }

  function rleDecode(runs, len) {
    const mask = new Uint8Array(len);
    let idx = 0, cur = 0;
    for (let k = 0; k < runs.length; k++) {
      const n = runs[k];
      if (cur && n) mask.fill(1, idx, idx + n);
      idx += n;
      cur = 1 - cur;
    }
    return mask;
  }

  function serializeState() {
    return {
      measurements: state.measurements.map((m) => ({ id: m.id, type: m.type, pane: m.pane, slice: m.slice, points: m.points.map((p) => ({ x: p.x, y: p.y })), value: m.value, visible: m.visible !== false, name: m.name || '', color: m.color || null })),
      volumes: state.volumes.map((v) => ({ id: v.id, name: v.name, color: v.color, material: v.material || '', visible: v.visible !== false, stats: v.stats, len: v.mask.length, rle: rleEncode(v.mask) })),
    };
  }

  function restoreState(payload) {
    if (!state.loaded || !payload) return;
    if (Array.isArray(payload.measurements)) {
      state.measurements = payload.measurements.map((m) => ({ ...m }));
      for (const m of state.measurements) if (m.id > state.measureId) state.measureId = m.id;
    }
    if (Array.isArray(payload.volumes)) {
      state.volumes = payload.volumes.map((v) => ({
        id: v.id, name: v.name, color: v.color, material: v.material || '', visible: v.visible !== false, stats: v.stats,
        mask: rleDecode(v.rle, v.len),
      }));
      for (const v of state.volumes) if (v.id > state.volumeIdSeq) state.volumeIdSeq = v.id;
    }
    for (const key of Object.keys(VIEWS)) { updateMeasureOverlay(key); updateRegionOverlay(key); }
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onMeasurementsChange) hooks.onMeasurementsChange(state.measurements);
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function clearGrow() {
    state.growSeeds = [];
    for (const id of Object.keys(volumeActors)) removeVolumeEntry(id);
    state.volumes = [];
    for (const k of Object.keys(VIEWS)) {
      seedActors[k].actor.setVisibility(false);
    }
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function deleteVolume(id) {
    removeVolumeEntry(id);
    state.volumes = state.volumes.filter((v) => v.id !== id);
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function setVolumeVisible(id, visible) {
    const vol = state.volumes.find((v) => v.id === id);
    if (!vol) return;
    vol.visible = !!visible;
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function setVolumeName(id, name) {
    const vol = state.volumes.find((v) => v.id === id);
    if (!vol) return;
    const trimmed = (name || '').trim();
    if (trimmed) vol.name = trimmed;
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function setVolumeColor(id, rgb) {
    const vol = state.volumes.find((v) => v.id === id);
    if (!vol) return;
    vol.color = rgb;
    const entry = volumeActors[id];
    if (entry) {
      for (const key of Object.keys(VIEWS)) {
        entry.panes[key].actor.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
        entry.panes[key].fillCtf.removeAllPoints();
        entry.panes[key].fillCtf.addRGBPoint(0, 0, 0, 0);
        entry.panes[key].fillCtf.addRGBPoint(1, rgb[0], rgb[1], rgb[2]);
      }
      if (entry.threeD) entry.threeD.actor.getProperty().setColor(rgb[0], rgb[1], rgb[2]);
    }
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
  }

  function updateVolumeOverlay(keyArg) {
    if (!state.loaded) return;
    const keys = keyArg ? [keyArg] : Object.keys(VIEWS);
    for (const key of keys) {
      const p = panes[key];
      const v = p.view;
      const ni = state.dims[v.i];
      const nj = state.dims[v.j];
      const di = state.spacing[v.i];
      const dj = state.spacing[v.j];

      // Seed markers (crosses) on this pane's current slice.
      const seedPts = [];
      const seedCells = [];
      let seedOff = 0;
      const cross = Math.max(di, dj) * 1.5;
      for (const s of state.growSeeds) {
        if (s[v.k] !== p.index) continue;   // seed lies on another slice
        let cx = (s[v.i] + 0.5) * di;
        let cy = (s[v.j] + 0.5) * dj;
        if (v.flipH) cx = ni * di - cx;      // mirror to match the displayed image
        if (v.flipV) cy = nj * dj - cy;
        seedPts.push(cx - cross, cy, 1.0, cx + cross, cy, 1.0);
        seedPts.push(cx, cy - cross, 1.0, cx, cy + cross, 1.0);
        seedCells.push(2, seedOff, seedOff + 1, 2, seedOff + 2, seedOff + 3);
        seedOff += 4;
      }
      const seedEntry = seedActors[key];
      if (seedPts.length === 0) {
        seedEntry.actor.setVisibility(false);
      } else {
        const pts = vtkPoints.newInstance();
        pts.setData(Float32Array.from(seedPts), 3);
        seedEntry.polyData.setPoints(pts);
        const ca = vtkCellArray.newInstance();
        ca.setData(Uint32Array.from(seedCells));
        seedEntry.polyData.setLines(ca);
        seedEntry.polyData.modified();
        seedEntry.actor.setVisibility(true);
      }

      // Volumes: contour + semi-transparent fill on this pane's current slice.
      for (const vol of state.volumes) {
        const e = ensureVolumeEntry(vol).panes[key];
        const mask2d = extractSliceMask(vol.mask, v, p.index, (m) => m);
        const vis = vol.visible !== false;
        if (vis && setContourLines(e.polyData, mask2d, ni, nj, di, dj, 0.5)) {
          e.actor.setVisibility(true);
        } else {
          e.actor.setVisibility(false);
        }
        e.fillImageData.setDimensions(ni, nj, 1);
        e.fillImageData.setSpacing(di, dj, 1);
        e.fillImageData.setOrigin(0, 0, 0.3);
        e.fillImageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'mask', values: Float32Array.from(mask2d), numberOfComponents: 1 }));
        e.fillImageData.modified();
        e.fillActor.setVisibility(vis);
      }
    }
  }

  // ── Auto-segmentation (HU threshold → contours via marching squares) ──
  const segActors = {};  // name -> { actor, mapper, polyData, visible, cls }

  // True only when the voxel values look like Hounsfield units (CT): integer
  // data with air values below -500. MR intensity and PET SUV don't qualify.
  function looksLikeHU() {
    if (!state.loaded || !state.data || state.floatData) return false;
    const step = Math.max(1, Math.floor(state.data.length / 50000));
    let mn = 32767;
    for (let i = 0; i < state.data.length; i += step) {
      const v = state.data[i];
      if (v < mn) mn = v;
    }
    return mn < -500;
  }

  // Auto-segmentation as named volumes: each HU window becomes a 3D mask
  // volume in the volumes tree (toggle / colour / name / stats). Returns -1 when
  // the data is not HU-like, otherwise the number of classes built.
  function erodeBinary(mask, ni, nj) {
    const out = new Uint8Array(ni * nj);
    for (let j = 0; j < nj; j++) {
      for (let i = 0; i < ni; i++) {
        const idx = j * ni + i;
        if (!mask[idx]) continue;
        const keep = (i > 0 ? mask[idx - 1] : 0) && (i < ni - 1 ? mask[idx + 1] : 0) &&
                     (j > 0 ? mask[idx - ni] : 0) && (j < nj - 1 ? mask[idx + ni] : 0);
        if (keep) out[idx] = 1;
      }
    }
    return out;
  }

  function largestComponent(mask, ni, nj) {
    const labels = new Int32Array(ni * nj).fill(-1);
    const sizes = [];
    for (let j = 0; j < nj; j++) {
      for (let i = 0; i < ni; i++) {
        const idx = j * ni + i;
        if (!mask[idx] || labels[idx] >= 0) continue;
        const q = [idx];
        labels[idx] = sizes.length;
        let size = 0;
        while (q.length) {
          const cur = q.pop();
          size++;
          const x = cur % ni, y = (cur / ni) | 0;
          if (x > 0 && mask[cur - 1] && labels[cur - 1] < 0) { labels[cur - 1] = labels[idx]; q.push(cur - 1); }
          if (x < ni - 1 && mask[cur + 1] && labels[cur + 1] < 0) { labels[cur + 1] = labels[idx]; q.push(cur + 1); }
          if (y > 0 && mask[cur - ni] && labels[cur - ni] < 0) { labels[cur - ni] = labels[idx]; q.push(cur - ni); }
          if (y < nj - 1 && mask[cur + ni] && labels[cur + ni] < 0) { labels[cur + ni] = labels[idx]; q.push(cur + ni); }
        }
        sizes.push(size);
      }
    }
    let maxL = -1, maxS = 0;
    for (let l = 0; l < sizes.length; l++) if (sizes[l] > maxS) { maxS = sizes[l]; maxL = l; }
    const out = new Uint8Array(ni * nj);
    if (maxL >= 0) for (let i = 0; i < ni * nj; i++) if (labels[i] === maxL) out[i] = 1;
    return out;
  }

  // Keep only connected components of a 3D mask that are >= minSize voxels
  // (6-connectivity). Removes small isolated islands: calcifications, noise,
  // air bubbles — while preserving large structures (bone, organs).
  function keepLargeComponents3D(mask, nx, ny, nz, minSize) {
    const total = nx * ny * nz;
    const labels = new Int32Array(total).fill(-1);
    const sizes = [];
    const NXY = nx * ny;
    for (let i = 0; i < total; i++) {
      if (!mask[i] || labels[i] >= 0) continue;
      const q = [i];
      labels[i] = sizes.length;
      let size = 0;
      while (q.length) {
        const cur = q.pop();
        size++;
        const iz = (cur / NXY) | 0;
        const rem = cur - iz * NXY;
        const iy = (rem / nx) | 0;
        const ix = rem - iy * nx;
        if (ix > 0 && mask[cur - 1] && labels[cur - 1] < 0) { labels[cur - 1] = labels[i]; q.push(cur - 1); }
        if (ix < nx - 1 && mask[cur + 1] && labels[cur + 1] < 0) { labels[cur + 1] = labels[i]; q.push(cur + 1); }
        if (iy > 0 && mask[cur - nx] && labels[cur - nx] < 0) { labels[cur - nx] = labels[i]; q.push(cur - nx); }
        if (iy < ny - 1 && mask[cur + nx] && labels[cur + nx] < 0) { labels[cur + nx] = labels[i]; q.push(cur + nx); }
        if (iz > 0 && mask[cur - NXY] && labels[cur - NXY] < 0) { labels[cur - NXY] = labels[i]; q.push(cur - NXY); }
        if (iz < nz - 1 && mask[cur + NXY] && labels[cur + NXY] < 0) { labels[cur + NXY] = labels[i]; q.push(cur + NXY); }
      }
      sizes.push(size);
    }
    const out = new Uint8Array(total);
    for (let i = 0; i < total; i++) if (labels[i] >= 0 && sizes[labels[i]] >= minSize) out[i] = 1;
    return out;
  }

  function segmentVolumes(classes) {
    if (!looksLikeHU()) return -1;
    const [nx, ny, nz] = state.dims;
    const src = state.data;
    const total = nx * ny * nz;
    const bodyThreshold = -400;
    const masks = {};
    for (const cls of (classes || [])) masks[cls.name] = new Uint8Array(total);
    for (let z = 0; z < nz; z++) {
      const base = z * nx * ny;
      const body = new Uint8Array(nx * ny);
      for (let i = 0; i < nx * ny; i++) if (src[base + i] >= bodyThreshold) body[i] = 1;
      // Erode to detach the couch/table, keep the largest component (the patient).
      const bodyMask = largestComponent(erodeBinary(body, nx, ny), nx, ny);
      // outside = non-body voxels reachable from the border (external air + couch).
      const outside = new Uint8Array(nx * ny);
      const q = [];
      const enq = (idx) => { if (!bodyMask[idx] && !outside[idx]) { outside[idx] = 1; q.push(idx); } };
      for (let i = 0; i < nx; i++) { enq(i); enq((ny - 1) * nx + i); }
      for (let j = 0; j < ny; j++) { enq(j * nx); enq(j * nx + nx - 1); }
      while (q.length) {
        const cur = q.pop();
        const x = cur % nx, y = (cur / nx) | 0;
        if (x > 0) enq(cur - 1);
        if (x < nx - 1) enq(cur + 1);
        if (y > 0) enq(cur - nx);
        if (y < ny - 1) enq(cur + nx);
      }
      for (let idx = 0; idx < nx * ny; idx++) {
        const v = src[base + idx];
        const inBody = bodyMask[idx] === 1;
        const enclosed = !inBody && outside[idx] === 0;
        for (const cls of classes) {
          if (v < cls.min || v >= cls.max) continue;
          const airLike = cls.max <= bodyThreshold;
          if (airLike ? enclosed : inBody) masks[cls.name][base + idx] = 1;
        }
      }
    }
    // Remove small isolated components from bone (calcifications / noise);
    // the threshold (500 voxels) is tunable via the function argument.
    for (const cls of classes) {
      if (cls.min >= 300 && masks[cls.name]) {
        masks[cls.name] = keepLargeComponents3D(masks[cls.name], nx, ny, nz, 500);
      }
    }
    let built = 0;
    for (const cls of classes) {
      const mask = masks[cls.name];
      const stats = computeMaskStats(mask);
      if (!stats || stats.count === 0) continue;
      const existing = state.volumes.find((v) => v.name === cls.name);
      if (existing) {
        existing.mask = mask; existing.stats = stats; existing.visible = true;
      } else {
        state.volumes.push({
          id: ++state.volumeIdSeq,
          name: cls.name,
          color: cls.color || VOLUME_COLORS[state.volumes.length % VOLUME_COLORS.length],
          mask, material: '', visible: true, stats,
        });
      }
      built++;
    }
    updateVolumeOverlay();
    updateVolume3D();
    renderAll();
    if (hooks.onVolumesChange) hooks.onVolumesChange(state.volumes);
    return built;
  }

  function setSegmentation(classes) {
    if (!looksLikeHU()) return false;  // HU thresholds only apply to CT-like data
    // Remove old seg actors (one per pane per class).
    for (const name of Object.keys(segActors)) {
      for (const key of Object.keys(VIEWS)) {
        try { panes[key].renderer.removeActor(segActors[name].panes[key].actor); } catch (e) { /* ignore */ }
      }
    }
    for (const k of Object.keys(segActors)) delete segActors[k];
    state.segClasses = classes || [];
    for (const cls of state.segClasses) {
      const panesMap = {};
      for (const key of Object.keys(VIEWS)) {
        const polyData = vtkPolyData.newInstance();
        const mapper = vtkMapper.newInstance();
        mapper.setInputData(polyData);
        const actor = vtkActor.newInstance();
        actor.setMapper(mapper);
        const c = cls.color || [1, 1, 1];
        actor.getProperty().setColor(c[0], c[1], c[2]);
        actor.getProperty().setLineWidth(1.5);
        actor.getProperty().setLighting(false);
        panes[key].renderer.addActor(actor);
        panesMap[key] = { actor, polyData };
      }
      segActors[cls.name] = { panes: panesMap, visible: true, cls };
    }
    updateSegOverlay();
    renderAll();
    return true;
  }

  function updateSegOverlay(keyArg) {
    if (!looksLikeHU()) return;  // HU thresholds only apply to CT-like data
    const src = state.data;
    const keys = keyArg ? [keyArg] : Object.keys(VIEWS);
    for (const cls of state.segClasses) {
      const entry = segActors[cls.name];
      if (!entry) continue;
      for (const key of keys) {
        const p = panes[key];
        const v = p.view;
        const ni = state.dims[v.i];
        const nj = state.dims[v.j];
        const di = state.spacing[v.i];
        const dj = state.spacing[v.j];
        const mask = extractSliceMask(src, v, p.index, (val) => val >= cls.min && val <= cls.max);
        const paneEntry = entry.panes[key];
        if (setContourLines(paneEntry.polyData, mask, ni, nj, di, dj, 0.5)) {
          paneEntry.actor.setVisibility(entry.visible);
        } else {
          paneEntry.actor.setVisibility(false);
        }
      }
    }
  }

  function setSegVisible(name, visible) {
    const entry = segActors[name];
    if (entry) {
      entry.visible = !!visible;
      updateSegOverlay();
      renderAll();
    }
  }

  // ── RTDOSE isodose overlay (color isolines at % of max dose) ──
  const ISODOSE_COLORS = [
    [1.0, 0.15, 0.15], [1.0, 0.55, 0.15], [1.0, 0.9, 0.15],
    [0.35, 0.85, 0.3], [0.2, 0.5, 1.0],
  ];
  const doseActors = {};  // levelIndex -> { panes: {key: {actor, polyData}}, visible }
  const doseWash = {};    // pane key -> { actor, imageData, ctf, pwf }

  // Extract the dose grid's slice for a pane (aligned with the CT grid).
  function extractDoseSlice(view, index) {
    const ni = state.dims[view.i];
    const nj = state.dims[view.j];
    const [nx, ny] = state.dims;
    const src = state.doseData;
    const out = new Float32Array(ni * nj);
    const xyz = [0, 0, 0];
    xyz[view.k] = index;
    for (let j = 0; j < nj; j++) {
      xyz[view.j] = j;
      for (let i = 0; i < ni; i++) {
        xyz[view.i] = i;
        out[j * ni + i] = src[(xyz[2] * ny + xyz[1]) * nx + xyz[0]];
      }
    }
    if (view.flipH || view.flipV) applyMirror(out, ni, nj, view.flipH, view.flipV);
    return out;
  }

  function buildDoseWash() {
    for (const key of Object.keys(VIEWS)) {
      if (doseWash[key]) continue;
      const imageData = vtkImageData.newInstance();
      imageData.setDimensions(1, 1, 1);
      imageData.setSpacing(1, 1, 1);
      imageData.setOrigin(0, 0, 0.4);
      imageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'dose', values: new Float32Array([0]), numberOfComponents: 1 }));
      imageData.modified();
      const mapper = vtkImageMapper.newInstance();
      mapper.setInputData(imageData);
      const actor = vtkImageSlice.newInstance();
      actor.setMapper(mapper);
      const ctf = vtkColorTransferFunction.newInstance();
      ctf.addRGBPoint(0.0, 0.1, 0.1, 0.9);
      ctf.addRGBPoint(0.25, 0.0, 0.7, 1.0);
      ctf.addRGBPoint(0.5, 0.0, 1.0, 0.2);
      ctf.addRGBPoint(0.75, 1.0, 1.0, 0.0);
      ctf.addRGBPoint(1.0, 1.0, 0.0, 0.0);
      const pwf = vtkPiecewiseFunction.newInstance();
      pwf.addPoint(0.0, 0.0);
      pwf.addPoint(1.0, 0.55);
      const prop = vtkImageProperty.newInstance();
      prop.setRGBTransferFunction(ctf);
      prop.setScalarOpacity(pwf);
      prop.setUseLookupTableScalarRange(true);
      prop.setInterpolationTypeToLinear();
      actor.setProperty(prop);
      panes[key].renderer.addActor(actor);
      doseWash[key] = { actor, imageData, ctf, pwf };
    }
  }

  function updateDoseWashForKey(key) {
    const w = doseWash[key];
    if (!w || !state.doseLoaded) return;
    const p = panes[key];
    const v = p.view;
    const ni = state.dims[v.i];
    const nj = state.dims[v.j];
    const di = state.spacing[v.i];
    const dj = state.spacing[v.j];
    const slice = extractDoseSlice(v, p.index);
    const m = state.doseMaxGy || 1;
    const norm = new Float32Array(slice.length);
    for (let i = 0; i < slice.length; i++) norm[i] = Math.min(1, Math.max(0, slice[i] / m));
    w.imageData.setDimensions(ni, nj, 1);
    w.imageData.setSpacing(di, dj, 1);
    w.imageData.setOrigin(0, 0, 0.4);
    w.imageData.getPointData().setScalars(vtkDataArray.newInstance({ name: 'dose', values: norm, numberOfComponents: 1 }));
    w.imageData.modified();
    w.actor.setVisibility(true);
  }

  function setDoseGrid(dose) {
    // Remove existing dose actors.
    for (const li of Object.keys(doseActors)) {
      for (const key of Object.keys(VIEWS)) {
        try { panes[key].renderer.removeActor(doseActors[li].panes[key].actor); } catch (e) { /* ignore */ }
      }
      delete doseActors[li];
    }
    state.isodoseLevels = null;
    state.isodoseFractions = null;
    state.doseData = null;
    if (!dose || !dose.doseGy || dose.doseGy.length === 0) { state.doseLoaded = false; renderAll(); return []; }

    const gy = dose.doseGy;
    const expected = state.dims[0] * state.dims[1] * state.dims[2];
    if (expected > 0 && gy.length !== expected) { state.doseLoaded = false; renderAll(); return []; }
    state.doseData = gy;
    let maxGy = 0;
    for (let i = 0; i < gy.length; i++) { const v = gy[i]; if (v > maxGy) maxGy = v; }
    if (maxGy <= 0) { renderAll(); return []; }
    const fractions = [0.98, 0.9, 0.8, 0.6, 0.4, 0.2, 0.1, 0.05];
    const levels = fractions.map((f) => f * maxGy);
    console.log('[mpr] setDoseGrid: maxGy=' + maxGy + ' n=' + gy.length + ' levels=' + levels.map((l) => l.toExponential(2)).join(','));
    state.isodoseLevels = levels;
    state.isodoseFractions = fractions;
    state.doseLoaded = true;
    state.doseMaxGy = maxGy;

    // Auto-centre the views on the hottest voxel so a (possibly narrow) dose
    // distribution is immediately visible instead of sitting on another slice.
    let maxIdx = 0;
    for (let i = 1; i < gy.length; i++) if (gy[i] > gy[maxIdx]) maxIdx = i;
    const nxy = state.dims[0] * state.dims[1];
    const mz = Math.floor(maxIdx / nxy);
    const my = Math.floor((maxIdx - mz * nxy) / state.dims[0]);
    const mx = maxIdx - mz * nxy - my * state.dims[0];
    if (panes.axial) panes.axial.index = mz;
    if (panes.coronal) panes.coronal.index = my;
    if (panes.sagittal) panes.sagittal.index = mx;

    buildDoseWash();

    for (let li = 0; li < levels.length; li++) {
      const panesMap = {};
      for (const key of Object.keys(VIEWS)) {
        const polyData = vtkPolyData.newInstance();
        const mapper = vtkMapper.newInstance();
        mapper.setInputData(polyData);
        const actor = vtkActor.newInstance();
        actor.setMapper(mapper);
        const c = ISODOSE_COLORS[li % ISODOSE_COLORS.length];
        actor.getProperty().setColor(c[0], c[1], c[2]);
        actor.getProperty().setLineWidth(1.5);
        actor.getProperty().setLighting(false);
        panes[key].renderer.addActor(actor);
        panesMap[key] = { actor, polyData };
      }
      doseActors[li] = { panes: panesMap, visible: true };
    }
    updateDoseOverlay();
    renderAll();
    return levels.map((gyVal, i) => ({
      gy: gyVal, fraction: fractions[i], color: ISODOSE_COLORS[i % ISODOSE_COLORS.length],
    }));
  }

  function updateDoseOverlay(keyArg) {
    if (!state.loaded || !state.isodoseLevels) {
      // Nothing to draw: hide any leftover dose actors.
      for (const li of Object.keys(doseActors)) {
        for (const key of Object.keys(VIEWS)) {
          try { doseActors[li].panes[key].actor.setVisibility(false); } catch (e) { /* ignore */ }
        }
      }
      for (const key of Object.keys(VIEWS)) {
        if (doseWash[key]) { try { doseWash[key].actor.setVisibility(false); } catch (e) { /* ignore */ } }
      }
      return;
    }
    const keys = keyArg ? [keyArg] : Object.keys(VIEWS);
    for (const key of keys) updateDoseWashForKey(key);
    for (let li = 0; li < state.isodoseLevels.length; li++) {
      const entry = doseActors[li];
      if (!entry) continue;
      const level = state.isodoseLevels[li];
      for (const key of keys) {
        const p = panes[key];
        const v = p.view;
        const ni = state.dims[v.i];
        const nj = state.dims[v.j];
        const di = state.spacing[v.i];
        const dj = state.spacing[v.j];
        const slice = extractDoseSlice(v, p.index);
        const segs = marchingSquaresIsoline(slice, ni, nj, level);
        const paneEntry = entry.panes[key];
        if (segs.length === 0) { paneEntry.actor.setVisibility(false); continue; }
        const points = [];
        const cells = [];
        let off = 0;
        for (let k = 0; k < segs.length; k += 4) {
          points.push(segs[k] * di, segs[k + 1] * dj, 0.5);
          points.push(segs[k + 2] * di, segs[k + 3] * dj, 0.5);
          cells.push(2, off, off + 1);
          off += 2;
        }
        const pts = vtkPoints.newInstance();
        pts.setData(Float32Array.from(points), 3);
        paneEntry.polyData.setPoints(pts);
        const ca = vtkCellArray.newInstance();
        ca.setData(Uint32Array.from(cells));
        paneEntry.polyData.setLines(ca);
        paneEntry.polyData.modified();
        paneEntry.actor.setVisibility(entry.visible);
      }
    }
  }

  function setDoseLevelVisible(levelIndex, visible) {
    const entry = doseActors[levelIndex];
    if (entry) {
      entry.visible = !!visible;
      updateDoseOverlay();
      renderAll();
    }
  }

  // ── RTPLAN beams (central-axis lines in the 3D volume pane) ──
  const beamActors = [];  // { actor }
  let beamsVisible = true;

  function beamDirection(gantryDeg, couchDeg) {
    const g = (gantryDeg || 0) * Math.PI / 180;
    const c = (couchDeg || 0) * Math.PI / 180;
    // Source-to-isocenter direction for gantry angle (IEC): (sin g, 0, -cos g),
    // then couch rotation about z (matches RTPlan::BeamDirection).
    const bx = Math.sin(g), by = 0, bz = -Math.cos(g);
    return [bx * Math.cos(c) - by * Math.sin(c),
            bx * Math.sin(c) + by * Math.cos(c),
            bz];
  }

  function setBeamsVisible(visible) {
    beamsVisible = !!visible;
    for (const entry of beamActors) entry.actor.setVisibility(beamsVisible);
    renderAll();
  }

  function setBeams(beams) {
    for (const entry of beamActors) {
      try { if (volumePane) volumePane.renderer.removeActor(entry.actor); } catch (e) { /* ignore */ }
    }
    beamActors.length = 0;
    if (!beams || !beams.length || !volumePane || !state.loaded) { renderAll(); return; }
    const [ox, oy, oz] = state.origin;
    console.log('[mpr] setBeams: n=' + beams.length + ' origin=' + ox + ',' + oy + ',' + oz);
    const L = 120;  // half-length of the drawn central-axis segment (mm)
    // Deduplicate: static IMRT fields repeat the same gantry/couch/isocenter in
    // every control point (tens of identical central axes), while VMAT arcs have
    // a different angle per point and must keep all of them.
    const seen = new Set();
    for (const beam of beams) {
      const cps = beam.controlPoints || [];
      for (const cp of cps) {
        const g = cp.gantryAngleDeg || 0;
        const c = cp.couchAngleDeg || 0;
        const iso = cp.isocenter || [0, 0, 0];
        console.log('[mpr] beam iso=' + iso.join(',') + ' g=' + g + ' c=' + c);
        const key = g.toFixed(2) + '|' + c.toFixed(2) + '|' + iso.map((v) => v.toFixed(2)).join(',');
        if (seen.has(key)) continue;
        seen.add(key);
        const dir = beamDirection(g, c);
        // Map LPS → renderer frame via the CT direction cosines. The vtk.js
        // volume is built without orientation, so the renderer's (x,y,z) are
        // (column, row, slice). For a standard axial CT column=+Y (posterior),
        // row=+X (left) — drawing the beam as (X,Y,Z) would rotate it 90°.
        const dc = (state.direction && state.direction.length >= 9) ? state.direction : [1, 0, 0, 0, 1, 0, 0, 0, 1];
        const rowD = [dc[0], dc[1], dc[2]];
        const colD = [dc[3], dc[4], dc[5]];
        const slcD = [dc[6], dc[7], dc[8]];
        const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        const off = [iso[0] - ox, iso[1] - oy, iso[2] - oz];
        const px = dot(off, colD), py = dot(off, rowD), pz = dot(off, slcD);
        const ddx = dot(dir, colD), ddy = dot(dir, rowD), ddz = dot(dir, slcD);
        const line = vtkLineSource.newInstance();
        line.setPoint1(px - ddx * L, py - ddy * L, pz - ddz * L);
        line.setPoint2(px + ddx * L, py + ddy * L, pz + ddz * L);
        const mapper = vtkMapper.newInstance();
        mapper.setInputConnection(line.getOutputPort());
        const actor = vtkActor.newInstance();
        actor.setMapper(mapper);
        actor.getProperty().setColor(0.9, 0.3, 0.95);
        actor.getProperty().setLineWidth(2);
        actor.getProperty().setLighting(false);
        actor.setVisibility(beamsVisible);
        volumePane.renderer.addActor(actor);
        beamActors.push({ actor });
      }
    }
    renderAll();
  }

  function getDvh(roiNumber) {
    if (!state.doseLoaded || !state.doseData) return null;
    const mask = state.roiMasks[roiNumber];
    if (!mask) return null;
    return computeDvh(state.doseData, mask, state.dims, state.spacing);
  }

  function getRoiStats(roiNumber) {
    if (!state.loaded || !state.data) return null;
    const mask = state.roiMasks[roiNumber];
    if (!mask) return null;
    const n = state.dims[0] * state.dims[1] * state.dims[2];
    let count = 0, sum = 0, min = Infinity, max = -Infinity;
    for (let i = 0; i < n; i++) {
      if (mask[i]) {
        const v = state.data[i];
        sum += v; count++;
        if (v < min) min = v;
        if (v > max) max = v;
      }
    }
    if (count === 0) return null;
    const mean = sum / count;
    let s2 = 0;
    for (let i = 0; i < n; i++) {
      if (mask[i]) { const d = state.data[i] - mean; s2 += d * d; }
    }
    const stdev = Math.sqrt(s2 / count);
    const dvCc = (state.spacing[0] * state.spacing[1] * state.spacing[2]) / 1000;
    return { count, volumeCc: count * dvCc, min, max, mean, stdev };
  }

  function getHistogram() {
    if (!state.loaded || !state.data) return null;
    // If a closed measurement region is active, histogram only its pixels.
    const t = state.measureType;
    const pane = state.measurePane ? panes[state.measurePane] : null;
    const pts = (state.measurePane && state.measurePoints[state.measurePane]) || [];
    let vals = null;
    if (pane && pane.sliceData && (t === 'rectangle' || t === 'ellipse' || t === 'polygon' || t === 'curve' || t === 'freehand')) {
      const need = (t === 'rectangle' || t === 'ellipse') ? 2 : 3;
      if (pts.length >= need) {
        const v = pane.view;
        const ni = state.dims[v.i], nj = state.dims[v.j];
        const inside = makeInsideTest(t, pts);
        const arr = [];
        for (let r = 0; r < nj; r++) for (let c = 0; c < ni; c++) {
          if (inside((c + 0.5) * state.spacing[v.i], (r + 0.5) * state.spacing[v.j])) arr.push(pane.sliceData[r * ni + c]);
        }
        if (arr.length) vals = arr;
      }
    }
    if (!vals) { const iz = Math.floor(state.dims[2] / 2); vals = Array.from(extractSlice(VIEWS.axial, iz)); }
    const n = vals.length;
    if (!n) return null;
    let min = Infinity, max = -Infinity;
    for (let i = 0; i < n; i++) { const v = vals[i]; if (v < min) min = v; if (v > max) max = v; }
    if (max <= min) return null;
    const NBINS = 128;
    const bins = new Uint32Array(NBINS);
    for (let i = 0; i < n; i++) {
      const b = Math.min(NBINS - 1, Math.max(0, Math.floor((vals[i] - min) / (max - min) * NBINS)));
      bins[b]++;
    }
    return { min, max, count: n, bins: Array.from(bins) };
  }

  function getThumbnail() {
    if (!state.loaded) return null;
    const iz = Math.floor(state.dims[2] / 2);
    return { w: state.dims[0], h: state.dims[1], data: extractSlice(VIEWS.axial, iz) };
  }

  function getState() {
    return {
      loaded: state.loaded,
      dims: state.dims.slice(),
      spacing: state.spacing.slice(),
      window: state.window,
      level: state.level,
      doseLoaded: state.doseLoaded,
      slabMm: state.slabMm,
      oblique: { azimuthDeg: state.oblique.azimuthDeg, elevationDeg: state.oblique.elevationDeg },
      slices: {
        axial: panes.axial.index,
        coronal: panes.coronal.index,
        sagittal: panes.sagittal.index,
      },
    };
  }

  // Layout selector (Weasis-style): switch between the 2×2 MPR layout and a
  // single full-size projection (axial / coronal / sagittal / 3D).
  function applyLayout(mode) {
    const grid = container.querySelector('.mpr-grid');
    if (!grid) return;
    state.layout = mode;
    const single = mode !== 'mpr';
    grid.style.gridTemplateColumns = single ? '1fr' : '1fr 1fr';
    grid.style.gridTemplateRows = single ? '1fr' : '1fr 1fr';
    for (const key of Object.keys(VIEWS)) {
      const p = panes[key];
      if (p.paneDiv) p.paneDiv.style.display = (!single || mode === key) ? 'flex' : 'none';
    }
    if (volumePane && volumePane.paneDiv) {
      volumePane.paneDiv.style.display = (!single || mode === '3d') ? 'flex' : 'none';
    }
    requestAnimationFrame(() => resize());
  }

  function resize() {
    for (const key of Object.keys(VIEWS)) {
      panes[key].glw.resize();
      if (state.loaded) { orientCamera(panes[key]); updateRulers(panes[key]); }
    }
    if (volumePane) {
      volumePane.glw.resize();
      if (state.loaded) orientVolumeCamera();
    }
    renderAll();
  }

  // Recompute the HTML overlay labels (ruler labels + zoom %) after the pane is
  // resized (window resize, fullscreen, relayout). Does NOT re-fit the camera,
  // so a user zoom survives.
  function refreshOverlayLabels() {
    if (!state.loaded) return;
    for (const key of Object.keys(VIEWS)) {
      const p = panes[key];
      const rect = p.canvasHost.getBoundingClientRect();
      if (rect.width < 1 || rect.height < 1) continue;
      updateRulers(p);
      refreshPaneInfo(p);   // also updates the zoom % via setOvBL
    }
    syncLens();   // keep the magnifier lens aligned on resize/fullscreen
  }

  // Re-fit the whole image into each pane (the same scale as on load).
  function fit() {
    if (!state.loaded) return;
    for (const key of Object.keys(VIEWS)) {
      panes[key].glw.resize();
      orientCamera(panes[key]);
      updateRulers(panes[key]);
      refreshPaneInfo(panes[key]);
    }
    if (volumePane) { volumePane.glw.resize(); orientVolumeCamera(); }
    renderAll();
  }

  // Keep the HTML overlay labels aligned when the container resizes. The vtk
  // canvas auto-resizes via `listenWindowResize`; these listeners keep the HTML
  // labels in sync (they are positioned from the camera + rect in worldToScreen).
  if (typeof ResizeObserver !== 'undefined') {
    const ro = new ResizeObserver(() => refreshOverlayLabels());
    for (const key of Object.keys(VIEWS)) ro.observe(panes[key].canvasHost);
  }
  const onWinResize = () => refreshOverlayLabels();
  const onFsChange = () => setTimeout(refreshOverlayLabels, 60);
  window.addEventListener('resize', onWinResize);
  document.addEventListener('fullscreenchange', onFsChange);

  // Keyboard shortcuts (Weasis-style), active only when the viewer is visible
  // and the focus is not in a text field.
  const onKeyDown = (e) => {
    if (!state.loaded) return;
    if (!container.getBoundingClientRect().width) return;   // tab hidden
    const tag = (e.target && e.target.tagName) || '';
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || e.target.isContentEditable) return;
    const k = e.key;
    const pane = panes[activePaneKey] || panes.axial;
    if (k === 'ArrowUp' || k === 'PageUp') { setSlice(activePaneKey, pane.index + 1); e.preventDefault(); }
    else if (k === 'ArrowDown' || k === 'PageDown') { setSlice(activePaneKey, pane.index - 1); e.preventDefault(); }
    else if (k === 'ArrowRight') { setSlice(activePaneKey, pane.index + 1); e.preventDefault(); }
    else if (k === 'ArrowLeft') { setSlice(activePaneKey, pane.index - 1); e.preventDefault(); }
    else if (k === 'f' || k === 'F') { fit(); e.preventDefault(); }
    else if (k === 'Enter') { if (state.measureMode && state.measureOpen) { finishMeasurement(); e.preventDefault(); } }
    else if (k === 'Escape') { if (state.measureMode && state.measureOpen) { cancelMeasurement(); e.preventDefault(); } }
    else if (k === 'Backspace') { if (state.measureMode && state.measureOpen) { removeLastVertex(); e.preventDefault(); } }
  };
  window.addEventListener('keydown', onKeyDown);

  function destroy() {
    try { window.removeEventListener('resize', onWinResize); } catch (e) { /* ignore */ }
    try { document.removeEventListener('fullscreenchange', onFsChange); } catch (e) { /* ignore */ }
    try { window.removeEventListener('keydown', onKeyDown); } catch (e) { /* ignore */ }
    for (const key of Object.keys(VIEWS)) {
      try { panes[key].glw.delete(); } catch (e) { /* ignore */ }
    }
    if (volumePane) {
      try { volumePane.glw.delete(); } catch (e) { /* ignore */ }
    }
    container.innerHTML = '';
  }

  return { load, reset, fit, setSlice, setWindowLevel, autoWindowLevel, setCrosshairsVisible, setSlabThickness, setProjectionMode, setZoom, setOblique, setRois, setRoiVisible, setRoiOpacity, setSegmentation, setSegVisible, setDoseGrid, setDoseLevelVisible, setBeams, setBeamsVisible, getDvh, getRoiStats, getHistogram, getThumbnail, setOverlayInfo, setVolumePreset, setLayout: applyLayout, setSeedMode, setPanMode, setInvert, setMagnifier, setMeasureMode, clearMeasurements, deleteMeasurement, setMeasurementVisible, selectMeasurement, setMeasurementName, setMeasurementColor, buildTumorFromContours, getMeasurements: () => state.measurements, finishMeasurement, resetWindowLevel, runGrow, clearGrow, getVolumes: () => state.volumes, deleteVolume, setVolumeVisible, setVolumeName, setVolumeColor, setVolumeMaterial, segmentVolumes, buildRtStructExport, buildTransferPayload, serializeState, restoreState, getState, resize, destroy };
}

export { decodeVolume, decodeInt16Volume } from './mpr-utils.js';

