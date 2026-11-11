#==============================================================================
# G4CARE - load.py — Geometry loading
#
# Geometry loading
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""YAML geometry -> viewer JSON (objects + GDML meshes + layers + signature)."""

import hashlib
import json
import math
import os
import re

import yaml

from . import config
from .gdml import load_gdml_meshes


def parse_dim(s):
    if isinstance(s, (int, float)):
        return float(s)
    m = re.match(r"^\s*([-+0-9.eE]+)\s*\*\s*([A-Za-z]+)\s*$", str(s).strip())
    if m:
        return float(m.group(1)) * config.UNIT_TO_MM.get(m.group(2), 1.0)
    return float(s)


def parse_pos(pos):
    if pos is None:
        return [0.0, 0.0, 0.0]
    if isinstance(pos, list):
        return [parse_dim(v) for v in (pos + [0, 0, 0])[:3]]
    if isinstance(pos, dict):
        return [parse_dim(pos.get("x", 0)), parse_dim(pos.get("y", 0)), parse_dim(pos.get("z", 0))]
    return [0.0, 0.0, 0.0]


def parse_color(v):
    if isinstance(v, list) and len(v) >= 3:
        try:
            return [float(x) for x in v[:3]]
        except (TypeError, ValueError):
            pass
    return None


def parse_appearance(cfg):
    """GEOMETRY.APPEARANCE: {volume_name: {color, opacity, visible, layer}}."""
    app = (cfg.get("GEOMETRY", {}) or {}).get("APPEARANCE") or {}
    if not isinstance(app, dict):
        return {}
    result = {}
    for name, v in app.items():
        if not isinstance(v, dict):
            continue
        entry = {}
        c = parse_color(v.get("color"))
        if c is not None:
            entry["color"] = c
        try:
            entry["opacity"] = float(v.get("opacity"))
        except (TypeError, ValueError):
            pass
        if "visible" in v:
            entry["visible"] = bool(v["visible"])
        if "layer" in v:
            entry["layer"] = str(v["layer"])
        result[name] = entry
    return result


def load_geometry(path):
    if not os.path.isfile(path):
        return {"objects": [], "layers": []}
    try:
        with open(path) as f:
            cfg = yaml.safe_load(f) or {}
    except Exception:
        return {"objects": [], "layers": []}
    objects = (cfg.get("GEOMETRY", {}) or {}).get("OBJECTS", {}) or {}
    appearance = parse_appearance(cfg)
    yaml_dir = os.path.dirname(os.path.abspath(path))
    geom = cfg.get("GEOMETRY", {}) or {}

    # ── GDML sources (GEOMETRY.FILE / GEOMETRY.WORLD.file / OBJECTS.<id>.file) ──
    meshes = []
    seen = set()
    gdml_refs = []
    scene_file = geom.get("FILE") if isinstance(geom, dict) else None
    if scene_file:
        gdml_refs.append(scene_file)
    world = geom.get("WORLD") if isinstance(geom, dict) else None
    if isinstance(world, dict) and world.get("file"):
        gdml_refs.append(world.get("file"))
    if isinstance(objects, dict):
        for obj in objects.values():
            if isinstance(obj, dict) and obj.get("file"):
                gdml_refs.append(obj.get("file"))
    for ref in gdml_refs:
        gp = config.resolve_gdml(ref, yaml_dir)
        if gp and gp not in seen:
            seen.add(gp)
            for m in load_gdml_meshes(gp):
                m["source"] = {"kind": "gdml", "file": gp}
                meshes.append(m)

    # Apply GEOMETRY.APPEARANCE overrides to GDML meshes (by placement name or
    # logical volume name). YAML APPEARANCE wins over GDML <auxiliary> tags.
    for m in meshes:
        a = appearance.get(m.get("name"))
        if not a:
            a = appearance.get(m.get("volume"))
        if not a:
            continue
        if a.get("color") is not None:
            m["color"] = a["color"]
        if "opacity" in a:
            m["opacity"] = a["opacity"]
        if "visible" in a:
            m["visible"] = a["visible"]

    def _safe_dim(v, default=0.0):
        try:
            return parse_dim(v if v is not None else default)
        except (TypeError, ValueError):
            return default

    def _entry(name, obj, a, pos, source):
        return {
            "name": name,
            "shape": obj.get("shape", "box"),
            "dims": [parse_dim(d) for d in (obj.get("DIMENSIONS", []) or [])],
            "pos": pos,
            "material": obj.get("material"),
            "mother": obj.get("mother"),
            "rotation": obj.get("ROTATION"),
            "isSensitive": bool(obj.get("isSensitive", False)),
            "color": a.get("color"),
            "opacity": a.get("opacity"),
            "visible": a.get("visible", True),
            "layer": a.get("layer"),
            "source": source,
        }

    axis_idx = {"x": 0, "y": 1, "z": 2}
    # In-plane basis (u, v) perpendicular to each axis, matching ObjectManager.cc.
    plane_basis = {
        "x": ((0, 1, 0), (0, 0, 1)),
        "y": ((1, 0, 0), (0, 0, 1)),
        "z": ((1, 0, 0), (0, 1, 0)),
    }

    result = []
    for key, obj in objects.items():
        if not isinstance(obj, dict):
            continue
        name = obj.get("name", key)
        a = appearance.get(name, {})
        base_pos = parse_pos(obj.get("POSITION"))
        base_rot = list(obj.get("ROTATION") or [0.0, 0.0, 0.0])
        replica = obj.get("REPLICA") if isinstance(obj.get("REPLICA"), dict) else None
        mode = str(replica.get("mode", "linear")).lower() if replica else "linear"
        axis = str(replica.get("axis", "x")).lower() if replica else "x"

        # Positions + optional per-copy rotation for each copy (C++-equivalent).
        copies = []
        if replica:
            if mode == "grid":
                nx = max(1, int(replica.get("nx", 0) or 0))
                ny = max(1, int(replica.get("ny", 0) or 0))
                nz = max(1, int(replica.get("nz", 0) or 0))
                dx = _safe_dim(replica.get("dx"), 0.0)
                dy = _safe_dim(replica.get("dy"), 0.0)
                dz = _safe_dim(replica.get("dz"), 0.0)
                u, v = plane_basis.get(axis, ((1, 0, 0), (0, 1, 0)))
                axv = axis_idx.get(axis, 2)
                for k in range(nz):
                    for j in range(ny):
                        for i in range(nx):
                            p = list(base_pos)
                            p[0] += i * dx * u[0] + j * dy * v[0]
                            p[1] += i * dx * u[1] + j * dy * v[1]
                            p[2] += i * dx * u[2] + j * dy * v[2]
                            p[axv] += k * dz
                            copies.append((p, list(base_rot)))
            elif mode == "polar":
                count = max(0, int(replica.get("count", 0) or 0))
                radius = _safe_dim(replica.get("radius"), 0.0)
                start = _safe_dim(replica.get("startAngle"), 0.0)
                total = _safe_dim(replica.get("totalAngle"), 360.0)
                rotate = bool(replica.get("rotate", False))
                step_ang = total / count if count > 1 else 0.0
                u, v = plane_basis.get(axis, ((1, 0, 0), (0, 1, 0)))
                for i in range(count):
                    ang = math.radians(start + i * step_ang)
                    p = [
                        base_pos[0] + radius * math.cos(ang) * u[0] + radius * math.sin(ang) * v[0],
                        base_pos[1] + radius * math.cos(ang) * u[1] + radius * math.sin(ang) * v[1],
                        base_pos[2] + radius * math.cos(ang) * u[2] + radius * math.sin(ang) * v[2],
                    ]
                    r = list(base_rot)
                    if rotate:
                        r[axis_idx.get(axis, 2)] += start + i * step_ang
                    copies.append((p, r))
            else:  # linear
                count = max(0, int(replica.get("count", 0) or 0))
                step = _safe_dim(replica.get("step"), 0.0)
                off = _safe_dim(replica.get("offset"), 0.0)
                axv = axis_idx.get(axis, 0)
                for i in range(count):
                    p = list(base_pos)
                    p[axv] += off + i * step
                    copies.append((p, list(base_rot)))

        n = len(copies)
        if n > 1:
            for i, (p, r) in enumerate(copies):
                if i == 0:
                    result.append(_entry(name, obj, a, p, {
                        "kind": "primitive",
                        "replica": {k: replica.get(k) for k in replica},
                    }))
                else:
                    src = {"kind": "replica", "parent": name, "index": i}
                    e = _entry(name + "[" + str(i) + "]", obj, a, p, src)
                    e["rotation"] = r
                    result.append(e)
        else:
            result.append(_entry(name, obj, a, base_pos, {"kind": "primitive"}))
    # Collect layers: explicit GEOMETRY.LAYERS list + any layer referenced by objects.
    layers = [str(x) for x in (geom.get("LAYERS") or []) if x]
    for obj in result:
        if obj.get("layer") and obj["layer"] not in layers:
            layers.append(obj["layer"])
    # DICOM transfer meshes (volumes marked in the DICOM tab, exported by the core).
    try:
        with open("/tmp/g4care_transfer_meshes.json", "r") as f:
            tm = json.load(f)
        if isinstance(tm, dict) and isinstance(tm.get("meshes"), list):
            for m in tm["meshes"]:
                if isinstance(m, dict):
                    m.setdefault("source", {})["kind"] = "dicom"
            meshes = list(meshes) + tm["meshes"]
    except Exception:
        pass
    return {"objects": result, "layers": sorted(set(layers)), "meshes": meshes}


def geometry_signature(path):
    """Cheap change-detection signature (YAML + GDML mtimes, object/mesh structure)."""
    try:
        st = os.stat(path)
        base = f"{st.st_mtime_ns}:{st.st_size}"
    except OSError:
        base = "none"
    try:
        data = load_geometry(path)
        parts = [base]
        for o in data.get("objects", []):
            parts.append(json.dumps(o, sort_keys=True))
        gdml_files = set()
        for m in data.get("meshes", []):
            src = m.get("source") or {}
            if src.get("kind") == "gdml" and src.get("file"):
                gdml_files.add(src["file"])
            parts.append(f"{m.get('name')}:{len(m.get('vertices', []))}:{len(m.get('triangles', []))}")
        # Include GDML source mtime/size so placement edits (which keep vertex
        # counts unchanged) still trigger a viewer reload.
        for gf in sorted(gdml_files):
            try:
                gst = os.stat(gf)
                parts.append(f"gdml:{gf}:{gst.st_mtime_ns}:{gst.st_size}")
            except OSError:
                parts.append(f"gdml:{gf}:none")
        try:
            tst = os.stat("/tmp/g4care_transfer_meshes.json")
            parts.append(f"transfer:{tst.st_mtime_ns}:{tst.st_size}")
        except OSError:
            pass
        return hashlib.md5("\n".join(parts).encode("utf-8")).hexdigest()
    except Exception:
        return base



def edit_dicom_transfer(data):
    """Delete / rename a DICOM transfer mesh in /tmp/g4care_transfer_meshes.json."""
    path = "/tmp/g4care_transfer_meshes.json"
    try:
        with open(path, "r") as f:
            doc = json.load(f)
    except Exception:
        return False, "no transfer file"
    meshes = doc.get("meshes", [])
    op = data.get("op")
    name = data.get("name")
    if op == "delete":
        meshes = [m for m in meshes if m.get("name") != name]
    elif op == "rename":
        new_name = data.get("newName")
        for m in meshes:
            if m.get("name") == name:
                m["name"] = new_name
    else:
        return False, "unknown op"
    doc["meshes"] = meshes
    try:
        with open(path, "w") as f:
            json.dump(doc, f)
    except Exception as e:
        return False, str(e)
    return True, ""
