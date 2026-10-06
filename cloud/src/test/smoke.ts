//==============================================================================
// G4CARE cloud — smoke test.
//
// 1. Unit-checks the manifest module (parse / stringify round-trip, detectType,
//    sha256).
// 2. Round-trips an artifact through the real Fastify server (upload → search →
//    metadata → download → verify) using Node's native fetch/FormData/Blob.
//
// Run:  npm test   (from the cloud/ directory)
//==============================================================================

import assert from 'node:assert/strict';
import { mkdtemp, writeFile, readFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import {
  parseMetaToml,
  stringifyMetaToml,
  stringifyManifest,
  detectType,
  sha256,
  sha256Sync,
} from '../shared/manifest.js';
import { scanDependencies } from '../shared/scan.js';
import { packBundle, unpackBundle, readManifestFromBundle } from '../shared/bundle.js';
import { buildServer } from '../backend/server.js';
import type { EmailSender } from '../backend/mailer.js';

const SAMPLE_META = `doi         = "10.5281/zenodo.1234567"
info_url    = "https://example.org/a"
description = "Silicon detector"
version     = "0.9.0"
created     = "2026-09-30"
tags        = ["detector", "nano"]

[author]
name  = "G4CARE Developers"
alias = "g4care-dev"

[affiliation]
group      = "G4CARE"
institute  = "TPU"
department = "Lab 42"
`;

class CapturingMailer implements EmailSender {
  lastCode = '';
  async send(_to: string, _subject: string, text: string): Promise<void> {
    const m = text.match(/\b(\d{6})\b/);
    if (m) this.lastCode = m[1];
  }
}

async function registerAndLogin(base: string, username: string, mailer: CapturingMailer): Promise<string> {
  const email = `${username}@example.org`;
  const reg = await fetch(`${base}/auth/register`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, email, password: 'secret123' }),
  });
  assert.equal(reg.status, 201, 'register should succeed');
  const ver = await fetch(`${base}/auth/verify`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email, code: mailer.lastCode }),
  });
  assert.equal(ver.status, 200, 'verify should succeed');
  const res = await fetch(`${base}/auth/login`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, password: 'secret123' }),
  });
  assert.equal(res.status, 200, 'login should succeed');
  const body = (await res.json()) as { token: string };
  return body.token;
}

async function testManifest() {
  const m = parseMetaToml(SAMPLE_META);
  assert.equal(m.doi, '10.5281/zenodo.1234567');
  assert.equal(m.description, 'Silicon detector');
  assert.deepEqual(m.tags, ['detector', 'nano']);
  assert.equal(m.author.name, 'G4CARE Developers');
  assert.equal(m.affiliation.institute, 'TPU');
  assert.equal(m.affiliation.department, 'Lab 42');

  // Round-trip: serialize and re-parse.
  const m2 = parseMetaToml(stringifyMetaToml(m));
  assert.equal(m2.author.alias, 'g4care-dev');
  assert.deepEqual(m2.tags, ['detector', 'nano']);

  assert.equal(detectType('scene.gdml'), 'gdml');
  assert.equal(detectType('config.yaml'), 'yaml');
  assert.equal(detectType('libPhys.so'), 'so');
  assert.equal(detectType('vis.mac'), 'mac');

  assert.equal(sha256Sync('abc'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  assert.equal(await sha256('abc'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');

  console.log('manifest unit checks: OK');
}

async function testServerRoundTrip() {
  const dir = await mkdtemp(join(tmpdir(), 'g4care-cloud-'));
  const mailer = new CapturingMailer();
  const app = await buildServer({
    storageDir: join(dir, 'objects'),
    indexFile: join(dir, 'index.json'),
    port: 0,
    host: '127.0.0.1',
    mailer,
  });

  await app.listen({ port: 0, host: '127.0.0.1' });
  const port = (app.server.address() as { port: number }).port;
  const base = `http://127.0.0.1:${port}`;
  const token = await registerAndLogin(base, 'roundtrip-user', mailer);

  // Upload.
  const content = Buffer.from('<gdml>sample geometry</gdml>');
  const form = new FormData();
  form.append('file', new Blob([content]), 'scene.gdml');
  form.append('meta', SAMPLE_META);
  const up = await fetch(`${base}/artifacts`, {
    method: 'POST',
    headers: { authorization: `Bearer ${token}` },
    body: form,
  });
  assert.equal(up.status, 201);
  const rec = await up.json() as Record<string, any>;
  assert.equal(rec.name, 'scene.gdml');
  assert.equal(rec.type, 'gdml');
  assert.equal(rec.sha256, sha256Sync(content));
  assert.equal(rec.meta.description, 'Silicon detector');

  // Search by tag / institute.
  const list = await (await fetch(`${base}/artifacts?tag=detector`)).json() as any[];
  assert.equal(list.length, 1);
  assert.equal(list[0].id, rec.id);

  const listInst = await (await fetch(`${base}/artifacts?institute=TPU`)).json() as any[];
  assert.equal(listInst.length, 1);

  // Metadata.
  const get = await fetch(`${base}/artifacts/${rec.id}`);
  assert.equal(get.status, 200);
  assert.equal(((await get.json()) as any).id, rec.id);

  // Download + verify.
  const dl = await fetch(`${base}/artifacts/${rec.id}/download`);
  assert.equal(dl.status, 200);
  const downloaded = Buffer.from(await dl.arrayBuffer());
  assert.deepEqual(downloaded, content);
  assert.equal(dl.headers.get('x-g4care-sha256'), rec.sha256);

  await app.close();
  console.log('server round-trip: OK');
}

async function testBundle() {
  const dir = await mkdtemp(join(tmpdir(), 'g4c-bundle-'));
  await writeFile(join(dir, 'detector.yaml'), 'INCLUDE:\n  - modular/geometry.yaml\nGEOMETRY:\n  FILE: scene.gdml\n');
  await mkdir(join(dir, 'modular'), { recursive: true });
  await writeFile(join(dir, 'modular', 'geometry.yaml'), 'WORLD:\n  shape: box\n');
  await writeFile(join(dir, 'scene.gdml'), '<gdml/>');

  const files = await scanDependencies(join(dir, 'detector.yaml'), dir);
  assert.ok(files.includes('detector.yaml'));
  assert.ok(files.includes('modular/geometry.yaml'));
  assert.ok(files.includes('scene.gdml'));

  const tarball = join(dir, 'detector.g4c');
  const list = await packBundle(dir, files, parseMetaToml(SAMPLE_META), tarball);
  assert.equal(list.length, 3);

  const m = await readManifestFromBundle(tarball);
  assert.equal(m.files.length, 3);
  assert.equal(m.meta.description, 'Silicon detector');

  const outDir = join(dir, 'out');
  await unpackBundle(tarball, outDir);
  assert.ok((await readFile(join(outDir, 'detector.yaml'), 'utf8')).includes('INCLUDE'));
  assert.ok(existsSync(join(outDir, 'modular', 'geometry.yaml')));
  assert.ok(existsSync(join(outDir, 'scene.gdml')));

  console.log('bundle pack/unpack: OK');
}

async function testServerBundle() {
  const dir = await mkdtemp(join(tmpdir(), 'g4c-srv-'));
  const mailer = new CapturingMailer();
  const app = await buildServer({
    storageDir: join(dir, 'objects'),
    indexFile: join(dir, 'index.json'),
    port: 0,
    host: '127.0.0.1',
    mailer,
  });
  await app.listen({ port: 0, host: '127.0.0.1' });
  const port = (app.server.address() as { port: number }).port;
  const base = `http://127.0.0.1:${port}`;
  const token = await registerAndLogin(base, 'bundle-user', mailer);

  const src = await mkdtemp(join(tmpdir(), 'g4c-src-'));
  await writeFile(join(src, 'detector.yaml'), 'INCLUDE:\n  - sub.yaml\n');
  await writeFile(join(src, 'sub.yaml'), 'A: 1\n');
  const files = await scanDependencies(join(src, 'detector.yaml'), src);
  const tarball = join(src, 'detector.g4c');
  await packBundle(src, files, parseMetaToml(SAMPLE_META), tarball);

  const { meta, files: manifestFiles } = await readManifestFromBundle(tarball);
  const form = new FormData();
  form.append('file', new Blob([await readFile(tarball)]), 'detector.g4c');
  form.append('meta', stringifyManifest(meta, manifestFiles));

  const up = await fetch(`${base}/artifacts`, {
    method: 'POST',
    headers: { authorization: `Bearer ${token}` },
    body: form,
  });
  assert.equal(up.status, 201);
  const rec = await up.json() as Record<string, any>;
  assert.equal(rec.type, 'bundle');
  assert.equal(rec.files.length, 2);

  const dl = await fetch(`${base}/artifacts/${rec.id}/download`);
  assert.equal(dl.status, 200);
  assert.deepEqual(Buffer.from(await dl.arrayBuffer()), await readFile(tarball));

  await app.close();
  console.log('server bundle round-trip: OK');
}

async function testAuth() {
  const dir = await mkdtemp(join(tmpdir(), 'g4c-auth-'));
  const mailer = new CapturingMailer();
  const app = await buildServer({
    storageDir: join(dir, 'objects'),
    indexFile: join(dir, 'index.json'),
    port: 0,
    host: '127.0.0.1',
    mailer,
  });
  await app.listen({ port: 0, host: '127.0.0.1' });
  const port = (app.server.address() as { port: number }).port;
  const base = `http://127.0.0.1:${port}`;

  const token = await registerAndLogin(base, 'alice', mailer);
  assert.ok(token);
  const authHeaders = { authorization: `Bearer ${token}` };
  const content = Buffer.from('secret-content');

  // private artifact
  const f1 = new FormData();
  f1.append('file', new Blob([content]), 'secret.gdml');
  f1.append('meta', SAMPLE_META);
  f1.append('visibility', 'private');
  const up1 = await fetch(`${base}/artifacts`, { method: 'POST', headers: authHeaders, body: f1 });
  assert.equal(up1.status, 201);
  const rec1 = await up1.json() as Record<string, any>;
  assert.equal(rec1.owner, 'alice');
  assert.equal(rec1.visibility, 'private');
  assert.equal(rec1.version, 1);

  // public artifact
  const f2 = new FormData();
  f2.append('file', new Blob([content]), 'public.gdml');
  const up2 = await fetch(`${base}/artifacts`, { method: 'POST', headers: authHeaders, body: f2 });
  assert.equal(up2.status, 201);
  const rec2 = await up2.json() as Record<string, any>;
  assert.equal(rec2.visibility, 'public');

  // anon list → public only
  const anon = await (await fetch(`${base}/artifacts`)).json() as any[];
  assert.ok(anon.some((r) => r.name === 'public.gdml'));
  assert.ok(!anon.some((r) => r.name === 'secret.gdml'));

  // authed list → both
  const authed = await (await fetch(`${base}/artifacts`, { headers: authHeaders })).json() as any[];
  assert.ok(authed.some((r) => r.name === 'secret.gdml'));

  // anon download of private → 403
  assert.equal((await fetch(`${base}/artifacts/${rec1.id}/download`)).status, 403);

  // versioning: re-upload secret.gdml → version 2, parent = v1
  const f3 = new FormData();
  f3.append('file', new Blob([content]), 'secret.gdml');
  f3.append('visibility', 'private');
  const up3 = await fetch(`${base}/artifacts`, { method: 'POST', headers: authHeaders, body: f3 });
  assert.equal(up3.status, 201);
  const rec3 = await up3.json() as Record<string, any>;
  assert.equal(rec3.version, 2);
  assert.equal(rec3.parentId, rec1.id);

  // upload without auth → 401
  const f4 = new FormData();
  f4.append('file', new Blob([content]), 'nope.gdml');
  assert.equal((await fetch(`${base}/artifacts`, { method: 'POST', body: f4 })).status, 401);

  await app.close();
  console.log('auth/visibility/versioning: OK');
}

async function main() {
  await testManifest();
  await testServerRoundTrip();
  await testBundle();
  await testServerBundle();
  await testAuth();
  console.log('\nALL SMOKE TESTS PASSED');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
