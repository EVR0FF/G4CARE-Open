//==============================================================================
// G4CARE - csel.js — Custom dropdown (replaces the native <select> popup)
//
// Custom dropdown (replaces the native <select> popup)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

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

function setupCustomSelects(root) {
    (root || document).querySelectorAll('select').forEach(customSelect);
}

// Install the custom-dropdown replacement globally (initial pass + observer for
// dynamically-added <select> elements).
export function initCustomSelects() {
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
}
