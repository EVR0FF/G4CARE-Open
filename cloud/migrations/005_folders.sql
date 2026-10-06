-- Folders (drive navigation).
CREATE TABLE IF NOT EXISTS folders (
    id         UUID PRIMARY KEY,
    name       TEXT NOT NULL,
    parent_id  UUID REFERENCES folders(id) ON DELETE CASCADE,
    owner      TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS folder_id UUID REFERENCES folders(id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id);
CREATE INDEX IF NOT EXISTS idx_artifacts_folder ON artifacts(folder_id);
