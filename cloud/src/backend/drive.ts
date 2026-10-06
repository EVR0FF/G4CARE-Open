//==============================================================================
// G4CARE cloud — drive store (folders + share links).
//
// Postgres-backed in production; in-memory fallback for the dev/smoke path.
//==============================================================================

import { Pool } from 'pg';
import { randomBytes, randomUUID } from 'node:crypto';
import type { FolderRecord } from '../shared/types.js';

export interface ShareLink {
  token: string;
  artifactId?: string;
  folderId?: string;
  createdBy: string;
  createdAt: string;
  expiresAt?: string;
}

export interface DriveStore {
  createFolder(name: string, owner: string, parentId?: string, id?: string): Promise<FolderRecord>;
  listFolders(owner: string): Promise<FolderRecord[]>;
  getFolder(id: string): Promise<FolderRecord | undefined>;
  renameFolder(id: string, name: string, owner: string): Promise<boolean>;
  moveFolder(id: string, parentId: string | null, owner: string): Promise<boolean>;
  deleteFolder(id: string, owner: string): Promise<boolean>;
  createShare(target: { artifactId?: string; folderId?: string }, owner: string): Promise<ShareLink>;
  getShare(token: string): Promise<ShareLink | undefined>;
  deleteShare(token: string, owner: string): Promise<boolean>;
}

function mapFolderRow(r: Record<string, unknown>): FolderRecord {
  return {
    id: String(r.id),
    name: String(r.name),
    parentId: r.parent_id ? String(r.parent_id) : undefined,
    owner: String(r.owner ?? ''),
    createdAt: r.created_at instanceof Date ? r.created_at.toISOString() : String(r.created_at),
  };
}

export class PostgresDriveStore implements DriveStore {
  constructor(private readonly pool: Pool) {}

  async createFolder(name: string, owner: string, parentId?: string, id: string = randomUUID()): Promise<FolderRecord> {
    await this.pool.query('INSERT INTO folders (id, name, parent_id, owner) VALUES ($1,$2,$3,$4)', [
      id, name, parentId ?? null, owner,
    ]);
    return { id, name, parentId, owner, createdAt: new Date().toISOString() };
  }

  async listFolders(owner: string): Promise<FolderRecord[]> {
    const res = await this.pool.query('SELECT * FROM folders WHERE owner = $1 ORDER BY name', [owner]);
    return res.rows.map((r) => mapFolderRow(r as Record<string, unknown>));
  }

  async getFolder(id: string): Promise<FolderRecord | undefined> {
    const res = await this.pool.query('SELECT * FROM folders WHERE id = $1', [id]);
    if (res.rows.length === 0) return undefined;
    return mapFolderRow(res.rows[0] as Record<string, unknown>);
  }

  async renameFolder(id: string, name: string, owner: string): Promise<boolean> {
    const res = await this.pool.query('UPDATE folders SET name = $2 WHERE id = $1 AND owner = $3', [id, name, owner]);
    return (res.rowCount ?? 0) > 0;
  }

  async moveFolder(id: string, parentId: string | null, owner: string): Promise<boolean> {
    const res = await this.pool.query('UPDATE folders SET parent_id = $2 WHERE id = $1 AND owner = $3', [id, parentId, owner]);
    return (res.rowCount ?? 0) > 0;
  }

  async deleteFolder(id: string, owner: string): Promise<boolean> {
    const res = await this.pool.query('DELETE FROM folders WHERE id = $1 AND owner = $2', [id, owner]);
    return (res.rowCount ?? 0) > 0;
  }

  async createShare(target: { artifactId?: string; folderId?: string }, owner: string): Promise<ShareLink> {
    const token = randomBytes(16).toString('hex');
    await this.pool.query(
      'INSERT INTO share_links (token, artifact_id, folder_id, created_by) VALUES ($1,$2,$3,$4)',
      [token, target.artifactId ?? null, target.folderId ?? null, owner],
    );
    return { token, artifactId: target.artifactId, folderId: target.folderId, createdBy: owner, createdAt: new Date().toISOString() };
  }

  async getShare(token: string): Promise<ShareLink | undefined> {
    const res = await this.pool.query('SELECT * FROM share_links WHERE token = $1', [token]);
    if (res.rows.length === 0) return undefined;
    const r = res.rows[0] as Record<string, unknown>;
    return {
      token: String(r.token),
      artifactId: r.artifact_id ? String(r.artifact_id) : undefined,
      folderId: r.folder_id ? String(r.folder_id) : undefined,
      createdBy: String(r.created_by ?? ''),
      createdAt: r.created_at instanceof Date ? r.created_at.toISOString() : String(r.created_at),
      expiresAt: r.expires_at
        ? (r.expires_at instanceof Date ? r.expires_at.toISOString() : String(r.expires_at))
        : undefined,
    };
  }

  async deleteShare(token: string, owner: string): Promise<boolean> {
    const res = await this.pool.query('DELETE FROM share_links WHERE token = $1 AND created_by = $2', [token, owner]);
    return (res.rowCount ?? 0) > 0;
  }
}

export class MemoryDriveStore implements DriveStore {
  private folders: FolderRecord[] = [];
  private shares: ShareLink[] = [];

  async createFolder(name: string, owner: string, parentId?: string, id: string = randomUUID()): Promise<FolderRecord> {
    const rec: FolderRecord = { id, name, parentId, owner, createdAt: new Date().toISOString() };
    this.folders.push(rec);
    return rec;
  }
  async listFolders(owner: string): Promise<FolderRecord[]> { return this.folders.filter((f) => f.owner === owner); }
  async getFolder(id: string): Promise<FolderRecord | undefined> { return this.folders.find((f) => f.id === id); }
  async renameFolder(id: string, name: string, owner: string): Promise<boolean> {
    const f = this.folders.find((f) => f.id === id && f.owner === owner);
    if (!f) return false;
    f.name = name;
    return true;
  }
  async moveFolder(id: string, parentId: string | null, owner: string): Promise<boolean> {
    const f = this.folders.find((f) => f.id === id && f.owner === owner);
    if (!f) return false;
    f.parentId = parentId ?? undefined;
    return true;
  }
  async deleteFolder(id: string, owner: string): Promise<boolean> {
    const i = this.folders.findIndex((f) => f.id === id && f.owner === owner);
    if (i === -1) return false;
    this.folders.splice(i, 1);
    return true;
  }
  async createShare(target: { artifactId?: string; folderId?: string }, owner: string): Promise<ShareLink> {
    const rec: ShareLink = { token: randomBytes(16).toString('hex'), artifactId: target.artifactId, folderId: target.folderId, createdBy: owner, createdAt: new Date().toISOString() };
    this.shares.push(rec);
    return rec;
  }
  async getShare(token: string): Promise<ShareLink | undefined> { return this.shares.find((s) => s.token === token); }
  async deleteShare(token: string, owner: string): Promise<boolean> {
    const i = this.shares.findIndex((s) => s.token === token && s.createdBy === owner);
    if (i === -1) return false;
    this.shares.splice(i, 1);
    return true;
  }
}
