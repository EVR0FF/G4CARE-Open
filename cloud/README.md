# G4CARE Cloud

CAD-подобный облачный обмен артефактами G4CARE (геометрия GDML, YAML-конфиги,
макросы, `.so`-библиотеки) с метаданными, поиском и превью.

## Структура

```
cloud/
├── src/
│   ├── shared/          # общие типы + манифест/метаданные (TOML) + SHA-256
│   ├── backend/         # Fastify REST API (+ хранилище, индекс)
│   ├── cli/             # g4care-cloud (init/push/pull/verify/list)
│   └── test/            # smoke-тест
├── frontend/            # веб-дашборд (React + TS + Vite + three.js)
├── converter/           # gdml2obj — GDML → OBJ (тесселяция Geant4)
├── migrations/          # PostgreSQL-схема (production-путь)
├── docker-compose.yml   # backend + postgres + seaweedfs
├── Dockerfile           # образ backend
└── package.json
```

## Быстрый старт (локально, без Docker)

```bash
cd cloud
npm install
npm run typecheck      # проверка типов
npm test               # smoke-тест (манифест + round-trip через сервер)
npm run test:integration  # интеграционные тесты (Postgres + S3: SeaweedFS/MinIO, fallback s3rver)
npm run backend        # запустить API на http://127.0.0.1:3000
```

В другом терминале — CLI:

```bash
cd cloud
npx tsx src/cli/index.ts login alice --password pw   # вход (сохраняет токен)
npx tsx src/cli/index.ts init scene.gdml        # создать scene.gdml.meta.toml
npx tsx src/cli/index.ts pack detector.yaml     # упаковать конфиг + зависимости в .g4c
npx tsx src/cli/index.ts push scene.gdml        # загрузить файл (+ метаданные)
npx tsx src/cli/index.ts push detector.g4c      # загрузить bundle
npx tsx src/cli/index.ts list --tag detector    # поиск
npx tsx src/cli/index.ts pull <id> out.gdml     # скачать (bundle → unpack в каталог)
npx tsx src/cli/index.ts verify out.gdml <sha>  # проверить целостность
```

## Веб-дашборд

React + TypeScript + Vite (каталог `frontend/`). Карточки артефактов, поиск и
фильтры (тэг/автор/институт/тип), детальная карточка с метаданными и скачиванием,
форма загрузки.

```bash
cd cloud/frontend
npm install
npm run dev          # dev-сервер http://127.0.0.1:5173 (прокси /artifacts → :3000)
npm test             # headless-тесты (vitest + Testing Library)
npm run build        # production-сборка в dist/
```

Сначала запустите backend (`npm run backend` в `cloud/`).

## 3D-превью

Для GDML-артефактов дашборд показывает интерактивный 3D-вьюер (three.js).
Бэкенд на лету конвертирует GDML в OBJ-меш через `converter/gdml2obj`
(тесселяция solids через Geant4 `HepPolyhedron`):

```bash
cd cloud/converter
g++ -std=c++17 gdml2obj.cc -o gdml2obj $(geant4-config --cflags) $(geant4-config --libs) -lxerces-c
```

Эндпоинт `GET /artifacts/:id/preview` возвращает OBJ (для `type=gdml`). Путь к
бинарю задаётся `G4CARE_GDML2OBJ` (по умолчанию `gdml2obj`).

### В Docker (production-путь)

Конвертер работает как отдельный микросервис (`converter/`, образ на
`ubuntu:24.04` с Geant4-библиотеками). Перед `docker compose build` на хосте,
где собран `gdml2obj`, выполни:

```bash
cd cloud/converter
./stage.sh          # копирует бинарь + Geant4-библиотеки в stage/
cd ..
docker compose up -d --build
```

Бэкенд зовёт его через `G4CARE_CONVERTER_URL` (по умолчанию
`http://converter:8081`); если переменная не задана — фолбэк на локальный бинарь.


## REST API

| Метод | Путь | Описание |
|---|---|---|
| `POST` | `/auth/register` | регистрация (username + email + пароль; шлёт код) |
| `POST` | `/auth/verify` | подтверждение email (6-значный OTP-код) |
| `POST` | `/auth/resend` | повторная отправка кода |
| `POST` | `/auth/forgot` | запрос кода для сброса пароля |
| `POST` | `/auth/reset` | сброс пароля по коду |
| `POST` | `/auth/login` | вход по email/username → JWT (`Bearer`) |
| `POST` | `/artifacts` | загрузка (multipart: `file` + опц. `meta`/`visibility`; требует auth) |
| `GET` | `/artifacts` | поиск/список (`?q=&tag=&author=&institute=&type=`) |
| `GET` | `/artifacts/:id` | метаданные артефакта |
| `GET` | `/artifacts/:id/download` | скачать (заголовок `X-G4CARE-SHA256`) |
| `GET` | `/artifacts/:id/preview` | 3D-превью (OBJ) для `type=gdml` |
| `GET` | `/health` | проверка живости (`200 {"status":"ok"}`) |

Загрузка требует JWT-токен (`Authorization: Bearer …`). Артефакт получает
владельца, видимость (`public`/`private`) и номер версии; `private` доступен
только владельцу. Повторная загрузка того же имени тем же пользователем
увеличивает `version` и связывает `parentId`.

## Формат метаданных (TOML)

Единый формат метаданных — sidecar `<артефакт>.meta.toml` (совпадает с C++
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

## Bundle (многофайловый набор)

`g4care-cloud pack <config>` обходит зависимости конфига (`INCLUDE`,
`GEOMETRY.FILE`, `GEOMETRY.LIBRARY`, `MACROS`, `PHYSICS.*.MACRO/LIBRARY`,
`GEOMETRY.OBJECTS.*.file`) и упаковывает их вместе с `manifest.toml` в
`.g4c`-тарболл:

```toml
# manifest.toml (внутри .g4c)
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

`push`/`pull` понимают `.g4c`; `pull` автоматически распаковывает bundle в каталог,
сохраняя относительную структуру.

## Production-путь

Бэкенды выбираются по переменным окружения (иначе — файлы + JSON-индекс):

- **S3** (MinIO / SeaweedFS / AWS…): при заданном `G4CARE_CLOUD_S3_ENDPOINT` используется `S3Storage`
  (`src/backend/storage-s3.ts`). Параметры: `G4CARE_CLOUD_S3_PORT` (по умолч. 9000),
  `G4CARE_CLOUD_S3_USESSL`, `G4CARE_CLOUD_S3_ACCESS_KEY`,
  `G4CARE_CLOUD_S3_SECRET_KEY`, `G4CARE_CLOUD_S3_BUCKET`.
- **PostgreSQL**: при заданном `G4CARE_CLOUD_DB_URL` используются `PostgresIndex`
  (метаданные) и `PostgresUserStore` (пользователи) — схема `migrations/001_init.sql`
  (таблицы `artifacts`, `tags`, `users`).

Все реализации наследуют те же интерфейсы `ObjectStore` / `MetadataIndex` /
`UserStore`, что и файловые, поэтому REST-слой не меняется.

При старте с PostgreSQL backend сам накатывает миграции из `migrations/*.sql`
(идемпотентно), поэтому апгрейд существующего тома `pgdata` не требует ручных
действий.

Безопасность: перед `dlopen` `.so` обязательна проверка SHA-256 (команда
`verify`) + доверенные источники.

## Деплой (Docker)

```bash
cd cloud
cp .env.example .env   # задайте секреты (JWT, пароли, S3-ключи)
docker compose up --build
```

Поднимает: backend (`:3000`), frontend (`:8080`, nginx проксирует `/artifacts`
и `/auth` на backend), postgres (`:5432`), seaweedfs (S3 на `:9000`).

В этом режиме backend использует **S3 + PostgreSQL** (объекты, метаданные и
пользователи) — файловый MVP не задействован. Секреты берутся из `.env`:
`G4CARE_CLOUD_JWT_SECRET` (сгенерируйте: `openssl rand -hex 32`),
`POSTGRES_PASSWORD`, `G4CARE_CLOUD_S3_ACCESS_KEY` / `G4CARE_CLOUD_S3_SECRET_KEY`
(должны совпадать с `seaweedfs-s3.json`). 3D-превью требует собранного
`gdml2obj` (см. «3D-превью») и переменной `G4CARE_GDML2OBJ`.

Образ backend собирается в два этапа (компиляция `tsc` → запуск `node` без
dev-зависимостей). Health-проверки: `GET /health` у backend, `pg_isready` у
postgres, S3-порт у seaweedfs (compose ждёт их готовности через `depends_on`).

Email-верификация: при регистрации на почту отправляется 6-значный код, без него
вход невозможен. Если `SMTP_HOST` пуст — код пишется в лог backend (демо-режим);
для реальной отправки задайте `SMTP_HOST`/`SMTP_PORT`/`SMTP_USER`/`SMTP_PASS`/
`SMTP_FROM` в `.env`. Поля профиля: email (уникальный), телефон, дата рождения,
ФИО. Фронтенд — Ant Design (окно авторизации с вкладками Sign in / Register +
восстановление пароля).

## Anubis (антибот, опционально)

Для защиты от ботов можно поставить Anubis перед фронтендом (proof-of-work
челлендж для браузеров). Добавьте сервис в `docker-compose.yml`:

```yaml
  anubis:
    image: ghcr.io/techaro/anubis   # уточните актуальный тег
    environment:
      ANUBIS_TARGET: http://frontend:80
      ANUBIS_BIND: ":8080"
    ports:
      - "8080:8080"
    depends_on:
      - frontend
```

(и уберите `8080:80` у `frontend`, чтобы `:8080` занимал Anubis).

CI: `.github/workflows/ci.yml` — backend (install/typecheck/test) и frontend
(install/typecheck/build) на Node 20.

## Статус этапов

- [x] 5a — backend + хранилище + индекс + CLI (этот каталог)
- [x] 5b — веб-дашборд (React + TS: карточки, поиск, фильтры, загрузка)
- [x] 5c — 3D-превью (GDML → OBJ через Geant4 + three.js-вьюер)
- [x] 5d — аутентификация (JWT) / владение / видимость / версии
- [x] 5e — деплой (docker-compose + nginx) и CI
