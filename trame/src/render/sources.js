//==============================================================================
// G4CARE - sources.js — Source rendering
//
// Source rendering
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import vtkCubeSource from '@kitware/vtk.js/Filters/Sources/CubeSource';
import vtkSphereSource from '@kitware/vtk.js/Filters/Sources/SphereSource';
import vtkCylinderSource from '@kitware/vtk.js/Filters/Sources/CylinderSource';
import vtkConeSource from '@kitware/vtk.js/Filters/Sources/ConeSource';
import vtkTorusSource from '@kitware/vtk.js/Filters/Sources/TorusSource';

// DIMENSIONS conventions (match src/Geometry/ObjectManager.cc):
//   box      [x, y, z]
//   sphere   [rmin, rmax]        (orb: [rmax])
//   cylinder [r, h]              (solid)
//   tube     [rmin, rmax, h]
//   cone     [rmin1, rmax1, rmin2, rmax2, h]
//   torus    [rmin, rmax, rtor]
//   para     [dx, dy, dz, alpha, theta, phi]
export function makeSource(shape, dims) {
  const d = (i, fb) => (dims && dims[i] !== undefined && dims[i] !== null ? dims[i] : fb);
  if (shape === 'box' || shape === 'para') {
    const s = vtkCubeSource.newInstance();
    s.setXLength(d(0, 10));
    s.setYLength(d(1, d(0, 10)));
    s.setZLength(d(2, d(0, 10)));
    return s;
  }
  if (shape === 'sphere' || shape === 'orb') {
    const s = vtkSphereSource.newInstance();
    s.setRadius(d(1, d(0, 10)));
    s.setThetaResolution(48);
    s.setPhiResolution(48);
    return s;
  }
  if (shape === 'cylinder' || shape === 'tube' || shape === 'tubs') {
    const s = vtkCylinderSource.newInstance();
    if (shape === 'cylinder') {
      s.setRadius(d(0, 10));
      s.setHeight(d(1, d(0, 10)));
    } else {
      s.setRadius(d(1, d(0, 10))); // outer radius (rmax)
      s.setHeight(d(2, 10));
    }
    s.setResolution(48);
    return s;
  }
  if (shape === 'cone') {
    const s = vtkConeSource.newInstance();
    s.setRadius(Math.max(d(1, 10), d(3, 10)));
    s.setHeight(d(4, 10));
    s.setResolution(48);
    return s;
  }
  if (shape === 'torus') {
    const s = vtkTorusSource.newInstance();
    s.setRadius(d(2, 20)); // ring radius (rtor)
    s.setTubeRadius(d(1, 10)); // cross-section radius (rmax)
    return s;
  }
  const s = vtkSphereSource.newInstance();
  s.setRadius(10);
  return s;
}

// G4 solids are axis-aligned along Z for cylinder/cone (G4Tubs/G4Cons), while
// vtk.js cylinder/cone sources are along Y — rotate to match.
export const Z_AXIS_SHAPES = new Set(['cylinder', 'tube', 'tubs', 'cone']);

export const DEFAULT_COLOR = [0.45, 0.6, 0.85];
export const HOVER_COLOR = [1.0, 0.55, 0.0];
export const SELECT_COLOR = [1.0, 0.35, 0.0];
export const CUT_SELECT_COLOR = [0.0, 0.9, 0.9];
export const LOCAL_TARGET_COLOR = [0.9, 0.1, 0.9];
