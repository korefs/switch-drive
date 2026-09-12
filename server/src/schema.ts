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
    google_sub text NOT NULL UNIQUE,
    email text NOT NULL,
    display_name text,
    refresh_token_ciphertext text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
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
