#==============================================================================
# G4CARE - geometry_server.py — Geometry server: YAML/GDML -> tessellation + edit API
#
# Geometry server: YAML/GDML -> tessellation + edit API
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""G4CARE geometry server — serves the vtk.js viewer (local rendering) + geometry JSON.

The 3D view is rendered client-side (vtk.js / WebGL in the Electron iframe).
This module is only the HTTP transport; domain logic lives in the `geom/`
package:

  geom/config.py       paths, constants, YAML IO, GDML path resolution
  geom/load.py         YAML geometry -> viewer JSON (objects + meshes + layers)
  geom/gdml.py         GDML scene -> triangle meshes / material extraction
  geom/materials.py    NIST + user + GDML material discovery/persistence
  geom/annotations.py  ANNOTATIONS list persistence
  geom/updates.py      YAML mutations from /api/update

Routes:
  GET /              -> viewer.html
  GET /viewer.js     -> the vtk.js client
  GET /geometry.json -> [{name, shape, dims, pos}, ...] parsed from the YAML config.
"""

import argparse
import json
import os
from urllib.parse import parse_qs
from http.server import HTTPServer, SimpleHTTPRequestHandler

from geom import config
from geom.annotations import load_annotations, save_annotations
from geom.gdml import load_gdml_materials_from_config
from geom.gdml_edit import edit_gdml
from geom.load import geometry_signature, load_geometry, edit_dicom_transfer
from geom.groups import load_groups, apply_group_update
from geom.sources import load_sources, apply_source_update, load_data_sections, write_gps_macro
from geom.script import load_script, apply_script_update
from geom.digitize import load_digitize, apply_digitize_update
from geom.materials import (
    create_material,
    delete_material,
    load_nist_materials,
    load_user_materials,
)
from geom.updates import apply_update, apply_batch

# Side-channel files written by the Electron main process (see gui/main.js):
#   setHighlight  -> /tmp/g4care_highlight.json  {"name": ...}
#   writeSettings -> /tmp/g4care_settings.json   {"grid": {...}, "axes_size": ...}
SIDE_FILES = {
    "/highlight.json": ("/tmp/g4care_highlight.json", '{"name": null}'),
    "/settings.json": ("/tmp/g4care_settings.json", "{}"),
}


class Handler(SimpleHTTPRequestHandler):
    def _send(self, data, ctype):
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _read(self, name):
        try:
            with open(os.path.join(config.HERE, name), "rb") as f:
                return f.read()
        except FileNotFoundError:
            return b""

    def _read_side(self, fname, default):
        try:
            with open(fname) as f:
                return f.read()
        except (FileNotFoundError, OSError):
            return default

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/geometry.json":
            self._send(json.dumps(load_geometry(config.YAML)), "application/json")
        elif path == "/geometry.sig":
            self._send(geometry_signature(config.YAML), "text/plain")
        elif path == "/materials.json":
            self._send(json.dumps({
                "nist": load_nist_materials(),
                "user": load_user_materials(),
                "gdml": load_gdml_materials_from_config(),
            }), "application/json")
        elif path == "/annotations.json":
            self._send(json.dumps(load_annotations(config.YAML)), "application/json")
        elif path == "/sources.json":
            self._send(json.dumps(load_sources()), "application/json")
        elif path == "/script.json":
            self._send(json.dumps(load_script()), "application/json")
        elif path == "/groups.json":
            self._send(json.dumps(load_groups()), "application/json")
        elif path == "/data.json":
            self._send(json.dumps(load_data_sections()), "application/json")
        elif path == "/api/digitize":
            name = parse_qs(self.path.split("?", 1)[1]).get("name", [""])[0] if "?" in self.path else ""
            self._send(json.dumps(load_digitize(name)), "application/json")
        elif path in ("/", "/index.html", "/viewer.html"):
            self._send(self._read("viewer.html"), "text/html")
        elif path in SIDE_FILES:
            fname, default = SIDE_FILES[path]
            self._send(self._read_side(fname, default), "application/json")
        elif path == "/viewer.bundle.js":
            self._send(self._read("viewer.bundle.js"), "text/javascript")
        elif path == "/viewer.js":
            self._send(self._read("src/viewer.js"), "text/javascript")
        elif path == "/debug":
            try:
                with open("/tmp/g4care_debug.log", "a") as f:
                    f.write(self.path.split("?", 1)[1] + "\n")
            except OSError:
                pass
            self._send("ok", "text/plain")
        else:
            super().do_GET()

    def do_POST(self):
        path = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length) if length else b""
        try:
            data = json.loads(body.decode("utf-8")) if body else {}
        except Exception:
            data = {}
        if path == "/api/update":
            ok, msg = apply_update(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/batch":
            ops = data.get("ops") if isinstance(data, dict) else data
            ok, msg = apply_batch(ops)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/gdml":
            ok, msg = edit_gdml(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/dicom":
            ok, msg = edit_dicom_transfer(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/source":
            ok, msg = apply_source_update(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/script":
            ok, msg = apply_script_update(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/gps-macro":
            ok, msg = write_gps_macro(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/material":
            ok, msg = create_material(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/material-delete":
            ok, msg = delete_material(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/annotations":
            ok, msg = save_annotations(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/groups":
            ok, msg = apply_group_update(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        elif path == "/api/digitize":
            ok, msg = apply_digitize_update(data)
            self._send(json.dumps({"ok": ok, "error": msg}), "application/json")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass  # keep the terminal clean


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("yaml", nargs="?", default=os.environ.get("G4CARE_YAML", "/tmp/g4care_gui_config.yaml"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("G4CARE_GEOMETRY_PORT", "8787")))
    args = ap.parse_args()
    config.set_yaml(args.yaml)  # resolve before chdir below

    os.chdir(config.HERE)  # serve viewer.js and other files from this directory
    server = HTTPServer(("127.0.0.1", args.port), Handler)
    print(f"[geometry] serving vtk.js viewer on http://127.0.0.1:{args.port} (yaml={args.yaml})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
