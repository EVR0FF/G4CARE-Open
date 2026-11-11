//==============================================================================
// G4CARE - context.js — Shared viewer context/state
//
// Shared viewer context/state
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkFullScreenRenderWindow from '@kitware/vtk.js/Rendering/Misc/FullScreenRenderWindow';

export const fullScreen = vtkFullScreenRenderWindow.newInstance({
  background: [0.15, 0.16, 0.18],
  rootContainer: document.getElementById('app'),
});

export const renderer = fullScreen.getRenderer();
export const renderWindow = fullScreen.getRenderWindow();
export const interactor = fullScreen.getInteractor();

export function canvasSize() {
  const c = fullScreen.getContainer().querySelector('canvas');
  return c ? [c.width, c.height] : [1, 1];
}
