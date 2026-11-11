#==============================================================================
# G4CARE - gdml_edit.py — GDML edit operations
#
# GDML edit operations
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""Edit GDML XML in place (physvol placement / duplicate / delete).

The geometry server keeps GEOMETRY.FILE as the source of truth; this module
mutates that file directly. Tessellation is cached per solid in gdml_loader, so
placement edits only re-apply transforms (no re-tessellation).

Tags are matched by local name so both namespaced and non-namespaced GDML work.
"""

import copy
import os
from xml.etree import ElementTree as ET

from . import config


def _local(tag):
    return tag.split("}", 1)[-1]


def _children(el, tag):
    return [c for c in el if _local(c.tag) == tag]


def _child(el, tag):
    for c in el:
        if _local(c.tag) == tag:
            return c
    return None


def _iter_physvols(root):
    for el in root.iter():
        if _local(el.tag) == "physvol":
            yield el


def _find_physvol(root, name):
    for pv in _iter_physvols(root):
        if pv.get("name") == name:
            return pv
    return None


def _find_parent(root, target):
    for el in root.iter():
        for c in el:
            if c is target:
                return el
    return None


def _get_position(pv):
    el = _child(pv, "position")
    if el is None:
        return None
    try:
        return [float(el.get("x", 0.0)), float(el.get("y", 0.0)), float(el.get("z", 0.0))]
    except (TypeError, ValueError):
        return None


def _set_position(pv, pos):
    el = _child(pv, "position")
    if el is None:
        el = ET.SubElement(pv, "position")
    el.set("unit", "mm")
    el.set("x", repr(pos[0]))
    el.set("y", repr(pos[1]))
    el.set("z", repr(pos[2]))
    return el


def _set_rotation(pv, rot):
    el = _child(pv, "rotation")
    if el is None:
        el = ET.SubElement(pv, "rotation")
    el.set("unit", "deg")
    el.set("x", repr(rot[0]))
    el.set("y", repr(rot[1]))
    el.set("z", repr(rot[2]))
    return el


def _set_position_physvol(root, name, value):
    pv = _find_physvol(root, name)
    if pv is None:
        return False
    _set_position(pv, [float(v) for v in value[:3]])
    return True


def _set_rotation_physvol(root, name, value):
    pv = _find_physvol(root, name)
    if pv is None:
        return False
    _set_rotation(pv, [float(v) for v in value[:3]])
    return True


def _duplicate_physvol(root, name, new_name, offset):
    pv = _find_physvol(root, name)
    if pv is None:
        return None
    parent = _find_parent(root, pv)
    if parent is None:
        return None
    new_pv = copy.deepcopy(pv)
    new_pv.set("name", new_name)
    pos = _get_position(pv)
    if pos is not None and offset:
        _set_position(new_pv, [
            pos[0] + float(offset[0]),
            pos[1] + float(offset[1]),
            pos[2] + float(offset[2]),
        ])
    idx = list(parent).index(pv)
    parent.insert(idx + 1, new_pv)
    return new_name


def _delete_physvol(root, name):
    pv = _find_physvol(root, name)
    if pv is None:
        return False
    parent = _find_parent(root, pv)
    if parent is None:
        return False
    parent.remove(pv)
    return True


def _existing_names(root):
    return {pv.get("name") for pv in _iter_physvols(root) if pv.get("name")}


def apply_edit(path, op):
    """Mutate `path` in place. op: {op, name, value?, offset?, newName?}."""
    kind = op.get("op")
    name = op.get("name")
    if not name:
        return False, "no name"
    try:
        tree = ET.parse(path)
    except Exception as e:
        return False, str(e)
    root = tree.getroot()

    if kind == "set_position":
        if not isinstance(op.get("value"), list) or len(op["value"]) < 3:
            return False, "bad value"
        if not _set_position_physvol(root, name, op["value"]):
            return False, "physvol not found: " + str(name)
    elif kind == "set_rotation":
        if not isinstance(op.get("value"), list) or len(op["value"]) < 3:
            return False, "bad value"
        if not _set_rotation_physvol(root, name, op["value"]):
            return False, "physvol not found: " + str(name)
    elif kind == "duplicate":
        offset = op.get("offset") or [10.0, 10.0, 0.0]
        new_name = op.get("newName")
        if not new_name:
            existing = _existing_names(root)
            base = name + "_copy"
            new_name = base
            i = 1
            while new_name in existing:
                i += 1
                new_name = base + str(i)
        if _duplicate_physvol(root, name, str(new_name), offset) is None:
            return False, "physvol not found: " + str(name)
    elif kind == "delete":
        if not _delete_physvol(root, name):
            return False, "physvol not found: " + str(name)
    else:
        return False, "unknown op: " + str(kind)

    try:
        tree.write(path, encoding="utf-8", xml_declaration=True)
        return True, ""
    except Exception as e:
        return False, str(e)


def edit_gdml(data):
    """Resolve GEOMETRY.FILE and apply a single GDML edit op."""
    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)
    geom = cfg.get("GEOMETRY", {}) or {}
    ref = geom.get("FILE") if isinstance(geom, dict) else None
    if not ref:
        return False, "no GEOMETRY.FILE"
    yaml_dir = os.path.dirname(os.path.abspath(config.YAML))
    gp = config.resolve_gdml(ref, yaml_dir)
    if not gp:
        return False, "gdml not found: " + str(ref)
    return apply_edit(gp, data)
