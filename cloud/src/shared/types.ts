//==============================================================================
// G4CARE - types.ts — Cloud shared: types
//
// Cloud shared: types
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

export type ArtifactType = 'yaml' | 'gdml' | 'so' | 'mac' | 'bundle' | 'other';

export interface MetaAuthor {
  name: string;    // ФИО
  alias: string;   // псевдоним
}

export interface MetaAffiliation {
  group: string;       // группа
  institute: string;   // институт
  department?: string; // (опционально) кафедра/лаборатория
}

export interface MetaInfo {
  doi?: string;
  info_url?: string;
  description?: string;
  version?: string;
  created?: string; // ISO-8601
  tags: string[];
  author: MetaAuthor;
  affiliation: MetaAffiliation;
}

export interface BundleFile {
  path: string;    // relative path within the bundle
  sha256: string;  // hex digest
  size: number;    // bytes
}

export interface ArtifactRecord {
  id: string;         // unique id (uuid)
  type: ArtifactType;
  name: string;       // original file name (bundle: <name>.g4c)
  meta: MetaInfo;
  sha256: string;     // hex digest of the file / bundle content
  size: number;       // bytes
  createdAt: string;  // ISO-8601
  storageKey: string; // key in object storage (== id for the file store)
  files?: BundleFile[]; // present for bundles: manifest file list
  owner?: string;       // username who uploaded
  visibility?: 'public' | 'private';
  version?: number;     // numeric version (1, 2, ...)
  parentId?: string;    // links versions of the same artifact
  folderId?: string;    // owning folder (drive)
  deletedAt?: string;   // ISO-8601 — set when moved to trash
}

export interface FolderRecord {
  id: string;
  name: string;
  parentId?: string;
  owner: string;
  createdAt: string;
  trashed?: boolean;
}

export interface SearchFilter {
  q?: string;        // free-text (name/description)
  tag?: string;
  author?: string;   // author.name or author.alias
  institute?: string;
  type?: ArtifactType;
  folderId?: string; // filter by owning folder
}
