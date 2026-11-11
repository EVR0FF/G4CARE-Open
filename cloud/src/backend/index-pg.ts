//==============================================================================
// G4CARE - index-pg.ts — Cloud backend: PostgreSQL entry
//
// Cloud backend: PostgreSQL entry
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { Pool } from 'pg';
import type { ArtifactRecord, BundleFile, SearchFilter } from '../shared/types.js';
import type { ArtifactPatch } from './index.js';

type Row = Record<string, unknown>;

function rowToRecord(row: Row, tags: string[]): ArtifactRecord {
  const filesRaw = row.files as unknown;
  const files: BundleFile[] | undefined = Array.isArray(filesRaw)
    ? (filesRaw as BundleFile[])
    : (typeof filesRaw === 'string' ? (JSON.parse(filesRaw) as BundleFile[]) : undefined);

  return {
    id: String(row.id),
    type: String(row.type) as ArtifactRecord['type'],
    name: String(row.name),
    meta: {
      doi: row.doi ? String(row.doi) : undefined,
      info_url: row.info_url ? String(row.info_url) : undefined,
      description: row.description ? String(row.description) : undefined,
      version: row.version ? String(row.version) : undefined,
      created: row.created ? String(row.created) : undefined,
      tags,
      author: { name: String(row.author_name ?? ''), alias: String(row.author_alias ?? '') },
      affiliation: {
        group: String(row.group_name ?? ''),
        institute: String(row.institute ?? ''),
        department: row.department ? String(row.department) : undefined,
      },
    },
    sha256: String(row.sha256),
    size: Number(row.size),
    createdAt: row.created_at instanceof Date ? row.created_at.toISOString() : String(row.created_at),
    storageKey: String(row.storage_key),
    files,
    owner: String(row.owner ?? ''),
    visibility: row.visibility === 'private' ? 'private' : 'public',
    version: Number(row.artifact_version ?? 1),
    parentId: row.parent_id ? String(row.parent_id) : undefined,
    folderId: row.folder_id ? String(row.folder_id) : undefined,
    deletedAt: row.deleted_at
      ? (row.deleted_at instanceof Date ? row.deleted_at.toISOString() : String(row.deleted_at))
      : undefined,
  };
}

export class PostgresIndex {
  private readonly pool: Pool;

  constructor(connectionString: string) {
    this.pool = new Pool({ connectionString });
  }

  async insert(rec: ArtifactRecord): Promise<void> {
    const client = await this.pool.connect();
    try {
      await client.query('BEGIN');
      await client.query(
        `INSERT INTO artifacts
           (id, type, name, doi, info_url, description, version, created,
            author_name, author_alias, group_name, institute, department,
            sha256, size, storage_key, files, owner, visibility, artifact_version, parent_id, folder_id)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22)`,
        [
          rec.id, rec.type, rec.name,
          rec.meta.doi ?? null, rec.meta.info_url ?? null,
          rec.meta.description ?? null, rec.meta.version ?? null,
          rec.meta.created ?? null,
          rec.meta.author.name, rec.meta.author.alias,
          rec.meta.affiliation.group, rec.meta.affiliation.institute,
          rec.meta.affiliation.department ?? null,
          rec.sha256, rec.size, rec.storageKey,
          rec.files ? JSON.stringify(rec.files) : null,
          rec.owner ?? '', rec.visibility ?? 'public', rec.version ?? 1, rec.parentId ?? null, rec.folderId ?? null,
        ],
      );
      for (const tag of rec.meta.tags) {
        await client.query('INSERT INTO tags (artifact_id, tag) VALUES ($1,$2)', [rec.id, tag]);
      }
      await client.query('COMMIT');
    } catch (err) {
      await client.query('ROLLBACK');
      throw err;
    } finally {
      client.release();
    }
  }

  async get(id: string): Promise<ArtifactRecord | undefined> {
    const res = await this.pool.query('SELECT * FROM artifacts WHERE id = $1', [id]);
    if (res.rows.length === 0) return undefined;
    const tags = await this.pool.query('SELECT tag FROM tags WHERE artifact_id = $1', [id]);
    return rowToRecord(res.rows[0] as Row, tags.rows.map((t) => String(t.tag)));
  }

  async list(filter: SearchFilter = {}): Promise<ArtifactRecord[]> {
    const where: string[] = ['a.deleted_at IS NULL'];
    const params: unknown[] = [];

    const add = (cond: string, val: unknown) => {
      params.push(val);
      // Replace EVERY "$X" placeholder (some conditions have two) with the
      // current parameter number. A function replacement avoids String.replace
      // parsing "$1" as a capture-group token.
      where.push(cond.replace(/\$X/g, () => `$${params.length}`));
    };

    if (filter.type) add('a.type = $X', filter.type);
    if (filter.tag) add('EXISTS (SELECT 1 FROM tags t WHERE t.artifact_id = a.id AND t.tag = $X)', filter.tag);
    if (filter.author) add('(a.author_name ILIKE $X OR a.author_alias ILIKE $X)', `%${filter.author}%`);
    if (filter.institute) add('a.institute ILIKE $X', `%${filter.institute}%`);
    if (filter.q) add('(a.name ILIKE $X OR a.description ILIKE $X)', `%${filter.q}%`);
    if (filter.folderId === '__root__') where.push('a.folder_id IS NULL');
    else if (filter.folderId) add('a.folder_id = $X', filter.folderId);

    const sql = `SELECT a.* FROM artifacts a${where.length ? ' WHERE ' + where.join(' AND ') : ''} ORDER BY a.created_at DESC`;
    const res = await this.pool.query(sql, params);

    const out: ArtifactRecord[] = [];
    for (const row of res.rows as Row[]) {
      const tags = await this.pool.query('SELECT tag FROM tags WHERE artifact_id = $1', [row.id]);
      out.push(rowToRecord(row, tags.rows.map((t) => String(t.tag))));
    }
    return out;
  }

  async close(): Promise<void> {
    await this.pool.end();
  }

  async remove(id: string): Promise<boolean> {
    const res = await this.pool.query('UPDATE artifacts SET deleted_at = now() WHERE id = $1', [id]);
    return (res.rowCount ?? 0) > 0;
  }

  async restore(id: string): Promise<boolean> {
    const res = await this.pool.query('UPDATE artifacts SET deleted_at = NULL WHERE id = $1', [id]);
    return (res.rowCount ?? 0) > 0;
  }

  async trash(owner?: string): Promise<ArtifactRecord[]> {
    const sql = owner
      ? 'SELECT * FROM artifacts WHERE deleted_at IS NOT NULL AND owner = $1 ORDER BY deleted_at DESC'
      : 'SELECT * FROM artifacts WHERE deleted_at IS NOT NULL ORDER BY deleted_at DESC';
    const res = owner ? await this.pool.query(sql, [owner]) : await this.pool.query(sql);
    const out: ArtifactRecord[] = [];
    for (const row of res.rows as Row[]) {
      const tags = await this.pool.query('SELECT tag FROM tags WHERE artifact_id = $1', [row.id]);
      out.push(rowToRecord(row, tags.rows.map((t) => String(t.tag))));
    }
    return out;
  }

  async purge(id: string): Promise<boolean> {
    const res = await this.pool.query('DELETE FROM artifacts WHERE id = $1', [id]);
    return (res.rowCount ?? 0) > 0;
  }

  async move(id: string, folderId: string | null): Promise<boolean> {
    const res = await this.pool.query('UPDATE artifacts SET folder_id = $2 WHERE id = $1', [id, folderId]);
    return (res.rowCount ?? 0) > 0;
  }

  async update(id: string, patch: ArtifactPatch): Promise<ArtifactRecord | undefined> {
    const sets: string[] = [];
    const params: unknown[] = [];
    const add = (col: string, val: unknown) => {
      params.push(val);
      sets.push(`${col} = $${params.length}`);
    };
    if (patch.name !== undefined) add('name', patch.name);
    if (patch.description !== undefined) add('description', patch.description);
    if (patch.version !== undefined) add('version', patch.version);
    if (patch.doi !== undefined) add('doi', patch.doi);
    if (patch.visibility !== undefined) add('visibility', patch.visibility);
    if (patch.authorName !== undefined) add('author_name', patch.authorName);
    if (patch.authorAlias !== undefined) add('author_alias', patch.authorAlias);
    if (patch.created !== undefined) add('created', patch.created);
    if (patch.institute !== undefined) add('institute', patch.institute);
    if (patch.group !== undefined) add('group_name', patch.group);
    if (sets.length > 0) {
      params.push(id);
      await this.pool.query(`UPDATE artifacts SET ${sets.join(', ')} WHERE id = $${params.length}`, params);
    }
    if (patch.tags !== undefined) {
      await this.pool.query('DELETE FROM tags WHERE artifact_id = $1', [id]);
      for (const t of patch.tags) {
        await this.pool.query('INSERT INTO tags (artifact_id, tag) VALUES ($1, $2)', [id, t]);
      }
    }
    return this.get(id);
  }
}
