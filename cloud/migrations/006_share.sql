-- Share links (public tokenized access to an artifact or a folder).
CREATE TABLE IF NOT EXISTS share_links (
    token       TEXT PRIMARY KEY,
    artifact_id UUID REFERENCES artifacts(id) ON DELETE CASCADE,
    folder_id   UUID REFERENCES folders(id) ON DELETE CASCADE,
    created_by  TEXT NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at  TIMESTAMPTZ,
    CHECK ((artifact_id IS NULL) <> (folder_id IS NULL))
);
