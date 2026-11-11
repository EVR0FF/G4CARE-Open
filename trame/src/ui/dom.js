//==============================================================================
// G4CARE - dom.js — DOM helpers
//
// DOM helpers
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { t } from '../../../gui/src/i18n/i18n.js';
// G4CARE vtk.js viewer - shared DOM helpers (overlays, rows, button style).
import { fullScreen } from '../context.js';

let toastTimer = null;
export function showToast(msg) {
  let el = document.getElementById('g4care-toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'g4care-toast';
    el.style.cssText = 'position:absolute;left:50%;bottom:64px;transform:translateX(-50%);background:rgba(32,35,41,0.95);border:1px solid #343941;border-radius:6px;padding:8px 16px;color:#e2e5ea;font:12px Arial;z-index:10010;pointer-events:none;transition:opacity 0.3s;';
    fullScreen.getContainer().appendChild(el);
  }
  el.textContent = msg;
  el.style.opacity = '1';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.style.opacity = '0'; }, 3500);
}

export const btnStyle = 'background:#202329;color:#e2e5ea;border:1px solid #343941;border-radius:0;padding:4px 8px;min-height:24px;cursor:pointer;font:12px Arial;';

export function infoRow(label, val) {
  const d = document.createElement('div');
  d.innerHTML = '<b>' + label + ':</b> ' + val;
  return d;
}

export function promptText(message, defaultValue, onDone) {
  const overlay = document.createElement('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10002;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;';
  const box = document.createElement('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;padding:14px;width:300px;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';
  const msg = document.createElement('div');
  msg.textContent = message;
  msg.style.cssText = 'margin-bottom:8px;';
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = defaultValue || '';
  inp.style.cssText = 'width:100%;box-sizing:border-box;background:#111317;color:#e2e5ea;border:1px solid #343941;border-radius:4px;padding:4px 6px;margin-bottom:10px;font:12px Arial;';
  const btnRow = document.createElement('div');
  btnRow.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;';
  const okBtn = document.createElement('button');
  okBtn.textContent = t('common.ok');
  okBtn.style.cssText = btnStyle;
  const cancelBtn = document.createElement('button');
  cancelBtn.textContent = t('settings.cancel');
  cancelBtn.style.cssText = btnStyle;
  btnRow.appendChild(okBtn);
  btnRow.appendChild(cancelBtn);
  box.appendChild(msg);
  box.appendChild(inp);
  box.appendChild(btnRow);
  const close = () => overlay.remove();
  cancelBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); });
  okBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); onDone(inp.value.trim()); });
  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
  inp.focus();
}

export function confirmAction(message, onConfirm, yesLabel) {
  const overlay = document.createElement('div');
  overlay.setAttribute('data-ui', '');
  overlay.style.cssText = 'position:absolute;inset:0;z-index:10002;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;';
  const box = document.createElement('div');
  box.style.cssText = 'background:#202329;border:1px solid #343941;border-radius:8px;padding:14px;width:300px;color:#e2e5ea;font:12px Arial;box-shadow:0 8px 24px rgba(0,0,0,0.6);';
  const msg = document.createElement('div');
  msg.textContent = message;
  msg.style.cssText = 'margin-bottom:12px;line-height:1.4;';
  box.appendChild(msg);
  const btnRow = document.createElement('div');
  btnRow.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;';
  const yesBtn = document.createElement('button');
  yesBtn.textContent = yesLabel || t('ctx.delete');
  yesBtn.style.cssText = 'background:#7a2a2a;color:#fff;border:1px solid #955;border-radius:4px;padding:4px 12px;cursor:pointer;font:12px Arial;';
  const noBtn = document.createElement('button');
  noBtn.textContent = t('settings.cancel');
  noBtn.style.cssText = btnStyle;
  btnRow.appendChild(yesBtn);
  btnRow.appendChild(noBtn);
  box.appendChild(btnRow);
  const close = () => overlay.remove();
  noBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); });
  yesBtn.addEventListener('pointerdown', (e) => { e.stopPropagation(); e.preventDefault(); close(); onConfirm(); });
  overlay.appendChild(box);
  fullScreen.getContainer().appendChild(overlay);
}
