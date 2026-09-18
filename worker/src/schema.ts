export const migrations = [
  `CREATE TABLE IF NOT EXISTS pairings (
    id uuid PRIMARY KEY,
    code_hash text NOT NULL,
    poll_secret_hash text NOT NULL,
    console_key_hash text NOT NULL,
    oauth_state_hash text,
    browser_nonce_hash text,
    status text NOT NULL CHECK (status IN ('pending','approved','claimed','expired','denied')),
    account_id uuid,
    expires_at timestamptz NOT NULL,
    attempts integer NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT now()
  )`,
  `CREATE TABLE IF NOT EXISTS accounts (
    id uuid PRIMARY KEY,
    google_sub text UNIQUE,
    drive_permission_id text,
    email text NOT NULL,
    display_name text,
    refresh_token_ciphertext text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
  )`,
  `ALTER TABLE accounts ALTER COLUMN google_sub DROP NOT NULL`,
  `ALTER TABLE accounts ADD COLUMN IF NOT EXISTS drive_permission_id text`,
  `CREATE UNIQUE INDEX IF NOT EXISTS accounts_drive_permission_idx ON accounts(drive_permission_id) WHERE drive_permission_id IS NOT NULL`,
  `CREATE TABLE IF NOT EXISTS account_drive_items (
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    file_id text NOT NULL,
    name text NOT NULL,
    mime_type text NOT NULL,
    resource_key text,
    size bigint NOT NULL DEFAULT 0,
    version text,
    md5_checksum text,
    can_download boolean NOT NULL DEFAULT false,
    PRIMARY KEY (account_id, file_id)
  )`,
  `CREATE TABLE IF NOT EXISTS console_accounts (
    console_key_hash text NOT NULL,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (console_key_hash, account_id)
  )`,
  `CREATE TABLE IF NOT EXISTS console_sessions (
    token_hash text PRIMARY KEY,
    console_key_hash text NOT NULL,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
  )`,
  `CREATE INDEX IF NOT EXISTS pairings_expiry_idx ON pairings(expires_at)`,
  `CREATE INDEX IF NOT EXISTS sessions_expiry_idx ON console_sessions(expires_at)`
];
