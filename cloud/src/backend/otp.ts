//==============================================================================
// G4CARE cloud — one-time verification codes (OTP).
//==============================================================================

import { createHash, randomInt } from 'node:crypto';

export const CODE_TTL_SECONDS = 15 * 60; // 15 minutes

/// 6-digit numeric code (zero-padded).
export function generateCode(): string {
  return String(randomInt(0, 1000000)).padStart(6, '0');
}

/// SHA-256 hex — codes are stored hashed, never in plaintext.
export function hashCode(code: string): string {
  return createHash('sha256').update(code).digest('hex');
}
