//==============================================================================
// G4CARE - i18n.js — i18n: locale dictionaries, t(), persistence, onLocaleChange
//
// i18n: locale dictionaries, t(), persistence, onLocaleChange
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

const dictionaries = {};
let currentLocale = 'ru';

try { currentLocale = localStorage.getItem('g4care-locale') || 'ru'; } catch (e) { currentLocale = 'ru'; }

export function registerLocale(locale, dict) { dictionaries[locale] = dict; }

export function setLocale(locale) {
    if (!dictionaries[locale]) locale = 'en';
    currentLocale = locale;
    try { localStorage.setItem('g4care-locale', locale); } catch (e) { /* ignore */ }
    applyI18n();
}

export function getLocale() { return currentLocale; }

export function t(key, params) {
    const dict = dictionaries[currentLocale] || dictionaries.en || {};
    let s = dict[key];
    if (s === undefined) s = (dictionaries.en && dictionaries.en[key]) || key;
    if (params) for (const [k, v] of Object.entries(params)) s = String(s).split('{' + k + '}').join(String(v));
    return s;
}

const bindings = [];
export function bindI18n(el, key) {
    // Idempotent: replace any previous binding for this element instead of
    // accumulating (so a single node can switch its message over time).
    for (let i = bindings.length - 1; i >= 0; i--) {
        if (bindings[i][0] === el) bindings.splice(i, 1);
    }
    bindings.push([el, key]);
    el.textContent = t(key);
}

const refreshers = [];
export function onLocaleChange(fn) { refreshers.push(fn); }

export function applyI18n() {
    document.querySelectorAll('[data-i18n]').forEach((el) => {
        el.textContent = t(el.getAttribute('data-i18n'));
    });
    document.querySelectorAll('[data-i18n-title]').forEach((el) => {
        el.setAttribute('title', t(el.getAttribute('data-i18n-title')));
    });
    bindings.forEach(([el, key]) => { el.textContent = t(key); });
    refreshers.forEach((fn) => { try { fn(); } catch (e) { /* ignore */ } });
}

export function initI18n(locales) {
    for (const [loc, dict] of Object.entries(locales)) registerLocale(loc, dict);
    if (!dictionaries[currentLocale]) currentLocale = 'en';
    applyI18n();
}
