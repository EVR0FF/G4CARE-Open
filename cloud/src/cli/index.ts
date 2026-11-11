//==============================================================================
// G4CARE - index.ts — Cloud CLI
//
// Cloud CLI
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { readFile, writeFile, rm, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, resolve, join } from 'node:path';
import { homedir } from 'node:os';
import { pathToFileURL } from 'node:url';

import { emptyMeta, stringifyMetaToml, parseMetaToml, stringifyManifest, sha256, deriveMetaPath } from '../shared/manifest.js';
import { scanDependencies } from '../shared/scan.js';
import { packBundle, unpackBundle, readManifestFromBundle } from '../shared/bundle.js';

const SERVER = process.env.G4CARE_CLOUD_SERVER ?? 'http://127.0.0.1:3000';
const TOKEN_FILE = join(homedir(), '.g4care-cloud', 'token');

async function readToken(): Promise<string | null> {
  try {
    const t = (await readFile(TOKEN_FILE, 'utf8')).trim();
    return t || null;
  } catch {
    return null;
  }
}

async function writeToken(token: string): Promise<void> {
  await mkdir(join(homedir(), '.g4care-cloud'), { recursive: true });
  await writeFile(TOKEN_FILE, token);
}

function argValue(args: string[], key: string): string | undefined {
  const i = args.indexOf(key);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : undefined;
}

function usage(): number {
  console.log(`g4care-cloud — G4CARE cloud artifact exchange

Commands:
  login <username> --password <pw>     authenticate (saves token)
  init <file>                          create <file>.meta.toml template
  pack <config> [--out <b>.g4c] [--strict]  bundle a config + its dependencies (--strict fails on missing files)
  push <file|bundle.g4c> [--meta <m>]  upload artifact / bundle
  pull <id> <dest>                     download (bundle → unpack into dir)
  unpack <bundle.g4c> <outDir>         extract a bundle
  verify <file> <sha256>               check file integrity
  list [--q|--tag|--author|--institute|--type]   search artifacts

Env: G4CARE_CLOUD_SERVER (default http://127.0.0.1:3000)`);
  return 0;
}

async function cmdInit(args: string[]): Promise<number> {
  const file = args[0];
  if (!file) {
    console.error('usage: g4care-cloud init <file>');
    return 1;
  }
  const metaPath = deriveMetaPath(file);
  await writeFile(metaPath, stringifyMetaToml(emptyMeta()));
  console.log(`created ${metaPath}`);
  return 0;
}

async function cmdPack(args: string[]): Promise<number> {
  const config = args[0];
  if (!config) {
    console.error('usage: g4care-cloud pack <config> [--out <bundle.g4c>] [--strict]');
    return 1;
  }
  const outPath = argValue(args, '--out') ?? `${config}.g4c`;
  const baseDir = dirname(resolve(config));
  const metaPath = argValue(args, '--meta') ??
    (existsSync(deriveMetaPath(config)) ? deriveMetaPath(config) : undefined);
  const meta = metaPath ? parseMetaToml(await readFile(metaPath, 'utf8')) : emptyMeta();

  const strict = args.includes('--strict');
  let files: string[];
  try {
    files = await scanDependencies(config, baseDir, new Set(), strict);
  } catch (err) {
    console.error(`pack failed: ${(err as Error).message}`);
    return 1;
  }
  const list = await packBundle(baseDir, files, meta, outPath);
  console.log(`packed ${list.length} file(s) into ${outPath}`);
  for (const f of list) console.log(`  ${f.path}  (${f.sha256.slice(0, 12)}…)`);
  return 0;
}

async function cmdLogin(args: string[]): Promise<number> {
  const username = args[0];
  const password = argValue(args, '--password') ?? process.env.G4CARE_CLOUD_PASSWORD;
  if (!username || !password) {
    console.error('usage: g4care-cloud login <username> --password <pw>');
    return 1;
  }
  const server = argValue(args, '--server') ?? SERVER;
  const res = await fetch(`${server}/auth/login`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    console.error('login failed:', body);
    return 1;
  }
  await writeToken((body as { token: string }).token);
  console.log(`logged in as ${username}`);
  return 0;
}

async function cmdPush(args: string[]): Promise<number> {
  const file = args[0];
  if (!file) {
    console.error('usage: g4care-cloud push <file|bundle.g4c> [--meta <meta.toml>]');
    return 1;
  }
  const server = argValue(args, '--server') ?? SERVER;
  const buf = await readFile(file);

  const isBundle = file.endsWith('.g4c');
  let metaText = '';
  if (isBundle) {
    const { meta, files } = await readManifestFromBundle(file);
    metaText = stringifyManifest(meta, files); // full manifest (meta + file list)
  } else {
    const metaPath = argValue(args, '--meta') ??
      (existsSync(deriveMetaPath(file)) ? deriveMetaPath(file) : undefined);
    metaText = metaPath ? await readFile(metaPath, 'utf8') : '';
  }

  const form = new FormData();
  form.append('file', new Blob([buf]), file.split('/').pop() ?? file);
  if (metaText) form.append('meta', metaText);

  const token = (await readToken()) ?? process.env.G4CARE_CLOUD_TOKEN ?? '';
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;

  const res = await fetch(`${server}/artifacts`, { method: 'POST', headers, body: form });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    console.error('push failed:', body);
    return 1;
  }
  console.log(JSON.stringify(body, null, 2));
  return 0;
}

async function cmdPull(args: string[]): Promise<number> {
  const id = args[0];
  const dest = args[1];
  if (!id || !dest) {
    console.error('usage: g4care-cloud pull <id> <dest>');
    return 1;
  }
  const server = argValue(args, '--server') ?? SERVER;

  const token = (await readToken()) ?? process.env.G4CARE_CLOUD_TOKEN ?? '';
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;

  const metaRes = await fetch(`${server}/artifacts/${id}`, { headers });
  const rec = await metaRes.json().catch(() => ({})) as { type?: string };
  const isBundle = rec.type === 'bundle';

  const dl = await fetch(`${server}/artifacts/${id}/download`, { headers });
  if (!dl.ok) {
    console.error('pull failed:', dl.status);
    return 1;
  }
  const buf = Buffer.from(await dl.arrayBuffer());

  if (isBundle) {
    const tmpTar = `${dest}.g4c.tmp`;
    await writeFile(tmpTar, buf);
    await unpackBundle(tmpTar, dest);
    await rm(tmpTar, { force: true });
    console.log(`unpacked bundle into ${dest}`);
  } else {
    await writeFile(dest, buf);
    console.log(`downloaded ${dest} (${buf.length} bytes)`);
  }
  return 0;
}

async function cmdUnpack(args: string[]): Promise<number> {
  const tarPath = args[0];
  const outDir = args[1];
  if (!tarPath || !outDir) {
    console.error('usage: g4care-cloud unpack <bundle.g4c> <outDir>');
    return 1;
  }
  await unpackBundle(tarPath, outDir);
  console.log(`unpacked ${tarPath} into ${outDir}`);
  return 0;
}

async function cmdVerify(args: string[]): Promise<number> {
  const file = args[0];
  const expected = args[1];
  if (!file || !expected) {
    console.error('usage: g4care-cloud verify <file> <sha256>');
    return 1;
  }
  const actual = await sha256(await readFile(file));
  if (actual === expected.toLowerCase()) {
    console.log('OK');
    return 0;
  }
  console.error(`MISMATCH\n  expected ${expected}\n  actual   ${actual}`);
  return 1;
}

async function cmdList(args: string[]): Promise<number> {
  const server = argValue(args, '--server') ?? SERVER;
  const params = new URLSearchParams();
  for (const k of ['q', 'tag', 'author', 'institute', 'type']) {
    const v = argValue(args, `--${k}`);
    if (v) params.set(k, v);
  }
  const qs = params.toString();
  const token = (await readToken()) ?? process.env.G4CARE_CLOUD_TOKEN ?? '';
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${server}/artifacts${qs ? `?${qs}` : ''}`, { headers });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    console.error('list failed:', body);
    return 1;
  }
  console.log(JSON.stringify(body, null, 2));
  return 0;
}

async function main(argv: string[]): Promise<number> {
  const [cmd, ...args] = argv;
  switch (cmd) {
    case 'login': return cmdLogin(args);
    case 'init': return cmdInit(args);
    case 'pack': return cmdPack(args);
    case 'push': return cmdPush(args);
    case 'pull': return cmdPull(args);
    case 'unpack': return cmdUnpack(args);
    case 'verify': return cmdVerify(args);
    case 'list': return cmdList(args);
    case 'help':
    case undefined:
      return usage();
    default:
      console.error(`unknown command: ${cmd}`);
      return usage();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main(process.argv.slice(2)).then((code) => process.exit(code)).catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
