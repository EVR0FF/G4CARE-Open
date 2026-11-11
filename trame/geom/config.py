#==============================================================================
# G4CARE - config.py — Geometry configuration
#
# Geometry configuration
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""Shared paths, constants and YAML IO for the geometry server."""

import os

import yaml
from yaml.representer import SafeRepresenter


# Keep short scalar lists (DIMENSIONS/POSITION/ROTATION/color) inline in YAML
# instead of bloating them into multi-line block sequences.
def _represent_compact_list(dumper, data):
    if data and all(isinstance(x, (str, int, float, bool)) for x in data):
        return dumper.represent_sequence("tag:yaml.org,2002:seq", data, flow_style=True)
    return dumper.represent_sequence("tag:yaml.org,2002:seq", data, flow_style=False)


SafeRepresenter.add_representer(list, _represent_compact_list)


UNIT_TO_MM = {
    "nm": 1e-6, "um": 1e-3, "mm": 1.0, "cm": 10.0, "m": 1000.0,
    "km": 1e6, "deg": 1.0, "rad": 57.2957795,
}

# config.py lives in trame/geom/, so the trame/ root is one level up.
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YAML = "/tmp/g4care_gui_config.yaml"
SCHEMA = os.path.join(HERE, "..", ".vscode", "config-schema.json")
MATERIALS_DIR = os.path.expanduser("~/Documents/g4care/materials")


def set_yaml(path):
    """Point the server at a specific YAML config (called from main())."""
    global YAML
    YAML = os.path.abspath(path)


def load_yaml():
    with open(YAML) as f:
        return yaml.safe_load(f) or {}


def save_yaml(cfg):
    with open(YAML, "w") as f:
        yaml.safe_dump(cfg, f, allow_unicode=True, sort_keys=False)


def resolve_gdml(ref, yaml_dir):
    """Resolve a relative GDML path against the YAML dir, /tmp side-channel and cwd."""
    if not ref:
        return None
    if os.path.isabs(ref):
        return ref if os.path.isfile(ref) else None
    bases = [yaml_dir]
    try:
        with open("/tmp/g4care_cfgdir") as f:
            d = f.read().strip()
            if d:
                bases.append(d)
    except (OSError, IOError):
        pass
    bases.append(os.getcwd())
    for base in bases:
        p = os.path.join(base, ref)
        if os.path.isfile(p):
            return p
    return None
