# Switch Drive Cloudflare Worker

This is a Worker deployment target for the pairing service. It preserves the
existing PostgreSQL schema, encrypts refresh tokens with AES-256-GCM, and uses
a Durable Object to rate-limit requests by client IP.

## Before deployment

1. Create a managed PostgreSQL database (for example, Neon or Supabase). Do
   not expose the laptop's Docker database directly to the Internet.
2. Create a Cloudflare Hyperdrive binding named `HYPERDRIVE` for that database:

   ```sh
   npx wrangler hyperdrive create switch-drive-db --connection-string='postgres://USER:PASSWORD@HOST:5432/DATABASE?sslmode=require'
   ```

   Copy its ID into the commented `hyperdrive` entry in `wrangler.jsonc`.

3. Attach a custom domain such as `api.example.com` in **Workers & Pages →
   switch-drive → Settings → Domains & Routes**. Do not use the temporary
   `workers.dev` URL for the Google OAuth client.
4. Enable the Google Drive API. In Google Cloud, register exactly
   `https://api.example.com/oauth/google/callback` as an authorized redirect
   URI. The same base URL becomes `PUBLIC_ORIGIN`.

The OAuth request uses `openid`, `email`, `profile`, and
`https://www.googleapis.com/auth/drive.readonly`. The first three identify the
account; `drive.readonly` lets the Switch browse and download all Drive content
visible to that account without creating, changing, or deleting files. Google
classifies that Drive scope as restricted. Test users can use a consent screen
in Testing; broader public use requires OAuth verification and the independent
security assessment when applicable under Google's restricted-scope rules.

For the official service, keep the origins separate:

- public homepage and legal pages: `https://swdrive.erok.qzz.io`
- Worker API and OAuth redirect: `https://api.erok.qzz.io`

In Google Auth Platform use `erok.qzz.io` as the authorized domain and verify
that root domain in Search Console.

## Deploy

```sh
cd worker
npm install
npx wrangler login
npx wrangler secret put PUBLIC_ORIGIN
npx wrangler secret put GOOGLE_CLIENT_ID
npx wrangler secret put GOOGLE_CLIENT_SECRET
npx wrangler secret put TOKEN_ENCRYPTION_KEY
npx wrangler secret put COOKIE_SECRET
npx wrangler deploy
```

Use the same `TOKEN_ENCRYPTION_KEY` as the Docker service only when migrating
its existing database; otherwise previously stored refresh tokens cannot be
decrypted. Generate new values with:

```sh
openssl rand -base64 32 # TOKEN_ENCRYPTION_KEY
openssl rand -base64 32 # COOKIE_SECRET
```

After deployment, open **Settings → Google OAuth Pairing API** in Switch Drive
and enter your origin:

```text
https://api.example.com
```

## Existing `drive.file` deployments

Update the OAuth consent configuration, deploy the Worker, and invalidate old
sessions and accounts during a planned cutover. Back up Neon first; then run
the cleanup SQL documented in the root README. Users must pair again to grant
the new read-only scope. Picker-era database columns are intentionally retained
for a non-destructive migration, but the service clears and no longer returns
their rows.
