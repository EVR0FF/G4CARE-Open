#==============================================================================
# G4CARE - annotations.py — Geometry annotations
#
# Geometry annotations
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""ANNOTATIONS persistence (labels with callouts in the YAML config)."""

import os

import yaml

from . import config


def load_annotations(path):
    """ANNOTATIONS list from the YAML config (labels with callouts)."""
    if not os.path.isfile(path):
        return []
    try:
        with open(path) as f:
            cfg = yaml.safe_load(f) or {}
    except Exception:
        return []
    ann = cfg.get("ANNOTATIONS") or []
    return ann if isinstance(ann, list) else []


def save_annotations(data):
    """Replace the ANNOTATIONS list in the YAML config."""
    ann = data.get("annotations")
    if not isinstance(ann, list):
        return False, "no annotations list"
    try:
        cfg = config.load_yaml()
        cfg["ANNOTATIONS"] = ann
        config.save_yaml(cfg)
        return True, ""
    except Exception as e:
        return False, str(e)
