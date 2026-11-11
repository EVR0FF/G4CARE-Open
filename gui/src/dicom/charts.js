//==============================================================================
// G4CARE - charts.js — Plotly chart helpers (DVH / histogram)
//
// Plotly chart helpers (DVH / histogram)
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import Plotly from 'plotly.js-dist-min';
import { t } from '../i18n/i18n.js';

export const PLOTLY_CONFIG = { responsive: true, displaylogo: false };

export function createGraphCfg() {
    return { xScale: 'linear', yScale: 'linear', grid: true, minorTicks: true, fontSize: 12, mode: 'lines' };
}

export function plotlyLayout(graphCfg, title, xTitle, yTitle) {
    return {
        title: { text: title, font: { size: graphCfg.fontSize + 2, color: '#e2e5ea' } },
        paper_bgcolor: '#0e0e10',
        plot_bgcolor: '#0e0e10',
        font: { color: '#969da8', size: graphCfg.fontSize },
        xaxis: {
            title: { text: xTitle }, type: graphCfg.xScale,
            gridcolor: graphCfg.grid ? '#343941' : 'transparent', zerolinecolor: '#343941',
            showgrid: graphCfg.grid, ticks: 'outside', tickfont: { size: graphCfg.fontSize - 1 },
            showline: true, linecolor: '#343941', linewidth: 1,
            minor: graphCfg.minorTicks ? { showgrid: true, gridcolor: '#14161a', ticklen: 3 } : {},
        },
        yaxis: {
            title: { text: yTitle }, type: graphCfg.yScale,
            gridcolor: graphCfg.grid ? '#343941' : 'transparent', zerolinecolor: '#343941',
            showgrid: graphCfg.grid, ticks: 'outside', tickfont: { size: graphCfg.fontSize - 1 },
            showline: true, linecolor: '#343941', linewidth: 1,
            minor: graphCfg.minorTicks ? { showgrid: true, gridcolor: '#14161a', ticklen: 3 } : {},
        },
        margin: { l: 60, r: 20, t: 40, b: 55 },
        legend: { font: { size: graphCfg.fontSize - 1 }, bgcolor: 'rgba(0,0,0,0.25)', orientation: 'h', y: 1.18 },
    };
}

export function attachGraphMenu(el, renderFn, graphCfg) {
    el.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        document.querySelectorAll('.g4care-menu').forEach((m) => m.remove());
        const menu = document.createElement('div');
        menu.className = 'g4care-menu';
        menu.style.cssText = 'position:fixed; background:var(--bg); border:1px solid var(--border); z-index:300; padding:4px; border-radius:3px; min-width:220px; box-shadow:0 2px 8px rgba(0,0,0,0.5);';
        menu.style.left = e.clientX + 'px';
        menu.style.top = e.clientY + 'px';
        const add = (label, fn) => {
            const b = document.createElement('button');
            b.textContent = label;
            b.style.cssText = 'display:block; width:100%; background:none; border:none; color:var(--text-2); padding:4px 8px; cursor:pointer; text-align:left; font:12px sans-serif;';
            b.onmouseenter = () => { b.style.background = 'var(--panel)'; };
            b.onmouseleave = () => { b.style.background = 'none'; };
            b.onclick = () => { menu.remove(); fn(); };
            menu.appendChild(b);
        };
        add(t('plot.xScale') + (graphCfg.xScale === 'log' ? 'log' : 'linear'), () => { graphCfg.xScale = graphCfg.xScale === 'log' ? 'linear' : 'log'; renderFn(); });
        add(t('plot.yScale') + (graphCfg.yScale === 'log' ? 'log' : 'linear'), () => { graphCfg.yScale = graphCfg.yScale === 'log' ? 'linear' : 'log'; renderFn(); });
        add(t('plot.grid') + (graphCfg.grid ? t('plot.on') : t('plot.off')), () => { graphCfg.grid = !graphCfg.grid; renderFn(); });
        add(t('plot.minorTicks') + (graphCfg.minorTicks ? t('plot.on') : t('plot.off')), () => { graphCfg.minorTicks = !graphCfg.minorTicks; renderFn(); });
        add(t('plot.font') + graphCfg.fontSize + 'px', () => { graphCfg.fontSize = graphCfg.fontSize >= 16 ? 10 : graphCfg.fontSize + 1; renderFn(); });
        add(t('plot.lineType') + (graphCfg.mode === 'lines' ? t('plot.solid') : t('plot.markers')), () => { graphCfg.mode = graphCfg.mode === 'lines' ? 'lines+markers' : 'lines'; renderFn(); });
        add(t('plot.resetZoom'), () => { Plotly.relayout(el, { 'xaxis.autorange': true, 'yaxis.autorange': true }); });
        add(t('plot.exportPng'), () => { Plotly.downloadImage(el, { format: 'png', filename: el.id, width: 1000, height: 560 }); });
        document.body.appendChild(menu);
        document.addEventListener('mousedown', () => menu.remove(), { once: true });
    });
}

export function renderDvh(results, graphCfg) {
    const traces = results.map((r) => ({
        x: Array.from(r.dvh.curve.dose),
        y: Array.from(r.dvh.curve.volFrac).map((v) => v * 100),
        type: 'scatter', mode: graphCfg.mode, name: r.name,
        line: { color: r.color, width: 2 }, marker: { size: 3, color: r.color },
    }));
    Plotly.react('dvh-plot', traces, plotlyLayout(graphCfg, 'DVH', t('plot.dose') + ' (Gy)', t('plot.volumePct')), PLOTLY_CONFIG);
}
