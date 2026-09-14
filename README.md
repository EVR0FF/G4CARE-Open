# G4CARE

**Geant4-based, YAML-configurable framework for radiation transport,
radiation chemistry (Geant4-DNA) and detector-response simulation.**

G4CARE reads a single YAML configuration file, initialises the Geant4 run
manager, geometry, physics lists and user actions, and writes analysis output
via ROOT (`RDataFrame` / `RNTuple`). It supports multi-stage runs, scoring,
macro execution and interactive visualisation.

Version **v0.9.0** · License **Apache-2.0**

## Features

- YAML-driven configuration. JSON-Schema files in `.vscode/schemas/` provide
  autocompletion and inline documentation for every configuration key.
- Geant4-DNA radiation chemistry (see `examples/chemistry/`).
- Custom physics: BEB cross-sections and user cross-section models
  (`examples/physics/`).
- Scoring (adaptive grids, voxels) with convergence validation.
- Multi-stage simulation and macro scripting (`examples/macros/`).
- ROOT output through RDataFrame and RNTuple backends.
- ExprTk expression evaluation in fields, sources and ntuple filters
  (`examples/exprtk/`).
- NIEL / g-factor / dose-probe target examples (`examples/nano/`).
- Interactive OpenGL visualisation.

## Prerequisites

External dependencies must be installed separately:

| Dependency | Purpose | Notes |
|------------|---------|-------|
| Geant4 ≥ 11 | simulation toolkit | built with `ui_all vis_all` |
| ROOT ≥ 6.26 | output | `ROOT::ROOTDataFrame`, `ROOT::ROOTNTuple` |
| TBB | threading | |
| CMake ≥ 3.10 | build system | |
| C++20 compiler | | GCC 11+ / Clang 14+ |

Third-party **source** dependencies are vendored in this repository and need
no extra download:

- `third_party/yaml-cpp` — YAML parsing.
- `include/Expression/exprtk.hpp` — expression evaluation (single header).

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

The executable is `build/G4CARE`.

## Usage

```bash
build/G4CARE config.yaml            # positional argument
build/G4CARE -c config.yaml         # explicit -c flag
build/G4CARE -c config.yaml -m vis.mac   # + optional macro
```

If no path is given, `config.yaml` in the current directory is used.

Key top-level configuration keys: `EVENTS`, `NTHREADS`, `VISUALIZATION`,
`MACROS`, `SEED`, `CHEMISTRY.*`, `PHYSICS.*`, `GEOMETRY_FILE`,
`SENSITIVE_VOLUMES`, `OUTPUT_FILE`, `DECAY_TIME_THRESHOLD`.

## Examples

Run-ready YAML configurations live in `examples/`:

```bash
build/G4CARE examples/tests/test_physics.yaml
build/G4CARE examples/chemistry/chem1/chem1.yaml
build/G4CARE examples/nano/pg_gamma23_target_dose_probe.yaml
```

- `examples/chemistry/` — Geant4-DNA radiation chemistry.
- `examples/exprtk/` — expression-driven fields, sources, ntuple filters.
- `examples/nano/` — nano-target NIEL / g-factor / dose-probe simulations.
- `examples/tests/` — feature smoke tests.
- `examples/macros/` — visualisation / geometry-export macros.
- `examples/physics/` — custom space-physics EM model.

## Repository layout

```
include/                headers
src/                    sources (entry point src/main.cc)
third_party/yaml-cpp/   vendored YAML parser
examples/               YAML configs and reference models
.vscode/schemas/        JSON-Schema for YAML autocompletion
CMakeLists.txt          build script
```

## License

Apache License 2.0 — see [LICENSE](LICENSE).

Copyright © 2026 G4CARE Developers.
