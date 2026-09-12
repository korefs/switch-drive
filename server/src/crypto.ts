import {createCipheriv, createDecipheriv, createHash, randomBytes, timingSafeEqual} from 'node:crypto';

export const hash = (value: string) => createHash('sha256').update(value).digest('hex');
export const secret = (bytes = 32) => randomBytes(bytes).toString('base64url');

export function sameHash(value: string, expected: string): boolean {
  const actual = Buffer.from(hash(value), 'hex');
  const desired = Buffer.from(expected, 'hex');
  return actual.length === desired.length && timingSafeEqual(actual, desired);
}

function key(): Buffer {
  const value = Buffer.from(required('TOKEN_ENCRYPTION_KEY'), 'base64');
  if (value.length !== 32) throw new Error('TOKEN_ENCRYPTION_KEY must decode to 32 bytes');
  return value;
}

export function encrypt(plain: string): string {
  const iv = randomBytes(12);
  const cipher = createCipheriv('aes-256-gcm', key(), iv);
  const encrypted = Buffer.concat([cipher.update(plain, 'utf8'), cipher.final()]);
  return [iv, cipher.getAuthTag(), encrypted].map(part => part.toString('base64url')).join('.');
}

export function decrypt(encoded: string): string {
  const [ivText, tagText, dataText] = encoded.split('.');
  if (!ivText || !tagText || !dataText) throw new Error('invalid encrypted token');
  const decipher = createDecipheriv('aes-256-gcm', key(), Buffer.from(ivText, 'base64url'));
  decipher.setAuthTag(Buffer.from(tagText, 'base64url'));
  return Buffer.concat([decipher.update(Buffer.from(dataText, 'base64url')), decipher.final()]).toString('utf8');
}

export function required(name: string): string {
  const value = process.env[name];
  if (!value) throw new Error(`${name} is required`);
  return value;
}
