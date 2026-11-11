#==============================================================================
# G4CARE - updates.py — Geometry updates
#
# Geometry updates
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""YAML mutation handlers (object properties, layers, duplicate/delete, batch)."""

import copy as _copy

from . import config
from .load import parse_dim
from .materials import load_material_def


def _find_key(objects, name):
    for k, obj in objects.items():
        if isinstance(obj, dict) and obj.get("name", k) == name:
            return k
    return None


def _existing_names(objects):
    return {obj.get("name", k) for k, obj in objects.items() if isinstance(obj, dict)}


def _duplicate(cfg, d):
    name = d.get("name")
    if not name:
        return False, "no name"
    geom = cfg.setdefault("GEOMETRY", {})
    objects = geom.setdefault("OBJECTS", {})
    src_key = _find_key(objects, name)
    if src_key is None:
        return False, "object not found: " + name
    new_obj = _copy.deepcopy(objects[src_key])

    existing = _existing_names(objects)
    base = d.get("newName") or (name + "_copy")
    new_name = base
    i = 1
    while new_name in existing:
        i += 1
        new_name = base + str(i)
    new_obj["name"] = new_name

    # Optional numeric offset (parses unit strings like "25*cm" via parse_dim).
    off = d.get("offset")
    pos = new_obj.get("POSITION")
    if isinstance(off, list) and len(off) >= 3 and isinstance(pos, list) and pos:
        try:
            num = [parse_dim(x) for x in pos[:3]]
        except (TypeError, ValueError):
            num = None
        if num is not None:
            new_obj["POSITION"] = [
                num[0] + float(off[0]),
                num[1] + float(off[1]),
                num[2] + float(off[2]),
            ]

    # Choose a free dictionary key (OBJECTS is keyed by a short id).
    key = new_name
    k2 = key
    j = 1
    while k2 in objects:
        j += 1
        k2 = key + str(j)
    objects[k2] = new_obj

    app = geom.get("APPEARANCE", {})
    if isinstance(app, dict) and name in app:
        app[new_name] = _copy.deepcopy(app[name])
    return True, new_name


def _delete(cfg, d):
    names = d.get("names")
    if not isinstance(names, list):
        names = [d.get("name")] if d.get("name") else []
    if not names:
        return False, "no names"
    geom = cfg.setdefault("GEOMETRY", {})
    objects = geom.setdefault("OBJECTS", {})
    removed = 0
    for name in names:
        key = _find_key(objects, name)
        if key is not None:
            del objects[key]
            removed += 1
        app = geom.get("APPEARANCE", {})
        if isinstance(app, dict) and name in app:
            del app[name]
    if not removed:
        return False, "objects not found"
    return True, "removed %d" % removed


def _insert(cfg, d):
    """Insert an object from full data (used by clipboard paste)."""
    obj = d.get("object")
    if not isinstance(obj, dict):
        return False, "no object data"
    geom = cfg.setdefault("GEOMETRY", {})
    objects = geom.setdefault("OBJECTS", {})
    base = obj.get("name") or "object"
    new_name = d.get("newName") or (base + "_copy")
    existing = _existing_names(objects)
    nm = new_name
    i = 1
    while nm in existing:
        i += 1
        nm = new_name + str(i)
    new_obj = _copy.deepcopy(obj)
    new_obj["name"] = nm
    off = d.get("offset")
    pos = new_obj.get("POSITION")
    if isinstance(off, list) and len(off) >= 3 and isinstance(pos, list) and pos:
        try:
            num = [parse_dim(x) for x in pos[:3]]
        except (TypeError, ValueError):
            num = None
        if num is not None:
            new_obj["POSITION"] = [num[0] + float(off[0]), num[1] + float(off[1]), num[2] + float(off[2])]
    key = nm
    k2 = key
    j = 1
    while k2 in objects:
        j += 1
        k2 = key + str(j)
    objects[k2] = new_obj
    return True, nm


def _set_replica(cfg, d):
    """Set/clear REPLICA (linear | grid | polar) on an object."""
    name = d.get("name")
    if not name:
        return False, "no name"
    geom = cfg.setdefault("GEOMETRY", {})
    objects = geom.setdefault("OBJECTS", {})
    key = _find_key(objects, name)
    if key is None:
        return False, "object not found: " + name
    obj = objects[key]

    mode = str(d.get("mode", "linear")).lower()
    # A grid needs nx*ny (>0); linear/polar need count > 1.
    if mode == "grid":
        try:
            nx = int(d.get("nx", 0) or 0)
            ny = int(d.get("ny", 0) or 0)
        except (TypeError, ValueError):
            nx = ny = 0
        active = nx > 0 and ny > 0
    else:
        try:
            count = int(d.get("count", 0) or 0)
        except (TypeError, ValueError):
            count = 0
        active = count > 1

    if not active:
        obj.pop("REPLICA", None)
        return True, ""

    rep = obj.setdefault("REPLICA", {})
    rep["mode"] = mode
    rep["axis"] = str(d.get("axis", "x")).lower()

    if mode == "grid":
        rep["nx"] = int(d.get("nx", 1) or 1)
        rep["ny"] = int(d.get("ny", 1) or 1)
        if d.get("nz") is not None:
            rep["nz"] = int(d.get("nz", 1) or 1)
        for f in ("dx", "dy", "dz"):
            if d.get(f) is not None:
                rep[f] = d[f]
    elif mode == "polar":
        rep["count"] = int(d.get("count", 2) or 2)
        if d.get("radius") is not None:
            rep["radius"] = d["radius"]
        if d.get("startAngle") is not None:
            rep["startAngle"] = d["startAngle"]
        if d.get("totalAngle") is not None:
            rep["totalAngle"] = d["totalAngle"]
        rep["rotate"] = bool(d.get("rotate", False))
    else:  # linear
        rep["count"] = int(d.get("count", 2) or 2)
        if d.get("step") is not None:
            rep["step"] = d["step"]
        if d.get("offset") is not None:
            rep["offset"] = d["offset"]
    return True, ""


def _apply_one(cfg, data):
    """Mutate cfg in-place for one operation. Returns (ok, msg)."""
    # Layer-list persistence (no object name required).
    if isinstance(data.get("layers"), list):
        geom = cfg.setdefault("GEOMETRY", {})
        geom["LAYERS"] = [str(x) for x in data["layers"] if x]
        return True, ""
    if isinstance(data.get("duplicate"), dict):
        return _duplicate(cfg, data["duplicate"])
    if isinstance(data.get("delete"), dict):
        return _delete(cfg, data["delete"])
    if isinstance(data.get("insert"), dict):
        return _insert(cfg, data["insert"])
    if isinstance(data.get("replica"), dict):
        return _set_replica(cfg, data["replica"])

    name = data.get("name")
    if not name:
        return False, "no name"
    geom = cfg.setdefault("GEOMETRY", {})
    objects = geom.setdefault("OBJECTS", {})
    obj_key = _find_key(objects, name)

    if obj_key is not None:
        obj = objects[obj_key]
        if isinstance(data.get("position"), list):
            obj["POSITION"] = [float(v) for v in data["position"][:3]]
        if data.get("material"):
            mat = str(data["material"])
            obj["material"] = mat
            # merge a folder-defined custom material into the config so the C++ sees it
            mdef = load_material_def(mat)
            if mdef:
                cfg.setdefault("materials", {})[mat] = mdef
        if data.get("shape"):
            obj["shape"] = str(data["shape"])
        if isinstance(data.get("rotation"), list):
            obj["ROTATION"] = [float(v) for v in data["rotation"][:3]]
        if "isSensitive" in data:
            obj["isSensitive"] = bool(data["isSensitive"])
        if "mother" in data:
            if data.get("mother"):
                obj["mother"] = str(data["mother"])
            else:
                obj.pop("mother", None)
        if data.get("newName"):
            new_name = str(data["newName"])
            obj["name"] = new_name
            app = geom.get("APPEARANCE", {})
            if isinstance(app, dict) and name in app:
                app[new_name] = app.pop(name)

    if "color" in data or "opacity" in data or "layer" in data or "visible" in data:
        app = geom.setdefault("APPEARANCE", {})
        entry = app.setdefault(name, {})
        if isinstance(data.get("color"), list) and len(data["color"]) >= 3:
            entry["color"] = [float(x) for x in data["color"][:3]]
        if "opacity" in data:
            entry["opacity"] = float(data["opacity"])
        if "layer" in data:
            entry["layer"] = str(data["layer"])
        if "visible" in data:
            entry["visible"] = bool(data["visible"])
    return True, ""


def apply_update(data):
    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)
    ok, msg = _apply_one(cfg, data)
    if not ok:
        return ok, msg
    try:
        config.save_yaml(cfg)
        return True, msg
    except Exception as e:
        return False, str(e)


def apply_batch(ops):
    """Apply a list of operations atomically (single load + single save)."""
    if not isinstance(ops, list):
        return False, "ops must be a list"
    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)
    messages = []
    for op in ops:
        ok, msg = _apply_one(cfg, op)
        if not ok:
            return False, "; ".join(messages + [msg])
        messages.append(msg if msg else "ok")
    try:
        config.save_yaml(cfg)
        return True, "; ".join(messages)
    except Exception as e:
        return False, str(e)

