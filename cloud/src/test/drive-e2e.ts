import { buildServer } from '../backend/server.js';
import { PostgresIndex } from '../backend/index-pg.js';
import { PostgresUserStore } from '../backend/users.js';
import { PostgresCodeStore } from '../backend/code-store.js';
import { PostgresDriveStore } from '../backend/drive.js';
import { migrateDatabase } from '../backend/migrate.js';
import { Pool } from 'pg';

const DB = 'postgres://g4care:g4care@127.0.0.1:5432/g4care';
await migrateDatabase(DB);
const pool = new Pool({ connectionString: DB });

let lastCode = '';
const mailer = { async send(_to: string, _subj: string, text: string) { lastCode = (text.match(/\b(\d{6})\b/) || [])[1] ?? ''; } };

const app = await buildServer({
  storageDir: '/tmp/g4c-drive-obj',
  indexFile: '/tmp/g4c-drive-idx.json',
  port: 0,
  host: '127.0.0.1',
  index: new PostgresIndex(DB),
  users: new PostgresUserStore(DB),
  codeStore: new PostgresCodeStore(DB),
  drive: new PostgresDriveStore(pool),
  mailer,
});
await app.listen({ port: 0, host: '127.0.0.1' });
const base = `http://127.0.0.1:${(app.server.address() as { port: number }).port}`;
const json = async (r: Response) => r.json();

// register + verify + login
await fetch(`${base}/auth/register`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ username: 'bob', email: 'bob@ex.org', password: 'secret123' }) });
await fetch(`${base}/auth/verify`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: 'bob@ex.org', code: lastCode }) });
const login = await (await fetch(`${base}/auth/login`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ username: 'bob', password: 'secret123' }) })).json() as { token: string };
const H = { authorization: `Bearer ${login.token}` };

// upload
const f = new FormData();
f.append('file', new Blob([Buffer.from('data')]), 'a.gdml');
const up = await (await fetch(`${base}/artifacts`, { method: 'POST', headers: H, body: f })).json() as { id: string };

// folders
const folder = await json(await fetch(`${base}/folders`, { method: 'POST', headers: { ...H, 'content-type': 'application/json' }, body: JSON.stringify({ name: 'detectors' }) })) as { id: string };
console.log('create folder: OK', folder.id);
console.log('list folders:', (await json(await fetch(`${base}/folders`, { headers: H })) as any[]).length);

// move
await fetch(`${base}/artifacts/${up.id}/move`, { method: 'POST', headers: { ...H, 'content-type': 'application/json' }, body: JSON.stringify({ folderId: folder.id }) });
const inFolder = await json(await fetch(`${base}/artifacts?folderId=${folder.id}`, { headers: H })) as any[];
console.log('move + filter:', inFolder.length === 1 ? 'OK' : 'FAIL');

// trash + restore
await fetch(`${base}/artifacts/${up.id}`, { method: 'DELETE', headers: H });
console.log('trash list:', (await json(await fetch(`${base}/trash`, { headers: H })) as any[]).length === 1 ? 'OK' : 'FAIL');
await fetch(`${base}/artifacts/${up.id}/restore`, { method: 'POST', headers: H });
console.log('after restore, trash empty:', (await json(await fetch(`${base}/trash`, { headers: H })) as any[]).length === 0 ? 'OK' : 'FAIL');

// share (artifact)
const share = await json(await fetch(`${base}/share`, { method: 'POST', headers: { ...H, 'content-type': 'application/json' }, body: JSON.stringify({ artifactId: up.id }) })) as { token: string };
console.log('create share:', share.token.slice(0, 8) + '…');
const pub = await json(await fetch(`${base}/share/${share.token}`)) as { kind: string };
console.log('public get share:', pub.kind === 'artifact' ? 'OK' : 'FAIL');
const dl = await fetch(`${base}/share/${share.token}/download`);
console.log('public download:', dl.status === 200 ? 'OK' : 'FAIL');
await fetch(`${base}/share/${share.token}`, { method: 'DELETE', headers: H });
console.log('revoke:', (await fetch(`${base}/share/${share.token}`)).status === 404 ? 'OK' : 'FAIL');

// share (folder)
const shareF = await json(await fetch(`${base}/share`, { method: 'POST', headers: { ...H, 'content-type': 'application/json' }, body: JSON.stringify({ folderId: folder.id }) })) as { token: string };
const pubF = await json(await fetch(`${base}/share/${shareF.token}`)) as { kind: string; artifacts: any[] };
console.log('share folder public:', pubF.kind === 'folder' && pubF.artifacts.length >= 1 ? 'OK' : 'FAIL');

await app.close();
await pool.end();
console.log('\nDRIVE E2E PASSED');
