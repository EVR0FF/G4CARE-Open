//==============================================================================
// G4CARE - yaml-objects.js — YAML geometry object parsing + POSITION editing helpers
//
// YAML geometry object parsing + POSITION editing helpers
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { parse } from 'yaml';

export function objectsFromYaml(text) {
    try {
        const doc = parse(text) || {};
        const objs = (doc.GEOMETRY && doc.GEOMETRY.OBJECTS) || {};
        return Object.entries(objs).map(([key, val]) => ({
            key,
            name: (val && val.name) || key,
            shape: (val && val.shape) || 'box',
            pos: (val && val.POSITION) || null,
            dims: (val && val.DIMENSIONS) || [],
        }));
    } catch (e) {
        return [];
    }
}

export function escapeRegex(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// "25*cm" -> {value:25, unit:"cm"} ; 0 -> {value:0, unit:null}
function parsePosComponent(v) {
    if (typeof v === 'number') return { value: v, unit: null };
    const m = /^\s*([-+0-9.eE]+)\s*\*\s*([A-Za-z]+)\s*$/.exec(String(v));
    if (m) return { value: parseFloat(m[1]), unit: m[2] };
    return { value: parseFloat(v) || 0, unit: null };
}

export function parsePosition(pos) {
    let comps = [0, 0, 0];
    if (Array.isArray(pos)) comps = [pos[0], pos[1], pos[2]];
    else if (pos && typeof pos === 'object') comps = [pos.x, pos.y, pos.z];
    const parsed = comps.map((c) => parsePosComponent(c === undefined ? 0 : c));
    while (parsed.length < 3) parsed.push({ value: 0, unit: null });
    return parsed;
}

function formatPosComponent(c) {
    return c.unit ? c.value + '*' + c.unit : String(c.value);
}

// Replace the POSITION line inside the object's YAML block, preserving the rest.
export function setObjectPosition(text, key, comps) {
    const lines = text.split('\n');
    const keyRe = new RegExp('^\\s*' + escapeRegex(key) + ':\\s*$');
    let start = -1;
    for (let i = 0; i < lines.length; i++) {
        if (keyRe.test(lines[i])) { start = i; break; }
    }
    if (start < 0) return text;
    const keyIndent = (lines[start].match(/^\s*/) || [''])[0].length;
    for (let i = start + 1; i < lines.length; i++) {
        const line = lines[i];
        if (line.trim() === '') continue;
        const indent = (line.match(/^\s*/) || [''])[0].length;
        if (indent <= keyIndent) break;  // end of this object's block
        if (/^\s*POSITION\s*:/.test(line)) {
            const newVal = '[' + comps.map(formatPosComponent).join(', ') + ']';
            lines[i] = line.replace(/^(\s*POSITION\s*:\s*).*$/, '$1' + newVal);
            return lines.join('\n');
        }
    }
    return text;
}
