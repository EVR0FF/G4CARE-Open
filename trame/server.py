#==============================================================================
# G4CARE - server.py — Trame server entry point
#
# Trame server entry point
#
# @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
# @date 2026-11-11  @version 0.9.3
# @copyright Copyright (c) 2026 G4CARE Developers
# @license SPDX-License-Identifier: Apache-2.0
#==============================================================================

"""G4CARE trame viewer — display a VTK scoring mesh (.vtu/.vtk) with field coloring.

Usage:
    python server.py scoring.vtu --port 8787
    G4CARE_VTK=scoring.vtu G4CARE_TRAME_PORT=8787 python server.py
"""
import argparse
import os

from trame.app import get_server
from trame.ui.vuetify3 import SinglePageLayout
from trame.widgets import vuetify3 as v3
from trame.widgets import vtk as vtk_widgets

import vtk


def make_reader(path):
    lower = path.lower()
    if lower.endswith(".vtu"):
        reader = vtk.vtkXMLUnstructuredGridReader()
    elif lower.endswith(".vtp"):
        reader = vtk.vtkXMLPolyDataReader()
    elif lower.endswith(".vtk"):
        reader = vtk.vtkUnstructuredGridReader()
    else:
        raise ValueError(f"unsupported file extension: {path}")
    reader.SetFileName(path)
    reader.Update()
    return reader


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file", nargs="?", default=os.environ.get("G4CARE_VTK", "scoring.vtu"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("G4CARE_TRAME_PORT", "8787")))
    args = ap.parse_args()

    if not os.path.isfile(args.file):
        print(f"[trame] file not found: {args.file}", flush=True)
        raise SystemExit(1)

    reader = make_reader(args.file)
    mesh = reader.GetOutput()
    pd = mesh.GetPointData()
    cd = mesh.GetCellData()

    fields = []
    for i in range(pd.GetNumberOfArrays()):
        fields.append(pd.GetArrayName(i))
    for i in range(cd.GetNumberOfArrays()):
        fields.append(cd.GetArrayName(i))
    if not fields:
        print("[trame] no scalar fields in mesh", flush=True)

    # ── VTK pipeline (mapper → actor → renderer → render window) ──
    mapper = vtk.vtkDataSetMapper()
    mapper.SetInputData(mesh)
    mapper.ScalarVisibilityOn()

    actor = vtk.vtkActor()
    actor.SetMapper(mapper)

    renderer = vtk.vtkRenderer()
    renderer.AddActor(actor)
    renderer.SetBackground(0.10, 0.10, 0.12)
    renderer.ResetCamera()

    render_window = vtk.vtkRenderWindow()
    render_window.SetSize(900, 700)
    render_window.AddRenderer(renderer)
    render_window.OffScreenRenderingOn()

    interactor = vtk.vtkRenderWindowInteractor()
    interactor.SetRenderWindow(render_window)
    interactor.Initialize()

    server = get_server(client_type="vue3")
    state, ctrl = server.state, server.controller

    def set_active(field):
        if not field:
            return
        arr = pd.GetArray(field)
        if arr is not None:
            mapper.SelectColorArray(field)
            mapper.SetScalarRange(arr.GetRange())
        else:
            arr = cd.GetArray(field)
            if arr is not None:
                mapper.SelectColorArray(field)
                mapper.SetScalarRange(arr.GetRange())

    state.fields = fields
    state.active_field = fields[0] if fields else ""

    if state.active_field:
        set_active(state.active_field)

    @state.change("active_field")
    def on_field_change(active_field, **kwargs):
        set_active(active_field)
        ctrl.view_update()

    with SinglePageLayout(server) as layout:
        layout.title.set_text("G4CARE — ParaView (trame)")
        with layout.toolbar:
            if fields:
                v3.VSelect(
                    v_model=("active_field", state.active_field),
                    items=("fields", fields),
                    label="Field",
                    dense=True,
                    hide_details=True,
                    style="max-width: 280px",
                )
            v3.VSpacer()
        with layout.content:
            with vtk_widgets.VtkRemoteView(render_window) as view:
                ctrl.view_update = view.update
                ctrl.view_reset_camera = view.reset_camera

    ctrl.on_client_connected.add(ctrl.view_reset_camera)
    print(f"[trame] serving {args.file} on http://127.0.0.1:{args.port}", flush=True)
    server.start(port=args.port, open_browser=False)


if __name__ == "__main__":
    main()
