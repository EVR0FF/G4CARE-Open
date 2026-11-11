//==============================================================================
// G4CARE - users.ts — Cloud backend: users
//
// Cloud backend: users
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { readFile, writeFile } from 'node:fs/promises';
import { Pool } from 'pg';
import { hashPassword, verifyPassword } from './auth.js';

export interface UserRecord {
  username: string;
  email: string;
  emailVerified: boolean;
  salt: string;
  hash: string;
  institute: string;
  group: string;
  fullName?: string;
  phone?: string;
  birthDate?: string;
  createdAt: string;
}

export interface RegisterInput {
  username: string;
  email: string;
  password: string;
  institute?: string;
  group?: string;
  fullName?: string;
  phone?: string;
  birthDate?: string;
}

export type RegisterResult =
  | { user: UserRecord }
  | { error: 'username_taken' | 'email_taken' };

export interface UserStore {
  register(input: RegisterInput): Promise<RegisterResult>;
  /// login = email OR username.
  verify(login: string, password: string): Promise<UserRecord | null>;
  findByEmail(email: string): Promise<UserRecord | undefined>;
  markEmailVerified(email: string): Promise<void>;
  updatePassword(email: string, newPassword: string): Promise<void>;
}

export class JsonUserStore implements UserStore {
  private users: UserRecord[] = [];
  private loaded = false;

  constructor(private readonly file: string) {}

  private async load(): Promise<void> {
    if (this.loaded) return;
    try {
      this.users = JSON.parse(await readFile(this.file, 'utf8')) as UserRecord[];
    } catch {
      this.users = [];
    }
    if (!Array.isArray(this.users)) this.users = [];
    this.loaded = true;
  }

  private async save(): Promise<void> {
    await writeFile(this.file, JSON.stringify(this.users, null, 2));
  }

  async register(input: RegisterInput): Promise<RegisterResult> {
    await this.load();
    if (this.users.some((u) => u.username === input.username)) return { error: 'username_taken' };
    if (this.users.some((u) => u.email === input.email)) return { error: 'email_taken' };
    const { salt, hash } = hashPassword(input.password);
    const rec: UserRecord = {
      username: input.username,
      email: input.email,
      emailVerified: false,
      salt,
      hash,
      institute: input.institute ?? '',
      group: input.group ?? '',
      fullName: input.fullName,
      phone: input.phone,
      birthDate: input.birthDate,
      createdAt: new Date().toISOString(),
    };
    this.users.push(rec);
    await this.save();
    return { user: rec };
  }

  /// Verify credentials by email OR username. Returns the user record, or null.
  async verify(login: string, password: string): Promise<UserRecord | null> {
    await this.load();
    const u = this.users.find((x) => x.username === login || x.email === login);
    if (!u) return null;
    if (!verifyPassword(password, u.salt, u.hash)) return null;
    return u;
  }

  async findByEmail(email: string): Promise<UserRecord | undefined> {
    await this.load();
    return this.users.find((x) => x.email === email);
  }

  async markEmailVerified(email: string): Promise<void> {
    await this.load();
    const u = this.users.find((x) => x.email === email);
    if (u) u.emailVerified = true;
    await this.save();
  }

  async updatePassword(email: string, newPassword: string): Promise<void> {
    await this.load();
    const u = this.users.find((x) => x.email === email);
    if (u) {
      const { salt, hash } = hashPassword(newPassword);
      u.salt = salt;
      u.hash = hash;
    }
    await this.save();
  }
}

//==============================================================================
// Postgres-backed user store (production path). Same contract as JsonUserStore.
//==============================================================================
export class PostgresUserStore implements UserStore {
  private readonly pool: Pool;

  constructor(connectionString: string) {
    this.pool = new Pool({ connectionString });
  }

  private rowToRecord(row: Record<string, unknown>): UserRecord {
    return {
      username: String(row.username),
      email: String(row.email ?? ''),
      emailVerified: Boolean(row.email_verified),
      salt: String(row.salt),
      hash: String(row.hash),
      institute: String(row.institute ?? ''),
      group: String(row.group_name ?? ''),
      fullName: row.full_name ? String(row.full_name) : undefined,
      phone: row.phone ? String(row.phone) : undefined,
      birthDate: row.birth_date
        ? row.birth_date instanceof Date
          ? row.birth_date.toISOString().slice(0, 10)
          : String(row.birth_date)
        : undefined,
      createdAt: row.created_at instanceof Date ? row.created_at.toISOString() : String(row.created_at),
    };
  }

  async register(input: RegisterInput): Promise<RegisterResult> {
    const existing = await this.pool.query(
      'SELECT username, email FROM users WHERE username = $1 OR email = $2',
      [input.username, input.email],
    );
    if (existing.rows.length > 0) {
      const row = existing.rows[0] as { username: string; email: string };
      if (row.username === input.username) return { error: 'username_taken' };
      return { error: 'email_taken' };
    }

    const { salt, hash } = hashPassword(input.password);
    await this.pool.query(
      `INSERT INTO users
         (username, email, salt, hash, institute, group_name, full_name, phone, birth_date, email_verified)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, false)`,
      [
        input.username,
        input.email,
        salt,
        hash,
        input.institute ?? '',
        input.group ?? '',
        input.fullName ?? null,
        input.phone ?? null,
        input.birthDate ?? null,
      ],
    );
    return {
      user: {
        username: input.username,
        email: input.email,
        emailVerified: false,
        salt,
        hash,
        institute: input.institute ?? '',
        group: input.group ?? '',
        fullName: input.fullName,
        phone: input.phone,
        birthDate: input.birthDate,
        createdAt: new Date().toISOString(),
      },
    };
  }

  /// Verify credentials by email OR username. Returns the user record, or null.
  async verify(login: string, password: string): Promise<UserRecord | null> {
    const res = await this.pool.query(
      `SELECT username, email, email_verified, salt, hash, institute, group_name,
              full_name, phone, birth_date, created_at
         FROM users WHERE username = $1 OR email = $1`,
      [login],
    );
    if (res.rows.length === 0) return null;
    const row = res.rows[0] as Record<string, unknown>;
    if (!verifyPassword(password, String(row.salt), String(row.hash))) return null;
    return this.rowToRecord(row);
  }

  async findByEmail(email: string): Promise<UserRecord | undefined> {
    const res = await this.pool.query(
      `SELECT username, email, email_verified, salt, hash, institute, group_name,
              full_name, phone, birth_date, created_at
         FROM users WHERE email = $1`,
      [email],
    );
    if (res.rows.length === 0) return undefined;
    return this.rowToRecord(res.rows[0] as Record<string, unknown>);
  }

  async markEmailVerified(email: string): Promise<void> {
    await this.pool.query('UPDATE users SET email_verified = true WHERE email = $1', [email]);
  }

  async updatePassword(email: string, newPassword: string): Promise<void> {
    const { salt, hash } = hashPassword(newPassword);
    await this.pool.query('UPDATE users SET salt = $2, hash = $3 WHERE email = $1', [email, salt, hash]);
  }

  async close(): Promise<void> {
    await this.pool.end();
  }
}
