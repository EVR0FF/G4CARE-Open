#==============================================================================
# G4CARE - materials.py — Materials handling
#
# Materials handling
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""Material discovery and persistence (NIST list, user folder, GDML, inline)."""

import glob
import json
import os

import yaml

from . import config


def load_nist_materials():
    """NIST material names from the VSCode schema (full Geant4 list)."""
    try:
        with open(config.SCHEMA) as f:
            schema = json.load(f)
        return list(schema.get("definitions", {}).get("materialEnum", {}).get("enum", []))
    except Exception:
        return ["G4_Galactic", "G4_AIR", "G4_WATER", "G4_Pb", "G4_Al",
                "G4_Cu", "G4_Fe", "G4_Si", "G4_C", "G4_W"]


def _read_material_name(fn):
    try:
        with open(fn) as f:
            d = yaml.safe_load(f) or {}
        if isinstance(d, dict):
            # file may be {MaterialName: {name, density, ...}} or {name: ...}
            for key, val in d.items():
                if isinstance(val, dict):
                    return val.get("name") or key
                return key
            return None
        return None
    except Exception:
        return None


def load_user_materials():
    """Custom materials: folder (~/Documents/g4care/materials/*.yaml) + inline `materials:` block."""
    result = []
    try:
        os.makedirs(config.MATERIALS_DIR, exist_ok=True)
        for fn in sorted(glob.glob(os.path.join(config.MATERIALS_DIR, "*.yaml"))):
            name = _read_material_name(fn)
            if name and name not in result:
                result.append(name)
    except Exception:
        pass
    try:
        cfg = config.load_yaml()
        mats = cfg.get("materials") or {}
        if isinstance(mats, dict):
            for key, m in mats.items():
                name = (m.get("name") if isinstance(m, dict) else key) or key
                if name and name not in result:
                    result.append(name)
    except Exception:
        pass
    return result


def load_material_def(name):
    """Return the {name, density, state, elements} dict for a folder material, or None."""
    fn = os.path.join(config.MATERIALS_DIR, name + ".yaml")
    try:
        with open(fn) as f:
            d = yaml.safe_load(f) or {}
        if isinstance(d, dict):
            for key, val in d.items():
                if isinstance(val, dict):
                    return val
                return {"name": key}
    except Exception:
        return None
    return None


def create_material(data):
    name = str(data.get("name") or "").strip()
    if not name:
        return False, "no name"
    mat = {
        "name": name,
        "density": data.get("density") or "1.0*g/cm3",
        "state": data.get("state") or "solid",
        "elements": data.get("elements") or {},
    }
    try:
        os.makedirs(config.MATERIALS_DIR, exist_ok=True)
        with open(os.path.join(config.MATERIALS_DIR, name + ".yaml"), "w") as f:
            yaml.safe_dump({name: mat}, f, allow_unicode=True, sort_keys=False)
        return True, ""
    except Exception as e:
        return False, str(e)


def delete_material(data):
    name = str(data.get("name") or "").strip()
    if not name:
        return False, "no name"
    fn = os.path.join(config.MATERIALS_DIR, name + ".yaml")
    try:
        if os.path.isfile(fn):
            os.remove(fn)
        # Also remove it from the inline `materials:` block (otherwise it reappears
        # in the dropdown because it was merged there on assignment).
        try:
            cfg = config.load_yaml()
            mats = cfg.get("materials")
            if isinstance(mats, dict) and name in mats:
                del mats[name]
                config.save_yaml(cfg)
        except Exception:
            pass
        return True, ""
    except Exception as e:
        return False, str(e)
