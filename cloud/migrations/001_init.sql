-- G4CARE cloud — PostgreSQL schema (production path).
-- The MVP uses a JSON-file index (src/backend/index.ts); this schema is the
-- scalable replacement with the same logical shape.

CREATE TABLE IF NOT EXISTS artifacts (
    id            UUID PRIMARY KEY,
    type          TEXT NOT NULL,              -- yaml | gdml | so | mac | other
    name          TEXT NOT NULL,
    doi           TEXT,
    info_url      TEXT,
    description   TEXT,
    version       TEXT,
    created       TEXT,                       -- ISO-8601
    author_name   TEXT,
    author_alias  TEXT,
    group_name    TEXT,
    institute     TEXT,
    department    TEXT,
    sha256        TEXT NOT NULL,
    size          BIGINT NOT NULL,
    storage_key   TEXT NOT NULL,
    files         JSONB,                      -- bundle file list [{path,sha256,size}]
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS tags (
    artifact_id UUID NOT NULL REFERENCES artifacts(id) ON DELETE CASCADE,
    tag         TEXT NOT NULL,
    PRIMARY KEY (artifact_id, tag)
);

CREATE INDEX IF NOT EXISTS idx_artifacts_type      ON artifacts(type);
CREATE INDEX IF NOT EXISTS idx_artifacts_institute ON artifacts(institute);
CREATE INDEX IF NOT EXISTS idx_artifacts_author    ON artifacts(author_alias);
CREATE INDEX IF NOT EXISTS idx_tags_tag            ON tags(tag);

-- Users (production path: replaces the JSON-file user store).
CREATE TABLE IF NOT EXISTS users (
    username    TEXT PRIMARY KEY,
    salt        TEXT NOT NULL,
    hash        TEXT NOT NULL,
    institute   TEXT NOT NULL DEFAULT '',
    group_name  TEXT NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
