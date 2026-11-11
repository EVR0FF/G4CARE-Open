//==============================================================================
// G4CARE - icon.js — SVG icon helper: inline sprite (Lucide + Tabler)
//
// SVG icon helper: inline sprite (Lucide + Tabler)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import sprite from './sprite.svg?raw';

let injected = false;

// Injects the sprite once. Idempotent; safe to call on every icon creation.
export function injectIconSprite(doc = document) {
    if (injected) return;
    injected = true;
    const holder = doc.createElement('div');
    holder.setAttribute('aria-hidden', 'true');
    holder.style.cssText = 'position:absolute;width:0;height:0;overflow:hidden;';
    holder.innerHTML = sprite;
    (doc.body || doc.documentElement).appendChild(holder);
}

// Returns an <svg><use> element for the given icon name.
export function icon(name, size = 16, cls = '') {
    injectIconSprite();
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('class', 'g4c-icon ' + cls);
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('aria-hidden', 'true');
    const use = document.createElementNS(ns, 'use');
    use.setAttribute('href', '#i-' + name);
    svg.appendChild(use);
    return svg;
}

// Returns an HTML string for use inside innerHTML templates.
export function iconHtml(name, size = 16, cls = '') {
    injectIconSprite();
    return '<svg class="g4c-icon ' + cls + '" width="' + size + '" height="' + size +
        '" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><use href="#i-' + name + '"></use></svg>';
}
