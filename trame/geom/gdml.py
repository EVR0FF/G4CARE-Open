#==============================================================================
# G4CARE - gdml.py — GDML handling
#
# GDML handling
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""GDML scene -> triangle meshes and material extraction (cached)."""

import os

from . import config

# GDML scene -> triangle meshes cache (parsing is expensive; the viewer polls
# /geometry.json every second so we must not re-tessellate on every request).
_GDML_CACHE = {}


def load_gdml_meshes(gdml_path):
    if not gdml_path:
        return []
    try:
        sig = (os.path.getmtime(gdml_path), os.path.getsize(gdml_path))
    except OSError:
        return []
    hit = _GDML_CACHE.get(gdml_path)
    if hit and hit[0] == sig:
        return hit[1]
    try:
        import gdml_loader
        meshes = gdml_loader.load_gdml(gdml_path)
    except Exception:
        meshes = []
    _GDML_CACHE[gdml_path] = (sig, meshes)
    return meshes


def load_gdml_materials_from_config():
    """Collect custom materials from every GDML file referenced in the config."""
    try:
        cfg = config.load_yaml()
    except Exception:
        return []
    yaml_dir = os.path.dirname(os.path.abspath(config.YAML))
    geom = cfg.get("GEOMETRY", {}) or {}
    refs = []
    if isinstance(geom, dict):
        if geom.get("FILE"):
            refs.append(geom["FILE"])
        world = geom.get("WORLD")
        if isinstance(world, dict) and world.get("file"):
            refs.append(world["file"])
    objects = geom.get("OBJECTS", {}) if isinstance(geom, dict) else {}
    if isinstance(objects, dict):
        for obj in objects.values():
            if isinstance(obj, dict) and obj.get("file"):
                refs.append(obj["file"])
    result = {}
    for ref in refs:
        gp = config.resolve_gdml(ref, yaml_dir)
        if not gp:
            continue
        try:
            import gdml_loader
            mats = gdml_loader.load_gdml_materials(gp)
        except Exception:
            mats = []
        for m in mats:
            if m.get("name"):
                result[m["name"]] = m
    return [result[n] for n in sorted(result)]
