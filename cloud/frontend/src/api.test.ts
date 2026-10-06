import { describe, it, expect, vi, beforeEach } from 'vitest';
import { listArtifacts, login, uploadArtifact, downloadUrl, previewUrl } from './api';

const mockFetch = vi.fn();
beforeEach(() => mockFetch.mockReset());

describe('api', () => {
  it('listArtifacts builds the query string', async () => {
    globalThis.fetch = mockFetch as unknown as typeof fetch;
    mockFetch.mockResolvedValue({ ok: true, json: async () => [] });
    await listArtifacts({ tag: 'nano', type: 'gdml' });
    const url = mockFetch.mock.calls[0][0] as string;
    expect(url).toContain('/artifacts?');
    expect(url).toContain('tag=nano');
    expect(url).toContain('type=gdml');
  });

  it('login POSTs credentials as JSON', async () => {
    globalThis.fetch = mockFetch as unknown as typeof fetch;
    mockFetch.mockResolvedValue({ ok: true, json: async () => ({ token: 't', username: 'u' }) });
    await login('alice', 'pw');
    const [url, opts] = mockFetch.mock.calls[0] as [string, RequestInit];
    expect(url).toContain('/auth/login');
    expect(JSON.parse(String(opts.body))).toEqual({ login: 'alice', password: 'pw' });
  });

  it('uploadArtifact sends multipart with auth header', async () => {
    globalThis.fetch = mockFetch as unknown as typeof fetch;
    mockFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
    await uploadArtifact(new File(['x'], 'a.gdml'), 'meta', 'tok123');
    const [, opts] = mockFetch.mock.calls[0] as [string, RequestInit];
    expect((opts.headers as Record<string, string>).authorization).toBe('Bearer tok123');
    expect(opts.body).toBeInstanceOf(FormData);
  });

  it('downloadUrl/previewUrl build relative URLs', () => {
    expect(downloadUrl('id1')).toBe('/artifacts/id1/download');
    expect(previewUrl('id1')).toBe('/artifacts/id1/preview');
  });
});
