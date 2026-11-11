# G4CARE

**Geant4-based, YAML-configurable framework for radiation transport,
radiation chemistry (Geant4-DNA) and detector-response simulation.**

**Фреймворк на базе Geant4 с конфигурацией на YAML для моделирования переноса
излучения, радиационной химии (Geant4-DNA) и отклика детекторов.**

G4CARE reads a single YAML configuration file, initialises the Geant4 run
manager, geometry, physics lists and user actions, and writes analysis output
via ROOT (`RDataFrame` / `RNTuple`). It supports multi-stage runs, scoring,
macro execution and interactive visualisation.

G4CARE читает один YAML-конфиг, инициализирует run-менеджер Geant4, геометрию,
физические листы и user actions, а результаты пишет через ROOT
(`RDataFrame` / `RNTuple`). Поддерживаются многостадийные расчёты, скоринг,
выполнение макросов и интерактивная визуализация.

Version **v0.9.3** · License **Apache-2.0** — Версия **v0.9.3** · Лицензия **Apache-2.0**

## Features / Возможности

- YAML-driven configuration. JSON-Schema files in `.vscode/schemas/` provide
  autocompletion and inline documentation for every configuration key.
  — Конфигурация на YAML; JSON-Schema в `.vscode/schemas/` даёт автодополнение
  и документацию для каждого ключа.
- Geant4-DNA radiation chemistry (see `examples/chemistry/`).
  — Радиационная химия Geant4-DNA (см. `examples/chemistry/`).
- Custom cross-section models: BEB (Binary-Encounter-Bethe) ionization data and
  user cross-section models that override Geant4 EM physics (`CROSS_SECTION.BEB_FILE`).
  — Пользовательские сечения: данные BEB (Binary-Encounter-Bethe) для ионизации
  и модели сечений, переопределяющие EM-физику Geant4 (`CROSS_SECTION.BEB_FILE`).
- Custom physics lists loaded as shared libraries, e.g. space-physics EM +
  QBBC hadronic (`examples/physics/`).
  — Кастомные физические листы как shared-библиотеки, например space-physics EM +
  QBBC адронный (`examples/physics/`).
- Scoring (adaptive grids, voxels) with convergence validation.
  — Скоринг (адаптивные сетки, воксели) с проверкой сходимости.
- Multi-stage simulation and macro scripting (`examples/macros/`).
  — Многостадийное моделирование и макросы (`examples/macros/`).
- ROOT output through RDataFrame and RNTuple backends.
  — Вывод ROOT через RDataFrame и RNTuple.
- ExprTk expression evaluation in fields, sources and ntuple filters (`examples/exprtk/`).
  — Вычисление выражений ExprTk в полях, источниках и фильтрах ntuple (`examples/exprtk/`).
- NIEL / g-factor / dose-probe target examples (`examples/nano/`).
  — Примеры NIEL / g-фактор / dose-probe (`examples/nano/`).
- Interactive OpenGL visualisation.
  — Интерактивная визуализация OpenGL.

## Prerequisites / Зависимости

External dependencies must be installed separately. — Внешние зависимости
устанавливаются отдельно:

| Dependency / Зависимость | Purpose / Назначение | Notes / Примечания |
|------------|---------|-------|
| Geant4 ≥ 11 | simulation toolkit / симуляция | built with `ui_all vis_all` |
| ROOT ≥ 6.26 | output / вывод | `ROOT::ROOTDataFrame`, `ROOT::ROOTNTuple` |
| TBB | threading / многопоточность | |
| GDCM ≥ 3 | DICOM import (CT phantom, RTSTRUCT, RTPLAN, RTDOSE) | optional / опционально, `-DG4CARE_WITH_DICOM=ON` |
| Node.js ≥ 18 + npm | GUI (Electron editor) and cloud backend | |
| Python 3 + trame/vtk | 3D geometry viewer / 3D-вьювер геометрии | see `trame/requirements.txt` |
| CMake ≥ 3.10 | build system / система сборки | |
| C++20 compiler | | GCC 11+ / Clang 14+ |

Third-party **source** dependencies are vendored in this repository and need
no extra download. — Сторонние **исходные** зависимости вендорятся в репозитории
и не требуют отдельной загрузки:

- `third_party/yaml-cpp` — YAML parsing / разбор YAML.
- `third_party/nlohmann/` — JSON (single header / один заголовок).
- `third_party/toml++/` — TOML for cloud metadata sidecars (header-only).
- `include/Expression/exprtk.hpp` — expression evaluation (single header) / вычисление выражений.

## Build / Сборка

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DG4CARE_WITH_DICOM=ON
cmake --build build -j8
```

The executable is `build/G4CARE`. — Исполняемый файл — `build/G4CARE`.

### GUI (Electron editor) / GUI (редактор на Electron)

```bash
cd gui
npm install
npm run build      # vite build + 3D-viewer bundle (trame/viewer.bundle.js)
npm start          # electron .
```

The GUI spawns `g4care-engine --rpc`; if the auto-detected paths are wrong,
override `G4CARE_ENGINE`, `G4CARE_GDCM_LIB`, `G4CARE_ROOT_LIB`,
`G4CARE_GEANT4_LIB` (see `gui/sidecar.js`).

GUI запускает `g4care-engine --rpc`; если автодетект путей неверен, задайте
`G4CARE_ENGINE`, `G4CARE_GDCM_LIB`, `G4CARE_ROOT_LIB`, `G4CARE_GEANT4_LIB`
(см. `gui/sidecar.js`).

### 3D geometry viewer (trame) / 3D-вьювер геометрии (trame)

```bash
cd trame
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python geometry_server.py
```

`trame/viewer.bundle.js` is the prebuilt vtk.js client served by
`geometry_server.py`; rebuild it with `cd gui && npm run build:viewer`.
— `trame/viewer.bundle.js` — собранный vtk.js-клиент, который отдаёт
`geometry_server.py`; пересборка: `cd gui && npm run build:viewer`.

### Cloud (optional) / Облако (опционально)

```bash
cd cloud && npm install
npm run backend     # Fastify server (PostgreSQL + S3/MinIO)
```

See `cloud/` for the CLI (`npm run cli`), migrations and the web frontend
(подробнее в `cloud/README.md`).

## Usage / Использование

```bash
build/G4CARE config.yaml            # positional argument / позиционный аргумент
build/G4CARE -c config.yaml         # explicit -c flag / явный флаг -c
build/G4CARE -c config.yaml -m vis.mac   # + optional macro / + опциональный макрос
```

If no path is given, `config.yaml` in the current directory is used.
— Если путь не задан, используется `config.yaml` в текущей директории.

Key top-level configuration keys: `EVENTS`, `NTHREADS`, `VISUALIZATION`,
`MACROS`, `SEED`, `CHEMISTRY.*`, `PHYSICS.*`, `GEOMETRY_FILE`,
`SENSITIVE_VOLUMES`, `OUTPUT_FILE`, `DECAY_TIME_THRESHOLD`.
— Ключевые ключи конфигурации: `EVENTS`, `NTHREADS`, `VISUALIZATION`, `MACROS`,
`SEED`, `CHEMISTRY.*`, `PHYSICS.*`, `GEOMETRY_FILE`, `SENSITIVE_VOLUMES`,
`OUTPUT_FILE`, `DECAY_TIME_THRESHOLD`.

## Modular configuration (INCLUDE) / Модульная конфигурация (INCLUDE)

A configuration can be split into multiple YAML files. The top-level `INCLUDE`
key loads and deep-merges other files; the main file's own keys take precedence.
— Конфигурацию можно разбить на несколько YAML-файлов. Ключ `INCLUDE`
загружает и глубоко объединяет другие файлы; собственные ключи основного
файла имеют приоритет.

```yaml
INCLUDE:
  - modular/geometry.yaml
  - modular/source.yaml
  - modular/physics.yaml

EVENTS: 10
VISUALIZATION: false
```

`INCLUDE` accepts a single path or a list. Paths are resolved relative to the
including file. The merge is **deep**: maps merge recursively, while scalars and
sequences are replaced. Later includes override earlier ones, and the including
file's own keys override everything. Circular includes are detected and skipped.
— `INCLUDE` принимает путь или список путей (относительно включающего файла).
Слияние **глубокое**: словари объединяются рекурсивно, скаляры и списки
заменяются. Поздние include переопределяют ранние, собственные ключи — всё.
Циклические include обнаруживаются и пропускаются.

See `examples/tests/test_modular.yaml` and `examples/tests/modular/`.

## GDML export / Экспорт GDML

`GEOMETRY.EXPORT` saves the geometry to GDML after the run (headless).
— `GEOMETRY.EXPORT` сохраняет геометрию в GDML после расчёта (без GUI):

```yaml
GEOMETRY:
  EXPORT:
    ENABLE: true
    FILE: "export.gdml"
    MODE: "scene"          # scene | single | array
    # VOLUME: "Target"      # for single / для single
    # OBJECTS: ["A","B"]    # for array (explicit) / для array (явно)
    # EXCLUDE_WORLD: true   # for array (logical: all except World/Hall)
```

- `scene`  — whole world / весь мир.
- `single` — one volume (`VOLUME`) / один объём (`VOLUME`).
- `array`  — several volumes: explicit `OBJECTS` or logical `EXCLUDE_WORLD` / несколько объёмов.

Subset exports are wrapped in a temporary world volume so they form a valid
GDML scene. See `examples/tests/test_gdml_export.yaml` and
`examples/tests/test_gdml_export_scene.yaml`.
— Выборочные экспорты оборачиваются во временный world, чтобы сцена GDML была
корректной. См. `examples/tests/test_gdml_export.yaml` и
`examples/tests/test_gdml_export_scene.yaml`.

## Custom geometry (.so) / Кастомная геометрия (.so)

`GEOMETRY.LIBRARY` + `GEOMETRY.CONSTRUCTOR` load a custom
`G4VUserDetectorConstruction` from a shared library (exported as
`create<CONSTRUCTOR>()`), replacing the built-in geometry construction.
— `GEOMETRY.LIBRARY` + `GEOMETRY.CONSTRUCTOR` загружают кастомный
`G4VUserDetectorConstruction` из shared-библиотеки (экспорт `create<CONSTRUCTOR>()`),
заменяя встроенное построение геометрии.

```yaml
GEOMETRY:
  LIBRARY: "libMyGeometry.so"
  CONSTRUCTOR: "MyGeometry"
```

See `examples/geometry/ExampleGeometry.cc`. (Primarily for single-threaded /
visualization use.) — См. `examples/geometry/ExampleGeometry.cc` (в основном
для однопоточного/визуализационного режима).

## Examples / Примеры

Run-ready YAML configurations live in `examples/`. — Готовые YAML-конфиги в `examples/`:

```bash
build/G4CARE examples/tests/test_physics.yaml
build/G4CARE examples/chemistry/chem1/chem1.yaml
build/G4CARE examples/nano/pg_gamma23_target_dose_probe.yaml
```

- `examples/chemistry/` — Geant4-DNA radiation chemistry / радиационная химия.
- `examples/exprtk/` — expression-driven fields, sources, ntuple filters.
- `examples/nano/` — nano-target NIEL / g-factor / dose-probe simulations.
- `examples/tests/` — feature smoke tests / смоук-тесты.
- `examples/macros/` — visualisation / geometry-export macros.
- `examples/physics/` — custom physics-list examples: `G4EmStandardPhysics_SpacePhysics`
  and `SpacePhysics_QBBC` (space EM + QBBC hadronic + decay), built as a shared library.

## Repository layout / Структура репозитория

```
include/                headers / заголовки
src/                    sources (entry point src/main.cc) / исходники
third_party/yaml-cpp/   vendored YAML parser
third_party/nlohmann/   vendored JSON
third_party/toml++/     vendored TOML
examples/               YAML configs and reference models / конфиги и модели
.vscode/schemas/        JSON-Schema for YAML autocompletion
CMakeLists.txt          build script / скрипт сборки
```

## Cloud metadata (TOML) / Метаданные облака (TOML)

Each cloud artifact (`.yaml`, `.gdml`, `.so`, `.mac`, …) may carry a metadata
sidecar named `<artifact>.meta.toml` for preview in cloud UIs.
— Каждый артефакт облака (`.yaml`, `.gdml`, `.so`, `.mac`, …) может иметь
метаданные `<artifact>.meta.toml` для превью в облачном UI.

```toml
doi         = "10.5281/zenodo.1234567"
info_url    = "https://example.org/articles/detector"
description = "Silicon detector ..."
version     = "0.9.0"
created     = "2026-09-30"
tags        = ["detector", "nano", "niel"]

[author]
name  = "G4CARE Developers"
alias = "g4care-dev"

[affiliation]
group     = "G4CARE"
institute = "TPU"
```

All fields are optional: `doi`, `info_url`, `description`, `version`, `created`
(ISO-8601 date), `tags` (list of strings), `[author]` (`name`, `alias`),
`[affiliation]` (`group`, `institute`, `department`).
— Все поля опциональны: `doi`, `info_url`, `description`, `version`, `created`
(дата ISO-8601), `tags` (список строк), `[author]` (`name`, `alias`),
`[affiliation]` (`group`, `institute`, `department`).

The parser is `MetaReader` (`include/DataIO/MetaReader.hh`,
`src/DataIO/MetaReader.cc`), backed by the header-only toml++ library vendored
under `third_party/toml++/`. See `examples/meta/example.meta.toml` and
`examples/tests/xray_TESdetector.gdml.meta.toml` for reference files.
— Парсер — `MetaReader` (`include/DataIO/MetaReader.hh`, `src/DataIO/MetaReader.cc`),
на header-only библиотеке toml++ (`third_party/toml++/`). См. примеры в
`examples/meta/example.meta.toml` и `examples/tests/xray_TESdetector.gdml.meta.toml`.

## Performance / Производительность

Quick reference numbers from the development workstation (AMD Ryzen 7 PRO 4750G,
8 cores / 16 threads, Geant4 11.4, `Release` build). Scenario: a 1 MeV electron
into a 10×10×10 cm water box (`QGSP_BERT_EMV`, default production cuts),
transport only (no scoring), 10⁶ events.
— Быстрые справочные цифры с рабочей станции (AMD Ryzen 7 PRO 4750G, 8 ядер /
16 потоков, Geant4 11.4, сборка `Release`). Сценарий: электрон 1 МэВ в воде
10×10×10 см (`QGSP_BERT_EMV`, стандартные куты), только транспорт (без
скоринга), 10⁶ событий:

```yaml
EVENTS: 1000000
NTHREADS: 8
SEED_ONCE_PER_COMMUNICATION: 1
GEOMETRY:
  WORLD: { shape: "box", material: "G4_AIR", DIMENSIONS: ["1*m", "1*m", "1*m"] }
  OBJECTS:
    water_box: { shape: "box", material: "G4_WATER", DIMENSIONS: ["10*cm", "10*cm", "10*cm"] }
SOURCE:
  e_gun:
    type: "particleGun"
    particle: "e-"
    energy: "1*MeV"
PHYSICS:
  BASIC: { LIST: "QGSP_BERT_EMV" }
```

| Threads / Потоков | SEED_ONCE=0 (events/s / событий/с) | SEED_ONCE=1 (events/s / событий/с) |
|---------|------------------------|------------------------|
| 1       | 16 022                 | 40 096                 |
| 8       | 127 770 (±0.9%)        | 283 204 (±5.1%)        |

- `SEED_ONCE_PER_COMMUNICATION: 1` is ≈2.2× faster than `0` (it skips the
  per-event `MixMaxRng::apply_bigskip`), at the cost of event reproducibility
  depending on the thread count.
  — `SEED_ONCE_PER_COMMUNICATION: 1` быстрее `0` в ≈2.2 раза (пропускается
  per-event `MixMaxRng::apply_bigskip`), но воспроизводимость события начинает
  зависеть от числа потоков.
- Scaling from 1 → 8 threads is ≈7–8× (86–97% efficiency).
  — Масштабирование 1 → 8 потоков ≈7–8× (эффективность 86–97%).

> Absolute numbers are approximate — measured with a GUI/browser running in
> parallel on the same machine. Re-run on a quiescent system for
> publication-grade figures.
> — Абсолютные цифры приблизительны: измерения проводились при работающем
> GUI/браузере на той же машине. Для публикационных цифр повторите на
> разгруженной системе.

## License / Лицензия

Apache License 2.0 — see [LICENSE](LICENSE). — Apache License 2.0 — см. [LICENSE](LICENSE).

Copyright © 2026 G4CARE Developers.
