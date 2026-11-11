//==============================================================================
// G4CARE - viewer.js — 3D viewer: scene, tools, UI, RPC wiring
//
// 3D viewer: scene, tools, UI, RPC wiring
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import '@kitware/vtk.js/Rendering/Profiles/Geometry';

import { fullScreen, renderer, renderWindow, interactor } from './context.js';
import vtkPointPicker from '@kitware/vtk.js/Rendering/Core/PointPicker';
import vtkLineSource from '@kitware/vtk.js/Filters/Sources/LineSource';
import vtkMapper from '@kitware/vtk.js/Rendering/Core/Mapper';
import vtkActor from '@kitware/vtk.js/Rendering/Core/Actor';
import vtkImageData from '@kitware/vtk.js/Common/DataModel/ImageData';
import vtkDataArray from '@kitware/vtk.js/Common/Core/DataArray';
import vtkImageMapper from '@kitware/vtk.js/Rendering/Core/ImageMapper';
import vtkImageSlice from '@kitware/vtk.js/Rendering/Core/ImageSlice';
import { DEFAULT_COLOR, Z_AXIS_SHAPES } from './render/sources.js';
import { DEG2RAD, normalize3, primBasis, orientationQuat, dot3, quatRotate, quatToEulerXYZ, mirrorQuaternion, quatInverse, cross3, quatFromAxisAngle } from './math3d.js';
import { actors, actorNameMap, baseColors, objectInfo, objectMeta, planeActorMap, multiSel, highlighted, IDENTITY_MATRIX, mergedMesh, gridState, materialsData, snap, view, section, primActorMap, measure, measureExtras, auxMarkers, localGrid, prims, primMarkers, planeBuild, expandedGroups, customLayers, annotations, dimensions, constraints, groups } from './state.js';
import { pushHistory, pushCommand, pushSnapshot, captureSceneState, globalUndo, globalRedo, setHistoryHooks } from './history.js';
import { postUpdate, postBatch, postGdml, dbg } from './api.js';
import { rgbCss, fmtNum, fmtVec } from './ui/format.js';
import { makeMarkerActor, removeMarker, makeGridActor, rebuildGrid, removeAllActors, computeSceneBounds } from './render/actors.js';
import { pickAt, highlightColorFor, setColorFor, setVolumeVisible, setVolumesVisible, applyHighlight, setPickingHooks } from './render/picking.js';
import { volumeMatrix, closestPointOnMesh, sampleMeshSurfacePoints, rayMeshIntersect } from './render/mesh-measure.js';
import { buildMergedCut, rebuildMergedMesh, setMeshCellColor, setMergedMeshHooks } from './render/mesh-merged.js';
import { placeLabelPoint, pickAnnotation, showAnnotationMenu, saveAnnotations, renderAnnotations, deleteAnnotation, loadAnnotations, setupAnnotations } from './tools/annotations.js';
import { btnStyle, infoRow, promptText, confirmAction, showToast } from './ui/dom.js';
import { checkboxInput, selectInput, textInput, vec3Input, rgbInput, opacityInput, numInput } from './ui/inputs.js';
import { setObjectPosition, setObjectRotation, setObjectMaterial, setObjectMother, setObjectName, setObjectType, setObjectSensitive, setObjectLayer, setObjectColor, setObjectOpacity, PRIMITIVE_TYPES, isGdmlObject, isReplicaObject, setObjectsHooks } from './panels/objects.js';
import { togglePrimMode, updatePrimHint, buildPrimActor, updatePrimMarkers, updatePrims, resolvePoint, addPrimPointAt, placePrimPoint, primSnapTargets, primCenterOf, setPrimPosition, setPrimRotation, primCenter, setupPrimitives, resetPrimMode } from './tools/primitives.js';
import { setupSnap, resetAuxMode, resetDeleteMode, toggleSnap, toggleAuxMode, toggleDeleteMode, updateAuxMarkers, addAuxPointAt, placeAuxPoint, deletePointAt, screenDist, collectSnapTargets, faceCentersOf, edgeMidpointsOf, collectSegments, segmentIntersections, tangentPoints, snapPoint, snapToVertex } from './tools/snap.js';
import { setupMeasure, clearRuler, addMeasurePointAt, placeMeasurePoint, setMeasureMode, toggleMeasure, clearMeasureExtras, makeMeasureLine, toggleMeasureSubMode, updateMeasureSubBtnUI, placeAnglePoint, placeMindistPick, computeMinDistance, placeThicknessPick } from './tools/measure.js';
import { setupSection, activePlane, surfaceNormalAt, handlePlaneBuildClick, updateActorMappers, rebuildPlaneListUI, updatePlaneVisuals, clearSectionPlanes, setSectionEnabled, addSectionPlane, deletePlane, removeActivePlane, toggleActiveCut, syncActiveSlider, flipActivePlane, setActivePosition, setPlaneMode, resetPlaneBuild, updatePlaneBuildMarkers, clearPlaneBuildMarkers, addPlaneWithOrigin, commitPlaneBuild, primPlane, tryCommitPrims, makeAnglePlane, isCutObject, updateSectionPlanes, syncCutBtn, planeHalfSize, buildPlaneGeometry, visibleSectionPlanes, sceneMarkerRadius, updatePickMarkers, planeVisualActors, updateFaceHover } from './tools/section.js';
import { loadMaterials, exportGdmlMaterial, exportAllGdmlMaterials, openMaterialDialog, setMaterialsRefreshUi } from './scene/materials.js';
import { loadGeometry, pollGeometrySig, pollSideChannels, setSceneHooks, loadGroups } from './scene/io.js';
import { axesWidget, displayCoord, isInWidget, axisAt, orientView, setHoveredAxis, updateAxisLabels, applyRepresentation, setViewFrom, fitView, setCursor, projectToGridPlane } from './tools/view.js';
import { setupSources, loadSources, clearSources, sourceHitForActor, getSourcePos, setSourceMarkerPos, deleteSource, openSourceEditor, buildGpsMacro } from './tools/sources.js';
import { openDigitizeEditor, renderDigitizeSummary } from './tools/digitize.js';
import { setupDimensions, setDimMode, resetDimMode, handleDimClick, clearDimensions, dimensionHitAt, editDimensionValue, removeDimension, primRemoved as dimensionPrimRemoved } from './tools/dimensions.js';
import { setupConstraints, setConstraintMode, resetConstraintMode, handleConstraintClick, removeConstraint, constraintLabel, clearConstraints, primRemoved as constraintPrimRemoved, objectRemoved as constraintObjectRemoved } from './tools/constraints.js';
import { icon, iconHtml } from '../../gui/src/icons/icon.js';
import { initI18n, t, setLocale, bindI18n, onLocaleChange } from '../../gui/src/i18n/i18n.js';
import ru from '../../gui/src/i18n/ru.json';
import en from '../../gui/src/i18n/en.json';
import fr from '../../gui/src/i18n/fr.json';
import de from '../../gui/src/i18n/de.json';

initI18n({ ru, en, fr, de });
(function injectViewerCss() {
  if (document.getElementById('g4c-viewer-style')) return;
  const st = document.createElement('style');
  st.id = 'g4c-viewer-style';
  st.textContent = 'select,button,input,textarea{outline:none;} input[type=range]{accent-color:#e8752a;} select:focus,input:focus,textarea:focus{border-color:#e8752a !important;} select option{background:#202329;color:#e2e5ea;} select option:hover,select option:checked{background:#e8752a;color:#1a1a1a;} .csel{position:relative;display:inline-flex;align-items:center;} .csel-btn{background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 8px;font:12px Arial;display:inline-flex;align-items:center;gap:6px;cursor:pointer;width:100%;box-sizing:border-box;} .csel-btn:hover{background:#2a2d33;} .csel-caret{color:#777;font-size:10px;flex:none;} .csel-label{flex:1;min-width:0;text-align:left;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;} .csel-menu{display:none;position:fixed;background:#202329;border:1px solid #343941;border-radius:4px;z-index:100000;max-height:240px;overflow-y:auto;box-shadow:0 4px 12px rgba(0,0,0,0.4);} .csel-menu.open{display:block;} .csel-item{padding:5px 10px;cursor:pointer;font:12px Arial;color:#e2e5ea;white-space:nowrap;} .csel-item:hover{background:#2a2d33;} .csel-item.sel{background:#e8752a;color:#1a1a1a;}';
  document.head.appendChild(st);
})();

// Custom dropdown: replaces the native <select> popup (the browser's blue hover
// highlight on options cannot be styled), giving a neutral hover + orange pick.
function customSelect(sel) {
    if (sel.dataset.csel) return;
    sel.dataset.csel = '1';
    const css = (sel.getAttribute('style') || '').replace(/display\s*:[^;]+;?/gi, '');
    sel.style.display = 'none';
    const wrap = document.createElement('span');
    wrap.className = 'csel';
    wrap.style.cssText = css;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'csel-btn';
    const label = document.createElement('span');
    label.className = 'csel-label';
    const caret = document.createElement('span');
    caret.className = 'csel-caret';
    caret.textContent = '▾';
    btn.appendChild(label); btn.appendChild(caret);
    const menu = document.createElement('div');
    menu.className = 'csel-menu';
    wrap.appendChild(btn);
    menu._btn = btn;
    const syncLabel = () => { const o = sel.options[sel.selectedIndex]; label.textContent = o ? o.textContent : ''; };
    const close = () => { menu.classList.remove('open'); };
    const open = () => {
        menu.innerHTML = '';
        for (let i = 0; i < sel.options.length; i++) {
            const o = sel.options[i];
            const item = document.createElement('div');
            item.className = 'csel-item' + (o.selected ? ' sel' : '');
            item.textContent = o.textContent;
            item.addEventListener('pointerdown', (ev) => { ev.stopPropagation(); ev.preventDefault(); sel.value = o.value; sel.dispatchEvent(new Event('change', { bubbles: true })); close(); });
            menu.appendChild(item);
        }
        if (menu.parentNode !== document.body) document.body.appendChild(menu);
        const r = btn.getBoundingClientRect();
        menu.style.left = r.left + 'px';
        menu.style.top = (r.bottom + 2) + 'px';
        menu.style.minWidth = r.width + 'px';
        menu.classList.add('open');
        const mh = menu.offsetHeight;
        if (r.bottom + 2 + mh > window.innerHeight && r.top - mh - 2 > 0) {
            menu.style.top = (r.top - mh - 2) + 'px';
        }
    };
    btn.addEventListener('pointerdown', (ev) => { ev.stopPropagation(); ev.preventDefault(); if (menu.classList.contains('open')) close(); else open(); });
    const pd = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value');
    Object.defineProperty(sel, 'value', { get() { return pd.get.call(sel); }, set(v) { pd.set.call(sel, v); syncLabel(); } });
    sel.parentNode.insertBefore(wrap, sel);
    syncLabel();
}
function setupCustomSelects(root) { (root || document).querySelectorAll('select').forEach(customSelect); }
document.addEventListener('pointerdown', (ev) => {
    document.querySelectorAll('.csel-menu.open').forEach((m) => { const b = m._btn; if (b && !b.contains(ev.target) && !m.contains(ev.target)) m.classList.remove('open'); });
}, true);
window.addEventListener('scroll', () => { document.querySelectorAll('.csel-menu.open').forEach((m) => m.classList.remove('open')); }, true);
window.addEventListener('resize', () => { document.querySelectorAll('.csel-menu.open').forEach((m) => m.classList.remove('open')); });
setupCustomSelects(document);
let cselPending = false;
new MutationObserver(() => {
    if (cselPending) return;
    cselPending = true;
    requestAnimationFrame(() => { cselPending = false; setupCustomSelects(document); });
}).observe(document.documentElement, { childList: true, subtree: true });

window.addEventListener('message', (e) => { if (e.data && e.data.type === 'g4care-locale' && e.data.locale) { setLocale(e.data.locale); } });

// Report uncaught errors to /tmp/g4care_debug.log so we can see the actual
// runtime error (the viewer runs in an iframe; its console is not visible).
window.addEventListener('error', (e) => {
  try { dbg('VIEWER_ERROR ' + (e.message || '') + ' @ ' + (e.filename || '') + ':' + (e.lineno || '') + ':' + (e.colno || '')); } catch (_) { /* ignore */ }
});
window.addEventListener('unhandledrejection', (e) => {
  try { dbg('VIEWER_PROMISE_REJECTION ' + (e.reason && e.reason.message ? e.reason.message : String(e.reason))); } catch (_) { /* ignore */ }
});

// (bootstrap singletons live in ./context.js)

// (toast lives in ./ui/dom.js)

// (screen-space markers + actor helpers live in ./render/actors.js)

// (parametric shape sources + color constants live in ./render/sources.js)

// (orientation + vector/geometry math helpers live in ./math3d.js)

// (scene-state collections + view scalars live in ./state.js)

// (mesh / point-cloud builders live in ./render/mesh.js)

// ── orientation axes / camera / view controls live in ./tools/view.js ──

// Ctrl/Shift-click multi-select in the tree (delete-selection set). Holds
// "constraint:<id>" keys; a floating "Удалить (N)" button clears them.
const deleteSel = new Set();
function removeSelectedConstraints() {
  for (const key of [...deleteSel]) {
    if (!key.startsWith('constraint:')) continue;
    const id = key.slice('constraint:'.length);
    const c = constraints.find((x) => x.id === id);
    if (c) removeConstraint(c);
  }
  deleteSel.clear();
  updateTreeUI();
}

interactor.onAnimation(updateAxisLabels);
interactor.onEndAnimation(updateAxisLabels);
// Reposition the axis labels when the window is resized / toggled fullscreen
// (vtkFullScreenRenderWindow resizes the canvas, but the DOM axis labels are
// positioned manually and would otherwise stay at their old windowed coords).
window.addEventListener('resize', () => {
  requestAnimationFrame(() => requestAnimationFrame(updateAxisLabels));
});
// Throttle the clipping-range recompute: wheel zoom fires an end-animation per
// tick, and recomputing bounds (55k-tri mesh) on every tick hurts FPS.
let lastClipReset = 0;
interactor.onEndAnimation(() => {
  const now = performance.now();
  if (now - lastClipReset > 250) {
    lastClipReset = now;
    renderer.resetCameraClippingRange();
  }
});

// (picking + highlight logic lives in ./render/picking.js)

// (merged GDML mesh lives in ./render/mesh-merged.js)

// (rebuild/apply/setMeshCellColor live in ./render/mesh-merged.js)

// (visibility/highlight functions live in ./render/picking.js)

function doHover(x, y, hit) {
  if (isInWidget(x, y)) {
    setHoveredAxis(axisAt(x, y));
    if (view.hoveredName) { view.hoveredName = null; applyHighlight(); }
    return;
  }
  setHoveredAxis(null);
  const name = hit ? hit.name : null;
  if (name !== view.hoveredName) { view.hoveredName = name; applyHighlight(); }
}

function doSelect(x, y) {
  if (pendingPaste) {
    const pos = resolvePoint(x, y) || projectToGridPlane(x, y);
    if (pos) pasteClipboardAt(pos);
    pendingPaste = null;
    return;
  }
  if (view.sourcePlaceMode) {
    handleSourcePlaceClick(x, y);
    return;
  }
  if (snap.deleteMode) {
    deletePointAt(x, y);
    return;
  }
  if (view.primMode) {
    placePrimPoint(x, y);
    return;
  }
  if (snap.auxMode) {
    placeAuxPoint(x, y);
    return;
  }
  if (measure.active) {
    placeMeasurePoint(x, y);
    return;
  }
  if (view.angleMode) {
    placeAnglePoint(x, y);
    return;
  }
  if (view.mindistMode) {
    placeMindistPick(x, y);
    return;
  }
  if (view.thicknessMode) {
    placeThicknessPick(x, y);
    return;
  }
  if (planeBuild.mode) {
    handlePlaneBuildClick(x, y);
    return;
  }
  if (view.labelMode) {
    placeLabelPoint(x, y);
    return;
  }
  if (view.alignMode) {
    handleAlignClick(x, y);
    return;
  }
  if (view.dimMode) {
    handleDimClick(x, y);
    return;
  }
  if (view.constraintMode) {
    handleConstraintClick(x, y);
    return;
  }
  if (isInWidget(x, y)) {
    const a = axisAt(x, y);
    if (a !== null) orientView(a);
    return;
  }
  if (section.scope === 'local' && section.enabled) {
    const hit = pickAt(x, y);
    if (hit) {
      section.localTarget = hit.name;
      updateActorMappers();
      applyHighlight();
    }
    return;
  }
  if (section.selective) {
    const hit = pickAt(x, y);
    if (hit) {
      if (section.cutSet.has(hit.name)) section.cutSet.delete(hit.name);
      else section.cutSet.add(hit.name);
      updateActorMappers();
      applyHighlight();
    }
    return;
  }
  const hit = pickAt(x, y);
  view.selectedName = hit ? hit.name : null;
  view.selectedAuxIndex = -1;
  view.selectedPrimIndex = (hit && hit.primIndex !== undefined) ? hit.primIndex : -1;
  view.selectedAnnotationIndex = -1;
  view.selectedSourceName = null;
  // Nautilus: a plain left-click selects only the clicked object (clears multi-selection);
  // clicking empty space clears the selection.
  if (hit && hit.name && objectMeta.has(hit.name)) {
    multiSel.clear();
    multiSel.add(hit.name);
    selAnchor = 'obj:' + hit.name;
  } else if (!hit) {
    multiSel.clear();
    selAnchor = null;
  }
  applySelectionHighlight();
  dbg('SEL doSelect hit=' + (hit ? (hit.name || hit.primIndex) : 'null') + ' sel=' + JSON.stringify([...multiSel]));
  applyHighlight();
  updateProps();
}

// ── measure ruler (vertex snapping + distance label) ──
const pointPicker = vtkPointPicker.newInstance();

// The vtk.js interactor binds to the full-screen container and calls
// setPointerCapture() on every pointerdown. That retargets subsequent
// pointer/click events to the container, which breaks native HTML controls
// (<select>, <details>, checkboxes) inside UI panels. Shield [data-ui] roots
// so their native controls keep working (toolbar buttons still fire first).
function shieldUi(el) {
  if (!el) return;
  for (const type of ['pointerdown', 'pointerup', 'wheel', 'contextmenu', 'dblclick', 'keydown', 'keypress', 'keyup']) {
    el.addEventListener(type, (e) => e.stopPropagation());
  }
}

const toolbar = document.createElement('div');
toolbar.setAttribute('data-ui', '');
toolbar.style.cssText = 'position:absolute;top:0;left:0;right:0;z-index:9999;display:flex;flex-direction:column;background:#131519;border-bottom:1px solid #343941;';
fullScreen.getContainer().appendChild(toolbar);
shieldUi(toolbar);

// ── Ribbon: functional sections; only the active section's tools are shown. ──
const RIBBON_SECTIONS = [
  ['create', 'ribbon.create'],
  ['transform', 'ribbon.transform'],
  ['deps', 'ribbon.deps'],
  ['measure', 'ribbon.measure'],
  ['sections', 'ribbon.sections'],
  ['sources', 'ribbon.sources'],
  ['export', 'ribbon.export'],
];
const DUP_LABELS = new Set(['group.source', 'group.measure', 'group.deps', 'group.section', 'group.export']);
const GROUP_SECTION = {
  'group.object': 'transform',
  'group.add': 'create',
  'group.placement': 'deps',
  'group.mirror': 'transform',
  'group.source': 'sources',
  'group.measure': 'measure',
  'group.planes': 'sections',
  'group.section': 'sections',
  'group.deps': 'deps',
  'group.export': 'export',
};

const ribbonTabsBar = document.createElement('div');
ribbonTabsBar.style.cssText = 'display:flex;gap:2px;padding:4px 6px 0;overflow-x:auto;';
toolbar.appendChild(ribbonTabsBar);

const ribbonSectionPanes = {};
const ribbonTabButtons = {};
let activeRibbonSection = 'create';
let ribbonCollapsed = false;
function activateSection(id) {
  activeRibbonSection = id;
  Object.keys(ribbonSectionPanes).forEach((k) => { ribbonSectionPanes[k].style.display = (k === id && !ribbonCollapsed) ? 'flex' : 'none'; });
  Object.keys(ribbonTabButtons).forEach((k) => {
    const active = k === id;
    ribbonTabButtons[k].style.background = active ? '#e8752a' : '#3a3a3a';
    ribbonTabButtons[k].style.color = active ? '#1a1a1a' : '#ccc';
    ribbonTabButtons[k].style.borderColor = active ? '#d97a3a' : '#555';
  });
}
RIBBON_SECTIONS.forEach(([id, label]) => {
  const btn = document.createElement('button');
  bindI18n(btn, label);
  btn.style.cssText = 'background:#202329;color:#969da8;border:1px solid #343941;border-radius:4px;padding:3px 10px;cursor:pointer;font:12px Arial;';
  btn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); activateSection(id); });
  ribbonTabsBar.appendChild(btn);
  ribbonTabButtons[id] = btn;
  const pane = document.createElement('div');
  pane.style.cssText = 'display:none;gap:8px;align-items:flex-end;flex-wrap:nowrap;overflow-x:auto;padding:4px 8px 5px;';
  ribbonSectionPanes[id] = pane;
  toolbar.appendChild(pane);
});
activateSection('create');

const ribbonCollapseBtn = document.createElement('button');
ribbonCollapseBtn.textContent = '▴';
ribbonCollapseBtn.setAttribute('data-i18n-title', 'tip.collapseRibbon'); ribbonCollapseBtn.title = t('tip.collapseRibbon');
onLocaleChange(() => { ribbonCollapseBtn.setAttribute('data-i18n-title', 'tip.collapseRibbon'); ribbonCollapseBtn.title = t('tip.collapseRibbon'); });
ribbonCollapseBtn.style.cssText = 'margin-left:auto;background:#202329;color:#969da8;border:1px solid #343941;border-radius:4px;padding:3px 8px;cursor:pointer;font:12px Arial;flex:none;';
ribbonCollapseBtn.addEventListener('pointerdown', (e) => {
  e.stopPropagation(); e.preventDefault();
  ribbonCollapsed = !ribbonCollapsed;
  ribbonCollapseBtn.textContent = ribbonCollapsed ? '▾' : '▴';
  activateSection(activeRibbonSection);
  if (typeof syncLayout === 'function') syncLayout();
});
ribbonTabsBar.appendChild(ribbonCollapseBtn);

function addGroup(key) {
  const g = document.createElement('div');
  g.style.cssText = 'display:flex;flex-direction:column;align-items:center;gap:2px;';
  const row = document.createElement('div');
  row.style.cssText = 'display:flex;gap:3px;align-items:center;flex-wrap:wrap;';
  g.appendChild(row);
  {
    const l = document.createElement('span');
    if (DUP_LABELS.has(key)) l.textContent = '\u00A0'; else bindI18n(l, key);
    l.style.cssText = 'color:#777;font:9px Arial;letter-spacing:0.4px;text-align:center;';
    g.appendChild(l);
  }
  const sectionId = GROUP_SECTION[key] || 'create';
  ribbonSectionPanes[sectionId].appendChild(g);
  return row;
}

const coordPanel = document.createElement('div');
coordPanel.setAttribute('data-ui', '');
coordPanel.style.cssText = 'position:absolute;display:none;z-index:9999;background:#202329;border:1px solid #343941;border-radius:6px;padding:6px;color:#e2e5ea;font:12px Arial;pointer-events:auto;';
fullScreen.getContainer().appendChild(coordPanel);
shieldUi(coordPanel);

// Drag handle so the coordinate entry window can be moved freely.
const coordHandle = document.createElement('div');
coordHandle.textContent = '≡';
coordHandle.setAttribute('data-i18n-title', 'tip.dragWindow'); coordHandle.title = t('tip.dragWindow');
coordHandle.style.cssText = 'text-align:center;color:#777;cursor:grab;font:12px Arial;padding:0 0 3px;user-select:none;';
coordHandle.addEventListener('pointerdown', (e) => {
  e.stopPropagation();
  e.preventDefault();
  const startX = e.clientX, startY = e.clientY;
  const panelLeft = coordPanel.offsetLeft, panelTop = coordPanel.offsetTop;
  const move = (ev) => {
    coordPanel.style.left = (panelLeft + ev.clientX - startX) + 'px';
    coordPanel.style.top = (panelTop + ev.clientY - startY) + 'px';
  };
  const up = () => {
    document.removeEventListener('pointermove', move);
    document.removeEventListener('pointerup', up);
  };
  document.addEventListener('pointermove', move);
  document.addEventListener('pointerup', up);
});
coordPanel.appendChild(coordHandle);

function coordField(label, val) {
  const w = document.createElement('div');
  w.style.cssText = 'display:flex;align-items:center;gap:4px;margin:1px 0;';
  const l = document.createElement('span');
  l.textContent = label;
  l.style.cssText = 'width:14px;color:#9ac;flex:none;';
  const i = document.createElement('input');
  i.type = 'number'; i.step = 'any'; i.value = val;
  i.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;max-width:90px;';
  w.appendChild(l); w.appendChild(i);
  return { w, i };
}

const cX = coordField('X', 0);
const cY = coordField('Y', 0);
const cZ = coordField('Z', 0);
coordPanel.appendChild(cX.w); coordPanel.appendChild(cY.w); coordPanel.appendChild(cZ.w);

const cLen = coordField('L', 0);
const cAng = coordField('∠', 0);
cLen.w.style.display = 'none';
cAng.w.style.display = 'none';
coordPanel.appendChild(cLen.w); coordPanel.appendChild(cAng.w);

const cOk = document.createElement('button');
bindI18n(cOk, 'common.ok');
cOk.style.cssText = btnStyle + ';margin-top:3px;width:100%;';
cOk.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); commitCoordInput(); });
coordPanel.appendChild(cOk);

[cX.i, cY.i, cZ.i, cLen.i, cAng.i].forEach((inp) => {
  inp.addEventListener('focus', () => { view.coordEditing = true; });
  inp.addEventListener('blur', () => { view.coordEditing = false; });
  inp.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.stopPropagation(); commitCoordInput(); }
  });
});

function toggleCoordMode() {
  view.coordMode = !view.coordMode;
  if (!view.coordMode) { coordPanel.style.display = 'none'; }
  else if (!view.coordPanelInitialized) {
    const rect = fullScreen.getContainer().getBoundingClientRect();
    // Dock bottom-right, left of the side panel and clear of the axes widget (bottom-left).
    coordPanel.style.left = Math.max(10, rect.width - 470) + 'px';
    coordPanel.style.top = Math.max(4, rect.height - 150) + 'px';
    view.coordPanelInitialized = true;
  }
}

function commitCoordInput() {
  const x = parseFloat(cX.i.value), y = parseFloat(cY.i.value), z = parseFloat(cZ.i.value);
  if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) return;
  if (pendingPaste) {
    pasteClipboardAt([x, y, z]);
    pendingPaste = null;
    view.coordEditing = false;
    coordPanel.style.display = 'none';
    if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
    return;
  }
  if (phantomMoveActive) {
    phantomMoveActive = false;
    view.moveMode = false;
    view.coordMode = false;
    moveBtn.style.background = '#3a3a3a';
    moveBtn.style.color = '#fff';
    setCursor('default');
    try { window.parent.postMessage({ type: 'g4care-phantom-position', position: [x, y, z] }, '*'); } catch (e) { /* ignore */ }
    view.coordEditing = false;
    coordPanel.style.display = 'none';
    if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
    return;
  }
  if (view.moveMode && view.selectedSourceName) {
    const nm = view.selectedSourceName;
    const oldPos = getSourcePos(nm);
    const newPos = [x, y, z];
    postSourcePos(nm, newPos);
    if (oldPos) pushCommand({
      label: t('cmd.moveSource'),
      undo: () => postSourcePos(nm, oldPos),
      redo: () => postSourcePos(nm, [...newPos]),
    });
    view.coordEditing = false;
    coordPanel.style.display = 'none';
    if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
    return;
  }
  // Precise move / rotate (numeric input; camera stays free).
  if (view.moveMode && view.selectedName) {
    const nm = view.selectedName;
    if (isGdmlObject(nm)) {
      postGdml({ op: 'set_position', name: nm, value: [x, y, z] }).then((res) => {
        showToast(res && res.ok ? t('toast.positionUpdated') : (t('error.generic') + ((res && res.error) || '')));
      });
    } else {
      setObjectPosition(nm, [x, y, z]);
    }
    view.coordEditing = false;
    coordPanel.style.display = 'none';
    if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
    return;
  }
  if (view.rotateMode && view.selectedName) {
    const nm = view.selectedName;
    if (isGdmlObject(nm)) {
      postGdml({ op: 'set_rotation', name: nm, value: [x, y, z] }).then((res) => {
        showToast(res && res.ok ? t('toast.rotationUpdated') : (t('error.generic') + ((res && res.error) || '')));
      });
    } else {
      setObjectRotation(nm, [x, y, z]);
    }
    view.coordEditing = false;
    coordPanel.style.display = 'none';
    if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
    return;
  }
  let p = [x, y, z];
  if (cLen.w.style.display !== 'none' && view.primMode && view.primPts.length >= 1) {
    const L = parseFloat(cLen.i.value);
    const A = parseFloat(cAng.i.value);
    if (Number.isFinite(L) && Number.isFinite(A)) {
      const base = view.primPts[0];
      const rad = A * DEG2RAD;
      p = [base[0] + L * Math.cos(rad), base[1] + L * Math.sin(rad), base[2]];
    }
  }
  if (view.primMode) addPrimPointAt(p);
  else if (snap.auxMode) addAuxPointAt(p);
  else if (measure.active) addMeasurePointAt(p);
  else if (planeBuild.mode === 'points') {
    planeBuild.pts.push(p);
    updatePlaneBuildMarkers();
    planeBuildHint.textContent = t('label.points') + ': ' + planeBuild.pts.length + t('sec.min3');
  }
  view.coordEditing = false;
  if (!view.primMode) coordPanel.style.display = 'none';
  if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
}

function updateCoordPanel(x, y) {
  if (!view.coordMode || view.coordEditing) return;
  if (phantomMoveActive) {
    const c = phantomCenter();
    cX.i.value = c[0].toFixed(2);
    cY.i.value = c[1].toFixed(2);
    cZ.i.value = c[2].toFixed(2);
    cLen.w.style.display = 'none';
    cAng.w.style.display = 'none';
    coordPanel.style.display = 'block';
    return;
  }
  const moving = (view.moveMode || view.rotateMode) && (view.selectedName || view.selectedSourceName);
  const active = view.primMode || snap.auxMode || measure.active || planeBuild.mode === 'points' || moving || pendingPaste;
  if (!active) { coordPanel.style.display = 'none'; return; }
  if (moving && !pendingPaste) {
    if (view.selectedSourceName) {
      const pos = getSourcePos(view.selectedSourceName) || [0, 0, 0];
      cX.i.value = pos[0].toFixed(2);
      cY.i.value = pos[1].toFixed(2);
      cZ.i.value = pos[2].toFixed(2);
      cLen.w.style.display = 'none';
      cAng.w.style.display = 'none';
      coordPanel.style.display = 'block';
      return;
    }
    const meta = objectMeta.get(view.selectedName);
    if (!meta) { coordPanel.style.display = 'none'; return; }
    if (view.moveMode) {
      const pos = meta.pos || [0, 0, 0];
      cX.i.value = pos[0].toFixed(2);
      cY.i.value = pos[1].toFixed(2);
      cZ.i.value = pos[2].toFixed(2);
    } else {
      const rot = meta.rot || [0, 0, 0];
      cX.i.value = rot[0].toFixed(2);
      cY.i.value = rot[1].toFixed(2);
      cZ.i.value = rot[2].toFixed(2);
    }
    cLen.w.style.display = 'none';
    cAng.w.style.display = 'none';
    coordPanel.style.display = 'block';
    return;
  }
  const p = resolvePoint(x, y) || projectToGridPlane(x, y);
  if (!p) return;
  cX.i.value = p[0].toFixed(2);
  cY.i.value = p[1].toFixed(2);
  cZ.i.value = p[2].toFixed(2);
  const directed = view.primMode && view.primPts.length >= 1 && ['line', 'arc', 'rect', 'ellipse'].includes(view.primType);
  cLen.w.style.display = directed ? 'flex' : 'none';
  cAng.w.style.display = directed ? 'flex' : 'none';
  if (directed) {
    const base = view.primPts[0];
    const dx = p[0] - base[0], dy = p[1] - base[1];
    cLen.i.value = Math.hypot(dx, dy).toFixed(2);
    cAng.i.value = (Math.atan2(dy, dx) / DEG2RAD).toFixed(2);
  }
  coordPanel.style.display = 'block';
}

// ── annotations (labels with callouts, persisted in YAML ANNOTATIONS) ──
// (annotation labels live in ./tools/annotations.js)

// (annotation create/save/delete/menu live in ./tools/annotations.js)

// (mesh geometry queries live in ./render/mesh-measure.js)

// (mesh geometry queries live in ./render/mesh-measure.js)




function setActiveMode(mode) {
  // mode: 'select' | 'measure' | 'aux' | 'delete' | 'prim' — mutually exclusive toolbar modes.
  if (mode !== 'measure') setMeasureMode(false);
  if (mode !== 'aux') resetAuxMode();
  if (mode !== 'delete') resetDeleteMode();
  if (mode !== 'prim') resetPrimMode();
  setSourcePlaceMode(false);
  resetAlignMode();
  resetDimMode();
  resetConstraintMode();
}


// ── cursor position readout (rendered inside the bottom-right view toolbar) ──
const coordReadout = document.createElement('span');
coordReadout.style.cssText = 'color:#e2e5ea;font:12px monospace;white-space:nowrap;padding-right:8px;border-right:1px solid #343941;pointer-events:none;';
let coordReadoutWired = false;

// (projectToGridPlane lives in ./tools/view.js)

function updateCursorPosition(x, y, hit) {
  let pos = null;
  let label = '';
  if (hit) {
    pos = hit.pos;
    label = hit.name;
  } else {
    pos = projectToGridPlane(x, y);
  }
  if (pos) {
    let txt = `X ${fmtNum(pos[0])}  Y ${fmtNum(pos[1])}  Z ${fmtNum(pos[2])}${label ? '    ' + label : ''}`;
    if (snap.enabled && snapPoint(x, y)) txt += t('toast.snapIndicator');
    if (coordReadout) coordReadout.textContent = txt;
  } else {
    if (coordReadout) coordReadout.textContent = '';
  }
}

// ── section (clipping planes) ──
// (section + primActorMap state live in ./state.js)


// ── view group ──
// ── Export GDML (moved to the "Экспорт" ribbon section) ──
const exportGroup = addGroup('group.export');
const exportGdmlBtn = document.createElement('button');
bindI18n(exportGdmlBtn, 'geom.exportGdml');
exportGdmlBtn.style.cssText = btnStyle;
exportGdmlBtn.setAttribute('data-i18n-title', 'tip.exportSceneGdml'); exportGdmlBtn.title = t('tip.exportSceneGdml');
exportGdmlBtn.addEventListener('pointerdown', (e) => {
  e.stopPropagation(); e.preventDefault();
  try { window.parent.postMessage({ type: 'g4care-export-gdml' }, '*'); } catch (err) { /* ignore */ }
});
exportGroup.appendChild(exportGdmlBtn);

// ── Viewport camera/render controls (kept out of the ribbon, always visible) ──
const viewportBar = document.createElement('div');
viewportBar.setAttribute('data-ui', '');
viewportBar.style.cssText = 'position:absolute;right:300px;bottom:0;z-index:9998;display:flex;flex-direction:row;gap:4px;align-items:center;background:#14161a;border:1px solid #343941;border-right:0;border-bottom:0;border-radius:4px 0 0 0;padding:6px 8px;';
fullScreen.getContainer().appendChild(viewportBar);
shieldUi(viewportBar);
if (!coordReadoutWired) { viewportBar.appendChild(coordReadout); coordReadoutWired = true; }

// (applyRepresentation lives in ./tools/view.js)

// Representation mode: a single <select> (Solid/Edges/Wireframe/Points).
const repSelect = document.createElement('select');
repSelect.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 6px;font:12px Arial;cursor:pointer;';
[['view.solid', 0], ['view.edges', 1], ['view.wireframe', 2], ['view.points', 3]].forEach(([label, val]) => {
  const opt = document.createElement('option');
  opt.value = val;
  bindI18n(opt, label);
  repSelect.appendChild(opt);
});
repSelect.value = String(view.repMode);
repSelect.addEventListener('pointerdown', (e) => e.stopPropagation());
repSelect.addEventListener('change', () => applyRepresentation(parseInt(repSelect.value, 10)));
viewportBar.appendChild(repSelect);

function syncRepSelect() {
  if (repSelect) repSelect.value = String(view.repMode);
}

// (setViewFrom lives in ./tools/view.js)

[['view.top', [0, 0, 1]], ['view.front', [0, 1, 0]], ['view.side', [1, 0, 0]], ['view.iso', [1, 1, 1]]].forEach(([label, dir]) => {
  const b = document.createElement('button');
  bindI18n(b, label);
  b.style.cssText = btnStyle;
  b.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); setViewFrom(dir); });
  viewportBar.appendChild(b);
});

// (fitView lives in ./tools/view.js)
const fitBtn = document.createElement('button');
bindI18n(fitBtn, 'view.fit');
fitBtn.style.cssText = btnStyle;
fitBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); fitView(); });
viewportBar.appendChild(fitBtn);

// ── object edit (move / rotate / clipboard) ──
const editGroup = addGroup('group.object');
const moveBtn = document.createElement('button');
bindI18n(moveBtn, 'tools.move');
moveBtn.style.cssText = btnStyle;
moveBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); view.selectedSourceName = null; setMoveMode(!view.moveMode); });
editGroup.appendChild(moveBtn);
const rotateBtn = document.createElement('button');
bindI18n(rotateBtn, 'tools.rotate');
rotateBtn.style.cssText = btnStyle;
rotateBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); view.selectedSourceName = null; setRotateMode(!view.rotateMode); });
editGroup.appendChild(rotateBtn);

// ── Добавить примитивный объём (box/sphere/cylinder/…) ──
function defaultDimsFor(shape) {
  switch (shape) {
    case 'box': case 'para': return '10, 10, 10';
    case 'sphere': return '0, 10';
    case 'orb': return '10';
    case 'cylinder': return '10, 10';
    case 'tube': case 'tubs': return '0, 10, 10';
    case 'cone': return '0, 10, 0, 10, 10';
    case 'torus': return '0, 10, 20';
    default: return '10';
  }
}
function openAddVolumeDialog(shape) {
  promptText(t('addVol.name'), shape + '_1', (name) => {
    name = (name || '').trim();
    if (!name) { showToast(t('addVol.nameEmpty')); return; }
    if (objectMeta.has(name)) { showToast(t('addVol.nameTaken', { name })); return; }
    promptText(t('addVol.dims', { shape }), defaultDimsFor(shape), (dimsStr) => {
      const dims = (dimsStr || '').split(',').map((s) => parseFloat(s.trim())).filter((n) => Number.isFinite(n));
      if (!dims.length) { showToast(t('addVol.badDims')); return; }
      const obj = { name, shape, DIMENSIONS: dims, POSITION: [0, 0, 0], material: 'G4_AIR' };
      postBatch([{ insert: { object: obj, newName: name } }]).then((res) => {
        if (res && res.ok) { showToast(t('addVol.created', { name })); selectObject(name); }
        else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  });
}
const addVolGroup = addGroup('group.add');
const addVolShapeSel = document.createElement('select');
addVolShapeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
PRIMITIVE_TYPES.forEach(([label, val]) => { const o = document.createElement('option'); o.value = val; bindI18n(o, label); addVolShapeSel.appendChild(o); });
addVolShapeSel.setAttribute('data-i18n-title', 'tip.volumeType'); addVolShapeSel.title = t('tip.volumeType');
addVolShapeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
addVolGroup.appendChild(addVolShapeSel);
const addVolBtn = document.createElement('button');
bindI18n(addVolBtn, 'tools.create');
addVolBtn.style.cssText = btnStyle;
addVolBtn.setAttribute('data-i18n-title', 'tip.newVolume'); addVolBtn.title = t('tip.newVolume');
addVolBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); openAddVolumeDialog(addVolShapeSel.value); });
addVolGroup.appendChild(addVolBtn);

// ── Умное размещение (совпадение центров / соосность / на расстоянии) ──
const alignGroup = addGroup('group.placement');
const alignCentersBtn = document.createElement('button');
bindI18n(alignCentersBtn, 'tools.centers');
alignCentersBtn.style.cssText = btnStyle;
alignCentersBtn.setAttribute('data-i18n-title', 'tip.centers'); alignCentersBtn.title = t('tip.centers');
alignCentersBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); setAlignMode('centers'); });
alignGroup.appendChild(alignCentersBtn);
const alignCoaxBtn = document.createElement('button');
bindI18n(alignCoaxBtn, 'tools.coaxial');
alignCoaxBtn.style.cssText = btnStyle;
alignCoaxBtn.setAttribute('data-i18n-title', 'tip.coaxial'); alignCoaxBtn.title = t('tip.coaxial');
alignCoaxBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); setAlignMode('coaxial'); });
alignGroup.appendChild(alignCoaxBtn);

// ── Зеркало (mirror) ──
const mirrorPlaneSel = (() => {
  const sel = document.createElement('select');
  sel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
  [['mirror.xy', 'xy'], ['mirror.xz', 'xz'], ['mirror.yz', 'yz'], ['mirror.plane', 'plane']].forEach(([k, v]) => {
    const o = document.createElement('option'); o.value = v; bindI18n(o, k); sel.appendChild(o);
  });
  sel.setAttribute('data-i18n-title', 'tip.mirrorPlane'); sel.title = t('tip.mirrorPlane');
  sel.addEventListener('pointerdown', (e) => e.stopPropagation());
  return sel;
})();
const mirrorGroup = addGroup('group.mirror');
mirrorGroup.appendChild(mirrorPlaneSel);
const mirrorCopyBtn = document.createElement('button');
bindI18n(mirrorCopyBtn, 'tools.copy');
mirrorCopyBtn.style.cssText = btnStyle;
mirrorCopyBtn.setAttribute('data-i18n-title', 'tip.mirrorCopy'); mirrorCopyBtn.title = t('tip.mirrorCopy');
mirrorCopyBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); applyMirror('copy'); });
mirrorGroup.appendChild(mirrorCopyBtn);
const mirrorMoveBtn = document.createElement('button');
bindI18n(mirrorMoveBtn, 'tools.transfer');
mirrorMoveBtn.style.cssText = btnStyle;
mirrorMoveBtn.setAttribute('data-i18n-title', 'tip.mirrorMove'); mirrorMoveBtn.title = t('tip.mirrorMove');
mirrorMoveBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); applyMirror('move'); });
mirrorGroup.appendChild(mirrorMoveBtn);

// ── DICOM (CT slicer) — receives slice data from the Electron UI and renders ──
const dicomState = { sliceActor: null, window: 400, level: 40 };

function renderDicomSlice(p) {
  if (!p || !p.data) return;
  if (!dicomState.sliceActor) {
    const imageData = vtkImageData.newInstance();
    const mapper = vtkImageMapper.newInstance();
    mapper.setInputData(imageData);
    dicomState.sliceActor = vtkImageSlice.newInstance();
    dicomState.sliceActor.setMapper(mapper);
    dicomState.sliceActor.getProperty().setInterpolationTypeToLinear();
    renderer.addActor(dicomState.sliceActor);
  }
  const meta = p.meta || {};
  const d = [meta.dx || 1, meta.dy || 1, meta.dz || 1];
  const o = meta.origin || [0, 0, 0];
  const axis = p.axis || 2, index = p.index || 0;
  let spacing, origin;
  if (axis === 2) { spacing = [d[0], d[1], 1]; origin = [o[0], o[1], o[2] + index * d[2]]; }
  else if (axis === 1) { spacing = [d[0], d[2], 1]; origin = [o[0], o[1] + index * d[1], o[2]]; }
  else { spacing = [d[1], d[2], 1]; origin = [o[0] + index * d[0], o[1], o[2]]; }
  const imageData = dicomState.sliceActor.getMapper().getInputData();
  imageData.setDimensions(p.w, p.h, 1);
  imageData.setSpacing(spacing[0], spacing[1], spacing[2]);
  imageData.setOrigin(origin[0], origin[1], origin[2]);
  const arr = vtkDataArray.newInstance({ name: 'HU', values: Float32Array.from(p.data), numberOfComponents: 1 });
  imageData.getPointData().setScalars(arr);
  imageData.modified();
  dicomState.sliceActor.getProperty().setColorWindow(dicomState.window);
  dicomState.sliceActor.getProperty().setColorLevel(dicomState.level);
  renderWindow.render();
}

window.addEventListener('message', (ev) => {
  const d = ev.data;
  if (!d) return;
  if (d.type === 'g4care-dicom-slice') renderDicomSlice(d);
  else if (d.type === 'g4care-dicom-window') {
    dicomState.window = d.window; dicomState.level = d.level;
    if (dicomState.sliceActor) {
      dicomState.sliceActor.getProperty().setColorWindow(d.window);
      dicomState.sliceActor.getProperty().setColorLevel(d.level);
      renderWindow.render();
    }
  } else if (d.type === 'g4care-dicom-clear') {
    if (dicomState.sliceActor) { renderer.removeActor(dicomState.sliceActor); dicomState.sliceActor = null; renderWindow.render(); }
  }
});

function setAlignMode(mode) {
  const turningOn = view.alignMode !== mode;
  view.alignMode = turningOn ? mode : null;
  view.alignRef = null;
  if (turningOn) {
    // Mutually exclusive with the other placement/edit modes.
    setMeasureMode(false);
    resetAuxMode();
    resetDeleteMode();
    resetPrimMode();
    setSourcePlaceMode(false);
  }
  alignCentersBtn.style.background = view.alignMode === 'centers' ? '#b45f06' : '#3a3a3a';
  alignCoaxBtn.style.background = view.alignMode === 'coaxial' ? '#b45f06' : '#3a3a3a';
  alignCentersBtn.style.color = '#fff';
  alignCoaxBtn.style.color = '#fff';
  setCursor(view.alignMode ? 'crosshair' : 'default');
  if (view.alignMode) {
    const hints = {
      centers: t('align.centersHint'),
      coaxial: t('align.coaxialHint'),
    };
    showToast(hints[mode]);
  }
}

function resetAlignMode() {
  view.alignMode = null;
  view.alignRef = null;
  alignCentersBtn.style.background = '#3a3a3a';
  alignCoaxBtn.style.background = '#3a3a3a';
  alignCentersBtn.style.color = '#fff';
  alignCoaxBtn.style.color = '#fff';
}

function v3sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function v3add(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
function v3scale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }

function objectAxisDir(name) {
  const meta = objectMeta.get(name);
  if (!meta) return [0, 0, 1];
  const rot = meta.rot || [0, 0, 0];
  const baseX = Z_AXIS_SHAPES.has(meta.shape) ? 90 : 0;
  // vtk.js cylinder/cone sources are along Y and rotated +90° about X (baseX),
  // so the revolution axis is the local Y direction of the vtk source.
  const localAxis = Z_AXIS_SHAPES.has(meta.shape) ? [0, 1, 0] : [0, 0, 1];
  return normalize3(quatRotate(localAxis, orientationQuat(rot, baseX)));
}

function pushAlignCommand(target, oldPos, oldRot, newPos, newRot) {
  pushCommand({
    label: t('cmd.placement'),
    undo: () => {
      if (oldRot) setObjectRotation(target, oldRot);
      if (oldPos) setObjectPosition(target, oldPos);
    },
    redo: () => {
      if (newRot) setObjectRotation(target, newRot);
      if (newPos) setObjectPosition(target, newPos);
    },
  });
}

function alignCenters(ref, target) {
  const rm = objectMeta.get(ref), tm = objectMeta.get(target);
  if (!rm || !tm) return;
  const newPos = rm.pos ? [...rm.pos] : null;
  const oldPos = tm.pos ? [...tm.pos] : null;
  if (!newPos || !oldPos) return;
  const dx = newPos[0] - oldPos[0], dy = newPos[1] - oldPos[1], dz = newPos[2] - oldPos[2];
  if (Math.hypot(dx, dy, dz) < 1e-6) { showToast(t('toast.centersAlready')); return; }
  setObjectPosition(target, newPos);
  pushAlignCommand(target, oldPos, null, newPos, null);
  showToast(t('toast.centersAligned'));
}

function alignCoaxial(ref, target) {
  const rm = objectMeta.get(ref), tm = objectMeta.get(target);
  if (!rm || !tm) return;
  const refPos = rm.pos || [0, 0, 0];
  const tgtPos = tm.pos || [0, 0, 0];
  const refRot = rm.rot || [0, 0, 0];
  const tgtRot = tm.rot || [0, 0, 0];
  const axis = objectAxisDir(ref);
  const t = dot3(v3sub(tgtPos, refPos), axis);
  const newPos = v3add(refPos, v3scale(axis, t));
  setObjectRotation(target, [...refRot]);
  setObjectPosition(target, newPos);
  pushAlignCommand(target, [...tgtPos], [...tgtRot], [...newPos], [...refRot]);
  showToast(t('toast.axesAligned'));
}

function handleAlignClick(x, y) {
  const hit = pickAt(x, y);
  const name = hit ? hit.name : null;
  if (!name) return;
  if (isReplicaObject(name)) {
    showToast(t('align.replicaLocked'));
    return;
  }
  if (!view.alignRef) {
    view.alignRef = name;
    view.selectedName = name;
    applyHighlight();
    showToast(t('align.selectSecond'));
    return;
  }
  const ref = view.alignRef;
  const target = name;
  const mode = view.alignMode;
  view.alignRef = null;
  if (mode === 'centers') { alignCenters(ref, target); setAlignMode('centers'); }
  else if (mode === 'coaxial') { alignCoaxial(ref, target); setAlignMode('coaxial'); }
}

// ── Родитель (mother) ──
// Find the smallest volume whose AABB contains the object's centre.
function autoParentFor(name) {
  const a = actors.get(name);
  if (!a) return null;
  const b = a.getBounds();
  const cx = (b[0] + b[1]) / 2, cy = (b[2] + b[3]) / 2, cz = (b[4] + b[5]) / 2;
  let best = null, bestVol = Infinity;
  for (const [n] of objectMeta) {
    if (n === name || isReplicaObject(n)) continue;
    const a2 = actors.get(n);
    if (!a2) continue;
    const bb = a2.getBounds();
    if (cx >= bb[0] && cx <= bb[1] && cy >= bb[2] && cy <= bb[3] && cz >= bb[4] && cz <= bb[5]) {
      const vol = (bb[1] - bb[0]) * (bb[3] - bb[2]) * (bb[5] - bb[4]);
      if (vol < bestVol) { bestVol = vol; best = n; }
    }
  }
  return best;
}

function autoAssignParent(name) {
  const meta = objectMeta.get(name);
  if (!meta || !meta.pos) return;
  const parent = autoParentFor(name);
  const parentMeta = parent ? objectMeta.get(parent) : null;
  pushHistory();
  if (!parent || !parentMeta || !parentMeta.pos) {
    setObjectMother(name, null);
    showToast(t('toast.parentReset'));
    return;
  }
  // Rebase the position into the parent's frame (translation only — axis-aligned
  // convention; parent rotation is ignored for now).
  const newPos = [meta.pos[0] - parentMeta.pos[0], meta.pos[1] - parentMeta.pos[1], meta.pos[2] - parentMeta.pos[2]];
  setObjectMother(name, parent);
  setObjectPosition(name, newPos);
  showToast(t('toast.parent') + parent);
}

// ── Зеркало (mirror) ──
// Mirror plane as { n (unit normal), o (origin point) }. The 'plane' option uses
// the active section plane; otherwise the coordinate plane through the origin.
function mirrorPlaneInfo() {
  const m = mirrorPlaneSel.value;
  if (m === 'plane') {
    const ap = activePlane();
    if (!ap) return null;
    return { n: normalize3([ap.getNormal()[0], ap.getNormal()[1], ap.getNormal()[2]]), o: [ap.getOrigin()[0], ap.getOrigin()[1], ap.getOrigin()[2]] };
  }
  const n = m === 'xy' ? [0, 0, 1] : (m === 'xz' ? [0, 1, 0] : [1, 0, 0]);
  return { n, o: [0, 0, 0] };
}

// True if the plane crosses the object's bounding box (a mirror would overlap
// the original).
function planeIntersectsObject(name, n, o) {
  const a = actors.get(name);
  if (!a) return true;
  const b = a.getBounds();
  const cx = (b[0] + b[1]) / 2, cy = (b[2] + b[3]) / 2, cz = (b[4] + b[5]) / 2;
  const hx = (b[1] - b[0]) / 2, hy = (b[3] - b[2]) / 2, hz = (b[5] - b[4]) / 2;
  const dist = Math.abs(n[0] * (cx - o[0]) + n[1] * (cy - o[1]) + n[2] * (cz - o[2]));
  const extent = Math.abs(n[0]) * hx + Math.abs(n[1]) * hy + Math.abs(n[2]) * hz;
  return dist < extent;
}

function mirrorObject(name, mode) {
  const meta = objectMeta.get(name);
  if (!meta || !meta.pos) return;
  if (isReplicaObject(name)) { showToast(t('mirror.replicaLocked')); return; }
  const info = mirrorPlaneInfo();
  if (!info) { showToast(t('mirror.noPlane')); return; }
  const { n, o } = info;
  if (planeIntersectsObject(name, n, o)) {
    showToast(t('mirror.intersects'));
    return;
  }
  const reflect = (v) => {
    const d = dot3([v[0] - o[0], v[1] - o[1], v[2] - o[2]], n);
    return [v[0] - 2 * d * n[0], v[1] - 2 * d * n[1], v[2] - 2 * d * n[2]];
  };
  const newPos = reflect(meta.pos);
  const q = orientationQuat(meta.rot || [0, 0, 0], Z_AXIS_SHAPES.has(meta.shape) ? 90 : 0);
  const newRot = quatToEulerXYZ(mirrorQuaternion(q, n));
  if (mode === 'copy') {
    const base = name + '_mirror';
    const obj = {
      name: base, shape: meta.shape, DIMENSIONS: meta.dims || [], POSITION: newPos,
      ROTATION: newRot, material: meta.material, mother: meta.mother, isSensitive: meta.isSensitive,
    };
    postBatch([{ insert: { object: obj, newName: base } }]).then((res) => {
      if (res && res.ok) showToast(t('toast.mirrorCopyCreated'));
      else showToast(t('error.generic') + ((res && res.error) || ''));
    });
    pushCommand({
      label: t('cmd.mirrorCopy'),
      undo: () => { postBatch([{ delete: { name: base } }]); },
      redo: () => { postBatch([{ insert: { object: obj, newName: base } }]); },
    });
  } else {
    pushHistory();
    setObjectRotation(name, newRot);
    setObjectPosition(name, newPos);
    showToast(t('toast.objectMirrored'));
  }
}

function mirrorPrim(index, mode) {
  const p = prims[index];
  if (!p || !p.pts) return;
  const info = mirrorPlaneInfo();
  if (!info) { showToast(t('mirror.noPlaneShort')); return; }
  const { n, o } = info;
  const reflect = (v) => {
    const d = dot3([v[0] - o[0], v[1] - o[1], v[2] - o[2]], n);
    return [v[0] - 2 * d * n[0], v[1] - 2 * d * n[1], v[2] - 2 * d * n[2]];
  };
  if (mode === 'copy') {
    const copy = {
      ...p,
      name: p.name + '_mirror',
      pts: p.pts.map((pt) => reflect(pt)),
      normal: p.normal ? reflect(p.normal) : undefined,
      actor: null,
    };
    prims.push(copy);
    pushCommand({
      label: t('cmd.mirrorPrimCopy'),
      undo: () => { const i = prims.indexOf(copy); if (i >= 0) { removePrimAtIndex(i); } },
      redo: () => { prims.push(copy); updatePrims(); },
    });
  } else {
    pushHistory();
    p.pts = p.pts.map((pt) => reflect(pt));
    if (p.normal && p.normal.length === 3) p.normal = reflect(p.normal);
    showToast(t('toast.primMirrored'));
  }
  updatePrims();
  updateTreeUI();
}

function applyMirror(mode) {
  if (view.selectedPrimIndex >= 0 && prims[view.selectedPrimIndex]) {
    mirrorPrim(view.selectedPrimIndex, mode);
  } else if (view.selectedName && objectMeta.has(view.selectedName)) {
    mirrorObject(view.selectedName, mode);
  } else {
    showToast(t('common.selectObjectOrFigure'));
  }
}


// ── Источник: place a particle source as a scene object by clicking ──
const srcGroup = addGroup('group.source');
const srcTypeSel = document.createElement('select');
srcTypeSel.style.cssText = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 4px;font:12px Arial;cursor:pointer;';
[['source.type.particleGun', 'particleGun'], ['source.type.gps', 'GPS'], ['source.type.radioactive', 'radioactive'], ['source.type.activation', 'activation'], ['source.type.mixture', 'mixture']].forEach(([k, val]) => {
  const o = document.createElement('option'); o.value = val; bindI18n(o, k); srcTypeSel.appendChild(o);
});
srcTypeSel.value = view.sourceType || 'particleGun';
srcTypeSel.setAttribute('data-i18n-title', 'tip.sourceType'); srcTypeSel.title = t('tip.sourceType');
srcTypeSel.addEventListener('pointerdown', (e) => e.stopPropagation());
srcTypeSel.addEventListener('change', () => { view.sourceType = srcTypeSel.value; });
srcGroup.appendChild(srcTypeSel);

const sourceBtn = document.createElement('button');
bindI18n(sourceBtn, 'tools.place');
sourceBtn.style.cssText = btnStyle;
sourceBtn.setAttribute('data-i18n-title', 'tip.placeSource'); sourceBtn.title = t('tip.placeSource');
sourceBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); setSourcePlaceMode(!view.sourcePlaceMode); });
srcGroup.appendChild(sourceBtn);

function isDirectionalSource(type) { return type === 'particleGun' || type === 'GPS'; }

let srcPreviewMarker = null;
let srcPreviewLine = null;
let srcPreviewLineSrc = null;

function clearSourcePreview() {
  if (srcPreviewMarker) { removeMarker(srcPreviewMarker); srcPreviewMarker = null; }
  if (srcPreviewLine) { renderer.removeActor(srcPreviewLine); srcPreviewLine = null; srcPreviewLineSrc = null; }
}

function showSourcePreviewOrigin(p) {
  clearSourcePreview();
  srcPreviewMarker = makeMarkerActor(0.95, 0.95, 0.2);
  srcPreviewMarker.setPosition(p[0], p[1], p[2]);
  srcPreviewLineSrc = vtkLineSource.newInstance();
  srcPreviewLineSrc.setPoint1(p[0], p[1], p[2]);
  srcPreviewLineSrc.setPoint2(p[0], p[1], p[2]);
  const m = vtkMapper.newInstance(); m.setInputConnection(srcPreviewLineSrc.getOutputPort());
  srcPreviewLine = vtkActor.newInstance(); srcPreviewLine.setMapper(m);
  srcPreviewLine.getProperty().setColor(0.95, 0.62, 0.2);
  srcPreviewLine.getProperty().setLineWidth(2);
  srcPreviewLine.setPickable(false);
  renderer.addActor(srcPreviewLine);
  renderWindow.render();
}

function updateSourcePreview(cursor) {
  if (srcPreviewLineSrc) {
    srcPreviewLineSrc.setPoint2(cursor[0], cursor[1], cursor[2]);
    renderWindow.render();
  }
}

function setSourcePlaceMode(on) {
  view.sourcePlaceMode = on;
  view.selectedSourceName = null;
  view.sourcePts = [];
  if (!on) clearSourcePreview();
  sourceBtn.style.background = on ? '#b45f06' : '#3a3a3a';
  sourceBtn.style.color = '#fff';
  setCursor(on ? 'crosshair' : 'default');
  if (on) {
    const t = view.sourceType || 'particleGun';
    showToast(isDirectionalSource(t)
      ? t('src.beamStart')
      : t('src.clickToPlace'));
  }
}

function postSourceCreate(payload) {
  return fetch('/api/source', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) })
    .then((r) => r.json())
    .then((res) => { showToast(res && res.ok ? t('src.created') + payload.name : (t('error.generic') + ((res && res.error) || ''))); })
    .catch(() => { showToast(t('src.createFailed')); })
    .then(() => { loadSources(); });
}

function postSourcePos(name, pos) {
  return fetch('/api/source', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name, pos: { x: pos[0], y: pos[1], z: pos[2] } }) })
    .then((r) => r.json())
    .then((res) => { showToast(res && res.ok ? t('toast.sourceMoved') : (t('error.generic') + ((res && res.error) || ''))); loadSources(); })
    .catch(() => { loadSources(); });
}

function createSourceAt(pos, dir) {
  const type = view.sourceType || 'particleGun';
  const name = 'src_' + Date.now().toString(36);
  const payload = { name, type, SOURCE_MODE: 'flow', pos: { x: pos[0], y: pos[1], z: pos[2] } };
  if (dir) payload.direction = { x: dir[0], y: dir[1], z: dir[2] };
  if (type === 'particleGun') { payload.particle = 'gamma'; payload.energy = '1'; }
  if (type === 'GPS') { payload.particle = 'gamma'; }
  postSourceCreate(payload);
  if (type === 'GPS') {
    const macro = buildGpsMacro(name, {
      particle: 'gamma',
      energy: '1',
      spectrum: 'Mono',
      pos: [pos[0], pos[1], pos[2]],
      direction: dir || [0, 0, 1],
    });
    fetch('/api/gps-macro', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name, macro }) })
      .catch(() => {});
  }
  pushCommand({
    label: t('cmd.createSource'),
    undo: () => { deleteSource(name).then(() => loadSources()); },
    redo: () => { postSourceCreate(payload); },
  });
}

function handleSourcePlaceClick(x, y) {
  const p = resolvePoint(x, y) || projectToGridPlane(x, y);
  if (!p) return;
  const type = view.sourceType || 'particleGun';
  if (isDirectionalSource(type)) {
    if (!view.sourcePts.length) {
      view.sourcePts.push(p);
      showSourcePreviewOrigin(p);
      showToast(t('src.beamDir'));
      return;
    }
    const p1 = view.sourcePts[0];
    let d = normalize3([p[0] - p1[0], p[1] - p1[1], p[2] - p1[2]]);
    if (d[0] === 0 && d[1] === 0 && d[2] === 0) d = [0, 0, 1];
    createSourceAt(p1, d);
    setSourcePlaceMode(false);
  } else {
    createSourceAt(p, null);
    setSourcePlaceMode(false);
  }
}

let clipboard = null; // { kind: 'yaml'|'gdml', name, data? }
let pendingPaste = null;

function setMoveMode(on) {
  if (on && view.selectedName && isReplicaObject(view.selectedName)) {
    showToast(t('toast.replicaCantMove'));
    return;
  }
  view.moveMode = on;
  view.rotateMode = false;
  moveBtn.style.background = on ? '#b45f06' : '#3a3a3a';
  moveBtn.style.color = '#fff';
  rotateBtn.style.background = '#3a3a3a';
  rotateBtn.style.color = '#ddd';
  setCursor(on ? 'move' : 'default');
}
function setRotateMode(on) {
  if (on && view.selectedName && isReplicaObject(view.selectedName)) {
    showToast(t('toast.replicaCantRotate'));
    return;
  }
  view.rotateMode = on;
  view.moveMode = false;
  rotateBtn.style.background = on ? '#b45f06' : '#3a3a3a';
  rotateBtn.style.color = '#fff';
  moveBtn.style.background = '#3a3a3a';
  moveBtn.style.color = '#ddd';
  setCursor(on ? 'crosshair' : 'default');
}

function objectToYaml(name) {
  const meta = objectMeta.get(name);
  if (!meta) return null;
  return {
    name, shape: meta.shape, DIMENSIONS: meta.dims || [], POSITION: meta.pos || [0, 0, 0],
    material: meta.material, mother: meta.mother, isSensitive: meta.isSensitive, ROTATION: meta.rot || [0, 0, 0],
  };
}
function pasteClipboardAt(pos) {
  if (!clipboard || !pos) return;
  if (clipboard.kind === 'yaml' && clipboard.data) {
    const orig = clipboard.data.POSITION || [0, 0, 0];
    const offset = [pos[0] - orig[0], pos[1] - orig[1], pos[2] - orig[2]];
    const newName = (clipboard.data.name || 'object') + '_copy_' + Date.now().toString(36);
    postBatch([{ insert: { object: clipboard.data, offset, newName } }]).then((res) => {
      showToast(res && res.ok ? t('toast.pasted') : (t('error.generic') + ((res && res.error) || '')));
    });
    pushCommand({
      label: t('cmd.pasteObject'),
      undo: () => { postBatch([{ delete: { name: newName } }]); },
      redo: () => { postBatch([{ insert: { object: clipboard.data, offset, newName } }]); },
    });
  } else if (clipboard.kind === 'gdml') {
    const orig = clipboard.pos || [0, 0, 0];
    const offset = [pos[0] - orig[0], pos[1] - orig[1], pos[2] - orig[2]];
    postGdml({ op: 'duplicate', name: clipboard.name, offset }).then((res) => {
      showToast(res && res.ok ? t('toast.pasted') : (t('error.generic') + ((res && res.error) || '')));
    });
  } else if (clipboard.kind === 'prim' && clipboard.prim) {
    const p = clipboard.prim;
    const c = primCenter(p);
    const off = [pos[0] - c[0], pos[1] - c[1], pos[2] - c[2]];
    const copy = { ...p, name: p.name + t('common.copySuffix'), pts: p.pts.map((pt) => [pt[0] + off[0], pt[1] + off[1], pt[2] + off[2]]), normal: p.normal ? [...p.normal] : undefined, actor: null };
    pushHistory();
    prims.push(copy);
    updatePrims();
    updateTreeUI();
  }
}

// Distinguish a click (press + release, no move) from a drag-rotate.
let pressPos = null;
let dragging = false;
let dragStartScreen = null;
let dragStartObjPos = null;
let dragStartObjRot = null;
let dragFinalPos = null;
let dragFinalRot = null;
let dragStartPrimPts = null;
let dragStartPrimNormal = null;
let dragStartScene = null;
let phantomStartVerts = null;
// (setCursor lives in ./tools/view.js)
// Clicks on UI panels (side panel / toolbar / context menu / dialogs) must not
// fall through to the 3D viewport (which would deselect the object). We detect
// them in the capture phase (before the interactor's own bubble-phase handler).
let uiPointer = false;
fullScreen.getContainer().addEventListener('pointerdown', (e) => {
  uiPointer = !!(e.target && e.target.closest && e.target.closest('[data-ui]'));
}, true);
let panPos = null;
function panCamera(x, y) {
  if (!panPos) return;
  const prev = panPos;
  panPos = [x, y];
  // Exact vtk.js trackball-camera pan: move the focal point by the display-space
  // delta converted back to world at the focal depth (grab convention, no sign bugs).
  const cam = renderer.getActiveCamera();
  const view = renderer.getRenderWindow().getViews()[0];
  const fp = cam.getFocalPoint();
  const d = view.worldToDisplay(fp[0], fp[1], fp[2], renderer);
  const oldP = view.displayToWorld(prev[0], prev[1], d[2], renderer);
  const newP = view.displayToWorld(x, y, d[2], renderer);
  const motion = [oldP[0] - newP[0], oldP[1] - newP[1], oldP[2] - newP[2]];
  cam.setFocalPoint(fp[0] + motion[0], fp[1] + motion[1], fp[2] + motion[2]);
  const pos = cam.getPosition();
  cam.setPosition(pos[0] + motion[0], pos[1] + motion[1], pos[2] + motion[2]);
  renderer.resetCameraClippingRange();
  renderWindow.render();
}
interactor.onLeftButtonPress((e) => {
  if (uiPointer) { pressPos = null; dragging = false; return; }
  pressPos = [e.position.x, e.position.y]; dragging = false; setCursor('grabbing');
  if ((view.moveMode || view.rotateMode) && view.selectedName && !view.coordMode) {
    const meta = objectMeta.get(view.selectedName);
    if (meta) {
      dragStartScreen = [e.position.x, e.position.y];
      dragStartScene = captureSceneState();
      dragStartObjPos = meta.pos ? [...meta.pos] : [0, 0, 0];
      dragStartObjRot = meta.rot ? [...meta.rot] : [0, 0, 0];
      // Lock camera: vtk trackball already started rotate/pan/spin.
      try {
        const style = interactor.getInteractorStyle && interactor.getInteractorStyle();
        if (style) {
          if (style.endRotate) style.endRotate();
          if (style.endPan) style.endPan();
          if (style.endSpin) style.endSpin();
        }
      } catch (_) { /* ignore */ }
    }
  } else if ((view.moveMode || view.rotateMode) && view.selectedPrimIndex >= 0 && !view.coordMode) {
    const prim = prims[view.selectedPrimIndex];
    if (prim) {
      dragStartScreen = [e.position.x, e.position.y];
      dragStartScene = captureSceneState();
      dragStartObjPos = primCenter(prim);
      dragStartPrimPts = prim.pts.map((pt) => [...pt]);
      dragStartPrimNormal = prim.normal ? [...prim.normal] : null;
      try {
        const style = interactor.getInteractorStyle && interactor.getInteractorStyle();
        if (style) {
          if (style.endRotate) style.endRotate();
          if (style.endPan) style.endPan();
          if (style.endSpin) style.endSpin();
        }
      } catch (_) { /* ignore */ }
    }
  } else if ((view.moveMode || view.rotateMode) && view.selectedSourceName && !view.coordMode) {
    const sp = getSourcePos(view.selectedSourceName);
    if (sp) {
      dragStartScreen = [e.position.x, e.position.y];
      dragStartObjPos = [...sp];
      try {
        const style = interactor.getInteractorStyle && interactor.getInteractorStyle();
        if (style) {
          if (style.endRotate) style.endRotate();
          if (style.endPan) style.endPan();
          if (style.endSpin) style.endSpin();
        }
      } catch (_) { /* ignore */ }
    }
  } else if (phantomMoveActive && view.moveMode && !view.coordMode) {
    dragStartScreen = [e.position.x, e.position.y];
    dragStartObjPos = phantomCenter();
    phantomStartVerts = capturePhantomVerts();
    try {
      const style = interactor.getInteractorStyle && interactor.getInteractorStyle();
      if (style) {
        if (style.endRotate) style.endRotate();
        if (style.endPan) style.endPan();
        if (style.endSpin) style.endSpin();
      }
    } catch (_) { /* ignore */ }
  }
});
interactor.onMouseMove((e) => {
  if (pressPos) {
    if (Math.hypot(e.position.x - pressPos[0], e.position.y - pressPos[1]) > 3) dragging = true;
  }
  if (panPos) panCamera(e.position.x, e.position.y);
  if (dragging && dragStartScreen && (view.moveMode || view.rotateMode) && view.selectedName) {
    const name = view.selectedName;
    if (view.moveMode) {
      const p0 = projectToGridPlane(dragStartScreen[0], dragStartScreen[1]);
      const p1 = projectToGridPlane(e.position.x, e.position.y);
      if (p0 && p1) {
        const np = [dragStartObjPos[0] + (p1[0] - p0[0]), dragStartObjPos[1] + (p1[1] - p0[1]), dragStartObjPos[2] + (p1[2] - p0[2])];
        if (isGdmlObject(name)) dragFinalPos = np;
        else setObjectPosition(name, np);
      }
    } else if (view.rotateMode) {
      const angleZ = (e.position.x - dragStartScreen[0]) * 0.5;
      const angleX = (e.position.y - dragStartScreen[1]) * 0.5;
      const nr = [dragStartObjRot[0] + angleX, dragStartObjRot[1], dragStartObjRot[2] + angleZ];
      if (isGdmlObject(name)) dragFinalRot = nr;
      else setObjectRotation(name, nr);
    }
  } else if (dragging && dragStartScreen && (view.moveMode || view.rotateMode) && view.selectedPrimIndex >= 0) {
    if (view.moveMode) {
      const p0 = projectToGridPlane(dragStartScreen[0], dragStartScreen[1]);
      const p1 = projectToGridPlane(e.position.x, e.position.y);
      if (p0 && p1) {
        const np = [dragStartObjPos[0] + (p1[0] - p0[0]), dragStartObjPos[1] + (p1[1] - p0[1]), dragStartObjPos[2] + (p1[2] - p0[2])];
        setPrimPosition(view.selectedPrimIndex, np);
      }
    } else if (view.rotateMode) {
      const angleZ = (e.position.x - dragStartScreen[0]) * 0.5;
      const angleX = (e.position.y - dragStartScreen[1]) * 0.5;
      const prim = prims[view.selectedPrimIndex];
      if (prim && dragStartPrimPts) {
        prim.pts = dragStartPrimPts.map((pt) => [...pt]);
        if (dragStartPrimNormal) prim.normal = [...dragStartPrimNormal];
        setPrimRotation(view.selectedPrimIndex, [angleX, 0, angleZ]);
      }
    }
  } else if (dragging && dragStartScreen && view.moveMode && view.selectedSourceName) {
    const p0 = projectToGridPlane(dragStartScreen[0], dragStartScreen[1]);
    const p1 = projectToGridPlane(e.position.x, e.position.y);
    if (p0 && p1) {
      const np = [dragStartObjPos[0] + (p1[0] - p0[0]), dragStartObjPos[1] + (p1[1] - p0[1]), dragStartObjPos[2] + (p1[2] - p0[2])];
      dragFinalPos = np;
      setSourceMarkerPos(view.selectedSourceName, np);
    }
  } else if (dragging && dragStartScreen && phantomMoveActive && view.moveMode && phantomStartVerts) {
    const p0 = projectToGridPlane(dragStartScreen[0], dragStartScreen[1]);
    const p1 = projectToGridPlane(e.position.x, e.position.y);
    if (p0 && p1) applyPhantomVerts(phantomStartVerts, [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]]);
  }
});
interactor.onLeftButtonRelease((e) => {
  const wasUI = uiPointer;
  uiPointer = false;
  if (wasUI) { setCursor('default'); return; }
  if (pressPos && !dragging) doSelect(e.position.x, e.position.y);
  else if (dragging && dragStartScreen && (view.moveMode || view.rotateMode) && view.selectedName) {
    const name = view.selectedName;
    if (view.moveMode && dragFinalPos && isGdmlObject(name)) {
      postGdml({ op: 'set_position', name, value: dragFinalPos }).then((res) => {
        showToast(res && res.ok ? t('toast.positionUpdated') : (t('error.generic') + ((res && res.error) || '')));
      });
    } else if (view.rotateMode && dragFinalRot && isGdmlObject(name)) {
      postGdml({ op: 'set_rotation', name, value: dragFinalRot }).then((res) => {
        showToast(res && res.ok ? t('toast.rotationUpdated') : (t('error.generic') + ((res && res.error) || '')));
      });
    } else if (dragStartScene) {
      pushSnapshot(dragStartScene);
    }
  } else if (dragging && dragStartScreen && (view.moveMode || view.rotateMode) && view.selectedPrimIndex >= 0) {
    if (dragStartScene) pushSnapshot(dragStartScene);
  } else if (dragging && dragStartScreen && view.moveMode && view.selectedSourceName && dragFinalPos) {
    const name = view.selectedSourceName;
    const oldPos = dragStartObjPos ? [...dragStartObjPos] : getSourcePos(name);
    const newPos = [...dragFinalPos];
    postSourcePos(name, newPos);
    if (oldPos) pushCommand({
      label: t('cmd.moveSource'),
      undo: () => postSourcePos(name, oldPos),
      redo: () => postSourcePos(name, newPos),
    });
  } else if (dragging && dragStartScreen && phantomMoveActive && view.moveMode) {
    phantomMoveActive = false;
    view.moveMode = false;
    view.coordMode = false;
    moveBtn.style.background = '#3a3a3a';
    moveBtn.style.color = '#fff';
    setCursor('default');
    const center = phantomCenter();
    try { window.parent.postMessage({ type: 'g4care-phantom-position', position: center }, '*'); } catch (e) { /* ignore */ }
  }
  dragStartScreen = null; dragFinalPos = null; dragFinalRot = null; dragStartScene = null;
  phantomStartVerts = null;
  pressPos = null;
  dragging = false;
  setCursor('default');
});
interactor.onMiddleButtonPress((e) => { panPos = [e.position.x, e.position.y]; setCursor('move'); });
interactor.onMiddleButtonRelease(() => { panPos = null; setCursor('default'); });

// Hover via DOM mousemove on the container (the vtk.js canvas has pointer-events
// disabled, so events land on the container).
const containerEl = fullScreen.getContainer();
let cachedHoverHit = null;
let lastHoverPickTime = 0;
containerEl.addEventListener('mousemove', (e) => {
  if (dragging) return;
  const canvas = containerEl.querySelector('canvas');
  const rect = containerEl.getBoundingClientRect();
  const x = (e.clientX - rect.left) * (canvas.width / rect.width);
  const y = canvas.height - (e.clientY - rect.top) * (canvas.height / rect.height);
  // Throttled pick (responsive highlight/coords, bounded ray-cast cost).
  const now = performance.now();
  if (now - lastHoverPickTime >= 40) {
    lastHoverPickTime = now;
    cachedHoverHit = pickAt(x, y);
  }
  doHover(x, y, cachedHoverHit);
  updateFaceHover(cachedHoverHit);
  updateCursorPosition(x, y, cachedHoverHit);
  updateCoordPanel(x, y);
  if (view.sourcePlaceMode && view.sourcePts.length) {
    const sp = resolvePoint(x, y) || projectToGridPlane(x, y);
    if (sp) updateSourcePreview(sp);
  }
});
containerEl.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  const canvas = containerEl.querySelector('canvas');
  if (!canvas) return;
  const rect = containerEl.getBoundingClientRect();
  const x = (e.clientX - rect.left) * (canvas.width / rect.width);
  const y = canvas.height - (e.clientY - rect.top) * (canvas.height / rect.height);
  const idx = pickAnnotation(x, y);
  if (idx >= 0) showAnnotationMenu(e.clientX, e.clientY, idx);
});

function clearClientSceneState() {
  // Clear client-only state (primitives, points, planes, measurements) so that
  // opening a different YAML/scene does not leave stale artifacts behind.
  // Wrapped in try/catch so it can never break loadGeometry().
  try {
    multiSel.clear();
    selAnchor = null;
    // Remove prim actors BEFORE clearing the list (updatePrims needs it intact).
    for (const p of prims) {
      if (p.actor) { renderer.removeActor(p.actor); primActorMap.delete(p.actor); p.actor = null; }
    }
    prims.length = 0;
    resetPrimMode();
    constraints.length = 0;
    clearDimensions();
    snap.auxPoints.length = 0;
    snap.auxMeta.length = 0;
    updateAuxMarkers();
    section.planes = [];
    section.active = 0;
    section.enabled = false;
    // sectionPanel / sectionBtn live in tools/section.js (module scope); use the
    // exported setter instead of touching them directly (ReferenceError).
    setSectionEnabled(false);
    rebuildPlaneListUI();
    updatePlaneVisuals();
    clearRuler();
    clearMeasureExtras();
    view.angleMode = false; view.mindistMode = false; view.thicknessMode = false;
    view.anglePts = []; view.mindistNames = [];
    // planeModeSel / planeBuildBtn / planeBuildHint live in tools/section.js
    // (module scope) — reset them via the exported helper (ReferenceError).
    resetPlaneBuild();
  } catch (e) {
    console.error('clearClientSceneState', e);
  }
}

// (scene loading/polling lives in ./scene/io.js)

// (scene polling lives in ./scene/io.js)

// ── object tree / layers + properties panel ──
const sidePanel = document.createElement('div');
sidePanel.setAttribute('data-ui', '');
sidePanel.style.cssText = 'position:absolute;top:0;right:0;bottom:0;z-index:10001;width:300px;min-width:220px;max-width:75vw;background:#14161a;border-left:1px solid #343941;color:#e2e5ea;font:12px Arial;overflow:auto;padding:8px;box-sizing:border-box;';
let treeFilter = '';
let treeTypeFilter = '';
const searchInput = document.createElement('input');
searchInput.type = 'text';
searchInput.placeholder = t('ph.search');
onLocaleChange(() => { searchInput.placeholder = t('ph.search'); });
searchInput.style.cssText = 'flex:1;min-width:0;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:3px 6px;font:12px Arial;';
searchInput.addEventListener('input', () => { treeFilter = searchInput.value.toLowerCase(); updateTreeUI(); });
const searchRow = document.createElement('div');
searchRow.style.cssText = 'display:flex;gap:4px;margin-bottom:4px;align-items:center;';
const exportMatsBtn = document.createElement('button');
exportMatsBtn.appendChild(icon('database', 13));
exportMatsBtn.setAttribute('data-i18n-title', 'tip.exportMats'); exportMatsBtn.title = t('tip.exportMats');
exportMatsBtn.style.cssText = 'flex:none;background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 9px;cursor:pointer;font:13px Arial;line-height:1;';
exportMatsBtn.addEventListener('pointerdown', (e) => {
  e.stopPropagation(); e.preventDefault();
  exportAllGdmlMaterials().then((n) => {
    showToast(n > 0 ? (t('mat.exportedCount') + n) : t('mat.noneToExport'));
    updateProps(); updateTreeUI();
  });
});
searchRow.appendChild(searchInput);
searchRow.appendChild(exportMatsBtn);
const typeFilter = document.createElement('select');
typeFilter.style.cssText = 'width:100%;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;margin-bottom:6px;font:12px Arial;';
[['filter.all', ''], ['box', 'box'], ['sphere', 'sphere'], ['cylinder', 'cylinder'], ['cone', 'cone'], ['torus', 'torus'], ['filter.point', 'point'], ['filter.plane', 'plane'], ['filter.prim', 'prim'], ['filter.label', 'label']].forEach(([k, v]) => { const o = document.createElement('option'); o.value = v; if (k.startsWith('filter.')) bindI18n(o, k); else o.textContent = k; typeFilter.appendChild(o); });
typeFilter.addEventListener('change', () => { treeTypeFilter = typeFilter.value; updateTreeUI(); });

const layerTitle = document.createElement('div');
layerTitle.style.cssText = 'font-weight:bold;margin-bottom:4px;color:#e8752a;display:flex;align-items:center;gap:6px;';
bindI18n(layerTitle, 'label.layers');
const addLayerBtn = document.createElement('button');
addLayerBtn.textContent = '+';
addLayerBtn.style.cssText = btnStyle;
addLayerBtn.setAttribute('data-i18n-title', 'tip.createLayer'); addLayerBtn.title = t('tip.createLayer');
addLayerBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); promptText(t('layer.name'), '', (name) => { if (name) createLayer(name); }); });
layerTitle.appendChild(addLayerBtn);
const layerList = document.createElement('div');
layerList.style.cssText = 'display:flex;flex-direction:column;margin-bottom:6px;';
const treeTitle = document.createElement('div');
bindI18n(treeTitle, 'label.objects');
treeTitle.style.cssText = 'font-weight:bold;margin-bottom:4px;color:#e8752a;';
const treeList = document.createElement('div');
treeList.style.cssText = 'display:flex;flex-direction:column;';
const propsTitle = document.createElement('div');
bindI18n(propsTitle, 'label.properties');
propsTitle.style.cssText = 'font-weight:bold;margin-bottom:4px;color:#e8752a;';
const propsContent = document.createElement('div');
propsContent.style.cssText = 'font:12px Arial;color:#969da8;line-height:1.55;';
const sourceTitle = document.createElement('div');
bindI18n(sourceTitle, 'label.sources');
sourceTitle.style.cssText = 'font-weight:bold;margin-bottom:4px;color:#e8752a;';
const sourceList = document.createElement('div');
sourceList.style.cssText = 'display:flex;flex-direction:column;';

// ── Sidebar tabs: Древо / Слои / Свойства / Источники ──
const tabBar = document.createElement('div');
tabBar.setAttribute('data-ui', '');
tabBar.style.cssText = 'display:flex;gap:2px;margin-bottom:6px;border-bottom:1px solid #343941;padding-bottom:4px;';
const tabPanels = {};
const tabButtons = {};
const TAB_DEFS = [['tab.tree', 'tree'], ['tab.layers', 'layers'], ['tab.props', 'props'], ['tab.sources', 'sources']];
let activeTabKey = 'tree';
let panelCollapsed = false;
function activateTab(key) {
  activeTabKey = key;
  for (const [k, b] of Object.entries(tabButtons)) {
    const on = k === key;
    b.style.background = on ? '#e8752a' : '#141416';
    b.style.color = on ? '#1a1a1a' : '#777';
    tabPanels[k].style.display = panelCollapsed ? 'none' : (on ? 'flex' : 'none');
  }
}
TAB_DEFS.forEach(([labelKey, key]) => {
  const b = document.createElement('button');
  bindI18n(b, labelKey);
  b.style.cssText = 'flex:1;background:#202329;color:#777;border:1px solid #343941;border-radius:4px;padding:5px 2px;cursor:pointer;font:12px Arial;';
  b.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); activateTab(key); });
  tabBar.appendChild(b);
  tabButtons[key] = b;
  const p = document.createElement('div');
  p.style.cssText = 'display:none;flex-direction:column;';
  tabPanels[key] = p;
});
const collapseBtn = document.createElement('button');
collapseBtn.textContent = '»';
collapseBtn.setAttribute('data-i18n-title', 'tip.collapsePanel'); collapseBtn.title = t('tip.collapsePanel');
collapseBtn.style.cssText = 'background:#202329;color:#777;border:1px solid #343941;border-radius:4px;cursor:pointer;font:12px Arial;padding:0 8px;flex:none;';
collapseBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); panelCollapsed = !panelCollapsed; collapseBtn.textContent = panelCollapsed ? '«' : '»'; for (const [k, b] of Object.entries(tabButtons)) b.style.display = panelCollapsed ? 'none' : 'flex'; for (const [k, p] of Object.entries(tabPanels)) p.style.display = (panelCollapsed ? 'none' : (k === activeTabKey ? 'flex' : 'none')); sidePanel.style.width = panelCollapsed ? 'auto' : '300px'; sidePanel.style.minWidth = panelCollapsed ? '0' : '220px'; syncLayout(); });
tabBar.appendChild(collapseBtn);
sidePanel.appendChild(tabBar);
sidePanel.appendChild(tabPanels.tree);
sidePanel.appendChild(tabPanels.layers);
sidePanel.appendChild(tabPanels.props);
sidePanel.appendChild(tabPanels.sources);
tabPanels.tree.appendChild(searchRow);
tabPanels.tree.appendChild(typeFilter);
tabPanels.tree.appendChild(treeTitle);
tabPanels.tree.appendChild(treeList);
tabPanels.layers.appendChild(layerTitle);
tabPanels.layers.appendChild(layerList);
tabPanels.props.appendChild(propsTitle);
tabPanels.props.appendChild(propsContent);
tabPanels.sources.appendChild(sourceTitle);
tabPanels.sources.appendChild(sourceList);
activateTab('tree');
fullScreen.getContainer().appendChild(sidePanel);
shieldUi(sidePanel);

// Keep the properties sidebar aligned with the (collapsible) ribbon height.
function syncLayout() {
  if (!toolbar || !sidePanel || !viewportBar) return;
  sidePanel.style.top = (toolbar.offsetHeight || 0) + 'px';
  viewportBar.style.right = (sidePanel.offsetWidth || 0) + 'px';
}
syncLayout();
window.addEventListener('resize', syncLayout);

// Resize handle at the bottom-left corner (the panel hugs the right edge,
// so the bottom-right corner would be unreachable).
const resizeHandle = document.createElement('div');
resizeHandle.style.cssText = 'position:absolute;left:0;bottom:0;width:14px;height:14px;cursor:nesw-resize;background:rgba(150,150,160,0.4);border-radius:0 0 0 3px;';
resizeHandle.setAttribute('data-i18n-title', 'tip.resize'); resizeHandle.title = t('tip.resize');
onLocaleChange(() => { resizeHandle.setAttribute('data-i18n-title', 'tip.resize'); resizeHandle.title = t('tip.resize'); });
resizeHandle.addEventListener('pointerdown', (e) => {
  e.stopPropagation();
  e.preventDefault();
  const startX = e.clientX, startY = e.clientY;
  const startW = sidePanel.offsetWidth, startH = sidePanel.offsetHeight;
  const move = (ev) => {
    // If the button was released but pointerup was missed (e.g. the cursor left
    // the window / entered DevTools), stop the drag instead of getting stuck.
    if (!(ev.buttons & 1)) { up(); return; }
    const w = Math.max(220, Math.min(innerWidth * 0.75, startW - (ev.clientX - startX)));
    const h = Math.max(140, Math.min(innerHeight - 16, startH + (ev.clientY - startY)));
    sidePanel.style.width = w + 'px';
    sidePanel.style.height = h + 'px';
    syncLayout();
  };
  const up = () => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', up);
    window.removeEventListener('blur', up);
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', up);
  window.addEventListener('blur', up);
});
sidePanel.appendChild(resizeHandle);

function makeTreeRow(label, vis, onEye, onSelect, onContext, opts) {
  const row = document.createElement('div');
  row.style.cssText = 'display:flex;align-items:center;gap:4px;cursor:pointer;padding:1px 4px;border-radius:3px;user-select:none;';
  row.addEventListener('mouseenter', () => { if (!row._sel) row.style.background = '#333'; });
  row.addEventListener('mouseleave', () => { if (!row._sel) row.style.background = 'transparent'; });
  if (onContext) {
    row.addEventListener('contextmenu', (e) => {
      e.stopPropagation(); e.preventDefault();
      if (opts && opts.selectable) {
        // Nautilus rule: right-click on an unselected row selects only it;
        // right-click on a selected row keeps the whole selection.
        const objs = opts.objs || [];
        const all = objs.length && objs.every((o) => multiSel.has(o));
        if (!all) {
          multiSel.clear();
          for (const o of objs) if (objectMeta.has(o)) multiSel.add(o);
          selAnchor = opts.selKey;
          if (opts.onPick) opts.onPick();
          else { view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1; }
          applySelectionHighlight();
          applyHighlight();
          updateProps();
        }
      }
      if (opts && opts.selectable) dbg('SEL ctxmenu key=' + opts.selKey + ' sel=' + JSON.stringify([...multiSel]));
      onContext(e);
    });
  }
  // Expand/collapse chevron for collapsible (folder-like) nodes.
  if (opts && opts.expandable) {
    const ch = document.createElement('span');
    ch.innerHTML = iconHtml(opts.expanded ? 'chevron-down' : 'chevron-right', 11);
    ch.style.cssText = 'width:12px;flex:none;color:#777;font:12px Arial;text-align:center;cursor:pointer;';
    ch.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); if (e.button !== 0) return; opts.onToggle(); });
    row.appendChild(ch);
  }
  const eye = document.createElement('button');
  eye.textContent = vis ? '👁' : '·';
  eye.style.cssText = 'background:none;border:none;color:#777;cursor:pointer;font:12px Arial;padding:0;width:16px;flex:none;';
  eye.title = vis ? t('ctx.hide') : t('ctx.show');
  eye.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); if (e.button !== 0) return; onEye(); });
  const lbl = document.createElement('span');
  lbl.textContent = label;
  lbl.style.cssText = 'flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;';
  lbl.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    if (e.button !== 0) return; // only left-click selects; ПКМ keeps the selection for the menu
    if (opts && opts.selectable) {
      // File-manager selection: click = select only, Ctrl/Cmd = toggle, Shift = range.
      if (e.shiftKey) rangeSelect(opts.selKey);
      else if (e.ctrlKey || e.metaKey) toggleSelect(opts.selKey);
      else selectOnly(opts.selKey);
    } else if (onSelect) {
      onSelect();
    }
  });
  row.appendChild(eye);
  row.appendChild(lbl);
  if (opts && opts.selectable) {
    row._selKey = opts.selKey;
    row._selObjs = opts.objs || [];
    selRowEls.set(opts.selKey, row);
    selectableRows.push({ key: opts.selKey, objs: opts.objs || [], onPick: opts.onPick });
  }
  return row;
}

// File-manager-style multi-select (no checkboxes): rows are highlighted,
// plain click = select only, Ctrl/Cmd-click = toggle, Shift-click = range.
let selAnchor = null;             // anchor row key for Shift-range selection
const selectableRows = [];        // ordered [{ key, objs, onPick }], rebuilt with the tree
const selRowEls = new Map();      // row key -> DOM element (for highlight)

function applyPick(d) {
  if (d && d.onPick) d.onPick();
  else { view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1; }
}

function selectOnly(key) {
  multiSel.clear();
  const d = selectableRows.find((r) => r.key === key);
  if (d) for (const o of d.objs) if (objectMeta.has(o)) multiSel.add(o);
  selAnchor = key;
  applyPick(d);
  selectionChanged();
}

function toggleSelect(key) {
  const d = selectableRows.find((r) => r.key === key);
  if (!d) return;
  const all = d.objs.length && d.objs.every((o) => multiSel.has(o));
  if (all) for (const o of d.objs) multiSel.delete(o);
  else for (const o of d.objs) if (objectMeta.has(o)) multiSel.add(o);
  selAnchor = key;
  applyPick(d);
  selectionChanged();
}

function rangeSelect(key) {
  let ai = selectableRows.findIndex((r) => r.key === selAnchor);
  let bi = selectableRows.findIndex((r) => r.key === key);
  if (ai < 0) { ai = bi; selAnchor = key; }
  if (bi < 0) return;
  const lo = Math.min(ai, bi), hi = Math.max(ai, bi);
  multiSel.clear();
  for (let i = lo; i <= hi; i++) for (const o of selectableRows[i].objs) if (objectMeta.has(o)) multiSel.add(o);
  applyPick(selectableRows[bi]);
  selectionChanged();
}

function clearSelection() {
  multiSel.clear();
  selAnchor = null;
  dbg('SEL clearSelection');
  view.selectedName = null;
  view.selectedPlaneIndex = -1;
  view.selectedAuxIndex = -1;
  view.selectedPrimIndex = -1;
  applySelectionHighlight();
  applyHighlight();
  updateProps();
}

function selectionChanged() {
  dbg('SEL selectionChanged sel=' + JSON.stringify([...multiSel]) + ' anchor=' + selAnchor);
  applySelectionHighlight();
  applyHighlight();
  updateProps();
}

function applySelectionHighlight() {
  for (const [key, row] of selRowEls) {
    const objs = row._selObjs || [];
    const n = objs.length;
    const all = n && objs.every((o) => multiSel.has(o));
    const some = n && !all && objs.some((o) => multiSel.has(o));
    row._sel = all || some;
    row.style.background = all ? '#3a5f8a' : (some ? '#4a4420' : 'transparent');
    row.style.color = all ? '#eaf4ff' : (some ? '#ffe9c0' : '');
  }
}

function selectionVolumes() {
  const vols = new Set();
  for (const n of multiSel) {
    const meta = objectMeta.get(n);
    if (meta) vols.add(meta.volume || n);
  }
  return [...vols];
}

// Right-click context menu for a geometry object in the tree.
let treeCtxMenu = null;
function closeTreeCtxMenu() {
  if (treeCtxMenu) { treeCtxMenu.remove(); treeCtxMenu = null; }
}

function showTreeCtxMenu(e, name) {
  const meta = objectMeta.get(name);
  if (!meta) return;
  closeTreeCtxMenu();
  const menu = document.createElement('div');
  treeCtxMenu = menu;
  menu.style.cssText = 'position:fixed;left:' + e.clientX + 'px;top:' + e.clientY + 'px;background:#26262b;border:1px solid #343941;border-radius:5px;padding:4px;z-index:10020;box-shadow:0 4px 12px rgba(0,0,0,.5);';
  const mk = (label, fn, danger) => {
    const b = document.createElement('button');
    b.textContent = label;
    b.style.cssText = 'display:block;width:100%;text-align:left;background:transparent;color:' + (danger ? '#f0b0b0' : '#ddd') + ';border:0;border-radius:3px;padding:5px 10px;font:12px Arial;cursor:pointer;';
    b.addEventListener('pointerdown', (ev) => { ev.stopPropagation(); ev.preventDefault(); });
    b.addEventListener('click', () => { closeTreeCtxMenu(); fn(); });
    menu.appendChild(b);
  };
  mk(t('ctx.properties'), () => {
    view.selectedName = name; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1;
    applyHighlight(); activateTab('props'); updateProps();
  });
  mk(meta.visible !== false ? t('ctx.hide') : t('ctx.show'), () => {
    setVolumeVisible(name, meta.visible === false); updateTreeUI(); renderWindow.render();
  });
  const mat = meta.material;
  if (mat && materialsData.gdml.some((m) => m.name === mat)) {
    mk(t('ctx.exportMaterial', {mat}), () => {
      exportGdmlMaterial(mat).then(() => { updateProps(); updateTreeUI(); });
    });
  }
  mk(t('ctx.saveGdml'), () => {
    try { window.parent.postMessage({ type: 'g4care-export-gdml-single', volume: meta.volume || name }, '*'); } catch (err) { /* ignore */ }
  });
  mk(t('ctx.deselect'), () => {
    view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1;
    applyHighlight(); updateProps();
  });
  document.body.appendChild(menu);
  const onDown = (ev) => { if (!menu.contains(ev.target)) closeTreeCtxMenu(); };
  document.addEventListener('pointerdown', onDown);
  document.addEventListener('contextmenu', onDown, { once: true });
}

function toggleLayer(names, vis) {
  setVolumesVisible(names, vis);
  updateTreeUI();
  updateLayerUI();
  renderWindow.render();
}

function collectLayers() {
  const s = new Set(view.globalLayers);
  for (const [, meta] of objectMeta) if (meta.layer) s.add(meta.layer);
  for (const l of customLayers) s.add(l);
  return [...s].sort();
}
function saveLayersToYaml() {
  postUpdate({ layers: collectLayers() });
}
function objectsInLayer(layer) {
  const out = [];
  for (const [name, meta] of objectMeta) if (meta.layer === layer) out.push(name);
  return out;
}
function updateLayerUI() {
  layerList.innerHTML = '';
  const layers = collectLayers();
  if (!layers.length) { layerList.innerHTML = '<span style="color:#777;">' + t('layer.none') + '</span>'; return; }
  for (const layer of layers) {
    const names = objectsInLayer(layer);
    const allVisible = names.every((n) => objectMeta.get(n).visible !== false);
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:4px;padding:1px 2px;';
    const eye = document.createElement('button');
    eye.textContent = allVisible ? '👁' : '·';
    eye.style.cssText = 'background:none;border:none;color:#777;cursor:pointer;font:12px Arial;padding:0;width:16px;flex:none;';
    eye.title = allVisible ? t('layer.hide') : t('layer.show');
    eye.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); toggleLayer(names, !allVisible); });
    const lbl = document.createElement('span');
    lbl.textContent = layer;
    lbl.style.cssText = 'flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#969da8;';
    const ren = document.createElement('button');
    ren.innerHTML = iconHtml('pencil', 12);
    ren.style.cssText = 'background:none;border:none;color:#777;cursor:pointer;font:12px Arial;padding:0;';
    ren.setAttribute('data-i18n-title', 'tip.rename'); ren.title = t('tip.rename');
    ren.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); promptText(t('layer.newName'), layer, (nv) => { if (nv && nv !== layer) renameLayer(layer, nv); }); });
    const del = document.createElement('button');
    del.textContent = '×';
    del.style.cssText = 'background:none;border:none;color:#f0b0b0;cursor:pointer;font:12px Arial;padding:0;';
    del.setAttribute('data-i18n-title', 'tip.deleteLayer'); del.title = t('tip.deleteLayer');
    del.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); deleteLayer(layer); });
    row.appendChild(eye);
    row.appendChild(lbl);
    row.appendChild(ren);
    row.appendChild(del);
    layerList.appendChild(row);
  }
}
function createLayer(name) {
  if (!name) return;
  customLayers.add(name);
  updateLayerUI();
  saveLayersToYaml();
}
function renameLayer(oldName, newName) {
  if (customLayers.has(oldName)) { customLayers.delete(oldName); customLayers.add(newName); }
  for (const [name, meta] of objectMeta) {
    if (meta.layer === oldName) { meta.layer = newName; postUpdate({ name, layer: newName }); }
  }
  updateLayerUI();
  updateTreeUI();
  saveLayersToYaml();
}
function deleteLayer(name) {
  customLayers.delete(name);
  for (const [n, meta] of objectMeta) {
    if (meta.layer === name) { meta.layer = ''; postUpdate({ name: n, layer: '' }); }
  }
  updateLayerUI();
  updateTreeUI();
  saveLayersToYaml();
}

function layerSelect(current, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  bindI18n(lbl, 'label.layer');
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const sel = document.createElement('select');
  sel.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;max-width:150px;';
  const empty = document.createElement('option');
  empty.value = '';
  empty.textContent = '—';
  sel.appendChild(empty);
  collectLayers().forEach((l) => { const o = document.createElement('option'); o.value = l; o.textContent = l; sel.appendChild(o); });
  sel.value = current || '';
  const newOpt = document.createElement('option');
  newOpt.value = '__new__';
  bindI18n(newOpt, 'label.new');
  sel.appendChild(newOpt);
  sel.addEventListener('change', () => {
    if (sel.value === '__new__') {
      sel.value = current || '';
      promptText(t('layer.name'), '', (name) => { if (name) { customLayers.add(name); updateLayerUI(); onChange(name); } });
    } else {
      onChange(sel.value);
    }
  });
  wrap.appendChild(lbl);
  wrap.appendChild(sel);
  return wrap;
}


let phantomMoveActive = false;
function capturePhantomVerts() {
  if (!mergedMesh.pd) return null;
  const dicomNames = new Set();
  for (const [name, meta] of objectMeta) if (meta.source && meta.source.kind === 'dicom') dicomNames.add(name);
  const cells = new Set();
  for (const name of dicomNames) {
    const cs = mergedMesh.nameToCells.get(name);
    if (cs) for (const ci of cs) cells.add(ci);
  }
  const verts = new Set();
  const data = mergedMesh.pd.getPolys().getData();
  let off = 0, ci = 0;
  while (off < data.length) {
    const n = data[off];
    if (cells.has(ci)) for (let k = 0; k < n; k++) verts.add(data[off + 1 + k]);
    off += n + 1;
    ci++;
  }
  const pts = mergedMesh.pd.getPoints();
  const out = [];
  for (const vi of verts) out.push([vi, pts.getPoint(vi)]);
  return out;
}
function applyPhantomVerts(verts, delta) {
  const pts = mergedMesh.pd.getPoints();
  for (const v of verts) pts.setPoint(v[0], v[1][0] + delta[0], v[1][1] + delta[1], v[1][2] + delta[2]);
  pts.modified();
  renderWindow.render();
}

function phantomCenter() {
  let mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
  for (const d of mergedMesh.defs) {
    if (!d || !d.source || d.source.kind !== 'dicom') continue;
    for (let i = 0; i + 2 < d.vertices.length; i += 3) {
      for (let a = 0; a < 3; a++) {
        if (d.vertices[i + a] < mn[a]) mn[a] = d.vertices[i + a];
        if (d.vertices[i + a] > mx[a]) mx[a] = d.vertices[i + a];
      }
    }
  }
  if (mn[0] > mx[0]) return [0, 0, 0];
  return [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2];
}

// ── Scene-graph groups (group → section → block hierarchy) ──
function postGroup(op) {
  return fetch('/api/groups', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(op),
  }).then((r) => r.json());
}
function groupChildGroups(gname) {
  const g = groups.get(gname);
  if (!g) return [];
  return (g.children || []).filter((c) => groups.has(c));
}
function groupDirectObjects(gname) {
  const g = groups.get(gname);
  if (!g) return [];
  return (g.children || []).filter((c) => !groups.has(c) && objectMeta.has(c));
}
function groupAllObjects(gname, acc, visited) {
  const seen = acc || new Set();
  const visitedG = visited || new Set();
  if (visitedG.has(gname)) return [...seen];
  visitedG.add(gname);
  const g = groups.get(gname);
  if (!g) return [...seen];
  for (const c of g.children || []) {
    if (groups.has(c)) groupAllObjects(c, seen, visitedG);
    else if (objectMeta.has(c)) seen.add(c);
  }
  return [...seen];
}
function groupRootNames() {
  const roots = [];
  for (const gname of groups.keys()) {
    let isChild = false;
    for (const g of groups.values()) if ((g.children || []).includes(gname)) { isChild = true; break; }
    if (!isChild) roots.push(gname);
  }
  return roots;
}
function objectGroupName(objName) {
  for (const [gname, g] of groups) if ((g.children || []).includes(objName)) return gname;
  return null;
}
function mergeSelectedIntoGroup() {
  const objNames = [...multiSel].filter((n) => objectMeta.has(n));
  if (!objNames.length) return;
  promptText(t('group.newName'), t('group.defaultName'), (gname) => {
    if (!gname) return;
    if (groups.has(gname)) { showToast(t('group.exists', {name: gname})); return; }
    postGroup({ op: 'merge', name: gname, members: objNames }).then((res) => {
      if (res && res.ok) {
        groups.set(gname, { parent: null, children: objNames });
        for (const [gn, g] of groups) if (gn !== gname) g.children = (g.children || []).filter((c) => !objNames.includes(c));
        multiSel.clear();
        updateTreeUI();
        showToast(t('group.created', {name: gname}));
      } else showToast(t('error.generic') + ((res && res.error) || ''));
    });
  });
}
function moveMembersIntoGroup(gname, members) {
  return postGroup({ op: 'moveMembers', name: gname, members });
}
function addGroupCtxItems(gname) {
  addCtxItem(t('ctx.rename'), () => {
    promptText(t('group.newName'), gname, (nv) => {
      if (!nv || nv === gname) return;
      postGroup({ op: 'rename', name: gname, newName: nv }).then((res) => {
        if (res && res.ok) {
          groups.set(nv, groups.get(gname));
          groups.delete(gname);
          updateTreeUI();
        } else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  });
  const objs = groupAllObjects(gname);
  const allVis = objs.length && objs.every((n) => objectMeta.get(n).visible !== false);
  addCtxItem(allVis ? t('ctx.hide') : t('ctx.show'), () => {
    setVolumesVisible(objs, !allVis);
    updateTreeUI();
    renderWindow.render();
  });
  addCtxItem(t('group.createSub'), () => {
    promptText(t('group.subName'), '', (sub) => {
      if (!sub) return;
      postGroup({ op: 'create', name: sub, parent: gname, children: [] }).then((res) => {
        if (res && res.ok) {
          groups.set(sub, { parent: gname, children: [] });
          const g = groups.get(gname);
          if (g) g.children = (g.children || []).concat([sub]);
          updateTreeUI();
        } else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  });
  if (multiSel.size) {
    addCtxItem(t('group.moveHere', {n: multiSel.size}), () => {
      const members = [...multiSel].filter((n) => objectMeta.has(n));
      if (!members.length) return;
      moveMembersIntoGroup(gname, members).then((res) => {
        if (res && res.ok) {
          const g = groups.get(gname);
          g.children = (g.children || []).filter((c) => !members.includes(c)).concat(members);
          for (const [og, gg] of groups) if (og !== gname) gg.children = (gg.children || []).filter((c) => !members.includes(c));
          multiSel.clear();
          updateTreeUI();
        } else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  }
  addCtxItem(t('ctx.ungroup'), () => {
    postGroup({ op: 'delete', name: gname }).then((res) => {
      if (res && res.ok) { groups.delete(gname); updateTreeUI(); }
      else showToast(t('error.generic') + ((res && res.error) || ''));
    });
  });
  addCtxItem(t('group.deleteWithObjects'), () => {
    confirmAction(t('group.deleteConfirm', {name: gname, n: objs.length}), () => {
      const ops = objs.map((n) => ({ delete: { name: n } }));
      postGroup({ op: 'delete', name: gname }).then(() => { groups.delete(gname); });
      if (ops.length) postBatch(ops).then((res) => { showToast(res && res.ok ? t('toast.deleted') : (t('error.generic') + ((res && res.error) || ''))); });
      multiSel.clear();
      updateTreeUI();
    });
  });
}

function updateTreeUI() {
  treeList.innerHTML = '';
  selectableRows.length = 0;
  selRowEls.clear();
  const matches = (name, type) => {
    if (treeTypeFilter && treeTypeFilter !== type) return false;
    if (treeFilter && !String(name).toLowerCase().includes(treeFilter)) return false;
    return true;
  };
  // ── Геометрия (Geant4) ──
  const geoHeader = document.createElement('div');
  geoHeader.innerHTML = iconHtml('chevron-right', 10) + ' ' + t('tree.geometry');
  geoHeader.style.cssText = 'color:#777;margin-top:4px;font:10px Arial;letter-spacing:0.5px;';
  treeList.appendChild(geoHeader);
  const children = new Map();
  const roots = [];
  for (const [name, meta] of objectMeta) {
    if (!matches(name, meta.shape)) continue;
    // DICOM transfer volumes are grouped under the collapsible "Фантом" node.
    if (meta.source && meta.source.kind === 'dicom') continue;
    // Objects that belong to a scene-graph group are rendered inside that group.
    if (objectGroupName(name)) continue;
    // Replica instances belong to their array parent, not the world/mother.
    if (meta.source && meta.source.kind === 'replica' && meta.source.parent) {
      if (!children.has(meta.source.parent)) children.set(meta.source.parent, []);
      children.get(meta.source.parent).push(name);
      continue;
    }
    const mother = meta.mother;
    if (mother && objectMeta.has(mother) && mother !== name) {
      if (!children.has(mother)) children.set(mother, []);
      children.get(mother).push(name);
    } else {
      roots.push(name);
    }
  }
  const makeObjectRow = (name, depth) => {
    const meta = objectMeta.get(name);
    const prefix = depth ? '└ ' + '  '.repeat(depth - 1) : '';
    treeList.appendChild(makeTreeRow(prefix + name, meta.visible !== false, () => {
      setVolumeVisible(name, meta.visible === false);
      updateTreeUI();
      renderWindow.render();
    }, () => {
      view.selectedName = name;
      view.selectedPlaneIndex = -1;
      view.selectedAuxIndex = -1;
      view.selectedPrimIndex = -1;
      applyHighlight();
      updateProps();
    }, (e) => showTreeEntityMenu(e, 'object', name), {
      selectable: true,
      selKey: 'obj:' + name,
      objs: [name],
      onPick: () => {
        view.selectedName = name;
        view.selectedPlaneIndex = -1;
        view.selectedAuxIndex = -1;
        view.selectedPrimIndex = -1;
      },
    }));
  };
  const renderNode = (name, depth) => {
    makeObjectRow(name, depth);
    // Group repeated placements of the same logical volume (e.g. detector pixels)
    // under a collapsible node, like the Geant4 GUI volume tree.
    const kids = children.get(name) || [];
    // Replica copies (array instances) get their own collapsible "Массив" node.
    const replicaKids = kids.filter((k) => {
      const km = objectMeta.get(k);
      return km && km.source && km.source.kind === 'replica';
    });
    const realKids = kids.filter((k) => {
      const km = objectMeta.get(k);
      return !(km && km.source && km.source.kind === 'replica');
    });
    if (replicaKids.length) {
      const key = name + '::array';
      const expanded = expandedGroups.has(key);
      const allVis = replicaKids.every((m) => objectMeta.get(m).visible !== false);
      const gprefix = depth ? '└ ' + '  '.repeat(depth) : '';
      treeList.appendChild(makeTreeRow(gprefix + t('tree.copies', {n: replicaKids.length}), allVis, () => {
        setVolumesVisible(replicaKids, !allVis);
        updateTreeUI();
        renderWindow.render();
      }, () => {
        if (expanded) expandedGroups.delete(key); else expandedGroups.add(key);
        updateTreeUI();
      }, undefined, {
        expandable: true,
        expanded,
        onToggle: () => { if (expanded) expandedGroups.delete(key); else expandedGroups.add(key); updateTreeUI(); },
      }));
      if (expanded) replicaKids.forEach((m) => renderNode(m, depth + 1));
    }
    const byVolume = new Map();
    for (const k of realKids) {
      const km = objectMeta.get(k);
      const vol = km.volume || k;
      if (!byVolume.has(vol)) byVolume.set(vol, []);
      byVolume.get(vol).push(k);
    }
    for (const [vol, members] of byVolume) {
      if (members.length > 1) {
        const key = name + '::' + vol;
        const expanded = expandedGroups.has(key);
        const allVis = members.every((m) => objectMeta.get(m).visible !== false);
        const gprefix = depth ? '└ ' + '  '.repeat(depth) : '';
        treeList.appendChild(makeTreeRow(gprefix + vol + ' (' + members.length + ')', allVis, () => {
          setVolumesVisible(members, !allVis);
          updateTreeUI();
          renderWindow.render();
        }, () => {
          if (expanded) expandedGroups.delete(key); else expandedGroups.add(key);
          updateTreeUI();
        }, undefined, {
          expandable: true,
          expanded,
          onToggle: () => { if (expanded) expandedGroups.delete(key); else expandedGroups.add(key); updateTreeUI(); },
        }));
        if (expanded) members.forEach((m) => renderNode(m, depth + 2));
      } else {
        members.forEach((m) => renderNode(m, depth + 1));
      }
    }
  };
  // Scene-graph groups (collapsible; objects inside are rendered flat).
  const renderGroupNode = (gname, depth) => {
    const direct = groupDirectObjects(gname);
    const subGroups = groupChildGroups(gname);
    const allMembers = groupAllObjects(gname);
    const key = '__group__' + gname;
    const expanded = expandedGroups.has(key);
    const allVis = allMembers.length && allMembers.every((n) => objectMeta.get(n).visible !== false);
    const prefix = depth ? '└ ' + '  '.repeat(depth - 1) : '';
    treeList.appendChild(makeTreeRow(prefix + '🗂 ' + gname + ' (' + allMembers.length + ')', allVis, () => {
      setVolumesVisible(allMembers, !allVis);
      updateTreeUI();
      renderWindow.render();
    }, () => {}, (e) => showTreeEntityMenu(e, 'group', gname), {
      selectable: true,
      selKey: 'group:' + gname,
      objs: allMembers,
      expandable: true,
      expanded,
      onToggle: () => { if (expanded) expandedGroups.delete(key); else expandedGroups.add(key); updateTreeUI(); },
    }));
    if (expanded) {
      subGroups.slice().sort().forEach((gn) => renderGroupNode(gn, depth + 1));
      direct.slice().sort().forEach((n) => makeObjectRow(n, depth + 1));
    }
  };
  groupRootNames().sort().forEach((gn) => renderGroupNode(gn, 0));
  roots.sort().forEach((name) => renderNode(name, 0));
  // ── Фантом (DICOM transfer volumes, grouped) ──
  {
    const dicomObjs = [];
    for (const [name, meta] of objectMeta) {
      if (meta.source && meta.source.kind === 'dicom' && matches(name, meta.shape)) dicomObjs.push(name);
    }
    const ph = groups.phantom || {};
    const phantomLabel = (ph && ph.label) || t('tree.phantom');
    const phantomUngrouped = !!(ph && ph.ungrouped);
    if (dicomObjs.length && phantomUngrouped) {
      // Разгруппировано: show DICOM volumes as flat rows.
      dicomObjs.sort().forEach((n) => makeObjectRow(n, 0));
    } else if (dicomObjs.length) {
      const key = '__dicom_phantom__';
      const expanded = expandedGroups.has(key);
      const allVis = dicomObjs.every((n) => objectMeta.get(n).visible !== false);
      treeList.appendChild(makeTreeRow(phantomLabel + ' (' + dicomObjs.length + ')', allVis, () => {
        setVolumesVisible(dicomObjs, !allVis);
        updateTreeUI();
        renderWindow.render();
      }, () => {}, (e) => showTreeEntityMenu(e, 'phantom'), {
        selectable: true,
        selKey: '__phantom__',
        objs: dicomObjs,
        expandable: true,
        expanded,
        onToggle: () => { if (expanded) expandedGroups.delete(key); else expandedGroups.add(key); updateTreeUI(); },
      }));
      if (expanded) dicomObjs.sort().forEach((n) => renderNode(n, 1));
    }
  }
  // ── GUI (точки + плоскости) ──
  const guiHeader = document.createElement('div');
  guiHeader.innerHTML = iconHtml('chevron-right', 10) + ' GUI';
  guiHeader.style.cssText = 'color:#777;margin-top:6px;font:10px Arial;letter-spacing:0.5px;';
  treeList.appendChild(guiHeader);
  if (treeTypeFilter === '' || treeTypeFilter === 'point') {
    for (let i = 0; i < snap.auxPoints.length; i++) {
      const nm = (snap.auxMeta[i] && snap.auxMeta[i].name) || (t('prop.pointPrefix') + (i + 1));
      if (!matches(nm, 'point')) continue;
      treeList.appendChild(makeTreeRow(nm, true, () => {}, () => {
        view.selectedName = null;
        view.selectedPlaneIndex = -1;
        view.selectedAuxIndex = i;
        view.selectedPrimIndex = -1;
        applyHighlight();
        updateProps();
      }, (e) => showTreeEntityMenu(e, 'aux', i)));
    }
  }
  if (treeTypeFilter === '' || treeTypeFilter === 'label') {
    annotations.forEach((ann, i) => {
      const nm = ann.text || (t('tree.label') + (i + 1));
      if (treeFilter && !String(nm).toLowerCase().includes(treeFilter)) return;
      treeList.appendChild(makeTreeRow(nm, true, () => {}, () => {
        view.selectedName = null;
        view.selectedPlaneIndex = -1;
        view.selectedAuxIndex = -1;
        view.selectedPrimIndex = -1;
        view.selectedAnnotationIndex = i;
        applyHighlight();
        updateProps();
      }));
    });
  }
  if (treeTypeFilter === '' || treeTypeFilter === 'plane') {
    section.planes.forEach((p, i) => {
      const nm = p.name || (t('prop.planePrefix') + (i + 1));
      if (!matches(nm, 'plane')) return;
      treeList.appendChild(makeTreeRow(nm, p.visible !== false, () => {
        p.visible = p.visible === false;
        updatePlaneVisuals();
        updateActorMappers();
        updateTreeUI();
      }, () => {
        view.selectedName = null;
        view.selectedPlaneIndex = i;
        view.selectedAuxIndex = -1;
        view.selectedPrimIndex = -1;
        applyHighlight();
        updateProps();
      }, (e) => showTreeEntityMenu(e, 'plane', i)));
    });
  }
  if (treeTypeFilter === '' || treeTypeFilter === 'prim') {
    prims.forEach((p, i) => {
      if (treeFilter && !String(p.name).toLowerCase().includes(treeFilter)) return;
      treeList.appendChild(makeTreeRow(p.name, p.visible !== false, () => {
        p.visible = p.visible === false;
        updatePrims();
        updateTreeUI();
      }, () => {
        view.selectedName = null;
        view.selectedPlaneIndex = -1;
        view.selectedAuxIndex = -1;
        view.selectedPrimIndex = i;
        applyHighlight();
        updateProps();
      }, (e) => showTreeEntityMenu(e, 'prim', i)));
    });
  }
  // ── Зависимости (постоянные размеры) ──
  const dimExpanded = expandedGroups.has('dependencies');
  const dimHeader = document.createElement('div');
  dimHeader.style.cssText = 'display:flex;align-items:center;gap:6px;color:#777;margin-top:6px;font:10px Arial;letter-spacing:0.5px;';
  const dimToggle = document.createElement('span');
  dimToggle.innerHTML = iconHtml(dimExpanded ? 'chevron-down' : 'chevron-right', 10) + ' ' + t('tree.constraints') + ' (' + (dimensions.length + constraints.length) + ')';
  dimToggle.style.cssText = 'cursor:pointer;flex:1;';
  dimToggle.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); if (dimExpanded) expandedGroups.delete('dependencies'); else expandedGroups.add('dependencies'); updateTreeUI(); });
  dimHeader.appendChild(dimToggle);
  if (deleteSel.size) {
    const delSelBtn = document.createElement('button');
    delSelBtn.textContent = t('label.remove') + ' (' + deleteSel.size + ')';
    delSelBtn.setAttribute('data-i18n-title', 'tip.clearSelectedDeps'); delSelBtn.title = t('tip.clearSelectedDeps');
    delSelBtn.style.cssText = 'background:#5a2a2a;color:#f0b0b0;border:1px solid #855;border-radius:3px;padding:1px 6px;cursor:pointer;font:10px Arial;';
    delSelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); removeSelectedConstraints(); });
    dimHeader.appendChild(delSelBtn);
  }
  const clearBtn = document.createElement('button');
  bindI18n(clearBtn, 'tools.clear');
  clearBtn.setAttribute('data-i18n-title', 'tip.clearDeps'); clearBtn.title = t('tip.clearDeps');
  clearBtn.style.cssText = 'background:#202329;color:#f0b0b0;border:1px solid #855;border-radius:3px;padding:1px 6px;cursor:pointer;font:10px Arial;';
  clearBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const beforeConstraints = [...constraints];
    const beforeDims = [...dimensions];
    clearConstraints();
    clearDimensions();
    pushCommand({
      label: t('cmd.clearConstraints'),
      undo: () => { constraints.push(...beforeConstraints); dimensions.push(...beforeDims); deleteSel.clear(); updateTreeUI(); },
      redo: () => { clearConstraints(); clearDimensions(); deleteSel.clear(); updateTreeUI(); },
    });
    updateTreeUI();
  });
  dimHeader.appendChild(clearBtn);
  treeList.appendChild(dimHeader);
  if (dimExpanded) {
    dimensions.forEach((dim, i) => {
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;align-items:center;gap:6px;cursor:pointer;padding:1px 4px;border-radius:3px;';
      row.addEventListener('mouseenter', () => { row.style.background = '#333'; });
      row.addEventListener('mouseleave', () => { row.style.background = 'transparent'; });
      row.addEventListener('contextmenu', (e) => { e.stopPropagation(); e.preventDefault(); showTreeEntityMenu(e, 'dimension', dim); });
      const lbl = document.createElement('span');
      lbl.textContent = t('tools.dimension') + ' ' + (i + 1) + ': ' + fmtNum(dim.value) + ' ' + t('unit.mm');
      lbl.style.cssText = 'flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;';
      lbl.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); editDimensionValue(dim); });
      row.appendChild(lbl);
      treeList.appendChild(row);
    });
    constraints.forEach((c) => {
      const selKey = 'constraint:' + c.id;
      const selected = deleteSel.has(selKey);
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;align-items:center;gap:6px;cursor:pointer;padding:1px 4px;border-radius:3px;color:#aef;' + (selected ? 'background:#3a5f8a;' : '');
      row.addEventListener('mouseenter', () => { if (!selected) row.style.background = '#333'; });
      row.addEventListener('mouseleave', () => { if (!selected) row.style.background = 'transparent'; });
      row.addEventListener('contextmenu', (e) => { e.stopPropagation(); e.preventDefault(); showTreeEntityMenu(e, 'constraint', c); });
      const lbl = document.createElement('span');
      lbl.textContent = constraintLabel(c);
      lbl.style.cssText = 'flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;';
      lbl.addEventListener('pointerdown', (e) => {
        e.stopPropagation(); e.preventDefault();
        if (e.ctrlKey || e.metaKey) {
          if (deleteSel.has(selKey)) deleteSel.delete(selKey); else deleteSel.add(selKey);
          updateTreeUI();
          return;
        }
        if (e.shiftKey) { deleteSel.add(selKey); updateTreeUI(); return; }
      });
      row.appendChild(lbl);
      treeList.appendChild(row);
    });
  }
  applySelectionHighlight();
}

// (object property mutations live in ./panels/objects.js)

// (property-panel inputs live in ./ui/inputs.js)

// (property-panel inputs live in ./ui/inputs.js)

// (material discovery/export/create dialog live in ./scene/materials.js)

function materialInput(current, onChange) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex;align-items:center;gap:6px;margin:2px 0;';
  const lbl = document.createElement('span');
  bindI18n(lbl, 'label.material');
  lbl.style.cssText = 'width:92px;color:#9ac;';
  const sel = document.createElement('select');
  sel.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:3px;padding:2px 4px;font:12px Arial;max-width:150px;';
  const gNist = document.createElement('optgroup');
  gNist.label = 'NIST';
  const gUser = document.createElement('optgroup');
  gUser.label = t('tree.custom');
  materialsData.nist.forEach((m) => { const o = document.createElement('option'); o.value = m; o.textContent = m; gNist.appendChild(o); });
  materialsData.user.forEach((m) => { const o = document.createElement('option'); o.value = m; o.textContent = m; gUser.appendChild(o); });
  sel.appendChild(gNist);
  sel.appendChild(gUser);
  if (current && ![...sel.options].some((o) => o.value === current)) {
    const o = document.createElement('option');
    o.value = current;
    o.textContent = current;
    gUser.appendChild(o);
  }
  sel.value = current || '';
  const custom = document.createElement('option');
  custom.value = '__custom__';
  custom.textContent = 'custom…';
  sel.appendChild(custom);
  sel.addEventListener('change', () => {
    if (sel.value === '__custom__') {
      sel.value = current || '';
      openMaterialDialog(onChange);
    } else {
      onChange(sel.value);
    }
  });
  const del = document.createElement('button');
  del.textContent = '×';
  del.setAttribute('data-i18n-title', 'tip.deleteMaterial'); del.title = t('tip.deleteMaterial');
  del.style.cssText = 'background:none;border:1px solid #855;color:#f0b0b0;border-radius:3px;padding:0 6px;cursor:pointer;font:12px Arial;line-height:16px;';
  const syncDel = () => { del.style.visibility = materialsData.user.includes(sel.value) ? 'visible' : 'hidden'; };
  del.addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    e.preventDefault();
    const name = sel.value;
    if (!materialsData.user.includes(name)) return;
    confirmAction(t('confirm.deleteMaterial', { name }), () => {
      fetch('/api/material-delete', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name }) })
        .then((r) => r.json())
        .then((r) => {
          if (r.ok) {
            materialsData.user = materialsData.user.filter((m) => m !== name);
            updateProps();
          }
        }).catch(() => {});
    });
  });
  sel.addEventListener('change', syncDel);
  syncDel();
  wrap.appendChild(lbl);
  wrap.appendChild(sel);
  wrap.appendChild(del);
  return wrap;
}

function updateProps() {
  propsContent.innerHTML = '';
  if (view.selectedPrimIndex >= 0 && prims[view.selectedPrimIndex]) {
    const p = prims[view.selectedPrimIndex];
    propsContent.appendChild(infoRow(t('prop.type'), t('prop.figure')));
    propsContent.appendChild(textInput(t('prop.name'), p.name, (v) => { p.name = v; updateTreeUI(); }));
    propsContent.appendChild(layerSelect(p.layer || '', (v) => { p.layer = v; updateLayerUI(); }));
    if (p.type === 'line' && p.pts.length >= 2) {
      const len = Math.hypot(p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]);
      propsContent.appendChild(numInput(t('prop.length'), len, (n) => { const d = normalize3([p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]]); p.pts[1] = [p.pts[0][0] + d[0] * n, p.pts[0][1] + d[1] * n, p.pts[0][2] + d[2] * n]; updatePrims(); updateProps(); }));
    } else if ((p.type === 'circle' || p.type === 'arc') && p.pts.length >= 2) {
      const r = Math.hypot(p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]);
      propsContent.appendChild(numInput(t('prop.radius'), r, (n) => { const d = normalize3([p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]]); p.pts[1] = [p.pts[0][0] + d[0] * n, p.pts[0][1] + d[1] * n, p.pts[0][2] + d[2] * n]; updatePrims(); updateProps(); }));
    } else if (p.type === 'polygon' && p.pts.length >= 2) {
      const r = Math.hypot(p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]);
      propsContent.appendChild(numInput(t('prop.radius'), r, (n) => { const d = normalize3([p.pts[1][0] - p.pts[0][0], p.pts[1][1] - p.pts[0][1], p.pts[1][2] - p.pts[0][2]]); p.pts[1] = [p.pts[0][0] + d[0] * n, p.pts[0][1] + d[1] * n, p.pts[0][2] + d[2] * n]; updatePrims(); updateProps(); }));
      propsContent.appendChild(numInput(t('prop.vertices'), p.n, (n) => { p.n = Math.max(3, Math.round(n)); updatePrims(); updateProps(); }));
    } else if (p.type === 'rect' && p.pts.length >= 2) {
      const { u, v } = primBasis(p.normal);
      const dx = p.pts[1][0]-p.pts[0][0], dy = p.pts[1][1]-p.pts[0][1], dz = p.pts[1][2]-p.pts[0][2];
      const w = dx*u[0]+dy*u[1]+dz*u[2];
      const h = dx*v[0]+dy*v[1]+dz*v[2];
      propsContent.appendChild(numInput(t('prop.widthA'), w, (n) => { p.pts[1] = [p.pts[0][0]+u[0]*n+v[0]*h, p.pts[0][1]+u[1]*n+v[1]*h, p.pts[0][2]+u[2]*n+v[2]*h]; updatePrims(); updateProps(); }));
      propsContent.appendChild(numInput(t('prop.heightB'), h, (n) => { p.pts[1] = [p.pts[0][0]+u[0]*w+v[0]*n, p.pts[0][1]+u[1]*w+v[1]*n, p.pts[0][2]+u[2]*w+v[2]*n]; updatePrims(); updateProps(); }));
    } else if (p.type === 'ellipse' && p.pts.length >= 2) {
      const { u, v } = primBasis(p.normal);
      const cx = p.pts[0][0], cy = p.pts[0][1], cz = p.pts[0][2];
      const ea = (p.pts[1][0]-cx)*u[0] + (p.pts[1][1]-cy)*u[1] + (p.pts[1][2]-cz)*u[2];
      const eb = (p.pts[1][0]-cx)*v[0] + (p.pts[1][1]-cy)*v[1] + (p.pts[1][2]-cz)*v[2];
      propsContent.appendChild(numInput('a', Math.abs(ea), (n) => { const s = ea >= 0 ? 1 : -1; p.pts[1] = [cx+u[0]*s*n+v[0]*eb, cy+u[1]*s*n+v[1]*eb, cz+u[2]*s*n+v[2]*eb]; updatePrims(); updateProps(); }));
      propsContent.appendChild(numInput('b', Math.abs(eb), (n) => { const s = eb >= 0 ? 1 : -1; p.pts[1] = [cx+u[0]*ea+v[0]*s*n, cy+u[1]*ea+v[1]*s*n, cz+u[2]*ea+v[2]*s*n]; updatePrims(); updateProps(); }));
    } else if (p.type === 'grid') {
      propsContent.appendChild(numInput(t('prop.gridStep'), p.spacing || 10, (n) => { p.spacing = Math.max(1e-9, n); updatePrims(); updateProps(); }));
      propsContent.appendChild(numInput(t('prop.halfSize'), p.halfSize || 100, (n) => { p.halfSize = Math.max(1, n); updatePrims(); updateProps(); }));
    }
    if (p.type !== 'line') {
      propsContent.appendChild(vec3Input(t('prop.normal'), p.normal || [0, 0, 1], (nv) => {
        p.normal = normalize3(nv);
        updatePrims();
        updateProps();
      }));
    }
    const del = document.createElement('button');
    bindI18n(del, 'tip.deletePrim');
    del.style.cssText = 'background:#5a2a2a;color:#f0b0b0;border:1px solid #855;border-radius:4px;padding:3px 8px;cursor:pointer;font:12px Arial;margin-top:6px;';
    del.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      const idx = view.selectedPrimIndex;
      pushHistory();
      removePrimAtIndex(idx);
      updateProps();
    });
    propsContent.appendChild(del);
  } else if (view.selectedAuxIndex >= 0 && snap.auxPoints[view.selectedAuxIndex]) {
    const p = snap.auxPoints[view.selectedAuxIndex];
    const meta = snap.auxMeta[view.selectedAuxIndex] || (snap.auxMeta[view.selectedAuxIndex] = { name: t('prop.pointPrefix') + (view.selectedAuxIndex + 1), layer: '' });
    propsContent.appendChild(infoRow(t('prop.type'), t('prop.point')));
    propsContent.appendChild(textInput(t('prop.name'), meta.name, (v) => { meta.name = v; updateTreeUI(); }));
    propsContent.appendChild(layerSelect(meta.layer || '', (v) => { meta.layer = v; updateLayerUI(); }));
    propsContent.appendChild(vec3Input(t('prop.position'), p, (v) => {
      snap.auxPoints[view.selectedAuxIndex] = v;
      updateAuxMarkers();
      renderWindow.render();
    }));
    const del = document.createElement('button');
    bindI18n(del, 'tip.deletePoint');
    del.style.cssText = 'background:#5a2a2a;color:#f0b0b0;border:1px solid #855;border-radius:4px;padding:3px 8px;cursor:pointer;font:12px Arial;margin-top:6px;';
    del.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      pushHistory();
      snap.auxPoints.splice(view.selectedAuxIndex, 1);
      snap.auxMeta.splice(view.selectedAuxIndex, 1);
      view.selectedAuxIndex = -1;
      view.selectedPrimIndex = -1;
      updateAuxMarkers();
      updateProps();
    });
    propsContent.appendChild(del);
  } else if (view.selectedPlaneIndex >= 0 && section.planes[view.selectedPlaneIndex]) {
    const p = section.planes[view.selectedPlaneIndex];
    const n = p.plane.getNormal();
    const o = p.plane.getOrigin();
    propsContent.appendChild(infoRow(t('prop.type'), t('prop.plane')));
    const cutToggle = document.createElement('button');
    cutToggle.textContent = p.cut !== false ? t('prop.cutYes') : t('prop.cutNo');
    cutToggle.style.cssText = btnStyle;
    cutToggle.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      p.cut = p.cut === false;
      rebuildPlaneListUI();
      updateActorMappers();
      updateTreeUI();
      updateProps();
    });
    propsContent.appendChild(cutToggle);
    propsContent.appendChild(textInput(t('prop.name'), p.name || (t('prop.planePrefix') + (view.selectedPlaneIndex + 1)), (v) => { p.name = v; updateTreeUI(); }));
    propsContent.appendChild(layerSelect(p.layer || '', (v) => { p.layer = v; updateLayerUI(); }));
    propsContent.appendChild(vec3Input(t('prop.normal'), [...n], (v) => {
      p.plane.setNormal(v[0], v[1], v[2]);
      updatePlaneVisuals();
      updateActorMappers();
    }));
    propsContent.appendChild(vec3Input(t('prop.position'), [...o], (v) => {
      p.plane.setOrigin(v[0], v[1], v[2]);
      updatePlaneVisuals();
      updateActorMappers();
    }));
    propsContent.appendChild(opacityInput(t('prop.opacity'), p.opacity !== undefined && p.opacity !== null ? p.opacity : 0.25, (v) => {
      p.opacity = v;
      updatePlaneVisuals();
      renderWindow.render();
    }));
  } else if (view.selectedAnnotationIndex >= 0 && annotations[view.selectedAnnotationIndex]) {
    const ann = annotations[view.selectedAnnotationIndex];
    propsContent.appendChild(infoRow(t('prop.type'), t('prop.annotation')));
    propsContent.appendChild(textInput(t('prop.text'), ann.text || '', (v) => { ann.text = v; saveAnnotations(); renderAnnotations(); updateTreeUI(); }));
    propsContent.appendChild(vec3Input(t('prop.position'), ann.position || [0, 0, 0], (v) => { ann.position = v; saveAnnotations(); renderAnnotations(); }));
    const del = document.createElement('button');
    bindI18n(del, 'tip.deleteLabel');
    del.style.cssText = 'background:#5a2a2a;color:#f0b0b0;border:1px solid #855;border-radius:4px;padding:3px 8px;cursor:pointer;font:12px Arial;margin-top:6px;';
    del.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); deleteAnnotation(view.selectedAnnotationIndex); });
    propsContent.appendChild(del);
  } else if (view.selectedName && objectMeta.has(view.selectedName)) {
    const m = objectMeta.get(view.selectedName);
    if (isReplicaObject(view.selectedName)) {
      propsContent.appendChild(infoRow(t('prop.arrayElement'), view.selectedName));
      propsContent.innerHTML += '<div style="color:#8a8a8a;margin-top:6px;">' + t('prop.arrayReadonly') + '</div>';
    } else {
      propsContent.appendChild(infoRow(t('prop.name'), view.selectedName));
      propsContent.appendChild(textInput(t('prop.name'), view.selectedName, (v) => setObjectName(view.selectedName, v)));
      propsContent.appendChild(selectInput(t('prop.type'), PRIMITIVE_TYPES.map(([l, vv]) => [t(l), vv]), m.shape || 'box', (v) => setObjectType(view.selectedName, v)));
      propsContent.appendChild(materialInput(m.material || '', (mat) => { pushHistory(); setObjectMaterial(view.selectedName, mat); }));
      propsContent.appendChild(checkboxInput(t('prop.sensitive'), m.isSensitive, (v) => setObjectSensitive(view.selectedName, v)));
      propsContent.appendChild(infoRow(t('prop.dimensions'), fmtVec(m.dims)));
      propsContent.appendChild(vec3Input(t('prop.position'), m.pos || [0, 0, 0], (v) => setObjectPosition(view.selectedName, v)));
      propsContent.appendChild(vec3Input(t('prop.angles'), m.rot || [0, 0, 0], (v) => setObjectRotation(view.selectedName, v)));
      // Родитель (mother): ручной выбор + авто-определение по габаритам.
      const parentOptions = [[t('prop.world'), '']];
      for (const [n] of objectMeta) {
        if (n !== view.selectedName && !isReplicaObject(n)) parentOptions.push([n, n]);
      }
      propsContent.appendChild(selectInput(t('prop.parent'), parentOptions, m.mother || '', (v) => { pushHistory(); setObjectMother(view.selectedName, v); }));
      const autoParentBtn = document.createElement('button');
      bindI18n(autoParentBtn, 'tools.autoParent');
      autoParentBtn.setAttribute('data-i18n-title', 'tip.autoParent'); autoParentBtn.title = t('tip.autoParent');
      autoParentBtn.style.cssText = btnStyle + 'margin:2px 0;';
      autoParentBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); autoAssignParent(view.selectedName); });
      propsContent.appendChild(autoParentBtn);
      propsContent.appendChild(layerSelect(m.layer || '', (v) => setObjectLayer(view.selectedName, v)));
      propsContent.appendChild(rgbInput(t('prop.color'), baseColors.get(view.selectedName) || DEFAULT_COLOR, (rgb) => setObjectColor(view.selectedName, rgb)));
      const op = m.opacity !== undefined && m.opacity !== null ? m.opacity : 1;
      propsContent.appendChild(opacityInput(t('prop.opacity'), op, (v) => setObjectOpacity(view.selectedName, v)));
      // ── Диджитайзер (DIGITIZE) ──
      const dzSec = document.createElement('div');
      dzSec.style.cssText = 'margin-top:8px;border-top:1px solid #343941;padding-top:6px;';
      const dzHead = document.createElement('div');
      bindI18n(dzHead, 'tools.digitizer');
      dzHead.style.cssText = 'font-weight:bold;color:#e8752a;margin-bottom:2px;';
      const dzBody = document.createElement('div');
      dzBody.style.cssText = 'color:#969da8;font:12px Arial;margin-bottom:4px;';
      const dzBtn = document.createElement('button');
      dzBtn.innerHTML = iconHtml('pencil', 12) + ' ' + t('prop.configure');
      dzBtn.style.cssText = btnStyle;
      dzBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); openDigitizeEditor(view.selectedName); });
      dzSec.appendChild(dzHead);
      dzSec.appendChild(dzBody);
      dzSec.appendChild(dzBtn);
      propsContent.appendChild(dzSec);
      renderDigitizeSummary(view.selectedName, dzBody);
    }
  } else {
    propsContent.innerHTML = '<span style="color:#777;">' + t('prop.nothingSelected') + '</span>';
  }
}
onLocaleChange(updateProps);
onLocaleChange(updateTreeUI);
onLocaleChange(updateLayerUI);

// ── context menu (ПКМ) ──
const contextMenu = document.createElement('div');
contextMenu.setAttribute('data-ui', '');
contextMenu.style.cssText = 'position:absolute;z-index:10001;background:#202329;border:1px solid #343941;border-radius:6px;padding:4px;display:none;color:#e2e5ea;font:12px Arial;min-width:150px;box-shadow:0 4px 12px rgba(0,0,0,0.5);';
fullScreen.getContainer().appendChild(contextMenu);
shieldUi(contextMenu);

function addCtxItem(label, fn) {
  const it = document.createElement('div');
  it.textContent = label;
  it.style.cssText = fn ? 'padding:5px 10px;cursor:pointer;border-radius:4px;white-space:nowrap;' : 'padding:5px 10px;border-radius:4px;white-space:nowrap;color:#7a7a7a;cursor:default;';
  if (fn) {
    it.addEventListener('mouseenter', () => { it.style.background = '#3a5f8a'; });
    it.addEventListener('mouseleave', () => { it.style.background = 'transparent'; });
    it.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); hideContextMenu(); fn(); });
  }
  contextMenu.appendChild(it);
}

function hideContextMenu() {
  contextMenu.style.display = 'none';
}

function pickActorAt(x, y) {
  pointPicker.pick([x, y, 0], renderer);
  const picked = pointPicker.getActors();
  return picked.length ? picked[0] : null;
}

// ── context-menu item builders (shared by scene right-click and tree right-click) ──
function selectObject(name) { view.selectedName = name; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1; applyHighlight(); activateTab('props'); updateProps(); }
function selectPrim(i) { view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = i; applyHighlight(); activateTab('props'); updateProps(); }
function selectAux(i) { view.selectedAuxIndex = i; view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedPrimIndex = -1; activateTab('props'); updateProps(); }

function cutObjectByName(name) {
  const data = objectToYaml(name);
  clipboard = { kind: 'yaml', name, data };
  pendingPaste = true;
  if (view.selectedName === name) view.selectedName = null;
  constraintObjectRemoved(name);
  postBatch([{ delete: { name } }]).then((res) => {
    showToast(res && res.ok ? t('toast.cutDone') : (t('error.generic') + ((res && res.error) || '')));
  });
  applyHighlight();
  if (data) pushCommand({
    label: t('cmd.cutObject'),
    undo: () => { postBatch([{ insert: { object: data, newName: name, offset: [0, 0, 0] } }]); },
    redo: () => { postBatch([{ delete: { name } }]); },
  });
}

function deleteObjectByName(name) {
  confirmAction(t('obj.deleteConfirm', {name}), () => {
    const data = objectToYaml(name);
    if (view.selectedName === name) view.selectedName = null;
    if (multiSel.has(name)) multiSel.delete(name);
    constraintObjectRemoved(name);
    postBatch([{ delete: { name } }]).then((res) => {
      showToast(res && res.ok ? t('toast.objectDeleted') : (t('error.generic') + ((res && res.error) || '')));
    });
    applyHighlight();
    if (data) pushCommand({
      label: t('cmd.deleteObject'),
      undo: () => { postBatch([{ insert: { object: data, newName: name, offset: [0, 0, 0] } }]); },
      redo: () => { postBatch([{ delete: { name } }]); },
    });
  });
}

// ── Arrays (REPLICA: linear / grid / polar) ──
// Applies a replica spec to an object, persisting it and recording undo/redo.
function applyArray(name, newRep, prevRep) {
  const op = { replica: Object.assign({ name }, newRep) };
  const undoOp = prevRep ? { replica: Object.assign({ name }, prevRep) } : { replica: { name, count: 0 } };
  postBatch([op]).then((res) => {
    showToast(res && res.ok ? t('toast.arrayApplied') : (t('error.generic') + ((res && res.error) || '')));
  });
  pushCommand({
    label: t('cmd.array'),
    undo: () => { postBatch([undoOp]); },
    redo: () => { postBatch([op]); },
  });
}

function openArrayDialog(name, rep) {
  const cur = rep || { mode: 'linear', axis: 'x', count: 5, step: 10, offset: 0, nx: 3, ny: 3, nz: 1, dx: 10, dy: 10, dz: 10, radius: 50, startAngle: 0, totalAngle: 360, rotate: true };
  const overlay = document.createElement('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10002;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;';
  const box = document.createElement('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;padding:14px;width:340px;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';

  const title = document.createElement('div');
  title.textContent = t('label.replica') + ' — ' + name;
  title.style.cssText = 'margin-bottom:8px;font-weight:bold;';

  const modeSel = document.createElement('select');
  modeSel.style.cssText = 'width:100%;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:4px;margin-bottom:10px;';
  [[t('arr.linear'), 'linear'], [t('arr.grid'), 'grid'], [t('arr.polar'), 'polar']].forEach(([l, v]) => {
    const o = document.createElement('option'); o.value = v; o.textContent = l; modeSel.appendChild(o);
  });
  modeSel.value = cur.mode || 'linear';

  const fields = document.createElement('div');
  fields.style.cssText = 'display:flex;flex-direction:column;gap:6px;margin-bottom:10px;';

  const inputs = {};
  function field(label, key, def) {
    const w = document.createElement('div');
    w.style.cssText = 'display:flex;align-items:center;gap:6px;';
    const l = document.createElement('span');
    l.textContent = label;
    l.style.cssText = 'width:150px;color:#9ac;flex:none;';
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.value = cur[key] !== undefined && cur[key] !== null ? cur[key] : def;
    inp.style.cssText = 'flex:1;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:3px 6px;';
    inputs[key] = inp;
    w.appendChild(l); w.appendChild(inp);
    fields.appendChild(w);
  }

  function renderFields(mode) {
    fields.innerHTML = '';
    field(t('arr.axis'), 'axis', 'x');
    if (mode === 'grid') {
      field(t('arr.nx'), 'nx', 3);
      field(t('arr.ny'), 'ny', 3);
      field(t('arr.nz'), 'nz', 1);
      field(t('arr.dx'), 'dx', 10);
      field(t('arr.dy'), 'dy', 10);
      field(t('arr.dz'), 'dz', 10);
    } else if (mode === 'polar') {
      field(t('arr.count'), 'count', 8);
      field(t('arr.radius'), 'radius', 50);
      field(t('arr.startAngle'), 'startAngle', 0);
      field(t('arr.totalAngle'), 'totalAngle', 360);
      field(t('arr.rotate'), 'rotate', 1);
    } else {
      field(t('arr.count'), 'count', 5);
      field(t('arr.step'), 'step', 10);
      field(t('arr.offset'), 'offset', 0);
    }
  }
  renderFields(modeSel.value);
  modeSel.addEventListener('change', () => renderFields(modeSel.value));

  const btnRow = document.createElement('div');
  btnRow.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;';
  const okBtn = document.createElement('button');
  bindI18n(okBtn, 'common.ok');
  okBtn.style.cssText = btnStyle;
  const cancelBtn = document.createElement('button');
  bindI18n(cancelBtn, 'settings.cancel');
  cancelBtn.style.cssText = btnStyle;
  btnRow.appendChild(okBtn);
  btnRow.appendChild(cancelBtn);

  box.appendChild(title);
  box.appendChild(modeSel);
  box.appendChild(fields);
  box.appendChild(btnRow);

  const close = () => overlay.remove();
  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); });

  okBtn.addEventListener('pointerdown', (e) => {
    e.stopPropagation(); e.preventDefault();
    const mode = modeSel.value;
    const spec = { mode, axis: (inputs.axis.value || 'x').toLowerCase() };
    const num = (k, fb) => { const v = parseFloat(inputs[k].value); return Number.isFinite(v) ? v : fb; };
    const intv = (k, fb) => { const v = parseInt(inputs[k].value, 10); return Number.isFinite(v) ? v : fb; };
    if (mode === 'grid') {
      spec.nx = intv('nx', 3); spec.ny = intv('ny', 3); spec.nz = intv('nz', 1);
      spec.dx = num('dx', 10); spec.dy = num('dy', 10); spec.dz = num('dz', 10);
      if (!(spec.nx > 0 && spec.ny > 0)) { showToast(t('arr.nxnyMin')); return; }
    } else if (mode === 'polar') {
      spec.count = intv('count', 8);
      spec.radius = num('radius', 50);
      spec.startAngle = num('startAngle', 0);
      spec.totalAngle = num('totalAngle', 360);
      spec.rotate = inputs.rotate.value === '1' || inputs.rotate.value.toLowerCase() === 'true';
      if (!(spec.count > 1)) { showToast(t('arr.countMin')); return; }
    } else {
      spec.count = intv('count', 5);
      spec.step = num('step', 10);
      spec.offset = num('offset', 0);
      if (!(spec.count > 1)) { showToast(t('arr.countMin')); return; }
    }
    close();
    applyArray(name, spec, rep);
  });

  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
}

// Remove a primitive and drop/shift its constraints and dimensions.
function removePrimAtIndex(index) {
  const prim = prims[index];
  if (!prim) return;
  if (prim.actor) { renderer.removeActor(prim.actor); primActorMap.delete(prim.actor); }
  constraintPrimRemoved(index);
  dimensionPrimRemoved(index);
  prims.splice(index, 1);
  if (view.selectedPrimIndex === index) view.selectedPrimIndex = -1;
  else if (view.selectedPrimIndex > index) view.selectedPrimIndex -= 1;
  updatePrims();
  updateTreeUI();
}

function addDimensionCtxItems(dim) {
  addCtxItem(t('ctx.editValue'), () => { editDimensionValue(dim); });
  addCtxItem(t('ctx.deleteDimension'), () => { removeDimension(dim); });
}

function addAuxCtxItems(i) {
  addCtxItem(t('ctx.properties'), () => { selectAux(i); });
  addCtxItem(t('ctx.deletePoint'), () => { pushHistory(); snap.auxPoints.splice(i, 1); snap.auxMeta.splice(i, 1); if (view.selectedAuxIndex === i) view.selectedAuxIndex = -1; updateAuxMarkers(); updateProps(); updateTreeUI(); });
}

function addPlaneCtxItems(i) {
  addCtxItem(t('ctx.properties'), () => { view.selectedName = null; view.selectedPlaneIndex = i; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1; applyHighlight(); activateTab('props'); updateProps(); });
  addCtxItem(t('ctx.delete'), () => { deletePlane(i); });
}

function addSourceCtxItems(srcHit) {
  addCtxItem(t('ctx.properties'), () => { activateTab('sources'); openSourceEditor(srcHit.source); });
  addCtxItem(t('ctx.move'), () => { view.selectedName = null; view.selectedPrimIndex = -1; view.selectedSourceName = srcHit.name; setMoveMode(true); });
  addCtxItem(t('ctx.delete'), () => {
    confirmAction(t('src.deleteConfirm', {name: srcHit.name}), () => {
      const srcData = srcHit.source;
      deleteSource(srcHit.name).then((res) => {
        showToast(res && res.ok ? t('toast.sourceDeleted') : (t('error.generic') + ((res && res.error) || '')));
        if (view.selectedSourceName === srcHit.name) view.selectedSourceName = null;
        loadSources();
      });
      pushCommand({ label: t('cmd.deleteSource'), undo: () => { if (srcData) postSourceCreate(srcData); }, redo: () => { deleteSource(srcHit.name).then(() => loadSources()); } });
    });
  });
}

function addPrimCtxItems(i) {
  const prim = prims[i];
  addCtxItem(t('ctx.properties'), () => { selectPrim(i); });
  addCtxItem(t('ctx.move'), () => { view.selectedName = null; view.selectedPrimIndex = i; setMoveMode(true); });
  addCtxItem(t('ctx.rotate'), () => { view.selectedName = null; view.selectedPrimIndex = i; setRotateMode(true); });
  if (prim && prim.type === 'grid') {
    addCtxItem(t('ctx.configureGrid'), () => {
      promptText(t('arr.gridStep'), String(prim.spacing || 10), (v) => {
        const n = parseFloat(v);
        if (!Number.isFinite(n) || n <= 0) { showToast(t('toast.invalidStep')); return; }
        prim.spacing = n;
        updatePrims();
        updateProps();
      });
    });
  }
  addCtxItem(t('ctx.copy'), () => {
    const src = prims[i];
    if (src) { clipboard = { kind: 'prim', prim: { ...src, name: src.name, pts: src.pts.map((pt) => [...pt]), normal: src.normal ? [...src.normal] : undefined, actor: null } }; pendingPaste = true; showToast(t('toast.clickToPaste')); }
  });
  addCtxItem(t('ctx.cut'), () => {
    const src = prims[i];
    if (src) {
      clipboard = { kind: 'prim', prim: { ...src, name: src.name, pts: src.pts.map((pt) => [...pt]), normal: src.normal ? [...src.normal] : undefined, actor: null } };
      pushHistory();
      removePrimAtIndex(i);
      pendingPaste = true;
      showToast(t('toast.clickToPlaceCut'));
    }
  });
  addCtxItem(t('ctx.deleteFigure'), () => {
    pushHistory();
    removePrimAtIndex(i);
  });
}

function addPhantomCtxItems() {
  addCtxItem(t('ctx.move'), () => {
    phantomMoveActive = true;
    view.selectedName = null;
    view.selectedSourceName = null;
    view.selectedPrimIndex = -1;
    setMoveMode(true);
  });
  addCtxItem(t('ctx.rename'), () => {
    const ph = groups.phantom || {};
    promptText(t('group.name'), (ph && ph.label) || t('tree.phantom'), (nv) => {
      if (!nv) return;
      postGroup({ op: 'phantom', label: nv }).then((res) => {
        if (res && res.ok) { groups.phantom = Object.assign({}, groups.phantom || {}, { label: nv }); updateTreeUI(); }
        else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  });
  addCtxItem(t('ctx.ungroup'), () => {
    postGroup({ op: 'phantom', ungrouped: true }).then((res) => {
      if (res && res.ok) { groups.phantom = Object.assign({}, groups.phantom || {}, { ungrouped: true }); updateTreeUI(); }
      else showToast(t('error.generic') + ((res && res.error) || ''));
    });
  });
}

function addObjectCtxItems(name) {
  const meta = objectMeta.get(name);
  if (!meta) return;
  if (isReplicaObject(name)) {
    // Replica instances are derived from the parent's REPLICA config — read-only.
    addCtxItem(t('ctx.isReplica'), null);
    addCtxItem(meta.visible !== false ? t('ctx.hide') : t('ctx.show'), () => { setVolumeVisible(name, meta.visible === false); updateTreeUI(); renderWindow.render(); });
    addCtxItem(t('ctx.center'), () => {
      const a = actors.get(name);
      if (a) { renderer.resetCamera(a.getBounds()); renderWindow.render(); }
    });
    return;
  }
  addCtxItem(t('ctx.properties'), () => { selectObject(name); });
  addCtxItem(t('ctx.moveToGroup'), () => {
    const gnames = [...groups.keys()];
    if (!gnames.length) { showToast(t('group.createFirst')); return; }
    promptText(t('group.moveTo', {name, list: gnames.join(', ')}), gnames[0], (gn) => {
      if (!gn || !groups.has(gn)) { if (gn) showToast(t('group.notFound', {name: gn})); return; }
      moveMembersIntoGroup(gn, [name]).then((res) => {
        if (res && res.ok) {
          groups.get(gn).children = (groups.get(gn).children || []).filter((c) => c !== name).concat([name]);
          for (const [og, g] of groups) if (og !== gn) g.children = (g.children || []).filter((c) => c !== name);
          updateTreeUI();
        } else showToast(t('error.generic') + ((res && res.error) || ''));
      });
    });
  });
  addCtxItem(t('ctx.move'), () => { view.selectedName = name; view.selectedSourceName = null; setMoveMode(true); });
  addCtxItem(t('ctx.rotate'), () => { view.selectedName = name; view.selectedSourceName = null; setRotateMode(true); });
  if (meta.source && meta.source.kind === 'primitive') {
    addCtxItem(t('ctx.copy'), () => { clipboard = { kind: 'yaml', name, data: objectToYaml(name) }; pendingPaste = true; showToast(t('toast.clickToPaste')); });
    addCtxItem(t('ctx.cut'), () => { cutObjectByName(name); });
    addCtxItem(t('ctx.delete'), () => { deleteObjectByName(name); });
    const rep = meta.source.replica;
    addCtxItem(rep ? t('ctx.editArray') : t('ctx.createArray'), () => { openArrayDialog(name, rep || null); });
    if (rep) {
      addCtxItem(t('ctx.removeArray'), () => { applyArray(name, { count: 0 }, rep); });
    }
    addCtxItem(t('ctx.autoParent'), () => { autoAssignParent(name); });
  }
  if (meta.source && meta.source.kind === 'gdml') {
    addCtxItem(t('ctx.copy'), () => { clipboard = { kind: 'gdml', name, pos: meta.pos ? [...meta.pos] : [0, 0, 0] }; pendingPaste = true; showToast(t('toast.clickToPaste')); });
    addCtxItem(t('ctx.delete'), () => {
      confirmAction(t('obj.deleteVolumeConfirm', {name}), () => {
        if (view.selectedName === name) view.selectedName = null;
        postGdml({ op: 'delete', name }).then((res) => {
          showToast(res && res.ok ? t('toast.volumeDeleted') : (t('error.generic') + ((res && res.error) || '')));
        });
        applyHighlight();
      });
    });
  }
  if (meta.source && meta.source.kind === 'dicom') {
    addCtxItem(t('ctx.delete'), () => {
      confirmAction(t('obj.deleteVolumeConfirm', {name}), () => {
        if (view.selectedName === name) view.selectedName = null;
        fetch('/api/dicom', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ op: 'delete', name }),
        }).then((r) => r.json()).then((res) => {
          showToast(res && res.ok ? t('toast.volumeDeleted') : (t('error.generic') + ((res && res.error) || '')));
          loadGeometry();
        });
      });
    });
  }
  addCtxItem(meta.visible !== false ? t('ctx.hide') : t('ctx.show'), () => { setVolumeVisible(name, meta.visible === false); updateTreeUI(); renderWindow.render(); });
  addCtxItem(t('ctx.center'), () => {
    const a = actors.get(name);
    if (a) { renderer.resetCamera(a.getBounds()); renderWindow.render(); }
    else if (mergedMesh.actor) { renderer.resetCamera(mergedMesh.actor.getBounds()); renderWindow.render(); }
  });
  const mat = meta.material;
  if (mat && materialsData.gdml.some((m) => m.name === mat) && !materialsData.user.includes(mat)) {
    addCtxItem(t('ctx.exportMaterial', {mat}), () => { exportGdmlMaterial(mat).then(() => { updateProps(); updateTreeUI(); }); });
  }
  addCtxItem(t('ctx.saveGdml'), () => {
    try { window.parent.postMessage({ type: 'g4care-export-gdml-single', volume: (meta.volume || meta.material) || name }, '*'); } catch (err) { /* ignore */ }
  });
}

function addEmptyCtxItems() {
  if (clipboard) addCtxItem(t('ctx.paste'), () => { pendingPaste = true; showToast(t('toast.clickOrPrecise')); });
  addCtxItem(t('ctx.deselect'), clearSelection);
}

// Batch actions for the current multi-selection, shown in the right-click menu
// (file-manager flow: select objects → ПКМ → act on the selection).
function addBatchCtxItems() {
  if (multiSel.size < 2) return;
  const primNames = [...multiSel].filter((n) => {
    const meta = objectMeta.get(n);
    return meta && meta.source && meta.source.kind === 'primitive';
  });
  addCtxItem(t('ctx.selected', {n: multiSel.size}), null);
  addCtxItem(t('ctx.mergeGroup'), mergeSelectedIntoGroup);
  if (primNames.length) {
    addCtxItem(t('ctx.copyCount', {n: primNames.length}), () => {
      postBatch(primNames.map((name) => ({ duplicate: { name, offset: [10, 10, 0] } }))).then((res) => {
        showToast(res && res.ok ? (t('toast.copiedCount', {n: primNames.length})) : (t('error.generic') + ((res && res.error) || '')));
      });
    });
    addCtxItem(t('label.remove') + ' (' + primNames.length + ')', () => {
      confirmAction(t('ctx.deleteCount', {n: primNames.length}), () => {
        multiSel.clear();
        postBatch([{ delete: { names: primNames } }]).then((res) => {
          showToast(res && res.ok ? (t('toast.deletedCount', {n: primNames.length})) : (t('error.generic') + ((res && res.error) || '')));
        });
        applyHighlight();
      });
    });
  }
  addCtxItem(t('ctx.saveGdmlArray'), () => {
    try { window.parent.postMessage({ type: 'g4care-export-gdml-array', volumes: selectionVolumes() }, '*'); } catch (err) { /* ignore */ }
  });
  addCtxItem(t('ctx.saveYamlArray'), () => {
    try { window.parent.postMessage({ type: 'g4care-save-yaml-array', volumes: selectionVolumes() }, '*'); } catch (err) { /* ignore */ }
  });
  addCtxItem(t('ctx.clearSelection'), clearSelection);
}

function openCtxMenu(relX, relY) {
  contextMenu.style.display = 'block';
  contextMenu.style.left = relX + 'px';
  contextMenu.style.top = relY + 'px';
}

// Right-click on a tree row: same menu as the scene, positioned at the cursor.
function showTreeEntityMenu(e, kind, key) {
  const rect = fullScreen.getContainer().getBoundingClientRect();
  const relX = e.clientX - rect.left;
  const relY = e.clientY - rect.top;
  contextMenu.innerHTML = '';
  if (kind === 'object' || kind === 'group' || kind === 'phantom') addBatchCtxItems();
  if (kind === 'object') addObjectCtxItems(key);
  else if (kind === 'phantom') addPhantomCtxItems();
  else if (kind === 'group') addGroupCtxItems(key);
  else if (kind === 'prim') addPrimCtxItems(key);
  else if (kind === 'aux') addAuxCtxItems(key);
  else if (kind === 'plane') addPlaneCtxItems(key);
  else if (kind === 'dimension') addDimensionCtxItems(key);
  else if (kind === 'constraint') addCtxItem(t('ctx.deleteConstraint'), () => { removeConstraint(key); });
  openCtxMenu(relX, relY);
}

function showContextMenu(relX, relY, x, y) {
  contextMenu.innerHTML = '';
  // 0) dimension (размер) under the cursor → edit value / delete
  const dimHit = dimensionHitAt(x, y);
  if (dimHit) {
    addCtxItem(t('ctx.editValue'), () => { editDimensionValue(dimHit); });
    addCtxItem(t('ctx.deleteDimension'), () => { removeDimension(dimHit); });
    contextMenu.style.display = 'block';
    contextMenu.style.left = relX + 'px';
    contextMenu.style.top = relY + 'px';
    return;
  }
  // 1) auxiliary point under the cursor → allow direct delete
  let auxIdx = -1;
  if (snap.auxPoints.length) {
    let bestD = 15;
    for (let i = 0; i < snap.auxPoints.length; i++) {
      const d = screenDist(x, y, snap.auxPoints[i]);
      if (d < bestD) { bestD = d; auxIdx = i; }
    }
  }
  if (auxIdx >= 0) {
    addCtxItem(t('ctx.properties'), () => { view.selectedAuxIndex = auxIdx; view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedPrimIndex = -1; activateTab('props'); updateProps(); });
    addCtxItem(t('ctx.deletePoint'), () => { pushHistory(); snap.auxPoints.splice(auxIdx, 1); snap.auxMeta.splice(auxIdx, 1); if (view.selectedAuxIndex === auxIdx) view.selectedAuxIndex = -1; updateAuxMarkers(); updateProps(); });
  } else {
    // 2) section plane under the cursor → delete / properties
    const actor = pickActorAt(x, y);
    const planeIdx = actor ? planeVisualActors.indexOf(actor) : -1;
    if (planeIdx >= 0) {
      addCtxItem(t('ctx.properties'), () => { view.selectedName = null; view.selectedPlaneIndex = planeIdx; view.selectedAuxIndex = -1; view.selectedPrimIndex = -1; applyHighlight(); activateTab('props'); updateProps(); });
      addCtxItem(t('ctx.delete'), () => { deletePlane(planeIdx); });
    } else {
      // 2.5) particle source marker under the cursor → edit / move / delete
      const srcHit = sourceHitForActor(actor);
      if (srcHit) {
        addCtxItem(t('ctx.properties'), () => { activateTab('sources'); openSourceEditor(srcHit.source); });
        addCtxItem(t('ctx.move'), () => { view.selectedName = null; view.selectedPrimIndex = -1; view.selectedSourceName = srcHit.name; setMoveMode(true); });
        addCtxItem(t('ctx.delete'), () => {
          confirmAction(t('src.deleteConfirm', {name: srcHit.name}), () => {
            const srcData = srcHit.source;
            deleteSource(srcHit.name).then((res) => {
              showToast(res && res.ok ? t('toast.sourceDeleted') : (t('error.generic') + ((res && res.error) || '')));
              if (view.selectedSourceName === srcHit.name) view.selectedSourceName = null;
              loadSources();
            });
            pushCommand({
              label: t('cmd.deleteSource'),
              undo: () => { if (srcData) postSourceCreate(srcData); },
              redo: () => { deleteSource(srcHit.name).then(() => loadSources()); },
            });
          });
        });
      } else {
      // 3) primitive / geometry object under the cursor
      const hit = pickAt(x, y);
      if (hit && hit.primIndex !== undefined) {
        addCtxItem(t('ctx.properties'), () => { view.selectedName = null; view.selectedPlaneIndex = -1; view.selectedAuxIndex = -1; view.selectedPrimIndex = hit.primIndex; applyHighlight(); activateTab('props'); updateProps(); });
        addCtxItem(t('ctx.move'), () => { view.selectedName = null; view.selectedPrimIndex = hit.primIndex; setMoveMode(true); });
        addCtxItem(t('ctx.rotate'), () => { view.selectedName = null; view.selectedPrimIndex = hit.primIndex; setRotateMode(true); });
        addCtxItem(t('ctx.copy'), () => {
          const src = prims[hit.primIndex];
          if (src) { clipboard = { kind: 'prim', prim: { ...src, name: src.name, pts: src.pts.map((pt) => [...pt]), normal: src.normal ? [...src.normal] : undefined, actor: null } }; pendingPaste = true; showToast(t('toast.clickToPaste')); }
        });
        addCtxItem(t('ctx.cut'), () => {
          const src = prims[hit.primIndex];
          if (src) {
            clipboard = { kind: 'prim', prim: { ...src, name: src.name, pts: src.pts.map((pt) => [...pt]), normal: src.normal ? [...src.normal] : undefined, actor: null } };
            pushHistory();
            removePrimAtIndex(hit.primIndex);
            pendingPaste = true;
            showToast(t('toast.clickToPlaceCut'));
          }
        });
        addCtxItem(t('ctx.deleteFigure'), () => {
          pushHistory();
          removePrimAtIndex(hit.primIndex);
        });
      } else if (hit && hit.name) {
        // Shared object menu (tree + scene): properties, copy/cut/delete, array
        // (REPLICA), visibility, centre, etc.
        // Nautilus rule: right-click on an unselected object selects only it.
        if (!multiSel.has(hit.name)) {
          multiSel.clear();
          if (objectMeta.has(hit.name)) multiSel.add(hit.name);
          selAnchor = 'obj:' + hit.name;
          view.selectedName = hit.name;
          view.selectedPlaneIndex = -1;
          view.selectedAuxIndex = -1;
          view.selectedPrimIndex = -1;
          applySelectionHighlight();
          applyHighlight();
          updateProps();
        }
        addBatchCtxItems();
        addObjectCtxItems(hit.name);
      } else {
        // Nautilus rule: right-click on empty area clears the selection.
        if (multiSel.size || view.selectedName) clearSelection();
        if (clipboard) {
          addCtxItem(t('ctx.paste'), () => { pendingPaste = true; showToast(t('toast.clickOrPrecise')); });
        }
        addBatchCtxItems();
        if (!multiSel.size) addCtxItem(t('ctx.deselect'), clearSelection);
      }
      }
    }
  }
  contextMenu.style.display = 'block';
  contextMenu.style.left = relX + 'px';
  contextMenu.style.top = relY + 'px';
}

containerEl.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  const canvas = containerEl.querySelector('canvas');
  const rect = containerEl.getBoundingClientRect();
  const x = (e.clientX - rect.left) * (canvas.width / rect.width);
  const y = canvas.height - (e.clientY - rect.top) * (canvas.height / rect.height);
  showContextMenu(e.clientX - rect.left, e.clientY - rect.top, x, y);
});
containerEl.addEventListener('pointerdown', () => { if (contextMenu.style.display === 'block') hideContextMenu(); });

setMaterialsRefreshUi(() => { updateProps(); updateTreeUI(); });
setMergedMeshHooks({ updateSectionPlanes, updateActorMappers });
setHistoryHooks(() => updateAuxMarkers(), () => applyHighlight(), () => updatePrims());
setSceneHooks({
  clearClientSceneState,
  rebuildMergedMesh,
  updateAxisLabels,
  updateActorMappers,
  updateTreeUI,
  updateLayerUI,
  applyHighlight,
  updateProps,
  axesWidget,
});
setPickingHooks({
  setMeshCellColor,
  buildMergedCut,
  rebuildMergedMesh,
});
setupAnnotations({
  addGroup,
  setActiveMode,
  resolvePoint,
  updateTreeUI,
  updateProps,
  displayCoord,
});
setObjectsHooks({
  setMeshCellColor,
  updateTreeUI,
  updateProps,
  updateLayerUI,
  updateActorMappers,
});
setupPrimitives({
  addGroup,
  setActiveMode,
  snapPoint,
  surfaceNormalAt,
  activePlane,
  projectToGridPlane,
  updateTreeUI,
});
setupSnap({
  addGroup,
  setActiveMode,
  setMeasureMode,
  displayCoord,
  projectToGridPlane,
});
setupMeasure({
  addGroup,
  setActiveMode,
  displayCoord,
  projectToGridPlane,
  sampleMeshSurfacePoints,
  closestPointOnMesh,
  rayMeshIntersect,
  surfaceNormalAt,
  toggleCoordMode,
});
setupSection({
  addGroup,
  setActiveMode,
  updateTreeUI,
  updateProps,
  buildMergedCut,
  volumeMatrix,
});
setupSources({ listEl: sourceList });
setupDimensions({
  addGroup,
  setActiveMode,
  refreshTree: updateTreeUI,
  moveObject: setObjectPosition,
  movePrim: (index, trans) => {
    const p = prims[index];
    if (p && p.pts) { p.pts = p.pts.map((pt) => [pt[0] + trans[0], pt[1] + trans[1], pt[2] + trans[2]]); updatePrims(); }
  },
});
setupConstraints({ addGroup, setActiveMode, refreshTree: updateTreeUI });
loadMaterials();
loadGeometry();
loadGroups();
loadSources();
loadAnnotations();
setInterval(pollGeometrySig, 1000);
setInterval(loadMaterials, 3000);
setInterval(loadSources, 2000);
setInterval(loadAnnotations, 1000);
setInterval(pollSideChannels, 150);

// Intercept vtk.js's own key shortcuts (s/w/v/r) in the CAPTURE phase so our
// representation logic runs instead — and never while typing in a UI field.
// vtk.js handles these on `keypress`, so we intercept keypress (not keydown).
containerEl.addEventListener('keypress', (e) => {
  const tag = (e.target && e.target.tagName) ? e.target.tagName.toLowerCase() : '';
  if (tag === 'input' || tag === 'select' || tag === 'textarea') return;
  const k = (e.key || '').toLowerCase();
  if (k === 's' || k === 'e' || k === 'w' || k === 'v' || k === 'r') {
    e.stopPropagation();
    e.preventDefault();
    if (k === 's') applyRepresentation(0);
    else if (k === 'e') applyRepresentation(1);
    else if (k === 'w') applyRepresentation(2);
    else if (k === 'v') applyRepresentation(3);
    else if (k === 'r') fitView();
    syncRepSelect();
  }
}, true);

// keyboard shortcuts
window.addEventListener('keydown', (e) => {
  const tag = (e.target && e.target.tagName) ? e.target.tagName.toLowerCase() : '';
  const inField = tag === 'input' || tag === 'select' || tag === 'textarea';
  if (inField) return; // never trigger scene shortcuts while typing in a UI field
  if (e.ctrlKey || e.metaKey) {
    const k = e.key.toLowerCase();
    if (k === 'z' || k === 'я') {
      e.preventDefault();
      if (e.shiftKey) globalRedo();
      else globalUndo();
      return;
    }
    if (k === 'y' || k === 'н') {
      e.preventDefault();
      globalRedo();
      return;
    }
  }
  if (e.key === 'm' || e.key === 'M') {
    if (measure.active) setMeasureMode(false);
    else { clearRuler(); setActiveMode('measure'); setMeasureMode(true); }
  } else if (e.key === 'Shift' && !e.repeat) {
    toggleSnap();
  } else if (e.key === 'Escape') {
    clearRuler();
    setMeasureMode(false);
    snap.auxMode = false;
    snap.deleteMode = false;
    resetAuxMode();
    resetDeleteMode();
    resetPrimMode();
    if (view.sourcePlaceMode) setSourcePlaceMode(false);
    resetAlignMode();
    resetDimMode();
    resetConstraintMode();
    if (pendingPaste) { pendingPaste = null; setCursor('default'); }
  }
});
