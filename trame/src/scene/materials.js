//==============================================================================
// G4CARE - materials.js — Scene materials
//
// Scene materials
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - material discovery, export, and creation dialog.
import { fullScreen } from '../context.js';
import { materialsData } from '../state.js';
import { btnStyle, showToast, confirmAction } from '../ui/dom.js';

let refreshUi = () => {};
export function setMaterialsRefreshUi(fn) { refreshUi = fn; }

export async function loadMaterials() {
  try {
    const res = await fetch('/materials.json');
    const d = await res.json();
    materialsData.nist = d.nist || [];
    materialsData.user = d.user || [];
    materialsData.gdml = d.gdml || [];
  } catch (e) { /* ignore */ }
}

export async function exportGdmlMaterial(name, force = false) {
  const m = materialsData.gdml.find((x) => x.name === name);
  if (!m) return false;
  if (materialsData.user.includes(m.name) && !force) {
    return new Promise((resolve) => {
      confirmAction(t('mat.exists', {name: m.name}), async () => {
        resolve(await exportGdmlMaterial(name, true));
      }, t('mat.overwrite'));
    });
  }
  try {
    const res = await fetch('/api/material', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(m),
    });
    const r = await res.json();
    if (r.ok) {
      if (!materialsData.user.includes(m.name)) materialsData.user.push(m.name);
      showToast(t('mat.exported', {name: m.name}));
    } else {
      showToast(t('mat.exportError', {name: m.name}) + (r.error || ''));
    }
    refreshUi();
    return r.ok;
  } catch (e) {
    showToast(t('mat.exportErrorMsg', {name: m.name}));
    return false;
  }
}

export async function exportAllGdmlMaterials() {
  const existing = materialsData.gdml.filter((m) => materialsData.user.includes(m.name));
  if (existing.length) {
    return new Promise((resolve) => {
      confirmAction(t('mat.existMany', {names: existing.map((e) => e.name).join(', ')}), async () => {
        let n = 0;
        for (const m of materialsData.gdml) {
          if (await exportGdmlMaterial(m.name, true)) n++;
        }
        resolve(n);
      }, t('mat.overwriteAll'));
    });
  }
  let n = 0;
  for (const m of materialsData.gdml) {
    if (await exportGdmlMaterial(m.name, true)) n++;
  }
  refreshUi();
  return n;
}

export function openMaterialDialog(onDone) {
  const overlay = document.createElement('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10002;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;';
  const box = document.createElement('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;padding:14px;width:320px;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';
  const title = document.createElement('div');
  bindI18n(title, 'msg.createMaterial');
  title.style.cssText = 'font-weight:bold;margin-bottom:8px;color:#e8752a;';
  box.appendChild(title);

  const fieldStyle = 'width:100%;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:4px 6px;margin-bottom:6px;font:12px Arial;';
  const nameInp = document.createElement('input');
  nameInp.placeholder = t('ph.materialName');
  nameInp.style.cssText = fieldStyle;
  const densInp = document.createElement('input');
  densInp.placeholder = t('ph.materialDensity');
  densInp.style.cssText = fieldStyle;
  const stateSel = document.createElement('select');
  stateSel.style.cssText = fieldStyle;
  ['solid', 'liquid', 'gas', 'undefined'].forEach((s) => { const o = document.createElement('option'); o.value = s; o.textContent = s; stateSel.appendChild(o); });
  const elems = document.createElement('textarea');
  elems.placeholder = t('ph.materialElements');
  elems.style.cssText = 'width:100%;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:4px 6px;margin-bottom:6px;min-height:60px;font:11px monospace;';

  box.appendChild(nameInp);
  box.appendChild(densInp);
  box.appendChild(stateSel);
  box.appendChild(elems);

  const err = document.createElement('div');
  err.style.cssText = 'color:#f0a0a0;min-height:14px;margin:2px 0;';
  box.appendChild(err);

  const btnRow = document.createElement('div');
  btnRow.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;margin-top:6px;';
  const okBtn = document.createElement('button');
  bindI18n(okBtn, 'tools.create');
  okBtn.style.cssText = btnStyle;
  const cancelBtn = document.createElement('button');
  bindI18n(cancelBtn, 'settings.cancel');
  cancelBtn.style.cssText = btnStyle;
  btnRow.appendChild(okBtn);
  btnRow.appendChild(cancelBtn);
  box.appendChild(btnRow);

  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); overlay.remove(); });
  okBtn.addEventListener('pointerdown', async (e) => {
    e.stopPropagation();
    const name = nameInp.value.trim();
    if (!name) { bindI18n(err, 'msg.enterName'); return; }
    const elements = {};
    for (const part of elems.value.split(',')) {
      const m = part.trim().match(/^(\d+)\s*:\s*([A-Za-z]+)\s*:\s*([0-9.eE+-]+)$/);
      if (m) elements[m[2]] = { name: m[2], Z: parseInt(m[1], 10), abundance: parseFloat(m[3]) };
    }
    const body = { name, density: densInp.value || '1.0*g/cm3', state: stateSel.value, elements };
    try {
      const res = await fetch('/api/material', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
      const r = await res.json();
      if (r.ok) {
        if (!materialsData.user.includes(name)) materialsData.user.push(name);
        overlay.remove();
        onDone(name);
      } else {
        err.textContent = r.error || t('common.errorShort');
      }
    } catch (ex) {
      err.textContent = String(ex);
    }
  });
  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
}
