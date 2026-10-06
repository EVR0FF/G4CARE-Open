-- G4CARE cloud — email verification + extended profile (auth phase 2).
-- Idempotent: applied by the backend on every boot (see src/backend/migrate.ts).

ALTER TABLE users ADD COLUMN IF NOT EXISTS email          TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS phone          TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS birth_date     DATE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS full_name      TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS email_verified BOOLEAN NOT NULL DEFAULT false;

CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email ON users(email);

-- One active verification code per (email, purpose).
CREATE TABLE IF NOT EXISTS verification_codes (
    email      TEXT NOT NULL,
    purpose    TEXT NOT NULL,           -- verify_email | reset_password
    code_hash  TEXT NOT NULL,           -- sha256 hex of the 6-digit code
    expires_at TIMESTAMPTZ NOT NULL,
    used       BOOLEAN NOT NULL DEFAULT false,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (email, purpose)
);
