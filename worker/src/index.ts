import {Client} from 'pg';
import {migrations} from './schema';

export interface Env {
  HYPERDRIVE: Hyperdrive;
  RATE_LIMITER: DurableObjectNamespace;
  PUBLIC_ORIGIN: string;
  GOOGLE_CLIENT_ID: string;
  GOOGLE_CLIENT_SECRET: string;
  TOKEN_ENCRYPTION_KEY: string;
  COOKIE_SECRET: string;
}

type Pairing = {status: string; expires_at: Date; console_key_hash: string; account_id: string | null; attempts: number; code_hash: string; poll_secret_hash: string; oauth_state_hash: string | null; browser_nonce_hash: string | null};
type Session = {consoleKeyHash: string};
type GoogleProfile = {sub?: string; email?: string; email_verified?: boolean; name?: string};

const scopes = ['openid', 'email', 'profile', 'https://www.googleapis.com/auth/drive.readonly'];
const encoder = new TextEncoder();
const decoder = new TextDecoder();
let databaseReady: Promise<void> | undefined;

class HttpError extends Error {
  constructor(readonly status: number, message: string) { super(message); }
}

function required(value: string | undefined, name: string): string {
  if (!value) throw new Error(`${name} is required`);
  return value;
}

async function ensureDatabase(db: Client) {
  databaseReady ??= (async () => {
    for (const sql of migrations) await db.query(sql);
    await db.query(`DELETE FROM pairings WHERE expires_at < now() - interval '1 day'; DELETE FROM console_sessions WHERE expires_at < now()`);
  })().catch(error => {
    // A transient Hyperdrive/PostgreSQL failure must not poison this isolate.
    databaseReady = undefined;
    throw error;
  });
  return databaseReady;
}

function bytesToBase64Url(bytes: Uint8Array): string {
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
}

function base64UrlToBytes(value: string): Uint8Array {
  const padded = value.replaceAll('-', '+').replaceAll('_', '/') + '='.repeat((4 - value.length % 4) % 4);
  const binary = atob(padded);
  return Uint8Array.from(binary, character => character.charCodeAt(0));
}

function asBuffer(bytes: Uint8Array): ArrayBuffer {
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
}

async function tokenHash(value: string): Promise<string> {
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', asBuffer(encoder.encode(value)))));
}

async function sameHash(value: string, expected: string): Promise<boolean> {
  const actual = await tokenHash(value);
  if (actual.length !== expected.length) return false;
  let difference = 0;
  for (let index = 0; index < actual.length; ++index) difference |= actual.charCodeAt(index) ^ expected.charCodeAt(index);
  return difference === 0;
}

function secret(bytes = 32): string {
  const data = new Uint8Array(bytes);
  crypto.getRandomValues(data);
  return bytesToBase64Url(data);
}

async function cryptoKey(env: Env): Promise<CryptoKey> {
  const key = base64UrlToBytes(required(env.TOKEN_ENCRYPTION_KEY, 'TOKEN_ENCRYPTION_KEY'));
  if (key.length !== 32) throw new Error('TOKEN_ENCRYPTION_KEY must decode to 32 bytes');
  return crypto.subtle.importKey('raw', asBuffer(key), 'AES-GCM', false, ['encrypt', 'decrypt']);
}

async function encrypt(env: Env, plain: string): Promise<string> {
  const iv = new Uint8Array(12);
  crypto.getRandomValues(iv);
  const encrypted = new Uint8Array(await crypto.subtle.encrypt({name: 'AES-GCM', iv: asBuffer(iv)}, await cryptoKey(env), asBuffer(encoder.encode(plain))));
  return [bytesToBase64Url(iv), bytesToBase64Url(encrypted.slice(-16)), bytesToBase64Url(encrypted.slice(0, -16))].join('.');
}

async function decrypt(env: Env, encoded: string): Promise<string> {
  const [iv, tag, ciphertext] = encoded.split('.');
  if (!iv || !tag || !ciphertext) throw new Error('invalid encrypted token');
  const data = base64UrlToBytes(ciphertext);
  const authTag = base64UrlToBytes(tag);
  const combined = new Uint8Array(data.length + authTag.length);
  combined.set(data); combined.set(authTag, data.length);
  return decoder.decode(await crypto.subtle.decrypt({name: 'AES-GCM', iv: asBuffer(base64UrlToBytes(iv))}, await cryptoKey(env), asBuffer(combined)));
}

async function sign(env: Env, value: string): Promise<string> {
  const key = await crypto.subtle.importKey('raw', asBuffer(encoder.encode(required(env.COOKIE_SECRET, 'COOKIE_SECRET'))), {name: 'HMAC', hash: 'SHA-256'}, false, ['sign']);
  return bytesToBase64Url(new Uint8Array(await crypto.subtle.sign('HMAC', key, asBuffer(encoder.encode(value)))));
}

async function signedCookie(env: Env, value: string): Promise<string> { return `${value}.${await sign(env, value)}`; }
async function validCookie(env: Env, value: string | undefined): Promise<string | undefined> {
  if (!value) return undefined;
  const boundary = value.lastIndexOf('.');
  if (boundary < 1) return undefined;
  const plain = value.slice(0, boundary);
  const signature = value.slice(boundary + 1);
  const expected = await sign(env, plain);
  if (signature.length !== expected.length) return undefined;
  let difference = 0;
  for (let index = 0; index < signature.length; ++index) difference |= signature.charCodeAt(index) ^ expected.charCodeAt(index);
  return difference ? undefined : plain;
}

function response(body: BodyInit | null, status = 200, headers: HeadersInit = {}): Response {
  return new Response(body, {status, headers: {'Cache-Control': 'no-store', ...headers}});
}
function json(data: unknown, status = 200, headers: HeadersInit = {}): Response { return response(JSON.stringify(data), status, {'Content-Type': 'application/json; charset=utf-8', ...headers}); }
function html(body: string, status = 200, headers: HeadersInit = {}): Response { return response(body, status, {'Content-Type': 'text/html; charset=utf-8', ...headers}); }
function escapeHtml(value: string): string { return value.replace(/[&<>"']/g, character => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[character]!)); }
function redirect(location: string, headers: HeadersInit = {}): Response { return response(null, 302, {Location: location, ...headers}); }
function isUuid(value: string): boolean { return /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value); }
function cookie(request: Request, name: string): string | undefined { return request.headers.get('Cookie')?.split(';').map(value => value.trim()).find(value => value.startsWith(`${name}=`))?.slice(name.length + 1); }
function cookieHeader(value: string, maxAge = 600): string { return `switch_drive_oauth=${encodeURIComponent(value)}; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=${maxAge}`; }

async function enforceRateLimit(request: Request, env: Env, route: string, limit: number, seconds: number) {
  const address = request.headers.get('CF-Connecting-IP') ?? 'unknown';
  const bucket = Math.floor(Date.now() / (seconds * 1000));
  const id = env.RATE_LIMITER.idFromName(`${route}:${address}:${bucket}`);
  const result = await env.RATE_LIMITER.get(id).fetch('https://rate-limit/', {method: 'POST', body: JSON.stringify({limit, seconds})});
  if (result.status === 429) throw new HttpError(429, 'Muitas tentativas');
}

async function requireSession(request: Request, db: Client): Promise<Session> {
  const header = request.headers.get('Authorization');
  if (!header?.startsWith('Bearer ')) throw new HttpError(401, 'Sessão ausente');
  const result = await db.query<{console_key_hash: string}>('SELECT console_key_hash FROM console_sessions WHERE token_hash = $1 AND expires_at > now()', [await tokenHash(header.slice(7))]);
  if (result.rowCount !== 1) throw new HttpError(401, 'Sessão expirada');
  return {consoleKeyHash: result.rows[0].console_key_hash};
}

async function pairSecret(request: Request, db: Client, id: string): Promise<Pairing> {
  const value = request.headers.get('X-Pairing-Secret');
  if (!value) throw new HttpError(401, 'Segredo de pareamento ausente');
  const result = await db.query<Pairing>('SELECT * FROM pairings WHERE id = $1', [id]);
  if (result.rowCount !== 1 || !await sameHash(value, result.rows[0].poll_secret_hash)) throw new HttpError(401, 'Pareamento inválido');
  return result.rows[0];
}

function origin(env: Env): string { return required(env.PUBLIC_ORIGIN, 'PUBLIC_ORIGIN').replace(/\/$/, ''); }
function googleAuthorizationUrl(env: Env, state: string): string {
  const parameters = new URLSearchParams({client_id: required(env.GOOGLE_CLIENT_ID, 'GOOGLE_CLIENT_ID'), redirect_uri: `${origin(env)}/oauth/google/callback`, response_type: 'code', access_type: 'offline', prompt: 'consent', include_granted_scopes: 'true', scope: scopes.join(' '), state});
  return `https://accounts.google.com/o/oauth2/v2/auth?${parameters}`;
}

async function googleToken(env: Env, parameters: Record<string, string>): Promise<Record<string, unknown>> {
  const body = new URLSearchParams({client_id: required(env.GOOGLE_CLIENT_ID, 'GOOGLE_CLIENT_ID'), client_secret: required(env.GOOGLE_CLIENT_SECRET, 'GOOGLE_CLIENT_SECRET'), ...parameters});
  const result = await fetch('https://oauth2.googleapis.com/token', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body});
  const payload = await result.json().catch(() => ({})) as Record<string, unknown>;
  if (!result.ok) {
    const code = typeof payload.error === 'string' ? payload.error : 'unknown_error';
    const description = typeof payload.error_description === 'string' ? payload.error_description : 'sem descrição';
    // OAuth codes and descriptions are safe to log; authorization codes,
    // access tokens, refresh tokens, client IDs, and secrets are never logged.
    console.warn('Google token request rejected', {status: result.status, code, description});
    throw new HttpError(401, `Google recusou a autorização (${code})`);
  }
  return payload;
}

async function authorizePairing(request: Request, env: Env, db: Client, id: string, code: string | undefined): Promise<Response> {
  if (!code) throw new HttpError(400, 'Código ausente');
  if (!isUuid(id)) throw new HttpError(404, 'Pareamento não encontrado');
  await enforceRateLimit(request, env, 'authorize', 5, 600);
  const pairing = await db.query<Pairing>('SELECT * FROM pairings WHERE id = $1 FOR UPDATE', [id]);
  if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date()) throw new HttpError(410, 'Pareamento expirado');
  if (pairing.rows[0].attempts >= 5) throw new HttpError(429, 'Muitas tentativas');
  if (!await sameHash(code, pairing.rows[0].code_hash)) {
    await db.query('UPDATE pairings SET attempts = attempts + 1 WHERE id = $1', [id]);
    throw new HttpError(401, 'Código incorreto');
  }
  // State carries the pairing ID plus an unguessable nonce. The callback can
  // then validate it against PostgreSQL even when an in-app camera/browser
  // loses the temporary first-party cookie during the Google redirect.
  const state = `${id}.${secret()}`;
  const browserNonce = secret();
  await db.query('UPDATE pairings SET oauth_state_hash=$2, browser_nonce_hash=$3 WHERE id=$1', [id, await tokenHash(state), await tokenHash(browserNonce)]);
  return redirect(googleAuthorizationUrl(env, state), {'Set-Cookie': cookieHeader(await signedCookie(env, `${id}.${browserNonce}`))});
}

async function callback(request: Request, env: Env, db: Client): Promise<Response> {
  const query = new URL(request.url).searchParams;
  const rawCookie = cookie(request, 'switch_drive_oauth');
  const value = await validCookie(env, rawCookie ? decodeURIComponent(rawCookie) : undefined);
  const state = query.get('state') ?? '';
  const [id, stateNonce] = state?.split('.') ?? [];
  const [, browserNonce] = value?.split('.') ?? [];
  const clear = {'Set-Cookie': cookieHeader('', 0)};
  if (!id || !stateNonce || !isUuid(id) || query.get('error') || !query.get('code')) throw new HttpError(401, 'Autorização não concluída');
  const pairing = await db.query<Pairing>('SELECT * FROM pairings WHERE id=$1 FOR UPDATE', [id]);
  if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date() || !await sameHash(state, pairing.rows[0].oauth_state_hash ?? '')) throw new HttpError(401, 'Sessão OAuth inválida');
  // A valid cookie provides an additional browser binding. Its absence is
  // expected for some camera-app handoffs, where the OAuth state remains the
  // CSRF protection required by the authorization flow.
  if (browserNonce && !await sameHash(browserNonce, pairing.rows[0].browser_nonce_hash ?? '')) throw new HttpError(401, 'Sessão OAuth inválida');
  const tokens = await googleToken(env, {code: query.get('code')!, grant_type: 'authorization_code', redirect_uri: `${origin(env)}/oauth/google/callback`});
  const accessToken = typeof tokens.access_token === 'string' ? tokens.access_token : '';
  const refreshToken = typeof tokens.refresh_token === 'string' ? tokens.refresh_token : '';
  if (!accessToken || !refreshToken) throw new HttpError(401, 'Google não forneceu acesso offline');
  const userInfo = await fetch('https://openidconnect.googleapis.com/v1/userinfo', {headers: {Authorization: `Bearer ${accessToken}`}});
  const profile = await userInfo.json() as GoogleProfile;
  if (!userInfo.ok || !profile.sub || !profile.email || profile.email_verified !== true) throw new HttpError(401, 'Conta Google inválida');
  const existing = await db.query<{id: string}>('SELECT id FROM accounts WHERE google_sub=$1', [profile.sub]);
  const accountId = existing.rows[0]?.id ?? crypto.randomUUID();
  await db.query('INSERT INTO accounts (id, google_sub, email, display_name, refresh_token_ciphertext) VALUES ($1,$2,$3,$4,$5) ON CONFLICT (google_sub) DO UPDATE SET email=EXCLUDED.email, display_name=EXCLUDED.display_name, refresh_token_ciphertext=EXCLUDED.refresh_token_ciphertext, updated_at=now()', [accountId, profile.sub, profile.email, profile.name ?? null, await encrypt(env, refreshToken)]);
  await db.query('INSERT INTO console_accounts (console_key_hash, account_id) VALUES ($1,$2) ON CONFLICT DO NOTHING', [pairing.rows[0].console_key_hash, accountId]);
  await db.query("UPDATE pairings SET status='approved', account_id=$2 WHERE id=$1", [id, accountId]);
  return html('<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Switch Drive</title><h1>Conta conectada</h1><p>Volte ao Switch para concluir.</p>', 200, clear);
}

async function route(request: Request, env: Env, db: Client): Promise<Response> {
  await enforceRateLimit(request, env, 'global', 120, 60);
  const url = new URL(request.url);
  const path = url.pathname;
  if (request.method === 'GET' && path === '/health') return json({ok: true});
  if (request.method === 'POST' && path === '/v1/pairings') {
    await enforceRateLimit(request, env, 'create-pairing', 10, 60);
    const body = await request.json().catch(() => null) as {consolePublicKey?: unknown} | null;
    const consolePublicKey = typeof body?.consolePublicKey === 'string' ? body.consolePublicKey : '';
    if (consolePublicKey.length < 32 || consolePublicKey.length > 4096) throw new HttpError(400, 'Chave do console inválida');
    const id = crypto.randomUUID();
    const code = String(Math.floor(100000 + Math.random() * 900000));
    const pollSecret = secret();
    const expiresAt = new Date(Date.now() + 10 * 60_000);
    await db.query("INSERT INTO pairings (id, code_hash, poll_secret_hash, console_key_hash, status, expires_at) VALUES ($1,$2,$3,$4,'pending',$5)", [id, await tokenHash(code), await tokenHash(pollSecret), await tokenHash(consolePublicKey), expiresAt]);
    const pairUrl = `${origin(env)}/pair/${id}`;
    return json({id, code, pollSecret, verificationUri: pairUrl, verificationUriComplete: `${pairUrl}/scan/${code}`, expiresAt: expiresAt.toISOString()}, 201);
  }
  const pairPage = path.match(/^\/pair\/([0-9a-f-]+)$/i);
  if (request.method === 'GET' && pairPage) {
    if (!isUuid(pairPage[1])) throw new HttpError(404, 'Pareamento não encontrado');
    const pairing = await db.query<{expires_at: Date}>('SELECT expires_at FROM pairings WHERE id=$1', [pairPage[1]]);
    if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date()) return html('<h1>Este código expirou.</h1>', 410);
    return html(`<!doctype html><html lang="pt-BR"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Switch Drive</title><body><main><h1>Conectar ao Switch Drive</h1><p>Digite o código exibido no Switch.</p><form method="post" action="/pair/${pairPage[1]}/authorize"><input name="code" inputmode="numeric" maxlength="6" required autofocus><button>Continuar com Google</button></form></main></body></html>`);
  }
  const scan = path.match(/^\/pair\/([0-9a-f-]+)\/scan\/([0-9]{6})$/i);
  if (request.method === 'GET' && scan) return authorizePairing(request, env, db, scan[1], scan[2]);
  const authorize = path.match(/^\/pair\/([0-9a-f-]+)\/authorize$/i);
  if (request.method === 'POST' && authorize) return authorizePairing(request, env, db, authorize[1], (await request.formData()).get('code')?.toString());
  if (request.method === 'GET' && path === '/oauth/google/callback') return callback(request, env, db);
  const poll = path.match(/^\/v1\/pairings\/([0-9a-f-]+)$/i);
  if (request.method === 'GET' && poll) {
    const pairing = await pairSecret(request, db, poll[1]);
    if (pairing.expires_at < new Date()) return json({status: 'expired'}, 410);
    if (pairing.status !== 'approved') return json({status: pairing.status});
    const account = await db.query<{id: string; email: string; display_name: string | null}>('SELECT id, email, display_name FROM accounts WHERE id=$1', [pairing.account_id]);
    return json({status: 'approved', account: {id: account.rows[0].id, email: account.rows[0].email, displayName: account.rows[0].display_name}});
  }
  const claim = path.match(/^\/v1\/pairings\/([0-9a-f-]+)\/claim$/i);
  if (request.method === 'POST' && claim) {
    const pairing = await pairSecret(request, db, claim[1]);
    if (pairing.expires_at < new Date()) return json({status: 'expired'}, 410);
    if (pairing.status !== 'approved' || !pairing.account_id) throw new HttpError(409, 'Pareamento não está pronto');
    const token = secret(48);
    await db.query("INSERT INTO console_sessions (token_hash, console_key_hash, expires_at) VALUES ($1,$2,now() + interval '180 days')", [await tokenHash(token), pairing.console_key_hash]);
    const updated = await db.query("UPDATE pairings SET status='claimed' WHERE id=$1 AND status='approved' RETURNING id", [claim[1]]);
    if (updated.rowCount !== 1) throw new HttpError(409, 'Pareamento já utilizado');
    const account = await db.query<{id: string; email: string; display_name: string | null}>('SELECT id, email, display_name FROM accounts WHERE id=$1', [pairing.account_id]);
    return json({sessionToken: token, account: {id: account.rows[0].id, email: account.rows[0].email, displayName: account.rows[0].display_name}});
  }
  if (request.method === 'GET' && path === '/v1/accounts') {
    const session = await requireSession(request, db);
    const result = await db.query<{id: string; email: string; display_name: string | null}>('SELECT a.id, a.email, a.display_name FROM accounts a JOIN console_accounts ca ON ca.account_id=a.id WHERE ca.console_key_hash=$1 ORDER BY a.email', [session.consoleKeyHash]);
    return json({accounts: result.rows.map(account => ({id: account.id, email: account.email, displayName: account.display_name}))});
  }
  const accessToken = path.match(/^\/v1\/accounts\/([0-9a-f-]+)\/access-token$/i);
  if (request.method === 'POST' && accessToken) {
    const session = await requireSession(request, db);
    const result = await db.query<{refresh_token_ciphertext: string}>('SELECT a.refresh_token_ciphertext FROM accounts a JOIN console_accounts ca ON ca.account_id=a.id WHERE a.id=$1 AND ca.console_key_hash=$2', [accessToken[1], session.consoleKeyHash]);
    if (result.rowCount !== 1) throw new HttpError(404, 'Conta não encontrada');
    const token = await googleToken(env, {refresh_token: await decrypt(env, result.rows[0].refresh_token_ciphertext), grant_type: 'refresh_token'});
    if (typeof token.access_token !== 'string') throw new HttpError(401, 'Reconecte esta conta Google');
    return json({accessToken: token.access_token, expiresIn: 300});
  }
  const account = path.match(/^\/v1\/accounts\/([0-9a-f-]+)$/i);
  if (request.method === 'DELETE' && account) {
    const session = await requireSession(request, db);
    const deleted = await db.query('DELETE FROM console_accounts WHERE console_key_hash=$1 AND account_id=$2', [session.consoleKeyHash, account[1]]);
    if (deleted.rowCount !== 1) throw new HttpError(404, 'Conta não encontrada');
    return response(null, 204);
  }
  throw new HttpError(404, 'Não encontrado');
}

export class RateLimiter {
  constructor(private readonly state: DurableObjectState) {}
  async fetch(request: Request): Promise<Response> {
    const {limit, seconds} = await request.json() as {limit?: unknown; seconds?: unknown};
    if (typeof limit !== 'number' || typeof seconds !== 'number' || !Number.isInteger(limit) || !Number.isInteger(seconds) || limit < 1 || seconds < 1) return new Response(null, {status: 400});
    const count = await this.state.storage.get<number>('count') ?? 0;
    if (count >= limit) return new Response(null, {status: 429});
    await this.state.storage.put('count', count + 1);
    await this.state.storage.setAlarm(Date.now() + seconds * 1000);
    return new Response(null, {status: 204});
  }
  async alarm() { await this.state.storage.deleteAll(); }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const db = new Client({connectionString: env.HYPERDRIVE.connectionString});
    try {
      await db.connect();
      await ensureDatabase(db);
      return await route(request, env, db);
    } catch (error) {
      if (error instanceof HttpError) {
        if (new URL(request.url).pathname === '/oauth/google/callback') {
          return html(`<!doctype html><html lang="pt-BR"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Switch Drive</title><body><main><h1>Não foi possível conectar</h1><p>${escapeHtml(error.message)}</p><p>Volte ao Switch e tente novamente.</p></main></body></html>`, error.status, {'Set-Cookie': cookieHeader('', 0)});
        }
        return json({error: error.message}, error.status);
      }
      console.error(error);
      return json({error: 'Erro interno'}, 500);
    } finally {
      await db.end().catch(error => console.error('Database close failed', error));
    }
  }
} satisfies ExportedHandler<Env>;
