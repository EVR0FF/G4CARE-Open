-- Artifact authorization (owner/visibility) + numeric versioning in Postgres.
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS owner            TEXT NOT NULL DEFAULT '';
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS visibility       TEXT NOT NULL DEFAULT 'public';
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS artifact_version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS parent_id        UUID;
CREATE INDEX IF NOT EXISTS idx_artifacts_owner ON artifacts(owner);
