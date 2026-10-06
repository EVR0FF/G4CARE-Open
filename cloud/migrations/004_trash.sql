-- Soft delete (trash) support.
ALTER TABLE artifacts ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMPTZ;
