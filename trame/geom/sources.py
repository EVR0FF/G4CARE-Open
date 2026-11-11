#==============================================================================
# G4CARE - sources.py — Particle sources (GPS macro generation)
#
# Particle sources (GPS macro generation)
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""SOURCE block parsing -> /sources.json for the viewer source panel."""

import os
import re

from . import config


# Keys that are top-level SOURCE-block settings, not source definitions.
_BLOCK_KEYS = {"SOURCE_MODE"}


def _parse_vec(v):
    """Normalise a pos/direction value to {x,y,z} with raw scalars preserved."""
    if isinstance(v, dict):
        return {
            "x": v.get("x"),
            "y": v.get("y"),
            "z": v.get("z"),
        }
    if isinstance(v, (list, tuple)) and len(v) >= 3:
        return {"x": v[0], "y": v[1], "z": v[2]}
    return None


def load_sources():
    """Return {mode, sources:[...]} parsed from GEOMETRY-agnostic SOURCE block.

    Values are kept raw (numbers stay numbers, expressions stay strings) so the
    viewer can decide how to render numeric vs ExprTK values.
    """
    try:
        cfg = config.load_yaml()
    except Exception:
        return {"mode": "flow", "sources": []}

    block = cfg.get("SOURCE", {}) or {}
    if not isinstance(block, dict):
        return {"mode": "flow", "sources": []}

    mode = block.get("SOURCE_MODE", "flow")
    sources = []
    for name, s in block.items():
        if name in _BLOCK_KEYS or not isinstance(s, dict):
            continue
        entry = {
            "name": name,
            "type": s.get("type"),
            "weight": s.get("weight", 1.0),
            "particle": s.get("particle"),
            "energy": s.get("energy"),
            "spectrum": s.get("spectrum"),
            "min": s.get("min"),
            "max": s.get("max"),
            "sigma": s.get("sigma"),
            "direction": _parse_vec(s.get("direction")),
            "direction_mode": s.get("direction_mode", "fixed"),
            "cone_angle": s.get("cone_angle"),
            "sigma_theta": s.get("sigma_theta"),
            "sigma_phi": s.get("sigma_phi"),
            "pos": _parse_vec(s.get("pos")),
            "isocenter": _parse_vec(s.get("isocenter")),
            "object": s.get("object"),
            "time": s.get("time"),
            "macro": s.get("macro"),
            "file": s.get("file"),
            "reject_if": s.get("reject_if"),
            "vertex_mode": s.get("vertex_mode"),
            "proper_time": s.get("proper_time"),
            "polarization": _parse_vec(s.get("polarization")),
            "Z": s.get("Z"),
            "A": s.get("A"),
            "excitation": s.get("excitation"),
            "magnetic_moment": s.get("magnetic_moment"),
            "lifetime": s.get("lifetime"),
            "iaea_code": s.get("iaea_code"),
            "px": s.get("px"),
            "py": s.get("py"),
            "pz": s.get("pz"),
            "data_alias": s.get("data_alias"),
            "data": s.get("data"),
            "mode": s.get("mode"),
            "reset_time": s.get("reset_time"),
            "loop": s.get("loop"),
            "isomer_table": s.get("isomer_table"),
            "ISOTOPES": s.get("ISOTOPES"),
            "PARTICLES": s.get("PARTICLES"),
            "VERTEX_GENERATOR": s.get("VERTEX_GENERATOR"),
            "VERTICES": s.get("VERTICES"),
            "components": s.get("components"),
            "decay": s.get("decay"),
        }
        sources.append(entry)
    return {"mode": mode, "sources": sources}


def _coerce(v):
    """Keep numbers as numbers and expression strings as strings."""
    if v is None or v == "":
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return v
    s = str(v).strip()
    try:
        f = float(s)
        return int(f) if f.is_integer() else f
    except ValueError:
        return s


def _coerce_bool(v):
    if isinstance(v, bool):
        return v
    return str(v).strip().lower() in ("true", "1", "yes", "on")


def _coerce_tree(v):
    """Recursively coerce scalars in a nested structure, dropping None leaves."""
    if isinstance(v, dict):
        out = {}
        for k, x in v.items():
            c = _coerce_tree(x)
            if c is not None:
                out[k] = c
        return out
    if isinstance(v, list):
        return [_coerce_tree(x) for x in v]
    return _coerce(v)


def load_data_sections():
    """Return DATA block subsection names (for the activation `data` picker)."""
    try:
        cfg = config.load_yaml()
    except Exception:
        return []
    block = cfg.get("DATA", {}) or {}
    if not isinstance(block, dict):
        return []
    return [k for k, v in block.items() if isinstance(v, dict)]


def apply_source_update(data):
    """Edit one source's fields and write back to YAML. Returns (ok, msg)."""
    name = data.get("name")
    if not name:
        return False, "no name"
    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)
    block = cfg.setdefault("SOURCE", {})
    if not isinstance(block, dict):
        block = {}
        cfg["SOURCE"] = block
    src = block.get(name)
    if not isinstance(src, dict):
        if data.get("delete"):
            return False, "source not found: " + str(name)
        # Creating a brand-new source (e.g. placed in the viewer via click).
        src = {}
        block[name] = src
        src.setdefault("type", data.get("type") or "particleGun")

    if data.get("delete"):
        block.pop(name, None)
        try:
            config.save_yaml(cfg)
            return True, ""
        except Exception as e:
            return False, str(e)

    if "SOURCE_MODE" in data:
        block["SOURCE_MODE"] = data["SOURCE_MODE"]

    def set_scalar(key, coerce_fn=_coerce):
        if key in data:
            v = coerce_fn(data[key])
            if v is None:
                src.pop(key, None)
            else:
                src[key] = v

    # scalar text fields (may be numbers or ExprTK expression strings)
    for key in ("type", "particle", "energy", "direction_mode", "object", "time",
                "macro", "file", "reject_if", "data_alias", "data", "mode", "isomer_table",
                "vertex_mode", "proper_time", "excitation", "magnetic_moment",
                "lifetime", "Z", "A", "iaea_code", "px", "py", "pz", "spectrum"):
        set_scalar(key)

    # numeric-or-expression scalars
    for key in ("weight", "cone_angle", "sigma_theta", "sigma_phi",
                "min", "max", "sigma"):
        set_scalar(key)

    # boolean fields (activation)
    for key in ("reset_time", "loop"):
        if key in data:
            src[key] = _coerce_bool(data[key])

    # vec fields (pos / direction / polarization / isocenter)
    for key in ("direction", "pos", "polarization", "isocenter"):
        if key in data and isinstance(data[key], dict):
            vec = {k: _coerce(data[key].get(k)) for k in ("x", "y", "z")}
            vec = {k: v for k, v in vec.items() if v is not None}
            if vec:
                src[key] = vec
            else:
                src.pop(key, None)

    if "ISOTOPES" in data:
        tree = _coerce_tree(data["ISOTOPES"])
        if tree:
            src["ISOTOPES"] = tree
        else:
            src.pop("ISOTOPES", None)

    # nested object fields (editor sends the full reconstructed structure)
    for key in ("VERTEX_GENERATOR", "VERTICES", "components", "decay",
                "PARTICLES"):
        if key in data and isinstance(data[key], dict):
            tree = _coerce_tree(data[key])
            if tree:
                src[key] = tree
            else:
                src.pop(key, None)

    try:
        config.save_yaml(cfg)
        return True, ""
    except Exception as e:
        return False, str(e)


def write_gps_macro(data):
    """Write a generated GPS macro file next to the YAML and set SOURCE.<name>.macro.

    The viewer builds the /gps/... text client-side and sends {name, macro}; this
    helper only persists it and wires the YAML reference.
    """
    name = data.get("name")
    macro = data.get("macro")
    if not name or not isinstance(macro, str) or not macro.strip():
        return False, "no name/macro"

    cfgdir = os.path.dirname(config.YAML)
    fname = "gps_" + re.sub(r"[^A-Za-z0-9_.-]", "_", str(name)) + ".mac"
    path = os.path.join(cfgdir, fname)
    try:
        with open(path, "w") as f:
            f.write(macro)
    except OSError as e:
        return False, str(e)

    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)

    block = cfg.setdefault("SOURCE", {})
    if not isinstance(block, dict):
        block = {}
        cfg["SOURCE"] = block
    src = block.get(name)
    if not isinstance(src, dict):
        src = {}
        block[name] = src
    src.setdefault("type", "GPS")
    src["macro"] = path
    try:
        config.save_yaml(cfg)
        return True, path
    except Exception as e:
        return False, str(e)
