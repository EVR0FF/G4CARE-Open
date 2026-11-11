//==============================================================================
// G4CARE - math3d.js — 3D math helpers
//
// 3D math helpers
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

export const DEG2RAD = Math.PI / 180;

// ── orientation helpers — exact Geant4 match ──
// Geant4 G4RotationMatrix::rotateX→rotateY→rotateZ (post-multiply) yields
// R = Rx·Ry·Rz (column vectors). vtk.js setOrientation() uses a different
// (Rz·Rx·Ry) convention, so we build the same quaternion as Geant4 and apply it
// via setOrientationFromQuaternion() for an exact match, including multi-axis.
export function quatFromAxisAngle(axis, deg) {
  const a = (deg * DEG2RAD) / 2;
  const s = Math.sin(a);
  return [axis[0] * s, axis[1] * s, axis[2] * s, Math.cos(a)]; // [x, y, z, w]
}

export function quatMul(q1, q2) {
  const [x1, y1, z1, w1] = q1;
  const [x2, y2, z2, w2] = q2;
  return [
    w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
    w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
    w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
    w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
  ];
}

// Convert a unit quaternion back to Geant4 Euler angles (Rx·Ry·Rz convention),
// i.e. the same convention as eulerXYZToQuat / orientationQuat.
export function quatToEulerXYZ(q) {
  const [x, y, z, w] = q;
  const sinr_cosp = 2 * (w * x + y * z);
  const cosr_cosp = 1 - 2 * (x * x + y * y);
  const rotX = Math.atan2(sinr_cosp, cosr_cosp) / DEG2RAD;
  const sinp = 2 * (w * y - z * x);
  let rotY;
  if (Math.abs(sinp) >= 1) rotY = Math.sign(sinp) * 90;
  else rotY = Math.asin(sinp) / DEG2RAD;
  const siny_cosp = 2 * (w * z + x * y);
  const cosy_cosp = 1 - 2 * (y * y + z * z);
  const rotZ = Math.atan2(siny_cosp, cosy_cosp) / DEG2RAD;
  return [rotX, rotY, rotZ];
}

function eulerXYZToQuat(degX, degY, degZ) {
  const qx = quatFromAxisAngle([1, 0, 0], degX);
  const qy = quatFromAxisAngle([0, 1, 0], degY);
  const qz = quatFromAxisAngle([0, 0, 1], degZ);
  return quatMul(quatMul(qx, qy), qz); // qx ⊗ qy ⊗ qz ≡ Rx·Ry·Rz
}

// Total orientation: base alignment (vtk-local → G4-local) then G4 rotation.
export function orientationQuat(rotDeg, baseX) {
  const qEuler = eulerXYZToQuat(rotDeg[0] || 0, rotDeg[1] || 0, rotDeg[2] || 0);
  if (baseX) return quatMul(qEuler, quatFromAxisAngle([1, 0, 0], baseX));
  return qEuler;
}

// Rotate a vector by a unit quaternion [x, y, z, w]:  v' = q · v · q⁻¹.
export function quatRotate(v, q) {
  const [qx, qy, qz, qw] = q;
  const c1 = cross3([qx, qy, qz], v);
  const c2 = cross3([qx, qy, qz], c1);
  return [
    v[0] + 2 * qw * c1[0] + 2 * c2[0],
    v[1] + 2 * qw * c1[1] + 2 * c2[1],
    v[2] + 2 * qw * c1[2] + 2 * c2[2],
  ];
}

// Inverse of a unit quaternion = its conjugate.
export function quatInverse(q) {
  return [-q[0], -q[1], -q[2], q[3]];
}

// Build a unit quaternion [x, y, z, w] from an orthonormal rotation matrix given
// by its three column vectors.
export function mat3ToQuat(c0, c1, c2) {
  // m[row*3+col], row-major.
  const m = [c0[0], c1[0], c2[0], c0[1], c1[1], c2[1], c0[2], c1[2], c2[2]];
  const t = m[0] + m[4] + m[8];
  let x, y, z, w;
  if (t > 0) {
    const s = Math.sqrt(t + 1) * 2;
    w = 0.25 * s;
    x = (m[5] - m[7]) / s;
    y = (m[6] - m[2]) / s;
    z = (m[1] - m[3]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const s = Math.sqrt(1 + m[0] - m[4] - m[8]) * 2;
    w = (m[5] - m[7]) / s;
    x = 0.25 * s;
    y = (m[3] + m[1]) / s;
    z = (m[6] + m[2]) / s;
  } else if (m[4] > m[8]) {
    const s = Math.sqrt(1 + m[4] - m[0] - m[8]) * 2;
    w = (m[6] - m[2]) / s;
    x = (m[3] + m[1]) / s;
    y = 0.25 * s;
    z = (m[7] + m[5]) / s;
  } else {
    const s = Math.sqrt(1 + m[8] - m[0] - m[4]) * 2;
    w = (m[1] - m[3]) / s;
    x = (m[6] + m[2]) / s;
    y = (m[7] + m[5]) / s;
    z = 0.25 * s;
  }
  return [x, y, z, w];
}

// Mirror a rotation (unit quaternion) across a plane with unit normal n:
// R' = M·R·M where M = I − 2·n·nᵀ (det M = −1), so R' stays a proper rotation
// (the mirror image of a symmetric solid is still a rotation).
export function mirrorQuaternion(q, n) {
  const reflect = (v) => {
    const d = dot3(v, n);
    return [v[0] - 2 * d * n[0], v[1] - 2 * d * n[1], v[2] - 2 * d * n[2]];
  };
  const c0 = reflect(quatRotate(reflect([1, 0, 0]), q));
  const c1 = reflect(quatRotate(reflect([0, 1, 0]), q));
  const c2 = reflect(quatRotate(reflect([0, 0, 1]), q));
  return mat3ToQuat(c0, c1, c2);
}


// ── vector helpers ──
export function dot3(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

export function cross3(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}

export function normalize3(v) {
  const l = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / l, v[1] / l, v[2] / l];
}

// ── geometry helpers ──
export function triArea(a, b, c) {
  const ab = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
  const ac = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
  const cr = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]];
  return 0.5 * Math.hypot(cr[0], cr[1], cr[2]);
}

export function closestPointOnSegment(A, B, P) {
  const ab = [B[0]-A[0], B[1]-A[1], B[2]-A[2]];
  const ap = [P[0]-A[0], P[1]-A[1], P[2]-A[2]];
  const l2 = ab[0]*ab[0] + ab[1]*ab[1] + ab[2]*ab[2];
  const t = l2 > 1e-12 ? Math.max(0, Math.min(1, (ap[0]*ab[0] + ap[1]*ab[1] + ap[2]*ab[2]) / l2)) : 0;
  return [A[0]+ab[0]*t, A[1]+ab[1]*t, A[2]+ab[2]*t];
}

export function closestPointOnTriangle(p, a, b, c) {
  const ab = [b[0]-a[0], b[1]-a[1], b[2]-a[2]];
  const ac = [c[0]-a[0], c[1]-a[1], c[2]-a[2]];
  const ap = [p[0]-a[0], p[1]-a[1], p[2]-a[2]];
  const d1 = dot3(ab, ap), d2 = dot3(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a.slice();
  const bp = [p[0]-b[0], p[1]-b[1], p[2]-b[2]];
  const d3 = dot3(ab, bp), d4 = dot3(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b.slice();
  const vc = d1*d4 - d3*d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / (d1 - d3);
    return [a[0] + v*ab[0], a[1] + v*ab[1], a[2] + v*ab[2]];
  }
  const cp = [p[0]-c[0], p[1]-c[1], p[2]-c[2]];
  const d5 = dot3(ab, cp), d6 = dot3(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c.slice();
  const vb = d5*d2 - d1*d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / (d2 - d6);
    return [a[0] + w*ac[0], a[1] + w*ac[1], a[2] + w*ac[2]];
  }
  const va = d3*d6 - d5*d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return [b[0] + w*(c[0]-b[0]), b[1] + w*(c[1]-b[1]), b[2] + w*(c[2]-b[2])];
  }
  const denom = 1 / (va + vb + vc);
  const v = vb * denom, w = vc * denom;
  return [a[0] + ab[0]*v + ac[0]*w, a[1] + ab[1]*v + ac[1]*w, a[2] + ab[2]*v + ac[2]*w];
}

export function segIntersect(a, b, c, d) {
  const rx = b[0] - a[0], ry = b[1] - a[1];
  const sx = d[0] - c[0], sy = d[1] - c[1];
  const denom = rx * sy - ry * sx;
  if (Math.abs(denom) < 1e-9) return null;
  const qpx = c[0] - a[0], qpy = c[1] - a[1];
  const t = (qpx * sy - qpy * sx) / denom;
  const u = (qpx * ry - qpy * rx) / denom;
  if (t < -1e-6 || t > 1 + 1e-6 || u < -1e-6 || u > 1 + 1e-6) return null;
  return [a[0] + t * rx, a[1] + t * ry, a[2]];
}

export function circleTangentPoints(P, C, r) {
  const dx = P[0] - C[0], dy = P[1] - C[1];
  const d2 = dx * dx + dy * dy;
  const r2 = r * r;
  if (d2 <= r2 || r2 <= 0) return [];
  const d = Math.sqrt(d2);
  const l = r2 / d;
  const h = Math.sqrt(r2 - l * l);
  const ux = dx / d, uy = dy / d;
  const fx = C[0] + l * ux, fy = C[1] + l * uy;
  const px = -uy, py = ux;
  return [
    [fx + h * px, fy + h * py, C[2]],
    [fx - h * px, fy - h * py, C[2]],
  ];
}

export function primBasis(normal) {
  const n = normalize3(normal || [0, 0, 1]);
  const ref = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
  const u = normalize3(cross3(ref, n));
  const v = cross3(n, u);
  return { u, v, n };
}
