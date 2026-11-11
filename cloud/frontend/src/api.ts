//==============================================================================
// G4CARE - api.ts — Cloud frontend: API client
//
// Cloud frontend: API client
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import type { ArtifactRecord, SearchFilter } from './types';

const BASE = import.meta.env.VITE_API_URL ?? '';

export async function listArtifacts(filter: SearchFilter = {}, token?: string): Promise<ArtifactRecord[]> {
  const params = new URLSearchParams();
  for (const [k, v] of Object.entries(filter)) {
    if (v) params.set(k, v);
  }
  const qs = params.toString();
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts${qs ? `?${qs}` : ''}`, { headers });
  if (!res.ok) throw new Error(`list failed: ${res.status}`);
  return res.json();
}

export async function getArtifact(id: string, token?: string): Promise<ArtifactRecord> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}`, { headers });
  if (!res.ok) throw new Error(`get failed: ${res.status}`);
  return res.json();
}

export async function getVersions(id: string, token?: string): Promise<ArtifactRecord[]> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}/versions`, { headers });
  if (!res.ok) throw new Error(`versions failed: ${res.status}`);
  return res.json();
}

export function downloadUrl(id: string, token?: string): string {
  return `${BASE}/artifacts/${id}/download${token ? `?token=${encodeURIComponent(token)}` : ''}`;
}

export function previewUrl(id: string, token?: string): string {
  return `${BASE}/artifacts/${id}/preview${token ? `?token=${encodeURIComponent(token)}` : ''}`;
}

async function errorFrom(res: Response, fallback: string): Promise<Error> {
  try {
    const body = (await res.json()) as { error?: string };
    if (body?.error) return new Error(body.error);
  } catch {
    /* ignore */
  }
  return new Error(`${fallback}: ${res.status}`);
}

export interface RegisterPayload {
  username: string;
  email: string;
  password: string;
  institute?: string;
  group?: string;
  fullName?: string;
  phone?: string;
  birthDate?: string;
}

export async function login(login: string, password: string): Promise<{ token: string; username: string; email: string }> {
  const res = await fetch(`${BASE}/auth/login`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ login, password }),
  });
  if (!res.ok) throw await errorFrom(res, 'login failed');
  return res.json();
}

export async function register(payload: RegisterPayload): Promise<{ username: string; email: string; verified: boolean }> {
  const res = await fetch(`${BASE}/auth/register`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(payload),
  });
  if (!res.ok) throw await errorFrom(res, 'register failed');
  return res.json();
}

export async function verifyEmail(email: string, code: string): Promise<void> {
  const res = await fetch(`${BASE}/auth/verify`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email, code }),
  });
  if (!res.ok) throw await errorFrom(res, 'verify failed');
}

export async function resendCode(email: string): Promise<void> {
  const res = await fetch(`${BASE}/auth/resend`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email }),
  });
  if (!res.ok) throw await errorFrom(res, 'resend failed');
}

export async function forgotPassword(email: string): Promise<void> {
  const res = await fetch(`${BASE}/auth/forgot`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email }),
  });
  if (!res.ok) throw await errorFrom(res, 'forgot failed');
}

export async function resetPassword(email: string, code: string, newPassword: string): Promise<void> {
  const res = await fetch(`${BASE}/auth/reset`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email, code, newPassword }),
  });
  if (!res.ok) throw await errorFrom(res, 'reset failed');
}

export async function uploadArtifact(file: File, metaToml: string, token?: string): Promise<ArtifactRecord> {
  const form = new FormData();
  form.append('file', file);
  if (metaToml) form.append('meta', metaToml);
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts`, { method: 'POST', headers, body: form });
  if (!res.ok) throw await errorFrom(res, 'upload failed');
  return res.json();
}

export async function deleteArtifact(id: string, token?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}`, { method: 'DELETE', headers });
  if (!res.ok) throw await errorFrom(res, 'delete failed');
}

export async function deleteArtifactForever(id: string, token?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}/permanent`, { method: 'DELETE', headers });
  if (!res.ok) throw await errorFrom(res, 'delete failed');
}

export async function restoreArtifact(id: string, token?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}/restore`, { method: 'POST', headers });
  if (!res.ok) throw await errorFrom(res, 'restore failed');
}

export async function listTrash(token?: string): Promise<ArtifactRecord[]> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/trash`, { headers });
  if (!res.ok) throw new Error(`trash failed: ${res.status}`);
  return res.json();
}

export interface FolderRecord {
  id: string;
  name: string;
  parentId?: string;
  owner: string;
  createdAt: string;
}

export async function listFolders(token?: string): Promise<FolderRecord[]> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders`, { headers });
  if (!res.ok) throw new Error(`folders failed: ${res.status}`);
  return res.json();
}

export async function createFolder(name: string, token?: string, parentId?: string): Promise<FolderRecord> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders`, { method: 'POST', headers, body: JSON.stringify({ name, parentId }) });
  if (!res.ok) throw await errorFrom(res, 'create folder failed');
  return res.json();
}

export async function deleteFolder(id: string, token?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders/${id}`, { method: 'DELETE', headers });
  if (!res.ok) throw await errorFrom(res, 'delete folder failed');
}

export async function renameFolder(id: string, name: string, token?: string): Promise<void> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders/${id}`, { method: 'PATCH', headers, body: JSON.stringify({ name }) });
  if (!res.ok) throw await errorFrom(res, 'rename folder failed');
}

export async function moveFolder(id: string, parentId: string | null, token?: string): Promise<void> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders/${id}/move`, { method: 'POST', headers, body: JSON.stringify({ parentId }) });
  if (!res.ok) throw await errorFrom(res, 'move folder failed');
}

export async function copyFolder(id: string, parentId: string | null, token?: string): Promise<void> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/folders/${id}/copy`, { method: 'POST', headers, body: JSON.stringify({ parentId }) });
  if (!res.ok) throw await errorFrom(res, 'copy folder failed');
}

export async function moveArtifact(id: string, folderId: string | null, token?: string): Promise<void> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}/move`, { method: 'POST', headers, body: JSON.stringify({ folderId }) });
  if (!res.ok) throw await errorFrom(res, 'move failed');
}

export async function copyArtifact(id: string, folderId: string | null, token?: string): Promise<ArtifactRecord> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}/copy`, { method: 'POST', headers, body: JSON.stringify({ folderId }) });
  if (!res.ok) throw await errorFrom(res, 'copy failed');
  return res.json();
}

export interface ShareResult {
  kind: 'artifact' | 'folder';
  artifact?: ArtifactRecord;
  folder?: FolderRecord;
  artifacts?: ArtifactRecord[];
  download?: string;
}

export async function createShare(body: { artifactId?: string; folderId?: string }, token?: string): Promise<{ token: string; url: string }> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/share`, { method: 'POST', headers, body: JSON.stringify(body) });
  if (!res.ok) throw await errorFrom(res, 'share failed');
  return res.json();
}

export async function getShare(token: string): Promise<ShareResult> {
  const res = await fetch(`${BASE}/share/${token}`);
  if (!res.ok) throw await errorFrom(res, 'share not found');
  return res.json();
}

export async function revokeShare(token: string, authToken?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (authToken) headers.authorization = `Bearer ${authToken}`;
  const res = await fetch(`${BASE}/share/${token}`, { method: 'DELETE', headers });
  if (!res.ok) throw await errorFrom(res, 'revoke failed');
}

export async function updateArtifact(id: string, patch: Record<string, unknown>, token?: string): Promise<ArtifactRecord> {
  const headers: Record<string, string> = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const res = await fetch(`${BASE}/artifacts/${id}`, { method: 'PATCH', headers, body: JSON.stringify(patch) });
  if (!res.ok) throw await errorFrom(res, 'update failed');
  return res.json();
}
