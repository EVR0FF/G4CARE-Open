#==============================================================================
# G4CARE - groups.py — Geometry groups
#
# Geometry groups
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""Group hierarchy persistence for the geometry viewer's object tree.

Groups are stored in a side-car JSON file (so they survive YAML reloads and are
independent of the engine). A group is {parent: str|None, children: [names...]};
children may be object names or nested group names.
"""

import json
import os

GROUPS_PATH = "/tmp/g4care_groups.json"


def load_groups():
    try:
        with open(GROUPS_PATH, "r") as f:
            data = json.load(f)
        if isinstance(data, dict):
            return data
    except Exception:
        pass
    return {}


def _save(groups):
    try:
        with open(GROUPS_PATH, "w") as f:
            json.dump(groups, f, ensure_ascii=False)
        return True, ""
    except OSError as e:
        return False, str(e)


def apply_group_update(data):
    groups = load_groups()
    op = data.get("op")
    if op == "create":
        name = data.get("name")
        if not name:
            return False, "no name"
        groups[name] = {
            "parent": data.get("parent"),
            "children": [c for c in (data.get("children") or []) if c != name],
        }
        parent = data.get("parent")
        if parent and parent in groups:
            ch = groups[parent].setdefault("children", [])
            if name not in ch:
                ch.append(name)
    elif op == "rename":
        name = data.get("name")
        new = data.get("newName")
        if not name or not new:
            return False, "no name/newName"
        if name not in groups:
            return False, "no such group"
        groups[new] = groups.pop(name)
        for g in groups.values():
            if g.get("parent") == name:
                g["parent"] = new
            ch = g.get("children")
            if ch:
                for i, c in enumerate(ch):
                    if c == name:
                        ch[i] = new
    elif op == "delete":
        name = data.get("name")
        if not name:
            return False, "no name"
        groups.pop(name, None)
        for g in groups.values():
            ch = g.get("children")
            if ch:
                g["children"] = [c for c in ch if c != name]
    elif op == "setParent":
        name = data.get("name")
        if name not in groups:
            return False, "no such group"
        groups[name]["parent"] = data.get("parent")
    elif op == "addMembers":
        name = data.get("name")
        if name not in groups:
            return False, "no such group"
        existing = groups[name].setdefault("children", [])
        for m in (data.get("members") or []):
            if m != name and m not in existing:
                existing.append(m)
    elif op == "removeMembers":
        name = data.get("name")
        if name in groups:
            members = set(data.get("members") or [])
            groups[name]["children"] = [c for c in groups[name].get("children", []) if c not in members]
    elif op == "merge":
        name = data.get("name")
        if not name:
            return False, "no name"
        members = list(data.get("members") or [])
        groups[name] = {"parent": data.get("parent"), "children": members}
        parent = data.get("parent")
        if parent and parent in groups:
            ch = groups[parent].setdefault("children", [])
            if name not in ch:
                ch.append(name)
        for gname, g in groups.items():
            if gname != name:
                g["children"] = [c for c in g.get("children", []) if c not in members]
    elif op == "moveMembers":
        name = data.get("name")
        if name not in groups:
            return False, "no such group"
        members = set(data.get("members") or [])
        for gname, g in groups.items():
            if gname != name:
                g["children"] = [c for c in g.get("children", []) if c not in members]
        existing = groups[name].setdefault("children", [])
        for m in members:
            if m != name and m not in existing:
                existing.append(m)
    elif op == "phantom":
        g = groups.setdefault("@phantom", {})
        if "label" in data:
            g["label"] = data["label"]
        if "ungrouped" in data:
            g["ungrouped"] = bool(data["ungrouped"])
    else:
        return False, "unknown op"
    return _save(groups)
