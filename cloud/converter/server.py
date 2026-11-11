#==============================================================================
# G4CARE - server.py — Cloud converter server
#
# Cloud converter server
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""Tiny HTTP wrapper around the `gdml2obj` CLI (GDML -> OBJ mesh)."""
import http.server
import os
import subprocess
import sys
import tempfile

PORT = int(os.environ.get("PORT", "8081"))


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body=b"", ctype="application/octet-stream"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/health", "/healthz"):
            self._send(200, b'{"status":"ok"}', "application/json")
        else:
            self._send(404)

    def do_POST(self):
        if self.path != "/convert":
            self._send(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        gdml = self.rfile.read(length)
        with tempfile.TemporaryDirectory() as d:
            inp = os.path.join(d, "scene.gdml")
            out = os.path.join(d, "scene.obj")
            with open(inp, "wb") as f:
                f.write(gdml)
            try:
                subprocess.run(
                    ["gdml2obj", inp, out],
                    check=True,
                    capture_output=True,
                    timeout=180,
                )
            except FileNotFoundError:
                self._send(500, b"gdml2obj binary not found")
                return
            except subprocess.CalledProcessError as e:
                self._send(500, b"converter error: " + (e.stderr or b""))
                return
            except subprocess.TimeoutExpired:
                self._send(500, b"converter timed out")
                return
            if not os.path.exists(out) or os.path.getsize(out) == 0:
                self._send(500, b"converter produced no output")
                return
            with open(out, "rb") as f:
                obj = f.read()
            self._send(200, obj, "text/plain")

    def log_message(self, fmt, *args):
        sys.stderr.write("[converter] " + fmt % args + "\n")


class Server(http.server.ThreadingHTTPServer):
    pass


if __name__ == "__main__":
    httpd = Server(("0.0.0.0", PORT), Handler)
    print(f"[converter] listening on :{PORT}", flush=True)
    httpd.serve_forever()
