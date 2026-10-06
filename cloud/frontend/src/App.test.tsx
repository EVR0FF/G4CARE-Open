import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import App from './App';

const mockFetch = vi.fn();

const artifact = {
  id: '1',
  type: 'gdml',
  name: 'a.gdml',
  meta: {
    description: 'd',
    tags: ['detector'],
    author: { name: 'G4CARE Developers', alias: 'g4care-dev' },
    affiliation: { group: 'G4CARE', institute: 'TPU' },
  },
  sha256: 'a'.repeat(64),
  size: 1,
  createdAt: '2026-01-01',
  storageKey: 'k',
};

function makeToken(): string {
  const enc = (o: object) => btoa(JSON.stringify(o)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  const header = enc({ alg: 'HS256', typ: 'JWT' });
  const payload = enc({ sub: 'alice', username: 'alice', exp: Math.floor(Date.now() / 1000) + 3600 });
  return `${header}.${payload}.sig`;
}

describe('App', () => {
  beforeEach(() => {
    mockFetch.mockReset();
    localStorage.clear();
    globalThis.fetch = mockFetch as unknown as typeof fetch;
    mockFetch.mockResolvedValue({ ok: true, json: async () => [artifact] });
  });

  it('renders artifact cards fetched from the API when authenticated', async () => {
    localStorage.setItem('g4care_token', makeToken());
    localStorage.setItem('g4care_username', 'alice');
    render(<App />);
    expect(await screen.findByText('a.gdml')).toBeInTheDocument();
    expect(screen.getByText('g4care-dev · TPU')).toBeInTheDocument();
  });

  it('shows the auth window when not authenticated', () => {
    render(<App />);
    expect(screen.getByPlaceholderText('Email or username')).toBeInTheDocument();
    expect(screen.getByPlaceholderText('Password')).toBeInTheDocument();
  });
});
