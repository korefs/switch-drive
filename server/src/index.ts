import cookie from '@fastify/cookie';
import formbody from '@fastify/formbody';
import rateLimit from '@fastify/rate-limit';
import sensible from '@fastify/sensible';
import Fastify, {FastifyReply, FastifyRequest} from 'fastify';
import {OAuth2Client} from 'google-auth-library';
import {Pool} from 'pg';
import {randomUUID} from 'node:crypto';
import {z} from 'zod';
import {decrypt, encrypt, hash, required, sameHash, secret} from './crypto.js';
import {migrations} from './schema.js';

const app = Fastify({logger: {redact: ['req.headers.authorization', 'req.headers.x-pairing-secret', 'res.headers.set-cookie']}});
const db = new Pool({connectionString: required('DATABASE_URL')});
const origin = required('PUBLIC_ORIGIN').replace(/\/$/, '');
const oauth = new OAuth2Client({
  clientId: required('GOOGLE_CLIENT_ID'),
  clientSecret: required('GOOGLE_CLIENT_SECRET'),
  redirectUri: `${origin}/oauth/google/callback`
});
const scopes = ['openid', 'email', 'profile', 'https://www.googleapis.com/auth/drive.readonly'];
const pairingInput = z.object({consolePublicKey: z.string().min(32).max(4096)});

type Session = {consoleKeyHash: string};

async function migrate() {
  for (const sql of migrations) await db.query(sql);
  await db.query(`DELETE FROM pairings WHERE expires_at < now() - interval '1 day'; DELETE FROM console_sessions WHERE expires_at < now()`);
}

function cookieOptions() {
  return {httpOnly: true, secure: true, sameSite: 'lax' as const, path: '/', signed: true};
}

async function requireSession(request: FastifyRequest): Promise<Session> {
  const header = request.headers.authorization;
  if (!header?.startsWith('Bearer ')) throw app.httpErrors.unauthorized('Sessão ausente');
  const tokenHash = hash(header.slice(7));
  const result = await db.query<{console_key_hash: string}>(
    `SELECT console_key_hash FROM console_sessions WHERE token_hash = $1 AND expires_at > now()`, [tokenHash]);
  if (result.rowCount !== 1) throw app.httpErrors.unauthorized('Sessão expirada');
  return {consoleKeyHash: result.rows[0].console_key_hash};
}

async function pairSecret(request: FastifyRequest, id: string) {
  const value = request.headers['x-pairing-secret'];
  if (typeof value !== 'string') throw app.httpErrors.unauthorized('Segredo de pareamento ausente');
  const result = await db.query(`SELECT * FROM pairings WHERE id = $1`, [id]);
  if (result.rowCount !== 1 || !sameHash(value, result.rows[0].poll_secret_hash)) throw app.httpErrors.unauthorized('Pareamento inválido');
  return result.rows[0] as {status: string; expires_at: Date; console_key_hash: string; account_id: string | null};
}

app.register(cookie, {secret: required('COOKIE_SECRET')});
app.register(formbody);
app.register(sensible);
app.register(rateLimit, {global: true, max: 120, timeWindow: '1 minute'});

app.get('/health', async () => ({ok: true}));

app.post('/v1/pairings', {config: {rateLimit: {max: 10, timeWindow: '1 minute'}}}, async (request, reply) => {
  const {consolePublicKey} = pairingInput.parse(request.body);
  const id = randomUUID();
  const code = `${Math.floor(100000 + Math.random() * 900000)}`;
  const pollSecret = secret();
  const consoleKeyHash = hash(consolePublicKey);
  const expiresAt = new Date(Date.now() + 10 * 60_000);
  await db.query(
    `INSERT INTO pairings (id, code_hash, poll_secret_hash, console_key_hash, status, expires_at) VALUES ($1,$2,$3,$4,'pending',$5)`,
    [id, hash(code), hash(pollSecret), consoleKeyHash, expiresAt]);
  return reply.code(201).send({id, code, pollSecret, verificationUri: `${origin}/pair/${id}`, verificationUriComplete: `${origin}/pair/${id}/scan/${code}`, expiresAt: expiresAt.toISOString()});
});

app.get('/pair/:id', async (request, reply) => {
  const id = (request.params as {id: string}).id;
  const pairing = await db.query(`SELECT status, expires_at FROM pairings WHERE id = $1`, [id]);
  if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date()) return reply.code(410).type('text/html').send('<h1>Este código expirou.</h1>');
  return reply.type('text/html').send(`<!doctype html><html lang="pt-BR"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Switch Drive</title><body><main><h1>Conectar ao Switch Drive</h1><p>Digite o código exibido no Switch.</p><form method="post" action="/pair/${id}/authorize"><input name="code" inputmode="numeric" maxlength="6" required autofocus><button>Continuar com Google</button></form></main></body></html>`);
});

async function authorizePairing(id: string, code: string | undefined, request: FastifyRequest, reply: FastifyReply) {
  if (!code) throw app.httpErrors.badRequest('Código ausente');
  const pairing = await db.query(`SELECT * FROM pairings WHERE id = $1 FOR UPDATE`, [id]);
  if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date()) throw app.httpErrors.gone('Pareamento expirado');
  if (pairing.rows[0].attempts >= 5) throw app.httpErrors.tooManyRequests('Muitas tentativas');
  if (!sameHash(code, pairing.rows[0].code_hash)) {
    await db.query(`UPDATE pairings SET attempts = attempts + 1 WHERE id = $1`, [id]);
    throw app.httpErrors.unauthorized('Código incorreto');
  }
  const state = secret();
  const browserNonce = secret();
  await db.query(`UPDATE pairings SET oauth_state_hash=$2, browser_nonce_hash=$3 WHERE id=$1`, [id, hash(state), hash(browserNonce)]);
  reply.setCookie('switch_drive_oauth', `${id}.${browserNonce}`, cookieOptions());
  return reply.redirect(oauth.generateAuthUrl({access_type: 'offline', prompt: 'consent', include_granted_scopes: true, scope: scopes, state}));
}

app.get('/pair/:id/scan/:code', {config: {rateLimit: {max: 5, timeWindow: '10 minutes'}}}, async (request, reply) => {
  const {id, code} = request.params as {id: string; code: string};
  return authorizePairing(id, code, request, reply);
});

app.post('/pair/:id/authorize', {config: {rateLimit: {max: 5, timeWindow: '10 minutes'}}}, async (request, reply) => {
  const id = (request.params as {id: string}).id;
  return authorizePairing(id, (request.body as {code?: string})?.code, request, reply);
});

app.get('/oauth/google/callback', async (request, reply) => {
  const query = request.query as {code?: string; state?: string; error?: string};
  const signedCookie = request.cookies.switch_drive_oauth;
  const unsigned = signedCookie ? request.unsignCookie(signedCookie) : {valid: false, value: ''};
  const [id, browserNonce] = unsigned.valid ? unsigned.value.split('.') : [];
  reply.clearCookie('switch_drive_oauth', cookieOptions());
  if (!id || !browserNonce || query.error || !query.code || !query.state) throw app.httpErrors.unauthorized('Autorização não concluída');
  const pairing = await db.query(`SELECT * FROM pairings WHERE id=$1 FOR UPDATE`, [id]);
  if (pairing.rowCount !== 1 || pairing.rows[0].expires_at < new Date() || !sameHash(query.state, pairing.rows[0].oauth_state_hash) || !sameHash(browserNonce, pairing.rows[0].browser_nonce_hash)) throw app.httpErrors.unauthorized('Sessão OAuth inválida');
  const {tokens} = await oauth.getToken(query.code);
  if (!tokens.refresh_token || !tokens.access_token || !tokens.id_token) throw app.httpErrors.unauthorized('Google não forneceu acesso offline');
  const ticket = await oauth.verifyIdToken({idToken: tokens.id_token, audience: required('GOOGLE_CLIENT_ID')});
  const profile = ticket.getPayload();
  if (!profile?.sub || !profile.email || profile.email_verified !== true) throw app.httpErrors.unauthorized('Conta Google inválida');
  const accountLookup = await db.query<{id: string}>(`SELECT id FROM accounts WHERE google_sub=$1`, [profile.sub]);
  const accountId = accountLookup.rows[0]?.id ?? randomUUID();
  await db.query(`INSERT INTO accounts (id, google_sub, email, display_name, refresh_token_ciphertext) VALUES ($1,$2,$3,$4,$5) ON CONFLICT (google_sub) DO UPDATE SET email=EXCLUDED.email, display_name=EXCLUDED.display_name, refresh_token_ciphertext=EXCLUDED.refresh_token_ciphertext, updated_at=now()`, [accountId, profile.sub, profile.email, profile.name ?? null, encrypt(tokens.refresh_token)]);
  await db.query(`INSERT INTO console_accounts (console_key_hash, account_id) VALUES ($1,$2) ON CONFLICT DO NOTHING`, [pairing.rows[0].console_key_hash, accountId]);
  await db.query(`UPDATE pairings SET status='approved', account_id=$2 WHERE id=$1`, [id, accountId]);
  return reply.type('text/html').send('<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Switch Drive</title><h1>Conta conectada</h1><p>Volte ao Switch para concluir.</p>');
});

app.get('/v1/pairings/:id', async (request, reply) => {
  const id = (request.params as {id: string}).id;
  const pairing = await pairSecret(request, id);
  if (pairing.expires_at < new Date()) return reply.code(410).send({status: 'expired'});
  if (pairing.status !== 'approved') return {status: pairing.status};
  const account = await db.query(`SELECT email, display_name FROM accounts WHERE id=$1`, [pairing.account_id]);
  return {status: 'approved', account: {id: pairing.account_id, email: account.rows[0].email, displayName: account.rows[0].display_name}};
});

app.post('/v1/pairings/:id/claim', async (request, reply) => {
  const id = (request.params as {id: string}).id;
  const pairing = await pairSecret(request, id);
  if (pairing.expires_at < new Date()) return reply.code(410).send({status: 'expired'});
  if (pairing.status !== 'approved' || !pairing.account_id) throw app.httpErrors.conflict('Pareamento não está pronto');
  const token = secret(48);
  await db.query(`INSERT INTO console_sessions (token_hash, console_key_hash, expires_at) VALUES ($1,$2,now() + interval '180 days')`, [hash(token), pairing.console_key_hash]);
  const updated = await db.query(`UPDATE pairings SET status='claimed' WHERE id=$1 AND status='approved' RETURNING id`, [id]);
  if (updated.rowCount !== 1) throw app.httpErrors.conflict('Pareamento já utilizado');
  const account = await db.query(`SELECT id, email, display_name FROM accounts WHERE id=$1`, [pairing.account_id]);
  return {sessionToken: token, account: {id: account.rows[0].id, email: account.rows[0].email, displayName: account.rows[0].display_name}};
});

app.get('/v1/accounts', async request => {
  const session = await requireSession(request);
  const result = await db.query(`SELECT a.id, a.email, a.display_name FROM accounts a JOIN console_accounts ca ON ca.account_id=a.id WHERE ca.console_key_hash=$1 ORDER BY a.email`, [session.consoleKeyHash]);
  return {accounts: result.rows.map(row => ({id: row.id, email: row.email, displayName: row.display_name}))};
});

app.post('/v1/accounts/:id/access-token', async request => {
  const session = await requireSession(request);
  const id = (request.params as {id: string}).id;
  const result = await db.query<{refresh_token_ciphertext: string}>(`SELECT a.refresh_token_ciphertext FROM accounts a JOIN console_accounts ca ON ca.account_id=a.id WHERE a.id=$1 AND ca.console_key_hash=$2`, [id, session.consoleKeyHash]);
  if (result.rowCount !== 1) throw app.httpErrors.notFound('Conta não encontrada');
  oauth.setCredentials({refresh_token: decrypt(result.rows[0].refresh_token_ciphertext)});
  const access = await oauth.getAccessToken();
  if (!access.token) throw app.httpErrors.unauthorized('Reconecte esta conta Google');
  return {accessToken: access.token, expiresIn: 300};
});

app.delete('/v1/accounts/:id', async (request, reply) => {
  const session = await requireSession(request);
  const id = (request.params as {id: string}).id;
  const result = await db.query(`DELETE FROM console_accounts WHERE console_key_hash=$1 AND account_id=$2`, [session.consoleKeyHash, id]);
  if (result.rowCount !== 1) throw app.httpErrors.notFound('Conta não encontrada');
  return reply.code(204).send();
});

await migrate();
await app.listen({port: Number(process.env.PORT ?? 3000), host: '0.0.0.0'});
