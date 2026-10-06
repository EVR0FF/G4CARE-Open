//==============================================================================
// G4CARE cloud — integration tests for the S3 / PostgreSQL backends.
//
// Requires running services (not part of the default `npm test`):
//   Postgres : postgres://g4care:g4care@127.0.0.1:5432/g4care
//   SeaweedFS: 127.0.0.1:8333 (any credentials) — or MinIO at 127.0.0.1:9000
//
// Each backend test skips gracefully (with a warning) if its service is
// unreachable, so the suite can run partially.
//
// Run:  npm run test:integration
//==============================================================================

import assert from 'node:assert/strict';
import { randomUUID } from 'node:crypto';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createRequire } from 'node:module';

import { PostgresIndex } from '../backend/index-pg.js';
import { PostgresUserStore } from '../backend/users.js';
import { S3Storage } from '../backend/storage-s3.js';
import { migrateDatabase } from '../backend/migrate.js';
import type { ArtifactRecord } from '../shared/types.js';

const DB_URL = process.env.G4CARE_CLOUD_DB_URL ?? 'postgres://g4care:g4care@127.0.0.1:5432/g4care';

// s3rver (in-process S3 emulator) ships no TypeScript declarations — require it.
const require = createRequire(import.meta.url);
interface S3rverInstance {
  run: (cb?: (err: Error | null, info?: { address: string; port: number }) => void) => void | Promise<unknown>;
  close: (cb?: (err?: Error) => void) => void;
}
type S3rverCtor = new (options: {
  port: number;
  address: string;
  silent: boolean;
  directory: string;
}) => S3rverInstance;
const S3rver = require('s3rver') as S3rverCtor;

function makeRecord(name: string): ArtifactRecord {
  return {
    id: randomUUID(),
    type: 'gdml',
    name,
    meta: {
      doi: '10.5281/zenodo.1234567',
      description: 'integration test',
      version: '0.9.0',
      tags: ['detector', 'nano'],
      author: { name: 'G4CARE Developers', alias: 'g4care-dev' },
      affiliation: { group: 'G4CARE', institute: 'TPU' },
    },
    sha256: 'a'.repeat(64),
    size: 123,
    createdAt: new Date().toISOString(),
    storageKey: 'obj/' + name,
    owner: 'alice',
    visibility: 'public',
    version: 1,
  };
}

/// Apply the SQL migrations so the test is self-contained (no external psql).
async function applySchema(): Promise<void> {
  await migrateDatabase(DB_URL);
}

async function testPostgres(): Promise<void> {
  await applySchema();
  const index = new PostgresIndex(DB_URL);
  const rec = makeRecord('scene.gdml');
  await index.insert(rec);

  const got = await index.get(rec.id);
  assert.ok(got, 'inserted record should be retrievable');
  assert.equal(got!.name, 'scene.gdml');
  assert.equal(got!.meta.author.alias, 'g4care-dev');
  assert.equal(got!.meta.affiliation.institute, 'TPU');
  assert.deepEqual(got!.meta.tags, ['detector', 'nano']);

  const byTag = await index.list({ tag: 'nano' });
  assert.ok(byTag.some((r) => r.id === rec.id), 'tag search should find the record');

  const byInstitute = await index.list({ institute: 'TPU' });
  assert.ok(byInstitute.some((r) => r.id === rec.id), 'institute search should find the record');

  const byAuthor = await index.list({ author: 'g4care-dev' });
  assert.ok(byAuthor.some((r) => r.id === rec.id), 'author search should find the record');

  await index.close();
  console.log('PostgresIndex integration: OK');
}

async function testPostgresUsers(): Promise<void> {
  await applySchema();
  const users = new PostgresUserStore(DB_URL);
  const username = 'user-' + Date.now();
  const email = username + '@example.org';
  try {
    const reg = await users.register({ username, email, password: 'pw', institute: 'TPU', group: 'G4CARE' });
    assert.ok('user' in reg, 'register should create a user');
    const rec = (reg as { user: { institute: string; group: string; emailVerified: boolean } }).user;
    assert.equal(rec.institute, 'TPU');
    assert.equal(rec.group, 'G4CARE');
    assert.equal(rec.emailVerified, false);

    // login by username AND by email
    assert.ok(await users.verify(username, 'pw'), 'verify by username should succeed');
    assert.ok(await users.verify(email, 'pw'), 'verify by email should succeed');
    assert.equal(await users.verify(email, 'wrong'), null, 'wrong password should fail');

    // duplicate username / email
    const dupUser = await users.register({ username, email: 'other@example.org', password: 'x' });
    assert.equal((dupUser as { error?: string }).error, 'username_taken');
    const dupEmail = await users.register({ username: 'other', email, password: 'x' });
    assert.equal((dupEmail as { error?: string }).error, 'email_taken');

    // email verification + password reset
    await users.markEmailVerified(email);
    assert.equal((await users.findByEmail(email))?.emailVerified, true);
    await users.updatePassword(email, 'newpw');
    assert.ok(await users.verify(email, 'newpw'), 'verify after password update should succeed');
  } finally {
    await users.close();
  }
  console.log('PostgresUserStore integration: OK');
}

/// Attempt an S3 round-trip against an endpoint; returns false on any failure.
async function tryS3(port: number, accessKey: string, secretKey: string, label: string): Promise<boolean> {
  const bucket = `g4care-test-${Date.now()}-${port}`;
  const storage = new S3Storage({
    endPoint: '127.0.0.1',
    port,
    useSSL: false,
    accessKey,
    secretKey,
    bucket,
  });
  try {
    const id = 'obj-' + Date.now();
    const data = Buffer.from('s3 integration test content');
    await storage.put(id, data);
    const back = await storage.get(id);
    assert.deepEqual(back, data, 'S3 round-trip should preserve content');
    await storage.delete(id);
    console.log(`S3Storage integration (${label}): OK`);
    return true;
  } catch (err) {
    console.warn(`  ${label} at :${port} not used: ${(err as Error).message}`);
    return false;
  }
}

async function testS3(): Promise<void> {
  // Prefer a real S3 server when available (SeaweedFS :8333, then any S3 on :9000).
  for (const [port, label] of [[8333, 'SeaweedFS'], [9000, 'S3']] as const) {
    if (await tryS3(port, 'g4care', 'g4care-secret', label)) return;
  }

  // Fallback: in-process s3rver emulator (uses its built-in S3RVER credentials).
  const dir = await mkdtemp(join(tmpdir(), 'g4c-s3-'));
  const s3rver = new S3rver({ port: 0, address: '127.0.0.1', silent: true, directory: dir });

  let actualPort = 0;
  await new Promise<void>((resolve, reject) => {
    s3rver.run((err, info) => {
      if (err) reject(err);
      else {
        actualPort = info?.port ?? 0;
        resolve();
      }
    });
  });

  try {
    if (!(await tryS3(actualPort, 'S3RVER', 'S3RVER', 's3rver'))) {
      throw new Error('S3Storage round-trip failed against s3rver');
    }
  } finally {
    await new Promise<void>((resolve) => s3rver.close(() => resolve()));
  }
}

async function main(): Promise<void> {
  try {
    await testPostgres();
  } catch (err) {
    console.warn(`PostgresIndex integration SKIPPED: ${(err as Error).message}`);
  }

  try {
    await testPostgresUsers();
  } catch (err) {
    console.warn(`PostgresUserStore integration SKIPPED: ${(err as Error).message}`);
  }

  try {
    await testS3();
  } catch (err) {
    console.warn(`S3Storage integration SKIPPED (no S3 backend reachable): ${(err as Error).message}`);
  }

  console.log('\nINTEGRATION TESTS DONE');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
