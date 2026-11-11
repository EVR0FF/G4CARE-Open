# G4CARE Cloud / Облако G4CARE

CAD-like cloud exchange of G4CARE artifacts (GDML geometry, YAML configs,
macros, `.so` libraries) with metadata, search and preview.
— CAD-подобный облачный обмен артефактами G4CARE (геометрия GDML, YAML-конфиги,
макросы, `.so`-библиотеки) с метаданными, поиском и превью.

## Structure / Структура

```
cloud/
├── src/
│   ├── shared/          # shared types + manifest/metadata (TOML) + SHA-256 / общие типы
│   ├── backend/         # Fastify REST API (+ storage, index) / хранилище, индекс
│   ├── cli/             # g4care-cloud (init/push/pull/verify/list)
│   └── test/            # smoke test / smoke-тест
├── frontend/            # web dashboard (React + TS + Vite + three.js) / веб-дашборд
├── converter/           # gdml2obj — GDML → OBJ (Geant4 tessellation / тесселяция)
├── migrations/          # PostgreSQL schema (production path) / схема
├── docker-compose.yml   # backend + postgres + seaweedfs
├── Dockerfile           # backend image / образ backend
└── package.json
```

## Quick start (local, no Docker) / Быстрый старт (локально, без Docker)

```bash
cd cloud
npm install
npm run typecheck      # type check / проверка типов
npm test               # smoke test (manifest + round-trip via server)
npm run test:integration  # integration tests (Postgres + S3: SeaweedFS/MinIO, fallback s3rver)
npm run backend        # run API at http://127.0.0.1:3000 / запустить API
```

In another terminal — the CLI / В другом терминале — CLI:

```bash
cd cloud
npx tsx src/cli/index.ts login alice --password pw   # login (saves token) / вход
npx tsx src/cli/index.ts init scene.gdml        # create scene.gdml.meta.toml
npx tsx src/cli/index.ts pack detector.yaml     # pack config + deps into .g4c
npx tsx src/cli/index.ts push scene.gdml        # upload file (+ metadata)
npx tsx src/cli/index.ts push detector.g4c      # upload bundle
npx tsx src/cli/index.ts list --tag detector    # search / поиск
npx tsx src/cli/index.ts pull <id> out.gdml     # download (bundle → unpack into dir)
npx tsx src/cli/index.ts verify out.gdml <sha>  # integrity check / проверка целостности
```

## Web dashboard / Веб-дашборд

React + TypeScript + Vite (`frontend/`). Artifact cards, search and filters
(tag/author/institute/type), a detail card with metadata and download, an upload
form.
— React + TypeScript + Vite (каталог `frontend/`). Карточки артефактов, поиск и
фильтры (тэг/автор/институт/тип), детальная карточка с метаданными и скачиванием,
форма загрузки.

```bash
cd cloud/frontend
npm install
npm run dev          # dev server http://127.0.0.1:5173 (proxy /artifacts → :3000)
npm test             # headless tests (vitest + Testing Library)
npm run build        # production build to dist/
```

Start the backend first (`npm run backend` in `cloud/`).
— Сначала запустите backend (`npm run backend` в `cloud/`).

## 3D preview / 3D-превью

For GDML artifacts the dashboard shows an interactive 3D viewer (three.js).
The backend converts GDML to an OBJ mesh on the fly via `converter/gdml2obj`
(solid tessellation through Geant4 `HepPolyhedron`):
— Для GDML-артефактов дашборд показывает интерактивный 3D-вьюер (three.js).
Бэкенд на лету конвертирует GDML в OBJ-меш через `converter/gdml2obj`
(тесселяция solids через Geant4 `HepPolyhedron`):

```bash
cd cloud/converter
g++ -std=c++17 gdml2obj.cc -o gdml2obj $(geant4-config --cflags) $(geant4-config --libs) -lxerces-c
```

`GET /artifacts/:id/preview` returns OBJ (for `type=gdml`). The binary path is
set via `G4CARE_GDML2OBJ` (default `gdml2obj`).
— Эндпоинт `GET /artifacts/:id/preview` возвращает OBJ (для `type=gdml`). Путь к
бинарю задаётся `G4CARE_GDML2OBJ` (по умолчанию `gdml2obj`).

### In Docker (production path) / В Docker (production-путь)

The converter runs as a separate microservice (`converter/`, image on
`ubuntu:24.04` with Geant4 libraries). Before `docker compose build`, on the
host where `gdml2obj` was built, run:
— Конвертер работает как отдельный микросервис (`converter/`, образ на
`ubuntu:24.04` с Geant4-библиотеками). Перед `docker compose build` на хосте,
где собран `gdml2obj`, выполни:

```bash
cd cloud/converter
./stage.sh          # copy binary + Geant4 libs into stage/
cd ..
docker compose up -d --build
```

The backend calls it via `G4CARE_CONVERTER_URL` (default
`http://converter:8081`); if unset — fallback to the local binary.
— Бэкенд зовёт его через `G4CARE_CONVERTER_URL` (по умолчанию
`http://converter:8081`); если переменная не задана — фолбэк на локальный бинарь.

## REST API

| Method / Метод | Path / Путь | Description / Описание |
|---|---|---|
| `POST` | `/auth/register` | register (username + email + password; sends a code) / регистрация |
| `POST` | `/auth/verify` | email confirmation (6-digit OTP) / подтверждение email |
| `POST` | `/auth/resend` | resend code / повторная отправка кода |
| `POST` | `/auth/forgot` | request password-reset code / запрос кода сброса |
| `POST` | `/auth/reset` | reset password by code / сброс пароля по коду |
| `POST` | `/auth/login` | login by email/username → JWT (`Bearer`) / вход |
| `POST` | `/artifacts` | upload (multipart: `file` + opt. `meta`/`visibility`; auth required) |
| `GET` | `/artifacts` | search/list (`?q=&tag=&author=&institute=&type=`) / поиск |
| `GET` | `/artifacts/:id` | artifact metadata / метаданные артефакта |
| `GET` | `/artifacts/:id/download` | download (`X-G4CARE-SHA256` header) / скачать |
| `GET` | `/artifacts/:id/preview` | 3D preview (OBJ) for `type=gdml` / 3D-превью |
| `GET` | `/health` | liveness (`200 {"status":"ok"}`) / проверка живости |

Uploads require a JWT token (`Authorization: Bearer …`). An artifact gets an
owner, visibility (`public`/`private`) and a version number; `private` is
accessible only to the owner. Re-uploading the same name by the same user
increments `version` and links `parentId`.
— Загрузка требует JWT-токен (`Authorization: Bearer …`). Артефакт получает
владельца, видимость (`public`/`private`) и номер версии; `private` доступен
только владельцу. Повторная загрузка того же имени тем же пользователем
увеличивает `version` и связывает `parentId`.

## Metadata format (TOML) / Формат метаданных (TOML)

The unified metadata format is a sidecar `<artifact>.meta.toml` (matching the
C++ `MetaReader`, see `include/DataIO/MetaReader.hh`):
— Единый формат метаданных — sidecar `<артефакт>.meta.toml` (совпадает с C++
`MetaReader`, см. `include/DataIO/MetaReader.hh`):

```toml
doi         = "10.5281/zenodo.1234567"
info_url    = "https://example.org/articles/detector"
description = "Silicon detector ..."
version     = "0.9.0"
created     = "2026-09-30"
tags        = ["detector", "nano"]

[author]
name  = "G4CARE Developers"
alias = "g4care-dev"

[affiliation]
group     = "G4CARE"
institute = "TPU"
```

## Bundle (multi-file set) / Bundle (многофайловый набор)

`g4care-cloud pack <config>` walks the config's dependencies (`INCLUDE`,
`GEOMETRY.FILE`, `GEOMETRY.LIBRARY`, `MACROS`, `PHYSICS.*.MACRO/LIBRARY`,
`GEOMETRY.OBJECTS.*.file`) and packs them together with a `manifest.toml` into a
`.g4c` tarball:
— `g4care-cloud pack <config>` обходит зависимости конфига (`INCLUDE`,
`GEOMETRY.FILE`, `GEOMETRY.LIBRARY`, `MACROS`, `PHYSICS.*.MACRO/LIBRARY`,
`GEOMETRY.OBJECTS.*.file`) и упаковывает их вместе с `manifest.toml` в
`.g4c`-тарболл:

```toml
# manifest.toml (inside .g4c / внутри .g4c)
format_version = 1
doi         = "10.5281/zenodo.1234567"
description = "Silicon detector assembly"
tags        = ["detector"]

[[files]]
path   = "detector.yaml"
sha256 = "abc…"
size   = 1234

[[files]]
path   = "modular/geometry.yaml"
sha256 = "def…"
size   = 567
```

`push`/`pull` understand `.g4c`; `pull` auto-unpacks the bundle into a directory,
preserving the relative structure.
— `push`/`pull` понимают `.g4c`; `pull` автоматически распаковывает bundle в каталог,
сохраняя относительную структуру.

## Production path / Production-путь

Backends are selected by environment variables (otherwise — files + JSON index):
— Бэкенды выбираются по переменным окружения (иначе — файлы + JSON-индекс):

- **S3** (MinIO / SeaweedFS / AWS…): when `G4CARE_CLOUD_S3_ENDPOINT` is set,
  `S3Storage` (`src/backend/storage-s3.ts`) is used. Options:
  `G4CARE_CLOUD_S3_PORT` (default 9000), `G4CARE_CLOUD_S3_USESSL`,
  `G4CARE_CLOUD_S3_ACCESS_KEY`, `G4CARE_CLOUD_S3_SECRET_KEY`,
  `G4CARE_CLOUD_S3_BUCKET`.
  — **S3** (MinIO / SeaweedFS / AWS…): при заданном `G4CARE_CLOUD_S3_ENDPOINT`
  используется `S3Storage` (`src/backend/storage-s3.ts`).
- **PostgreSQL**: when `G4CARE_CLOUD_DB_URL` is set, `PostgresIndex` (metadata)
  and `PostgresUserStore` (users) are used — schema `migrations/001_init.sql`
  (tables `artifacts`, `tags`, `users`).
  — **PostgreSQL**: при заданном `G4CARE_CLOUD_DB_URL` используются
  `PostgresIndex` (метаданные) и `PostgresUserStore` (пользователи).

All implementations share the same `ObjectStore` / `MetadataIndex` /
`UserStore` interfaces as the file-based ones, so the REST layer is unchanged.
— Все реализации наследуют те же интерфейсы `ObjectStore` / `MetadataIndex` /
`UserStore`, что и файловые, поэтому REST-слой не меняется.

On startup with PostgreSQL the backend applies migrations from `migrations/*.sql`
(idempotently), so upgrading an existing `pgdata` volume needs no manual steps.
— При старте с PostgreSQL backend сам накатывает миграции из `migrations/*.sql`
(идемпотентно).

Security: before `dlopen` of a `.so`, SHA-256 verification (the `verify` command)
+ trusted sources are mandatory.
— Безопасность: перед `dlopen` `.so` обязательна проверка SHA-256 (команда
`verify`) + доверенные источники.

## Deploy (Docker) / Деплой (Docker)

```bash
cd cloud
cp .env.example .env   # set secrets (JWT, passwords, S3 keys) / задайте секреты
docker compose up --build
```

Brings up: backend (`:3000`), frontend (`:8080`, nginx proxies `/artifacts` and
`/auth` to the backend), postgres (`:5432`), seaweedfs (S3 on `:9000`).
— Поднимает: backend (`:3000`), frontend (`:8080`, nginx проксирует `/artifacts`
и `/auth` на backend), postgres (`:5432`), seaweedfs (S3 на `:9000`).

In this mode the backend uses **S3 + PostgreSQL** (objects, metadata and users) —
the file-based MVP is not used. Secrets come from `.env`:
`G4CARE_CLOUD_JWT_SECRET` (generate: `openssl rand -hex 32`),
`POSTGRES_PASSWORD`, `G4CARE_CLOUD_S3_ACCESS_KEY` / `G4CARE_CLOUD_S3_SECRET_KEY`
(must match `seaweedfs-s3.json`). 3D preview requires a built `gdml2obj` (see
«3D preview») and `G4CARE_GDML2OBJ`.
— В этом режиме backend использует **S3 + PostgreSQL**. Секреты берутся из `.env`:
`G4CARE_CLOUD_JWT_SECRET` (сгенерируйте: `openssl rand -hex 32`),
`POSTGRES_PASSWORD`, `G4CARE_CLOUD_S3_ACCESS_KEY` / `G4CARE_CLOUD_S3_SECRET_KEY`
(должны совпадать с `seaweedfs-s3.json`). 3D-превью требует собранного
`gdml2obj` и переменной `G4CARE_GDML2OBJ`.

The backend image is built in two stages (`tsc` compile → run `node` without dev
dependencies). Health checks: `GET /health` for the backend, `pg_isready` for
postgres, the S3 port for seaweedfs (compose waits via `depends_on`).
— Образ backend собирается в два этапа (компиляция `tsc` → запуск `node` без
dev-зависимостей). Health-проверки: `GET /health` у backend, `pg_isready` у
postgres, S3-порт у seaweedfs.

Email verification: on registration a 6-digit code is sent to the email; login is
impossible without it. If `SMTP_HOST` is empty the code is written to the backend
log (demo mode); for real delivery set `SMTP_HOST`/`SMTP_PORT`/`SMTP_USER`/
`SMTP_PASS`/`SMTP_FROM` in `.env`. Profile fields: email (unique), phone, birth
date, full name. The frontend is Ant Design (an auth window with Sign in /
Register tabs + password recovery).
— Email-верификация: при регистрации на почту отправляется 6-значный код, без
него вход невозможен. Если `SMTP_HOST` пуст — код пишется в лог backend
(демо-режим); для реальной отправки задайте `SMTP_HOST`/`SMTP_PORT`/`SMTP_USER`/
`SMTP_PASS`/`SMTP_FROM` в `.env`. Фронтенд — Ant Design.

## Anubis (anti-bot, optional) / Anubis (антибот, опционально)

To protect against bots, Anubis can be placed in front of the frontend
(proof-of-work challenge for browsers). Add the service to `docker-compose.yml`:
— Для защиты от ботов можно поставить Anubis перед фронтендом (proof-of-work
челлендж для браузеров). Добавьте сервис в `docker-compose.yml`:

```yaml
  anubis:
    image: ghcr.io/techaro/anubis   # check the current tag / уточните тег
    environment:
      ANUBIS_TARGET: http://frontend:80
      ANUBIS_BIND: ":8080"
    ports:
      - "8080:8080"
    depends_on:
      - frontend
```

(and remove `8080:80` from `frontend` so Anubis owns `:8080`).
— (и уберите `8080:80` у `frontend`, чтобы `:8080` занимал Anubis).

CI: `.github/workflows/ci.yml` — backend (install/typecheck/test) and frontend
(install/typecheck/build) on Node 20.
— CI: `.github/workflows/ci.yml` — backend (install/typecheck/test) и frontend
(install/typecheck/build) на Node 20.

## Stage status / Статус этапов

- [x] 5a — backend + storage + index + CLI (this directory / этот каталог)
- [x] 5b — web dashboard (React + TS: cards, search, filters, upload)
- [x] 5c — 3D preview (GDML → OBJ via Geant4 + three.js viewer)
- [x] 5d — authentication (JWT) / ownership / visibility / versions
- [x] 5e — deploy (docker-compose + nginx) and CI
