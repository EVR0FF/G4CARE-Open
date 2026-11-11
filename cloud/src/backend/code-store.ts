//==============================================================================
// G4CARE - code-store.ts — Cloud backend: code store
//
// Cloud backend: code store
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { Pool } from 'pg';

export interface CodeStore {
  /// Store (or replace) the active code for (email, purpose).
  upsert(email: string, purpose: string, codeHash: string, ttlSeconds: number): Promise<void>;
  /// Atomically consume the code if it matches and is still valid.
  verify(email: string, purpose: string, codeHash: string): Promise<boolean>;
}

export class PostgresCodeStore implements CodeStore {
  private readonly pool: Pool;

  constructor(connectionString: string) {
    this.pool = new Pool({ connectionString });
  }

  async upsert(email: string, purpose: string, codeHash: string, ttlSeconds: number): Promise<void> {
    await this.pool.query(
      `INSERT INTO verification_codes (email, purpose, code_hash, expires_at, used)
       VALUES ($1, $2, $3, now() + ($4 * interval '1 second'), false)
       ON CONFLICT (email, purpose)
       DO UPDATE SET code_hash = $3, expires_at = now() + ($4 * interval '1 second'), used = false`,
      [email, purpose, codeHash, ttlSeconds],
    );
  }

  async verify(email: string, purpose: string, codeHash: string): Promise<boolean> {
    const res = await this.pool.query(
      `UPDATE verification_codes
          SET used = true
        WHERE email = $1 AND purpose = $2 AND code_hash = $3
          AND used = false AND expires_at > now()
      RETURNING email`,
      [email, purpose, codeHash],
    );
    return (res.rowCount ?? 0) > 0;
  }

  async close(): Promise<void> {
    await this.pool.end();
  }
}

export class InMemoryCodeStore implements CodeStore {
  private readonly codes = new Map<string, { hash: string; expiresAt: number }>();

  private key(email: string, purpose: string): string {
    return `${purpose}:${email}`;
  }

  async upsert(email: string, purpose: string, codeHash: string, ttlSeconds: number): Promise<void> {
    this.codes.set(this.key(email, purpose), { hash: codeHash, expiresAt: Date.now() + ttlSeconds * 1000 });
  }

  async verify(email: string, purpose: string, codeHash: string): Promise<boolean> {
    const entry = this.codes.get(this.key(email, purpose));
    if (!entry) return false;
    if (entry.expiresAt < Date.now()) return false;
    if (entry.hash !== codeHash) return false;
    this.codes.delete(this.key(email, purpose));
    return true;
  }
}
