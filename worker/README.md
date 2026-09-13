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
4. In Google Cloud, register exactly
   `https://api.example.com/oauth/google/callback` as an authorized redirect
   URI. The same base URL becomes `PUBLIC_ORIGIN`.

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

After deployment, put this on the Switch SD card as
`sd:/switch-drive/config.json`:

```json
{"service_url":"https://api.example.com"}
```
