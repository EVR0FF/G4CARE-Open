//==============================================================================
// G4CARE - auth.ts — Cloud backend: authentication
//
// Cloud backend: authentication
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { createHmac, scryptSync, randomBytes, timingSafeEqual } from 'node:crypto';

export function hashPassword(password: string, salt?: string): { salt: string; hash: string } {
  const s = salt ?? randomBytes(16).toString('hex');
  const hash = scryptSync(password, s, 64).toString('hex');
  return { salt: s, hash };
}

export function verifyPassword(password: string, salt: string, hash: string): boolean {
  const actual = scryptSync(password, salt, 64);
  const expected = Buffer.from(hash, 'hex');
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}

function b64url(obj: object): string {
  return Buffer.from(JSON.stringify(obj)).toString('base64url');
}

export function signToken(payload: Record<string, unknown>, secret: string, ttlSeconds: number): string {
  const header = { alg: 'HS256', typ: 'JWT' };
  const now = Math.floor(Date.now() / 1000);
  const body = { ...payload, iat: now, exp: now + ttlSeconds };
  const data = `${b64url(header)}.${b64url(body)}`;
  const sig = createHmac('sha256', secret).update(data).digest('base64url');
  return `${data}.${sig}`;
}

export function verifyToken(token: string, secret: string): Record<string, unknown> | null {
  const parts = token.split('.');
  if (parts.length !== 3) return null;
  const [h, p, s] = parts;
  const expected = createHmac('sha256', secret).update(`${h}.${p}`).digest('base64url');
  if (expected !== s) return null;
  try {
    const payload = JSON.parse(Buffer.from(p, 'base64url').toString()) as Record<string, unknown>;
    if (typeof payload.exp !== 'number' || payload.exp < Math.floor(Date.now() / 1000)) return null;
    return payload;
  } catch {
    return null;
  }
}
