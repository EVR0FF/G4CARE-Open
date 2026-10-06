// Mirrors cloud/src/shared/types.ts (backend) so the dashboard stays consistent.

export type ArtifactType = 'yaml' | 'gdml' | 'so' | 'mac' | 'bundle' | 'other';

export interface MetaAuthor {
  name: string;
  alias: string;
}

export interface MetaAffiliation {
  group: string;
  institute: string;
  department?: string;
}

export interface MetaInfo {
  doi?: string;
  info_url?: string;
  description?: string;
  version?: string;
  created?: string;
  tags: string[];
  author: MetaAuthor;
  affiliation: MetaAffiliation;
}

export interface BundleFile {
  path: string;
  sha256: string;
  size: number;
}

export interface ArtifactRecord {
  id: string;
  type: ArtifactType;
  name: string;
  meta: MetaInfo;
  sha256: string;
  size: number;
  createdAt: string;
  storageKey: string;
  files?: BundleFile[];
  owner?: string;
  visibility?: 'public' | 'private';
  version?: number;
  parentId?: string;
  folderId?: string;
  deletedAt?: string;
}

export interface FolderRecord {
  id: string;
  name: string;
  parentId?: string;
  owner: string;
  createdAt: string;
}

export interface SearchFilter {
  q?: string;
  tag?: string;
  author?: string;
  institute?: string;
  type?: string;
  folderId?: string;
}
