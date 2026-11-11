//==============================================================================
// G4CARE - index.ts — Cloud backend: entry point
//
// Cloud backend: entry point
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { readFile, writeFile } from 'node:fs/promises';
import type { ArtifactRecord, SearchFilter } from '../shared/types.js';

export interface ArtifactPatch {
  name?: string;
  visibility?: 'public' | 'private';
  description?: string;
  tags?: string[];
  version?: string;
  doi?: string;
  authorName?: string;
  authorAlias?: string;
  created?: string;
  institute?: string;
  group?: string;
}

export interface MetadataIndex {
  insert(rec: ArtifactRecord): Promise<void>;
  get(id: string): Promise<ArtifactRecord | undefined>;
  list(filter?: SearchFilter): Promise<ArtifactRecord[]>;
  trash(owner?: string): Promise<ArtifactRecord[]>;
  remove(id: string): Promise<boolean>;   // move to trash (soft delete)
  restore(id: string): Promise<boolean>;  // restore from trash
  purge(id: string): Promise<boolean>;    // permanent delete
  move(id: string, folderId: string | null): Promise<boolean>;
  update(id: string, patch: ArtifactPatch): Promise<ArtifactRecord | undefined>;
}

export class JsonIndex implements MetadataIndex {
  private records: ArtifactRecord[] = [];
  private loaded = false;

  constructor(private readonly file: string) {}

  private async load(): Promise<void> {
    if (this.loaded) return;
    try {
      this.records = JSON.parse(await readFile(this.file, 'utf8')) as ArtifactRecord[];
      if (!Array.isArray(this.records)) this.records = [];
    } catch {
      this.records = [];
    }
    this.loaded = true;
  }

  private async save(): Promise<void> {
    await writeFile(this.file, JSON.stringify(this.records, null, 2));
  }

  async insert(rec: ArtifactRecord): Promise<void> {
    await this.load();
    this.records.push(rec);
    await this.save();
  }

  async get(id: string): Promise<ArtifactRecord | undefined> {
    await this.load();
    return this.records.find((r) => r.id === id);
  }

  async list(filter: SearchFilter = {}): Promise<ArtifactRecord[]> {
    await this.load();
    let out = this.records.filter((r) => !r.deletedAt);

    if (filter.type) out = out.filter((r) => r.type === filter.type);

    if (filter.folderId === '__root__') out = out.filter((r) => !r.folderId);
    else if (filter.folderId) out = out.filter((r) => r.folderId === filter.folderId);

    if (filter.tag) out = out.filter((r) => r.meta.tags.includes(filter.tag as string));

    if (filter.author) {
      const a = (filter.author as string).toLowerCase();
      out = out.filter((r) =>
        r.meta.author.name.toLowerCase().includes(a) ||
        r.meta.author.alias.toLowerCase().includes(a));
    }

    if (filter.institute) {
      const inst = (filter.institute as string).toLowerCase();
      out = out.filter((r) => r.meta.affiliation.institute.toLowerCase().includes(inst));
    }

    if (filter.q) {
      const q = (filter.q as string).toLowerCase();
      out = out.filter((r) =>
        r.name.toLowerCase().includes(q) ||
        (r.meta.description ?? '').toLowerCase().includes(q));
    }

    return out;
  }

  async remove(id: string): Promise<boolean> {
    await this.load();
    const rec = this.records.find((r) => r.id === id);
    if (!rec) return false;
    rec.deletedAt = new Date().toISOString();
    await this.save();
    return true;
  }

  async restore(id: string): Promise<boolean> {
    await this.load();
    const rec = this.records.find((r) => r.id === id);
    if (!rec) return false;
    rec.deletedAt = undefined;
    await this.save();
    return true;
  }

  async trash(owner?: string): Promise<ArtifactRecord[]> {
    await this.load();
    return this.records.filter((r) => r.deletedAt && (!owner || r.owner === owner));
  }

  async purge(id: string): Promise<boolean> {
    await this.load();
    const idx = this.records.findIndex((r) => r.id === id);
    if (idx === -1) return false;
    this.records.splice(idx, 1);
    await this.save();
    return true;
  }

  async move(id: string, folderId: string | null): Promise<boolean> {
    await this.load();
    const rec = this.records.find((r) => r.id === id);
    if (!rec) return false;
    rec.folderId = folderId ?? undefined;
    await this.save();
    return true;
  }

  async update(id: string, patch: ArtifactPatch): Promise<ArtifactRecord | undefined> {
    await this.load();
    const rec = this.records.find((r) => r.id === id);
    if (!rec) return undefined;
    if (patch.name !== undefined) rec.name = patch.name;
    if (patch.visibility !== undefined) rec.visibility = patch.visibility;
    if (patch.description !== undefined) rec.meta.description = patch.description;
    if (patch.tags !== undefined) rec.meta.tags = patch.tags;
    if (patch.version !== undefined) rec.meta.version = patch.version;
    if (patch.doi !== undefined) rec.meta.doi = patch.doi;
    if (patch.authorName !== undefined) rec.meta.author.name = patch.authorName;
    if (patch.authorAlias !== undefined) rec.meta.author.alias = patch.authorAlias;
    if (patch.created !== undefined) rec.meta.created = patch.created;
    if (patch.institute !== undefined) rec.meta.affiliation.institute = patch.institute;
    if (patch.group !== undefined) rec.meta.affiliation.group = patch.group;
    await this.save();
    return rec;
  }
}
