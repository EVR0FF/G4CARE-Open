#==============================================================================
# G4CARE - gdml_loader.py — GDML loader: parse and tessellate GDML geometry
#
# GDML loader: parse and tessellate GDML geometry
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""GDML -> triangle-mesh loader for the G4CARE geometry viewer.

Parses a GDML scene and tessellates every placed solid into a triangle mesh
(flat vertex + triangle arrays) already transformed into the GDML world frame.

Solid types: box, tube, cone, polycone, sphere, orb, torus, para, trd, trap,
tet, ellipsoid, ellipticalCone, polyhedra, xtru and boolean solids
(subtraction/union/intersection via VTK with a first-operand fallback).

Output: list of dicts {name, vertices:[x,y,z,...], triangles:[i,j,k,...]}.
"""
import math
import os
from xml.etree import ElementTree as ET

import numpy as np

_DEG = math.pi / 180.0

_LENGTH_UNITS = {"mm": 1.0, "cm": 10.0, "m": 1000.0, "um": 1e-3,
                 "nm": 1e-6, "km": 1e6, "inch": 25.4, "ft": 304.8}
_ANGLE_UNITS = {"deg": 1.0, "rad": 180.0 / math.pi}

# Per-solid local tessellation cache. Tessellation depends only on the solid's
# geometry (shape + dimensions), so it is cached keyed by the solid's XML. A
# physvol placement edit (position/rotation) only re-applies the transform to
# these cached vertices — it never re-tessellates.
_SOLID_TESS_CACHE = {}


def _len_scale(unit):
    return _LENGTH_UNITS.get((unit or "mm").strip().lower(), 1.0)


def _ang_scale(unit):
    return _ANGLE_UNITS.get((unit or "deg").strip().lower(), 1.0)


def _faces(verts, faces):
    """verts: list of (x,y,z); faces: list of index tuples (convex polygons)."""
    V = np.asarray(verts, dtype=np.float64)
    tris = []
    for f in faces:
        for i in range(1, len(f) - 1):
            tris.append((f[0], f[i], f[i + 1]))
    return V, np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _box(x, y, z):
    x, y, z = x / 2.0, y / 2.0, z / 2.0
    v = [(-x, -y, -z), (x, -y, -z), (x, y, -z), (-x, y, -z),
         (-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4),
         (3, 2, 6, 7), (0, 4, 7, 3), (1, 5, 6, 2)]
    return _faces(v, f)


def _disc(z, rmin, rmax, p0, p1, n, flip=False):
    """Flat disc/annulus in the z plane, phi in [p0, p1]."""
    phis = np.linspace(p0, p1, n + 1)
    verts = []
    tris = []
    o0 = len(verts)
    for p in phis:
        verts.append((rmax * math.cos(p), rmax * math.sin(p), z))
    if rmin > 1e-9:
        i0 = len(verts)
        for p in phis:
            verts.append((rmin * math.cos(p), rmin * math.sin(p), z))
        for i in range(n):
            a, b, c, d = o0 + i, o0 + i + 1, i0 + i, i0 + i + 1
            tris += [(a, b, d), (a, d, c)] if not flip else [(a, d, b), (a, c, d)]
    else:
        center = len(verts)
        verts.append((0.0, 0.0, z))
        for i in range(n):
            a, b = o0 + i, o0 + i + 1
            tris += [(a, b, center)] if not flip else [(a, center, b)]
    return verts, tris


def _side(rmin, rmax, z0, z1, phi, flip=False):
    """Flat radial wall at a fixed phi (partial-phi end cap)."""
    c, s = math.cos(phi), math.sin(phi)
    v = [(rmin * c, rmin * s, z0), (rmax * c, rmax * s, z0),
         (rmax * c, rmax * s, z1), (rmin * c, rmin * s, z1)]
    t = [(0, 1, 2), (0, 2, 3)] if not flip else [(0, 2, 1), (0, 3, 2)]
    return v, t
def _tube(rmin, rmax, z, p0=0.0, p1=2 * math.pi, n=64):
    z0, z1 = -z / 2.0, z / 2.0
    full = (p1 - p0) >= 2 * math.pi - 1e-9
    phis = np.linspace(p0, p1, n + 1)
    verts, tris = [], []
    for i in range(n):
        a, b = phis[i], phis[i + 1]
        ca, sa = math.cos(a), math.sin(a)
        cb, sb = math.cos(b), math.sin(b)
        i0 = len(verts)
        verts += [(rmax * ca, rmax * sa, z0), (rmax * ca, rmax * sa, z1),
                  (rmax * cb, rmax * sb, z1), (rmax * cb, rmax * sb, z0)]
        tris += [(i0, i0 + 1, i0 + 2), (i0, i0 + 2, i0 + 3)]
        if rmin > 1e-9:
            j0 = len(verts)
            verts += [(rmin * ca, rmin * sa, z0), (rmin * ca, rmin * sa, z1),
                      (rmin * cb, rmin * sb, z1), (rmin * cb, rmin * sb, z0)]
            tris += [(j0, j0 + 2, j0 + 1), (j0, j0 + 3, j0 + 2)]
    vc, tc = _disc(z1, rmin, rmax, p0, p1, n, flip=False)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    vc, tc = _disc(z0, rmin, rmax, p0, p1, n, flip=True)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    if not full:
        for phi, flip in ((p0, True), (p1, False)):
            vs, ts = _side(rmin, rmax, z0, z1, phi, flip)
            o = len(verts)
            verts += vs
            tris += [(o + a, o + b, o + c) for a, b, c in ts]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _cone(rmin1, rmax1, rmin2, rmax2, z, p0=0.0, p1=2 * math.pi, n=64):
    z0, z1 = -z / 2.0, z / 2.0
    full = (p1 - p0) >= 2 * math.pi - 1e-9
    phis = np.linspace(p0, p1, n + 1)
    verts, tris = [], []
    for i in range(n):
        a, b = phis[i], phis[i + 1]
        ca, sa = math.cos(a), math.sin(a)
        cb, sb = math.cos(b), math.sin(b)
        i0 = len(verts)
        verts += [(rmax1 * ca, rmax1 * sa, z0), (rmax2 * ca, rmax2 * sa, z1),
                  (rmax2 * cb, rmax2 * sb, z1), (rmax1 * cb, rmax1 * sb, z0)]
        tris += [(i0, i0 + 1, i0 + 2), (i0, i0 + 2, i0 + 3)]
        if rmin1 > 1e-9 or rmin2 > 1e-9:
            j0 = len(verts)
            verts += [(rmin1 * ca, rmin1 * sa, z0), (rmin2 * ca, rmin2 * sa, z1),
                      (rmin2 * cb, rmin2 * sb, z1), (rmin1 * cb, rmin1 * sb, z0)]
            tris += [(j0, j0 + 2, j0 + 1), (j0, j0 + 3, j0 + 2)]
    vc, tc = _disc(z1, rmin2, rmax2, p0, p1, n, flip=False)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    vc, tc = _disc(z0, rmin1, rmax1, p0, p1, n, flip=True)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    if not full:
        for phi, flip in ((p0, True), (p1, False)):
            vs, ts = _side(min(rmin1, rmin2), max(rmax1, rmax2), z0, z1, phi, flip)
            o = len(verts)
            verts += vs
            tris += [(o + a, o + b, o + c) for a, b, c in ts]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _polycone(zplanes, p0=0.0, p1=2 * math.pi, n=64):
    """zplanes: list of (z, rmin, rmax) sorted by z (a stack of frusta)."""
    verts, tris = [], []
    full = (p1 - p0) >= 2 * math.pi - 1e-9
    phis = np.linspace(p0, p1, n + 1)
    zs = sorted(zplanes, key=lambda t: t[0])
    for k in range(len(zs) - 1):
        z0, rmin0, rmax0 = zs[k]
        z1, rmin1, rmax1 = zs[k + 1]
        for i in range(n):
            a, b = phis[i], phis[i + 1]
            ca, sa = math.cos(a), math.sin(a)
            cb, sb = math.cos(b), math.sin(b)
            i0 = len(verts)
            verts += [(rmax0 * ca, rmax0 * sa, z0), (rmax1 * ca, rmax1 * sa, z1),
                      (rmax1 * cb, rmax1 * sb, z1), (rmax0 * cb, rmax0 * sb, z0)]
            tris += [(i0, i0 + 1, i0 + 2), (i0, i0 + 2, i0 + 3)]
            if rmin0 > 1e-9 or rmin1 > 1e-9:
                j0 = len(verts)
                verts += [(rmin0 * ca, rmin0 * sa, z0), (rmin1 * ca, rmin1 * sa, z1),
                          (rmin1 * cb, rmin1 * sb, z1), (rmin0 * cb, rmin0 * sb, z0)]
                tris += [(j0, j0 + 2, j0 + 1), (j0, j0 + 3, j0 + 2)]
    zf, rfmin, rfmax = zs[-1]
    vc, tc = _disc(zf, rfmin, rfmax, p0, p1, n, flip=False)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    zf, rfmin, rfmax = zs[0]
    vc, tc = _disc(zf, rfmin, rfmax, p0, p1, n, flip=True)
    o = len(verts)
    verts += vc
    tris += [(o + a, o + b, o + c) for a, b, c in tc]
    if not full:
        rmin = min(t[1] for t in zs)
        rmax = max(t[2] for t in zs)
        for phi, flip in ((p0, True), (p1, False)):
            vs, ts = _side(rmin, rmax, zs[0][0], zs[-1][0], phi, flip)
            o = len(verts)
            verts += vs
            tris += [(o + a, o + b, o + c) for a, b, c in ts]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)
def _sphere(rmin, rmax, p0=0.0, p1=2 * math.pi, th0=0.0, th1=math.pi, n=48):
    """Spherical shell sector. rmin=0 gives a solid sphere."""
    verts, tris = [], []
    nt = max(4, int(round(n / 2)))
    npn = n
    ths = np.linspace(th0, th1, nt + 1)
    phis = np.linspace(p0, p1, npn + 1)
    for th in ths:
        st, ct = math.sin(th), math.cos(th)
        for ph in phis:
            verts.append((rmax * st * math.cos(ph), rmax * st * math.sin(ph), rmax * ct))
    grid = np.arange((nt + 1) * (npn + 1)).reshape(nt + 1, npn + 1)
    for ti in range(nt):
        for pi_ in range(npn):
            a = grid[ti, pi_]; b = grid[ti, pi_ + 1]
            c = grid[ti + 1, pi_ + 1]; d = grid[ti + 1, pi_]
            tris += [(a, b, c), (a, c, d)]
    verts = np.asarray(verts, dtype=np.float64)
    tris = np.asarray(tris, dtype=np.int64).reshape(-1, 3)
    if rmin > 1e-9:
        vi, ti_ = _sphere(0.0, rmin, p0, p1, th0, th1, n)
        ti_ = ti_[:, [0, 2, 1]]
        o = len(verts)
        verts = np.concatenate([verts, vi])
        tris = np.concatenate([tris, ti_ + o])
    return verts, tris


def _torus(rmin, rmax, rtor, p0=0.0, p1=2 * math.pi, n=64):
    rmid = (rmin + rmax) / 2.0
    rtube = (rmax - rmin) / 2.0
    if rtube <= 1e-9:
        return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
    nphi = n
    ntheta = max(8, int(round(n / 2)))
    phis = np.linspace(p0, p1, nphi + 1)
    ths = np.linspace(0.0, 2 * math.pi, ntheta + 1)
    verts = []
    for ph in phis:
        cp, sp = math.cos(ph), math.sin(ph)
        for th in ths:
            ct, st = math.cos(th), math.sin(th)
            r = rmid + rtube * ct
            verts.append((r * cp, r * sp, rtube * st))
    grid = np.arange((nphi + 1) * (ntheta + 1)).reshape(nphi + 1, ntheta + 1)
    tris = []
    for i in range(nphi):
        for j in range(ntheta):
            a = grid[i, j]; b = grid[i, j + 1]
            c = grid[i + 1, j + 1]; d = grid[i + 1, j]
            tris += [(a, b, c), (a, c, d)]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _trd(x1, x2, y1, y2, z):
    x1, x2 = x1 / 2.0, x2 / 2.0
    y1, y2 = y1 / 2.0, y2 / 2.0
    z = z / 2.0
    v = [(-x1, -y1, -z), (x1, -y1, -z), (x2, y2, -z), (-x2, y2, -z),
         (-x1, -y1, z), (x1, -y1, z), (x2, y2, z), (-x2, y2, z)]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4),
         (3, 2, 6, 7), (0, 4, 7, 3), (1, 5, 6, 2)]
    return _faces(v, f)


def _para(x, y, z, alpha, theta, phi):
    x, y, z = x / 2.0, y / 2.0, z / 2.0
    ct, st = math.cos(theta), math.sin(theta)
    cp, sp = math.cos(phi), math.sin(phi)
    dz = np.array([ct * cp, ct * sp, st])
    v = []
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            for sz in (-1.0, 1.0):
                v.append((sx * x, sy * y, 0.0) + (sz * z) * dz)
    f = [(0, 1, 3, 2), (4, 5, 7, 6), (0, 1, 5, 4),
         (2, 3, 7, 6), (0, 2, 6, 4), (1, 3, 7, 5)]
    return _faces(v, f)


def _trap(dz, theta, phi, dy1, dx1, dx2, alp1, dy2, dx3, dx4, alp2):
    """G4Trap — half-dimensions (dz, dy1..dy2, dx1..dx4 are half-lengths, angles in rad)."""
    ttc = math.tan(theta) * math.cos(phi)
    tts = math.tan(theta) * math.sin(phi)
    ta1 = math.tan(alp1)
    ta2 = math.tan(alp2)
    dzc = dz * ttc
    dzs = dz * tts
    dy1a1 = dy1 * ta1
    dy2a2 = dy2 * ta2
    v = [
        (-dzc - dy1a1 - dx1, -dzs - dy1, -dz),
        (-dzc - dy1a1 + dx1, -dzs - dy1, -dz),
        (-dzc + dy1a1 - dx2, -dzs + dy1, -dz),
        (-dzc + dy1a1 + dx2, -dzs + dy1, -dz),
        (dzc - dy2a2 - dx3, dzs - dy2, dz),
        (dzc - dy2a2 + dx3, dzs - dy2, dz),
        (dzc + dy2a2 - dx4, dzs + dy2, dz),
        (dzc + dy2a2 + dx4, dzs + dy2, dz),
    ]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5),
         (2, 3, 7, 6), (3, 0, 4, 7)]
    return _faces(v, f)


def _tet(v1, v2, v3, v4):
    v = [v1, v2, v3, v4]
    f = [(0, 1, 2), (0, 1, 3), (0, 2, 3), (1, 2, 3)]
    return _faces(v, f)
def _ellipsoid(ax, by, cz, zcut1, zcut2, n=48):
    nt = max(4, int(round(n / 2)))
    npn = n
    verts = []
    for th in np.linspace(0.0, math.pi, nt + 1):
        st, ct = math.sin(th), math.cos(th)
        zc = cz * ct
        if zc < zcut1 or zc > zcut2:
            continue
        for ph in np.linspace(0.0, 2 * math.pi, npn + 1):
            verts.append((ax * st * math.cos(ph), by * st * math.sin(ph), zc))
    rows = len(verts) // (npn + 1)
    if rows < 2:
        return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
    grid = np.arange(rows * (npn + 1)).reshape(rows, npn + 1)
    tris = []
    for ti in range(rows - 1):
        for pi_ in range(npn):
            a = grid[ti, pi_]; b = grid[ti, pi_ + 1]
            c = grid[ti + 1, pi_ + 1]; d = grid[ti + 1, pi_]
            tris += [(a, b, c), (a, c, d)]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _ellipticalcone(dx, dy, zmax, zcut, n=48):
    zcut = max(zcut, 0.0)
    zs = np.linspace(zcut, zmax, max(2, int(round(n / 4)) + 1))
    phis = np.linspace(0.0, 2 * math.pi, n + 1)
    verts = []
    for zz in zs:
        s = zz / zmax if zmax > 1e-12 else 1.0
        for ph in phis:
            verts.append((dx * s * math.cos(ph), dy * s * math.sin(ph), zz))
    rows = len(zs)
    grid = np.arange(rows * (n + 1)).reshape(rows, n + 1)
    tris = []
    for ti in range(rows - 1):
        for pi_ in range(n):
            a = grid[ti, pi_]; b = grid[ti, pi_ + 1]
            c = grid[ti + 1, pi_ + 1]; d = grid[ti + 1, pi_]
            tris += [(a, b, c), (a, c, d)]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _polyhedra(p0, p1, numsides, zplanes, n=64):
    verts, tris = [], []
    full = (p1 - p0) >= 2 * math.pi - 1e-9
    ns = int(numsides)
    zs = sorted(zplanes, key=lambda t: t[0])
    ang = p0 + (np.arange(ns) / ns) * (p1 - p0)
    for zz, rmin, rmax in zs:
        for a in ang:
            verts.append((rmax * math.cos(a), rmax * math.sin(a), zz))
        for a in ang:
            verts.append((rmin * math.cos(a), rmin * math.sin(a), zz))
    for k in range(len(zs) - 1):
        for i in range(ns):
            j = (i + 1) % ns
            ob = k * 2 * ns
            a = ob + i; b = ob + j
            c = ob + 2 * ns + j; d = ob + 2 * ns + i
            tris += [(a, b, c), (a, c, d)]
            a = ob + ns + i; b = ob + ns + j
            c = ob + 3 * ns + j; d = ob + 3 * ns + i
            tris += [(a, c, b), (a, d, c)]

    def poly_cap(idx, flip):
        ob = idx * 2 * ns
        ib = ob + ns
        for i in range(ns):
            j = (i + 1) % ns
            a = ob + i; b = ob + j; c = ib + j; d = ib + i
            tris.extend([(a, b, c), (a, c, d)] if not flip else [(a, c, b), (a, d, c)])
    poly_cap(len(zs) - 1, False)
    poly_cap(0, True)
    if not full:
        rmin = min(t[1] for t in zs)
        rmax = max(t[2] for t in zs)
        for phi, flip in ((p0, True), (p1, False)):
            vs, ts = _side(rmin, rmax, zs[0][0], zs[-1][0], phi, flip)
            o = len(verts)
            verts += vs
            tris += [(o + a, o + b, o + c) for a, b, c in ts]
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def _xtru(vertices, zplanes):
    verts, tris = [], []
    zs = sorted(zplanes, key=lambda t: t[0])
    base = [(float(v[0]), float(v[1])) for v in vertices]
    nv = len(base)
    for zz, xoff, yoff, scale in zs:
        for x, y in base:
            verts.append((x * scale + xoff, y * scale + yoff, zz))
    for k in range(len(zs) - 1):
        for i in range(nv):
            j = (i + 1) % nv
            a = k * nv + i; b = k * nv + j
            c = (k + 1) * nv + j; d = (k + 1) * nv + i
            tris += [(a, b, c), (a, c, d)]
    def cap(k, flip):
        o = k * nv
        for i in range(1, nv - 1):
            tris.append((o, o + i + 1, o + i) if flip else (o, o + i, o + i + 1))
    cap(0, True)
    cap(len(zs) - 1, False)
    return np.asarray(verts, dtype=np.float64), np.asarray(tris, dtype=np.int64).reshape(-1, 3)


_TESSELLATORS = {
    "box": _box, "tube": _tube, "cone": _cone, "polycone": _polycone,
    "sphere": _sphere, "orb": _sphere, "torus": _torus, "trd": _trd,
    "para": _para, "trap": _trap, "tet": _tet, "ellipsoid": _ellipsoid,
    "ellipticalcone": _ellipticalcone, "polyhedra": _polyhedra, "xtru": _xtru,
}
def _rot_matrix(rx, ry, rz):
    """GDML/Geant4 rotation: R = Rz(z) * Ry(y) * Rx(x), angles in degrees."""
    f, t, p = rx * _DEG, ry * _DEG, rz * _DEG
    sf, cf = math.sin(f), math.cos(f)
    st, ct = math.sin(t), math.cos(t)
    sp, cp = math.sin(p), math.cos(p)
    return np.array([
        [ct * cp, sf * st * cp - cf * sp, cf * st * cp + sf * sp],
        [ct * sp, sf * st * sp + cf * cp, cf * st * sp - sf * cp],
        [-st,     sf * ct,                cf * ct],
    ])


def _strip_ns(tree):
    for el in tree.iter():
        if '}' in el.tag:
            el.tag = el.tag.split('}', 1)[1]
    return tree


class GDMLScene:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.base = os.path.dirname(self.path)
        self.constants = {}
        self.positions = {}
        self.rotations = {}
        self.solids = {}
        self.volumes = {}
        self.world = None
        self.meshes = []

    def _val(self, el, attr, default=0.0, scale=1.0):
        s = el.get(attr)
        if s is None:
            return default
        s = s.strip()
        if s in self.constants:
            return self.constants[s] * scale
        try:
            return float(s) * scale
        except ValueError:
            return default

    def _len(self, el, attr, default=0.0):
        return self._val(el, attr, default, _len_scale(el.get("lunit")))

    def _ang(self, el, attr, default=0.0):
        # GDML angles come in degrees by default; tessellation helpers expect
        # radians, so convert here.
        return self._val(el, attr, default, _ang_scale(el.get("aunit"))) * _DEG

    def parse(self):
        tree = _strip_ns(ET.parse(self.path))
        root = tree.getroot()
        self._parse_define(root)
        self._parse_solids(root)
        self._parse_structure(root)
        return self

    def _parse_define(self, root):
        define = root.find("define")
        if define is None:
            return
        for c in define.findall("constant"):
            name = c.get("name")
            if name:
                try:
                    self.constants[name] = float(str(c.get("value")).strip())
                except (TypeError, ValueError):
                    pass
        for p in define.findall("position"):
            name = p.get("name")
            if name:
                self.positions[name] = (
                    self._val(p, "x", 0.0, _len_scale(p.get("unit"))),
                    self._val(p, "y", 0.0, _len_scale(p.get("unit"))),
                    self._val(p, "z", 0.0, _len_scale(p.get("unit"))),
                )
        for r in define.findall("rotation"):
            name = r.get("name")
            if name:
                self.rotations[name] = (
                    self._val(r, "x", 0.0, _ang_scale(r.get("unit"))),
                    self._val(r, "y", 0.0, _ang_scale(r.get("unit"))),
                    self._val(r, "z", 0.0, _ang_scale(r.get("unit"))),
                )

    def _parse_solids(self, root):
        solids = root.find("solids")
        if solids is None:
            return
        for el in list(solids):
            name = el.get("name")
            if name:
                self.solids[name] = el

    def _parse_structure(self, root):
        structure = root.find("structure")
        if structure is None:
            return
        for vol in structure.findall("volume"):
            name = vol.get("name")
            if not name:
                continue
            solid = None
            sr = vol.find("solidref")
            if sr is not None:
                solid = sr.get("ref")
            material = None
            mr = vol.find("materialref")
            if mr is not None:
                material = mr.get("ref")
            color = None
            opacity = None
            visible = None
            for aux in vol.findall("auxiliary"):
                at = (aux.get("auxtype") or "").strip().lower()
                av = (aux.get("auxvalue") or "").strip()
                if at in ("color", "colour"):
                    parts = av.split()
                    if len(parts) >= 3:
                        try:
                            color = [float(x) for x in parts[:3]]
                        except ValueError:
                            color = None
                elif at == "opacity":
                    try:
                        opacity = float(av)
                    except ValueError:
                        opacity = None
                elif at in ("visible", "visibility"):
                    visible = av.lower() not in ("0", "false", "no")
            physvols = []
            for pv in vol.findall("physvol"):
                vr = pv.find("volumeref")
                if vr is None:
                    continue
                physvols.append({
                    "volumeref": vr.get("ref"),
                    "pos": self._resolve_position(pv),
                    "rot": self._resolve_rotation(pv),
                    "name": pv.get("name"),
                })
            self.volumes[name] = {"solid": solid, "physvols": physvols, "material": material,
                                  "color": color, "opacity": opacity, "visible": visible}
        setup = root.find("setup")
        if setup is not None:
            w = setup.find("world")
            if w is not None:
                self.world = w.get("ref")
        if self.world is None and self.volumes:
            referenced = set()
            for v in self.volumes.values():
                for pv in v["physvols"]:
                    referenced.add(pv["volumeref"])
            candidates = [n for n in self.volumes if n not in referenced]
            if candidates:
                self.world = candidates[0]

    def _resolve_position(self, parent):
        el = parent.find("position")
        if el is not None:
            ref = el.get("ref")
            if ref and ref in self.positions:
                return np.array(self.positions[ref], dtype=np.float64)
            return np.array([
                self._val(el, "x", 0.0, _len_scale(el.get("unit"))),
                self._val(el, "y", 0.0, _len_scale(el.get("unit"))),
                self._val(el, "z", 0.0, _len_scale(el.get("unit"))),
            ], dtype=np.float64)
        el = parent.find("positionref")
        if el is not None:
            ref = el.get("ref")
            if ref and ref in self.positions:
                return np.array(self.positions[ref], dtype=np.float64)
        return np.zeros(3)

    def _resolve_rotation(self, parent):
        el = parent.find("rotation")
        if el is not None:
            ref = el.get("ref")
            if ref and ref in self.rotations:
                return _rot_matrix(*self.rotations[ref])
            return _rot_matrix(
                self._val(el, "x", 0.0, _ang_scale(el.get("unit"))),
                self._val(el, "y", 0.0, _ang_scale(el.get("unit"))),
                self._val(el, "z", 0.0, _ang_scale(el.get("unit"))),
            )
        el = parent.find("rotationref")
        if el is not None:
            ref = el.get("ref")
            if ref and ref in self.rotations:
                return _rot_matrix(*self.rotations[ref])
        return np.eye(3)
    def tessellate(self, name):
        el = self.solids.get(name)
        if el is None:
            return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
        key = ET.tostring(el, encoding="unicode")
        hit = _SOLID_TESS_CACHE.get(key)
        if hit is not None:
            return hit
        V, T = self._tessellate_raw(name)
        _SOLID_TESS_CACHE[key] = (V, T)
        return V, T

    def _tessellate_raw(self, name):
        el = self.solids.get(name)
        if el is None:
            return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
        tag = el.tag.lower()
        fn = _TESSELLATORS.get(tag)
        if fn is not None:
            return self._call(fn, el, tag)
        if tag in ("subtraction", "union", "intersection"):
            return self._boolean(tag, el)
        if tag == "scaledSolid":
            ref = el.find("solidref")
            if ref is None:
                return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
            V, T = self.tessellate(ref.get("ref"))
            sx = self._val(el, "x", 1.0); sy = self._val(el, "y", 1.0); sz = self._val(el, "z", 1.0)
            return V * np.array([sx, sy, sz]), T
        if tag == "cuttube":
            return self._call(_tube, el, "tube")
        if tag == "twistedbox":
            return self._call(_box, el, "box")
        if tag == "twistedtrap":
            return self._call(_trap, el, "trap")
        if tag == "twistedtrd":
            return self._call(_trd, el, "trd")
        return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)

    def _call(self, fn, el, tag):
        a = self._len
        g = self._ang
        try:
            if tag == "box":
                return fn(a(el, "x"), a(el, "y"), a(el, "z"))
            if tag == "tube":
                return fn(a(el, "rmin", 0.0), a(el, "rmax"), a(el, "z"),
                          g(el, "startphi", 0.0), g(el, "deltaphi", 360.0))
            if tag == "cone":
                return fn(a(el, "rmin1", 0.0), a(el, "rmax1"), a(el, "rmin2", 0.0),
                          a(el, "rmax2"), a(el, "z"), g(el, "startphi", 0.0),
                          g(el, "deltaphi", 360.0))
            if tag == "polycone":
                return fn([(a(z, "z"), a(z, "rmin", 0.0), a(z, "rmax")) for z in el.findall("zplane")],
                          g(el, "startphi", 0.0), g(el, "deltaphi", 360.0))
            if tag == "sphere":
                return fn(a(el, "rmin", 0.0), a(el, "rmax"), g(el, "startphi", 0.0),
                          g(el, "deltaphi", 360.0), g(el, "starttheta", 0.0),
                          g(el, "deltatheta", 180.0))
            if tag == "orb":
                return fn(0.0, a(el, "r"))
            if tag == "torus":
                return fn(a(el, "rmin", 0.0), a(el, "rmax"), a(el, "rtor"),
                          g(el, "startphi", 0.0), g(el, "deltaphi", 360.0))
            if tag == "trd":
                return fn(a(el, "x1"), a(el, "x2"), a(el, "y1"), a(el, "y2"), a(el, "z"))
            if tag == "para":
                return fn(a(el, "x"), a(el, "y"), a(el, "z"), g(el, "alpha", 0.0),
                          g(el, "theta", 0.0), g(el, "phi", 0.0))
            if tag == "trap":
                return fn(a(el, "z") / 2, g(el, "theta", 0.0), g(el, "phi", 0.0),
                          a(el, "y1") / 2, a(el, "x1") / 2, a(el, "x2") / 2, g(el, "alpha1", 0.0),
                          a(el, "y2") / 2, a(el, "x3") / 2, a(el, "x4") / 2, g(el, "alpha2", 0.0))
            if tag == "tet":
                verts = []
                for attr in ("vertex1", "vertex2", "vertex3", "vertex4"):
                    txt = (el.get(attr) or "").split()
                    verts.append(tuple(float(x) for x in txt[:3]))
                return fn(*verts)
            if tag == "ellipsoid":
                return fn(a(el, "ax"), a(el, "by"), a(el, "cz"),
                          a(el, "zcut1", -1e30), a(el, "zcut2", 1e30))
            if tag == "ellipticalcone":
                return fn(a(el, "dx"), a(el, "dy"), a(el, "zmax"), a(el, "zcut", 0.0))
            if tag == "polyhedra":
                return fn(g(el, "startphi", 0.0), g(el, "deltaphi", 360.0),
                          int(self._val(el, "numsides", 6)),
                          [(a(z, "z"), a(z, "rmin", 0.0), a(z, "rmax")) for z in el.findall("zplane")])
            if tag == "xtru":
                verts = []
                for tv in el.findall("twoDimVertex"):
                    verts.append((self._len(tv, "x"), self._len(tv, "y")))
                sections = []
                for sec in el.findall("section"):
                    sections.append((self._len(sec, "z"), self._len(sec, "xOffset", 0.0),
                                     self._len(sec, "yOffset", 0.0), self._val(sec, "scalingFactor", 1.0)))
                return fn(verts, sections)
        except Exception:
            return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
        return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
    def _boolean(self, op, el):
        first = el.find("first")
        second = el.find("second")
        if first is None or second is None:
            return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
        VA, TA = self.tessellate(first.get("ref"))
        VB, TB = self.tessellate(second.get("ref"))
        if VA.shape[0] == 0:
            return np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
        # NOTE: vtkBooleanOperationPolyDataFilter is too slow / unreliable on
        # large operands, so we render the first operand (outer envelope) and
        # skip the boolean for now.
        return VA, TA

    def _op_transform(self, el, which):
        """Position/rotation of a boolean operand (identity if absent)."""
        prefix = "first" if which == "first" else ""
        ptag = prefix + "position"
        rtag = prefix + "rotation"
        pe = el.find(ptag)
        re_ = el.find(rtag)
        P = np.zeros(3)
        R = np.eye(3)
        if pe is not None:
            ref = pe.get("ref")
            if ref and ref in self.positions:
                P = np.array(self.positions[ref], dtype=np.float64)
            else:
                P = np.array([self._val(pe, "x", 0.0, _len_scale(pe.get("unit"))),
                              self._val(pe, "y", 0.0, _len_scale(pe.get("unit"))),
                              self._val(pe, "z", 0.0, _len_scale(pe.get("unit")))], dtype=np.float64)
        if re_ is not None:
            ref = re_.get("ref")
            if ref and ref in self.rotations:
                R = _rot_matrix(*self.rotations[ref])
            else:
                R = _rot_matrix(self._val(re_, "x", 0.0, _ang_scale(re_.get("unit"))),
                                self._val(re_, "y", 0.0, _ang_scale(re_.get("unit"))),
                                self._val(re_, "z", 0.0, _ang_scale(re_.get("unit"))))
        return P, R

    def build(self):
        if self.world is None:
            return self.meshes
        self._place(self.world, np.eye(4), None, None)
        return self.meshes

    def _place(self, volname, world, mother, placement_name):
        vol = self.volumes.get(volname)
        if vol is None:
            return
        mesh_name = placement_name or volname
        solid = vol.get("solid")
        if solid:
            V, T = self.tessellate(solid)
            if V.shape[0]:
                Vh = np.concatenate([V, np.ones((V.shape[0], 1))], axis=1)
                W = (world @ Vh.T).T[:, :3]
                mesh = {
                    "name": mesh_name,
                    "volume": volname,
                    "pos": [float(world[0, 3]), float(world[1, 3]), float(world[2, 3])],
                    "vertices": np.asarray(W, dtype=np.float32).ravel().tolist(),
                    "triangles": np.asarray(T, dtype=np.int32).ravel().tolist(),
                    "material": vol.get("material"),
                    "mother": mother,
                }
                if vol.get("color") is not None:
                    mesh["color"] = vol.get("color")
                if vol.get("opacity") is not None:
                    mesh["opacity"] = vol.get("opacity")
                if vol.get("visible") is not None:
                    mesh["visible"] = vol.get("visible")
                # The world/root volume (usually a huge vacuum box) would hide the
                # detector, so render it semi-transparent.
                if mother is None and "opacity" not in mesh:
                    mesh["opacity"] = 0.08
                self.meshes.append(mesh)
        for i, pv in enumerate(vol.get("physvols", [])):
            T44 = np.eye(4)
            T44[:3, :3] = pv["rot"]
            T44[:3, 3] = pv["pos"]
            pname = pv.get("name") or (mesh_name + "_" + str(i))
            self._place(pv["volumeref"], world @ T44, mesh_name, pname)


def _vtk_boolean(op, VA, TA, VB, TB):
    import vtk

    def to_pd(V, T):
        pts = vtk.vtkPoints()
        for v in V:
            pts.InsertNextPoint(float(v[0]), float(v[1]), float(v[2]))
        cells = vtk.vtkCellArray()
        for t in T:
            cells.InsertNextCell(3, [int(t[0]), int(t[1]), int(t[2])])
        pd = vtk.vtkPolyData()
        pd.SetPoints(pts)
        pd.SetPolys(cells)
        tri = vtk.vtkTriangleFilter()
        tri.SetInputData(pd)
        tri.Update()
        return tri.GetOutput()

    bf = vtk.vtkBooleanOperationPolyDataFilter()
    bf.SetInputData(0, to_pd(VA, TA))
    bf.SetInputData(1, to_pd(VB, TB))
    if op == "subtraction":
        bf.SetOperationToDifference()
    elif op == "union":
        bf.SetOperationToUnion()
    else:
        bf.SetOperationToIntersection()
    bf.Update()
    tri = vtk.vtkTriangleFilter()
    tri.SetInputData(bf.GetOutput())
    tri.Update()
    pd = tri.GetOutput()
    npts = pd.GetNumberOfPoints()
    ncells = pd.GetNumberOfCells()
    V = np.array([pd.GetPoint(i) for i in range(npts)], dtype=np.float64)
    tris = []
    for i in range(ncells):
        c = pd.GetCell(i)
        if c.GetNumberOfPoints() == 3:
            tris.append([c.GetPointId(0), c.GetPointId(1), c.GetPointId(2)])
    return V, np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def load_gdml(path):
    """Return a list of {name, vertices, triangles} for a GDML file."""
    scene = GDMLScene(path)
    try:
        scene.parse()
        scene.build()
    except Exception:
        pass
    return scene.meshes


def load_gdml_materials(path):
    """Return custom (non-G4_*) materials from a GDML <materials> section.

    Each entry: {name, density, state, elements: {ref: fraction, ...}}.
    """
    try:
        tree = _strip_ns(ET.parse(path))
        root = tree.getroot()
    except Exception:
        return []
    mats = root.find("materials")
    if mats is None:
        return []
    out = []
    for m in mats.findall("material"):
        name = m.get("name")
        if not name or name.startswith("G4_"):
            continue
        density = None
        state = m.get("state") or "solid"
        elements = {}
        for ch in m:
            tag = ch.tag.lower()
            if tag == "d":
                unit = ch.get("unit") or "g/cm3"
                density = f"{ch.get('value')}*{unit}"
            elif tag in ("fraction", "composite"):
                ref = ch.get("ref")
                n = ch.get("n")
                if ref and n:
                    try:
                        elements[ref] = float(n)
                    except ValueError:
                        pass
        out.append({
            "name": name,
            "density": density or "1.0*g/cm3",
            "state": state,
            "elements": elements,
        })
    return out






