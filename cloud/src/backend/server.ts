//==============================================================================
// G4CARE - server.ts — Cloud backend: HTTP server
//
// Cloud backend: HTTP server
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import Fastify, { type FastifyInstance } from 'fastify';
import multipart from '@fastify/multipart';
import { randomUUID } from 'node:crypto';
import { mkdir, mkdtemp, writeFile, readFile, rm } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { pathToFileURL } from 'node:url';

import type { ArtifactRecord, ArtifactType, SearchFilter } from '../shared/types.js';
import { emptyMeta, parseManifest, detectType, sha256Sync } from '../shared/manifest.js';
import { FileStorage, type ObjectStore } from './storage.js';
import { JsonIndex, type MetadataIndex, type ArtifactPatch } from './index.js';
import { S3Storage } from './storage-s3.js';
import { PostgresIndex } from './index-pg.js';
import { signToken, verifyToken } from './auth.js';
import { JsonUserStore, PostgresUserStore, type UserStore } from './users.js';
import { migrateDatabase } from './migrate.js';
import { Pool } from 'pg';
import { PostgresDriveStore, MemoryDriveStore, type DriveStore } from './drive.js';
import { generateCode, hashCode, CODE_TTL_SECONDS } from './otp.js';
import { mailerFromEnv, type EmailSender } from './mailer.js';
import { InMemoryCodeStore, PostgresCodeStore, type CodeStore } from './code-store.js';

export interface CloudConfig {
  storageDir: string;
  indexFile: string;
  port: number;
  host: string;
  usersFile?: string;
  jwtSecret?: string;
  storage?: ObjectStore;   // optional injection (defaults to FileStorage)
  index?: MetadataIndex;   // optional injection (defaults to JsonIndex)
  users?: UserStore;       // optional injection (defaults to JsonUserStore)
  codeStore?: CodeStore;   // optional injection (defaults to InMemoryCodeStore)
  mailer?: EmailSender;    // optional injection (defaults to SMTP/console from env)
  drive?: DriveStore;      // optional injection (folders + share links)
}

const VALID_TYPES = new Set<string>(['yaml', 'gdml', 'so', 'mac', 'other']);

export async function buildServer(cfg: CloudConfig): Promise<FastifyInstance> {
  const storage: ObjectStore = cfg.storage ?? new FileStorage(cfg.storageDir);
  const index: MetadataIndex = cfg.index ?? new JsonIndex(cfg.indexFile);

  if (!cfg.storage) await mkdir(cfg.storageDir, { recursive: true });
  if (!cfg.index) await mkdir(dirname(cfg.indexFile), { recursive: true });

  const app = Fastify({ logger: true });
  await app.register(multipart, { limits: { fileSize: 512 * 1024 * 1024 } });

  // ── Health check ──
  app.get('/health', async () => ({ status: 'ok' }));

  // ── Auth state ──
  const jwtSecret = cfg.jwtSecret ?? process.env.G4CARE_CLOUD_JWT_SECRET ?? 'dev-secret-change-me';
  const usersFile = cfg.usersFile ?? join(dirname(cfg.indexFile), 'users.json');
  const users: UserStore = cfg.users ?? new JsonUserStore(usersFile);
  const codeStore: CodeStore = cfg.codeStore ?? new InMemoryCodeStore();
  const mailer: EmailSender = cfg.mailer ?? mailerFromEnv();
  const drive: DriveStore = cfg.drive ?? new MemoryDriveStore();

  type AuthedUser = Record<string, unknown> | null;
  app.decorateRequest('user', null);
  app.addHook('preHandler', async (req) => {
    let token: string | undefined;
    const auth = req.headers.authorization;
    if (auth && auth.startsWith('Bearer ')) token = auth.slice(7);
    if (!token) {
      const q = (req.query as Record<string, unknown> | undefined)?.token;
      if (typeof q === 'string' && q) token = q;
    }
    if (token) {
      const payload = verifyToken(token, jwtSecret);
      if (payload) (req as unknown as { user: AuthedUser }).user = payload;
    }
  });

  const canAccess = (rec: ArtifactRecord, user: AuthedUser): boolean => {
    if (rec.deletedAt) return false;
    if (rec.visibility !== 'private') return true;
    return !!user && rec.owner === String((user as { username?: string }).username ?? '');
  };

  // ── Auth routes ──
  const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

  const sendCode = async (email: string, purpose: 'verify_email' | 'reset_password') => {
    const code = generateCode();
    await codeStore.upsert(email, purpose, hashCode(code), CODE_TTL_SECONDS);
    try {
      await mailer.send(
        email,
        purpose === 'verify_email' ? 'G4CARE verification code' : 'G4CARE password reset code',
        `Your code is ${code}. It expires in 15 minutes.`,
      );
    } catch (e) {
      console.error('failed to send email:', (e as Error).message);
    }
  };

  app.post('/auth/register', async (req, reply) => {
    const body = req.body as {
      username?: string; email?: string; password?: string;
      institute?: string; group?: string; fullName?: string; phone?: string; birthDate?: string;
    } | undefined;
    if (!body?.username || !body?.email || !body?.password) {
      return reply.code(400).send({ error: 'username, email and password are required' });
    }
    if (!EMAIL_RE.test(body.email)) {
      return reply.code(400).send({ error: 'invalid email address' });
    }
    const res = await users.register({
      username: body.username,
      email: body.email,
      password: body.password,
      institute: body.institute,
      group: body.group,
      fullName: body.fullName,
      phone: body.phone,
      birthDate: body.birthDate,
    });
    if ('error' in res) {
      return reply.code(409).send({
        error: res.error === 'email_taken' ? 'email already registered' : 'username already taken',
      });
    }
    await sendCode(body.email, 'verify_email');
    return reply.code(201).send({ username: res.user.username, email: res.user.email, verified: false });
  });

  app.post('/auth/verify', async (req, reply) => {
    const body = req.body as { email?: string; code?: string } | undefined;
    if (!body?.email || !body?.code) {
      return reply.code(400).send({ error: 'email and code are required' });
    }
    const ok = await codeStore.verify(body.email, 'verify_email', hashCode(body.code));
    if (!ok) return reply.code(400).send({ error: 'invalid or expired code' });
    await users.markEmailVerified(body.email);
    return { verified: true };
  });

  app.post('/auth/resend', async (req, reply) => {
    const body = req.body as { email?: string } | undefined;
    if (!body?.email) return reply.code(400).send({ error: 'email is required' });
    const user = await users.findByEmail(body.email);
    if (!user) return { sent: true }; // don't leak whether the email exists
    if (user.emailVerified) return reply.code(409).send({ error: 'email already verified' });
    await sendCode(body.email, 'verify_email');
    return { sent: true };
  });

  app.post('/auth/forgot', async (req, reply) => {
    const body = req.body as { email?: string } | undefined;
    if (!body?.email) return reply.code(400).send({ error: 'email is required' });
    const user = await users.findByEmail(body.email);
    if (user) await sendCode(body.email, 'reset_password');
    return { sent: true }; // always 200 to avoid user enumeration
  });

  app.post('/auth/reset', async (req, reply) => {
    const body = req.body as { email?: string; code?: string; newPassword?: string } | undefined;
    if (!body?.email || !body?.code || !body?.newPassword) {
      return reply.code(400).send({ error: 'email, code and newPassword are required' });
    }
    const ok = await codeStore.verify(body.email, 'reset_password', hashCode(body.code));
    if (!ok) return reply.code(400).send({ error: 'invalid or expired code' });
    await users.updatePassword(body.email, body.newPassword);
    return { reset: true };
  });

  app.post('/auth/login', async (req, reply) => {
    const body = req.body as { login?: string; email?: string; username?: string; password?: string } | undefined;
    const login = body?.login ?? body?.email ?? body?.username;
    if (!login || !body?.password) {
      return reply.code(400).send({ error: 'login and password are required' });
    }
    const rec = await users.verify(login, body.password);
    if (!rec) return reply.code(401).send({ error: 'invalid credentials' });
    if (!rec.emailVerified) return reply.code(403).send({ error: 'email not verified' });
    const token = signToken(
      { username: rec.username, email: rec.email, institute: rec.institute, group: rec.group },
      jwtSecret,
      60 * 60 * 24,
    );
    return { token, username: rec.username, email: rec.email };
  });

  // ── Upload (auth required) ──
  app.post('/artifacts', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });

    let fileBuf: Buffer | null = null;
    let filename = 'artifact';
    let metaText = '';
    let visibility: 'public' | 'private' = 'public';

    for await (const part of req.parts()) {
      if (part.type === 'file') {
        filename = part.filename || filename;
        const chunks: Buffer[] = [];
        for await (const chunk of part.file) chunks.push(chunk as Buffer);
        fileBuf = Buffer.concat(chunks);
      } else if (part.fieldname === 'meta') {
        metaText = String((part as { value?: unknown }).value ?? '');
      } else if (part.fieldname === 'visibility') {
        if (String((part as { value?: unknown }).value ?? '') === 'private') visibility = 'private';
      }
    }

    if (!fileBuf) {
      return reply.code(400).send({ error: 'file part is required' });
    }

    const username = String((user as { username?: string }).username ?? '');
    const parsed = metaText ? parseManifest(metaText) : { meta: emptyMeta(), files: [] };
    const isBundle = parsed.files.length > 0 || /\.g4c$/i.test(filename);

    // Simple versioning: prior versions of the same name by the same owner.
    const prior = (await index.list({})).filter((r) => r.name === filename && r.owner === username);
    const version = prior.length + 1;
    const parentId = prior.length > 0 ? prior[prior.length - 1].id : undefined;

    const id = randomUUID();
    const rec: ArtifactRecord = {
      id,
      type: isBundle ? 'bundle' : detectType(filename),
      name: filename,
      meta: parsed.meta,
      sha256: sha256Sync(fileBuf),
      size: fileBuf.length,
      createdAt: new Date().toISOString(),
      storageKey: `obj/${filename}`,
      files: isBundle ? parsed.files : undefined,
      owner: username,
      visibility,
      version,
      parentId,
    };

    await storage.put(id, fileBuf);
    await index.insert(rec);

    return reply.code(201).send(rec);
  });

  // ── Search / list ──
  app.get<{ Querystring: SearchFilter }>('/artifacts', async (req) => {
    const q = req.query.q;
    const typeRaw = req.query.type;
    const type = typeRaw && VALID_TYPES.has(String(typeRaw)) ? (String(typeRaw) as ArtifactType) : undefined;
    const filter: SearchFilter = {
      q: q ? String(q) : undefined,
      tag: req.query.tag ? String(req.query.tag) : undefined,
      author: req.query.author ? String(req.query.author) : undefined,
      institute: req.query.institute ? String(req.query.institute) : undefined,
      type,
      folderId: req.query.folderId ? String(req.query.folderId) : undefined,
    };
    const user = (req as unknown as { user: AuthedUser }).user;
    const all = await index.list(filter);
    return all.filter((r) => canAccess(r, user));
  });

  // ── Metadata ──
  app.get<{ Params: { id: string } }>('/artifacts/:id', async (req, reply) => {
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (!canAccess(rec, (req as unknown as { user: AuthedUser }).user)) {
      return reply.code(403).send({ error: 'forbidden' });
    }
    return rec;
  });

  // ── Version chain (same name, ascending version numbers) ──
  app.get<{ Params: { id: string } }>('/artifacts/:id/versions', async (req, reply) => {
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (!canAccess(rec, (req as unknown as { user: AuthedUser }).user)) {
      return reply.code(403).send({ error: 'forbidden' });
    }
    const versions: ArtifactRecord[] = [rec];
    const seen = new Set<string>([rec.id]);
    let cur = rec;
    while (cur.parentId && !seen.has(cur.parentId)) {
      const prev = await index.get(cur.parentId);
      if (!prev) break;
      seen.add(prev.id);
      versions.push(prev);
      cur = prev;
    }
    return versions.sort((a, b) => (b.version ?? 0) - (a.version ?? 0));
  });

  // ── Download ──
  app.get<{ Params: { id: string } }>('/artifacts/:id/download', async (req, reply) => {
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (!canAccess(rec, (req as unknown as { user: AuthedUser }).user)) {
      return reply.code(403).send({ error: 'forbidden' });
    }
    const buf = await storage.get(rec.id);
    reply.header('Content-Type', 'application/octet-stream');
    reply.header('Content-Disposition', `attachment; filename="${rec.name}"`);
    reply.header('X-G4CARE-SHA256', rec.sha256);
    return reply.send(buf);
  });

  // ── Delete → trash (owner only, soft delete) ──
  app.delete<{ Params: { id: string } }>('/artifacts/:id', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (rec.owner !== String((user as { username?: string }).username ?? '')) {
      return reply.code(403).send({ error: 'only the owner can delete' });
    }
    await index.remove(rec.id);
    return { deleted: true, trashed: true };
  });

  // ── Permanent delete (owner only) ──
  app.delete<{ Params: { id: string } }>('/artifacts/:id/permanent', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (rec.owner !== String((user as { username?: string }).username ?? '')) {
      return reply.code(403).send({ error: 'only the owner can delete' });
    }
    await index.purge(rec.id);
    try { await storage.delete(rec.id); } catch { /* ignore storage errors */ }
    return { deleted: true };
  });

  // ── Restore from trash (owner only) ──
  app.post<{ Params: { id: string } }>('/artifacts/:id/restore', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (rec.owner !== String((user as { username?: string }).username ?? '')) {
      return reply.code(403).send({ error: 'only the owner can restore' });
    }
    await index.restore(rec.id);
    return { restored: true };
  });

  // ── Trash list (owner only) ──
  app.get('/trash', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    return index.trash(String((user as { username?: string }).username ?? ''));
  });

  // ── Folders ──
  app.post('/folders', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const body = req.body as { name?: string; parentId?: string } | undefined;
    if (!body?.name) return reply.code(400).send({ error: 'name is required' });
    const username = String((user as { username?: string }).username ?? '');
    const folder = await drive.createFolder(body.name, username, body.parentId);
    return reply.code(201).send(folder);
  });

  app.get('/folders', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    return drive.listFolders(String((user as { username?: string }).username ?? ''));
  });

  app.delete<{ Params: { id: string } }>('/folders/:id', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    // Soft delete: move to trash so files inside can be restored.
    const ok = await drive.deleteFolder(req.params.id, String((user as { username?: string }).username ?? ''));
    if (!ok) return reply.code(404).send({ error: 'not found' });
    return { trashed: true };
  });

  app.get('/folders/trash', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    return drive.listTrashedFolders(String((user as { username?: string }).username ?? ''));
  });

  app.post<{ Params: { id: string } }>('/folders/:id/restore', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const ok = await drive.restoreFolder(req.params.id, String((user as { username?: string }).username ?? ''));
    if (!ok) return reply.code(404).send({ error: 'not found' });
    return { restored: true };
  });

  app.delete<{ Params: { id: string } }>('/folders/:id/permanent', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const ok = await drive.deleteFolderPermanent(req.params.id, String((user as { username?: string }).username ?? ''));
    if (!ok) return reply.code(404).send({ error: 'not found' });
    return { deleted: true };
  });

  app.patch<{ Params: { id: string } }>('/folders/:id', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const body = req.body as { name?: string } | undefined;
    if (!body?.name) return reply.code(400).send({ error: 'name is required' });
    const ok = await drive.renameFolder(req.params.id, body.name, String((user as { username?: string }).username ?? ''));
    if (!ok) return reply.code(404).send({ error: 'not found' });
    return { renamed: true };
  });

  // ── Move folder (change parent) ──
  app.post<{ Params: { id: string } }>('/folders/:id/move', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const username = String((user as { username?: string }).username ?? '');
    const folder = await drive.getFolder(req.params.id);
    if (!folder) return reply.code(404).send({ error: 'not found' });
    if (folder.owner !== username) return reply.code(403).send({ error: 'only the owner can move' });
    const body = req.body as { parentId?: string | null } | undefined;
    await drive.moveFolder(req.params.id, body?.parentId ?? null, username);
    return { moved: true };
  });

  // ── Copy folder recursively ──
  app.post<{ Params: { id: string } }>('/folders/:id/copy', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const username = String((user as { username?: string }).username ?? '');
    const src = await drive.getFolder(req.params.id);
    if (!src) return reply.code(404).send({ error: 'not found' });
    if (src.owner !== username) return reply.code(403).send({ error: 'only the owner can copy' });
    const body = req.body as { parentId?: string | null } | undefined;
    const targetParent = body?.parentId ?? src.parentId ?? null;
    const allFolders = await drive.listFolders(username);

    const copyFolder = async (srcId: string, newParentId: string | null): Promise<string> => {
      const sf = allFolders.find((f) => f.id === srcId);
      const newId = randomUUID();
      await drive.createFolder(sf?.name ?? 'folder', username, newParentId ?? undefined, newId);
      const artifacts = await index.list({ folderId: srcId });
      for (const a of artifacts) {
        const newArtId = randomUUID();
        await storage.put(newArtId, await storage.get(a.id));
        await index.insert({
          ...a,
          id: newArtId,
          storageKey: `obj/${newArtId}`,
          folderId: newId,
          owner: username,
          version: 1,
          parentId: undefined,
          createdAt: new Date().toISOString(),
        });
      }
      for (const sub of allFolders.filter((f) => f.parentId === srcId)) {
        await copyFolder(sub.id, newId);
      }
      return newId;
    };

    const newRootId = await copyFolder(req.params.id, targetParent);
    return reply.code(201).send({ copied: true, id: newRootId });
  });

  // ── Move artifact to / out of a folder (owner only) ──
  app.post<{ Params: { id: string } }>('/artifacts/:id/move', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (rec.owner !== String((user as { username?: string }).username ?? '')) {
      return reply.code(403).send({ error: 'only the owner can move' });
    }
    const body = req.body as { folderId?: string | null } | undefined;
    await index.move(rec.id, body?.folderId ?? null);
    return { moved: true };
  });

  // ── Copy artifact (duplicate into a folder) ──
  app.post<{ Params: { id: string } }>('/artifacts/:id/copy', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (!canAccess(rec, user)) return reply.code(403).send({ error: 'forbidden' });
    const body = req.body as { folderId?: string | null } | undefined;
    const newId = randomUUID();
    await storage.put(newId, await storage.get(rec.id));
    const copy: ArtifactRecord = {
      ...rec,
      id: newId,
      storageKey: `obj/${newId}`,
      owner: String((user as { username?: string }).username ?? ''),
      folderId: body?.folderId ?? rec.folderId,
      version: 1,
      parentId: undefined,
      createdAt: new Date().toISOString(),
    };
    await index.insert(copy);
    return reply.code(201).send(copy);
  });

  // ── Share links ──
  app.post('/share', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const username = String((user as { username?: string }).username ?? '');
    const body = req.body as { artifactId?: string; folderId?: string } | undefined;
    if (!body?.artifactId && !body?.folderId) {
      return reply.code(400).send({ error: 'artifactId or folderId is required' });
    }
    if (body.artifactId) {
      const rec = await index.get(body.artifactId);
      if (!rec || rec.owner !== username) return reply.code(403).send({ error: 'only the owner can share' });
    } else if (body.folderId) {
      const folder = await drive.getFolder(body.folderId);
      if (!folder || folder.owner !== username) return reply.code(403).send({ error: 'only the owner can share' });
    }
    const link = await drive.createShare({ artifactId: body.artifactId, folderId: body.folderId }, username);
    return reply.code(201).send({ token: link.token, url: `/share/${link.token}` });
  });

  app.get<{ Params: { token: string } }>('/share/:token', async (req, reply) => {
    const link = await drive.getShare(req.params.token);
    if (!link) return reply.code(404).send({ error: 'invalid or expired link' });
    if (link.artifactId) {
      const rec = await index.get(link.artifactId);
      if (!rec || rec.deletedAt) return reply.code(404).send({ error: 'not found' });
      return { kind: 'artifact', artifact: rec, download: `/share/${link.token}/download` };
    }
    if (link.folderId) {
      const folder = await drive.getFolder(link.folderId);
      if (!folder) return reply.code(404).send({ error: 'not found' });
      const all = await index.list({ folderId: link.folderId });
      const artifacts = all.filter((r) => r.visibility !== 'private');
      return { kind: 'folder', folder, artifacts };
    }
    return reply.code(404).send({ error: 'not found' });
  });

  app.get<{ Params: { token: string } }>('/share/:token/download', async (req, reply) => {
    const link = await drive.getShare(req.params.token);
    if (!link || !link.artifactId) return reply.code(404).send({ error: 'not found' });
    const rec = await index.get(link.artifactId);
    if (!rec || rec.deletedAt) return reply.code(404).send({ error: 'not found' });
    const buf = await storage.get(rec.id);
    reply.header('Content-Type', 'application/octet-stream');
    reply.header('Content-Disposition', `attachment; filename="${rec.name}"`);
    return reply.send(buf);
  });

  app.delete<{ Params: { token: string } }>('/share/:token', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const ok = await drive.deleteShare(req.params.token, String((user as { username?: string }).username ?? ''));
    if (!ok) return reply.code(404).send({ error: 'not found' });
    return { revoked: true };
  });

  // ── Update metadata / rename (owner only) ──
  app.patch<{ Params: { id: string } }>('/artifacts/:id', async (req, reply) => {
    const user = (req as unknown as { user: AuthedUser }).user;
    if (!user) return reply.code(401).send({ error: 'authentication required' });
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (rec.owner !== String((user as { username?: string }).username ?? '')) {
      return reply.code(403).send({ error: 'only the owner can edit' });
    }
    const body = req.body as ArtifactPatch | undefined;
    const updated = await index.update(rec.id, body ?? {});
    if (!updated) return reply.code(404).send({ error: 'not found' });
    return updated;
  });

  // ── 3D preview (OBJ) for GDML artifacts ──
  const execFileAsync = promisify(execFile);
  app.get<{ Params: { id: string } }>('/artifacts/:id/preview', async (req, reply) => {
    const rec = await index.get(req.params.id);
    if (!rec) return reply.code(404).send({ error: 'not found' });
    if (!canAccess(rec, (req as unknown as { user: AuthedUser }).user)) {
      return reply.code(403).send({ error: 'forbidden' });
    }
    if (rec.type !== 'gdml') return reply.code(400).send({ error: 'preview only for GDML artifacts' });

    const gdml = await storage.get(rec.id);

    // Preferred path: a dedicated converter microservice (HTTP).
    const converterUrl = process.env.G4CARE_CONVERTER_URL;
    if (converterUrl) {
      try {
        const res = await fetch(`${converterUrl}/convert`, {
          method: 'POST',
          headers: { 'content-type': 'application/octet-stream' },
          body: gdml,
        });
        if (!res.ok) {
          const detail = await res.text().catch(() => '');
          return reply.code(500).send({ error: `converter failed: ${res.status} ${detail}` });
        }
        reply.header('Content-Type', 'text/plain');
        return reply.send(Buffer.from(await res.arrayBuffer()));
      } catch (e) {
        return reply.code(500).send({ error: `converter unreachable: ${(e as Error).message}` });
      }
    }

    // Fallback: local `gdml2obj` binary (needs Geant4 on the host).
    const converter = process.env.G4CARE_GDML2OBJ ?? 'gdml2obj';
    const dir = await mkdtemp(join(tmpdir(), 'g4c-preview-'));
    try {
      const gdmlPath = join(dir, 'scene.gdml');
      const objPath = join(dir, 'scene.obj');
      await writeFile(gdmlPath, gdml);
      await execFileAsync(converter, [gdmlPath, objPath]);
      const obj = await readFile(objPath, 'utf8');
      reply.header('Content-Type', 'text/plain');
      return reply.send(obj);
    } catch (e) {
      return reply.code(500).send({ error: `GDML converter not available: ${(e as Error).message}` });
    } finally {
      await rm(dir, { recursive: true, force: true });
    }
  });

  return app;
}

export async function startServer(cfg: CloudConfig): Promise<void> {
  const app = await buildServer(cfg);
  await app.listen({ port: cfg.port, host: cfg.host });
}

/// Resolve optional S3 / PostgreSQL backends from the environment (production path).
function backendFromEnv(): Partial<CloudConfig> {
  const out: Partial<CloudConfig> = {};

  const ep = process.env.G4CARE_CLOUD_S3_ENDPOINT;
  if (ep) {
    out.storage = new S3Storage({
      endPoint: ep,
      port: Number(process.env.G4CARE_CLOUD_S3_PORT ?? 9000),
      useSSL: process.env.G4CARE_CLOUD_S3_USESSL === 'true',
      accessKey: process.env.G4CARE_CLOUD_S3_ACCESS_KEY ?? '',
      secretKey: process.env.G4CARE_CLOUD_S3_SECRET_KEY ?? '',
      bucket: process.env.G4CARE_CLOUD_S3_BUCKET ?? 'g4care',
    });
  }

  const db = process.env.G4CARE_CLOUD_DB_URL;
  if (db) {
    out.index = new PostgresIndex(db);
    out.users = new PostgresUserStore(db);
    out.codeStore = new PostgresCodeStore(db);
    out.drive = new PostgresDriveStore(new Pool({ connectionString: db }));
  }

  return out;
}

// Run only when invoked directly (not when imported by the test).
if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const db = process.env.G4CARE_CLOUD_DB_URL;
  const cfg: CloudConfig = {
    storageDir: process.env.G4CARE_CLOUD_STORAGE ?? './data/objects',
    indexFile: process.env.G4CARE_CLOUD_INDEX ?? './data/index.json',
    port: Number(process.env.G4CARE_CLOUD_PORT ?? 3000),
    host: process.env.G4CARE_CLOUD_HOST ?? '127.0.0.1',
    ...backendFromEnv(),
  };

  const boot = async () => {
    // Apply schema migrations (idempotent) so upgrades work on existing volumes.
    if (db) await migrateDatabase(db);
    await startServer(cfg);
  };
  boot().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
