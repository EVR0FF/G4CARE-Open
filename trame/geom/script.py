#==============================================================================
# G4CARE - script.py — Script generation
#
# Script generation
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""SCRIPT block parsing -> /script.json for the viewer script panel."""

from . import config


def load_script():
    """Return {variables, functions, scripts} parsed from the SCRIPT block."""
    try:
        cfg = config.load_yaml()
    except Exception:
        return {"variables": {}, "functions": {}, "scripts": []}
    block = cfg.get("SCRIPT", {}) or {}
    if not isinstance(block, dict):
        return {"variables": {}, "functions": {}, "scripts": []}
    variables = block.get("variables", {}) or {}
    functions = block.get("functions", {}) or {}
    scripts = block.get("scripts", []) or []
    if not isinstance(variables, dict):
        variables = {}
    if not isinstance(functions, dict):
        functions = {}
    if not isinstance(scripts, list):
        scripts = []
    return {"variables": variables, "functions": functions, "scripts": scripts}


def apply_script_update(data):
    """Replace the SCRIPT block (variables / functions / scripts).

    Returns (ok, msg).  Each key may be omitted to leave it untouched;
    a falsy value removes that subsection.  If none of the three keys are
    present, the whole SCRIPT block is deleted.
    """
    variables = data.get("variables")
    functions = data.get("functions")
    scripts = data.get("scripts")

    try:
        cfg = config.load_yaml()
    except Exception as e:
        return False, str(e)

    if variables is None and functions is None and scripts is None:
        cfg.pop("SCRIPT", None)
        try:
            config.save_yaml(cfg)
            return True, ""
        except Exception as e:
            return False, str(e)

    block = cfg.setdefault("SCRIPT", {})
    if not isinstance(block, dict):
        block = {}
        cfg["SCRIPT"] = block

    if variables is not None:
        if isinstance(variables, dict) and variables:
            block["variables"] = variables
        else:
            block.pop("variables", None)
    if functions is not None:
        if isinstance(functions, dict) and functions:
            block["functions"] = functions
        else:
            block.pop("functions", None)
    if scripts is not None:
        if isinstance(scripts, list) and scripts:
            block["scripts"] = scripts
        else:
            block.pop("scripts", None)

    if not block:
        cfg.pop("SCRIPT", None)
    try:
        config.save_yaml(cfg)
        return True, ""
    except Exception as e:
        return False, str(e)
