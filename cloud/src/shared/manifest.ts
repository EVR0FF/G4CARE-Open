//==============================================================================
// G4CARE cloud — manifest / metadata handling (TOML).
//
// The metadata format is TOML and mirrors the C++ MetaReader canonical layout
// (see include/DataIO/MetaReader.hh). It is intentionally distinct from the
// YAML configuration so the two can never be confused.
//==============================================================================

import { parse, stringify } from 'smol-toml';
import { createHash } from 'node:crypto';
import { readFile, stat } from 'node:fs/promises';
import type { ArtifactType, BundleFile, MetaInfo } from './types.js';

export function emptyMeta(): MetaInfo {
  return {
    tags: [],
    author: { name: '', alias: '' },
    affiliation: { group: '', institute: '' },
  };
}

function str(v: unknown): string {
  return typeof v === 'string' ? v : '';
}

function asRecord(v: unknown): Record<string, unknown> | undefined {
  return v !== null && typeof v === 'object' ? (v as Record<string, unknown>) : undefined;
}

/// Parse the canonical G4CARE `.meta.toml` structure into a MetaInfo.
export function parseMetaToml(text: string): MetaInfo {
  const t = asRecord(parse(text)) ?? {};
  const meta = emptyMeta();

  meta.doi = str(t.doi);
  meta.info_url = str(t.info_url);
  meta.description = str(t.description);
  meta.version = str(t.version);
  meta.created = str(t.created);

  if (Array.isArray(t.tags)) {
    meta.tags = t.tags.filter((x): x is string => typeof x === 'string');
  }

  const author = asRecord(t.author);
  if (author) {
    meta.author.name = str(author.name);
    meta.author.alias = str(author.alias);
  }

  const aff = asRecord(t.affiliation);
  if (aff) {
    meta.affiliation.group = str(aff.group);
    meta.affiliation.institute = str(aff.institute);
    meta.affiliation.department = str(aff.department);
  }

  return meta;
}

/// Serialize a MetaInfo back into the canonical `.meta.toml` layout.
export function stringifyMetaToml(meta: MetaInfo): string {
  const obj: Record<string, unknown> = {
    doi: meta.doi ?? '',
    info_url: meta.info_url ?? '',
    description: meta.description ?? '',
    version: meta.version ?? '',
    created: meta.created ?? '',
    tags: meta.tags,
    author: { name: meta.author.name, alias: meta.author.alias },
    affiliation: {
      group: meta.affiliation.group,
      institute: meta.affiliation.institute,
      department: meta.affiliation.department ?? '',
    },
  };
  return stringify(obj);
}

/// Infer the artifact type from a file name.
export function detectType(filename: string): ArtifactType {
  const dot = filename.lastIndexOf('.');
  const ext = dot >= 0 ? filename.slice(dot + 1).toLowerCase() : '';
  if (ext === 'yaml' || ext === 'yml') return 'yaml';
  if (ext === 'gdml') return 'gdml';
  if (ext === 'so' || ext === 'dll') return 'so';
  if (ext === 'mac') return 'mac';
  return 'other';
}

/// SHA-256 hex digest (async) of a buffer or string.
export async function sha256(data: Buffer | string): Promise<string> {
  return createHash('sha256').update(data).digest('hex');
}

/// SHA-256 hex digest (sync) of a buffer or string.
export function sha256Sync(data: Buffer | string): string {
  return createHash('sha256').update(data).digest('hex');
}

/// Derive the metadata sidecar path for an artifact: `<artifact>.meta.toml`.
export function deriveMetaPath(artifactPath: string): string {
  return `${artifactPath}.meta.toml`;
}

/// SHA-256 of a file on disk.
export async function sha256File(path: string): Promise<string> {
  return sha256(await readFile(path));
}

/// Size of a file on disk (bytes).
export async function fileSize(path: string): Promise<number> {
  return (await stat(path)).size;
}

/// Serialize a bundle manifest (metadata + file list) to TOML.
export function stringifyManifest(meta: MetaInfo, files: BundleFile[]): string {
  const obj: Record<string, unknown> = {
    format_version: 1,
    doi: meta.doi ?? '',
    info_url: meta.info_url ?? '',
    description: meta.description ?? '',
    version: meta.version ?? '',
    created: meta.created ?? '',
    tags: meta.tags,
    author: { name: meta.author.name, alias: meta.author.alias },
    affiliation: {
      group: meta.affiliation.group,
      institute: meta.affiliation.institute,
      department: meta.affiliation.department ?? '',
    },
    files: files.map((f) => ({ path: f.path, sha256: f.sha256, size: f.size })),
  };
  return stringify(obj);
}

/// Parse a manifest (metadata + optional file list). For a plain `.meta.toml`
/// (no `files` key) the returned `files` array is empty.
export function parseManifest(text: string): { meta: MetaInfo; files: BundleFile[] } {
  const t = asRecord(parse(text)) ?? {};
  const meta = parseMetaToml(text);
  const files: BundleFile[] = [];
  if (Array.isArray(t.files)) {
    for (const f of t.files) {
      const fr = asRecord(f);
      if (!fr) continue;
      const path = str(fr.path);
      if (!path) continue;
      files.push({
        path,
        sha256: str(fr.sha256),
        size: typeof fr.size === 'number' ? fr.size : 0,
      });
    }
  }
  return { meta, files };
}
