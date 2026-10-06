import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import ArtifactCard from './ArtifactCard';
import type { ArtifactRecord } from '../types';

const rec: ArtifactRecord = {
  id: '1',
  type: 'gdml',
  name: 'scene.gdml',
  meta: {
    description: 'Test detector',
    tags: ['detector', 'nano'],
    author: { name: 'G4CARE Developers', alias: 'g4care-dev' },
    affiliation: { group: 'G4CARE', institute: 'TPU' },
  },
  sha256: 'a'.repeat(64),
  size: 1234,
  createdAt: '2026-01-01',
  storageKey: 'obj/x',
};

describe('ArtifactCard', () => {
  it('renders name, description, tags, author and institute', () => {
    render(<ArtifactCard artifact={rec} onClick={() => {}} />);
    expect(screen.getByText('scene.gdml')).toBeInTheDocument();
    expect(screen.getByText('Test detector')).toBeInTheDocument();
    expect(screen.getByText('detector')).toBeInTheDocument();
    expect(screen.getByText('nano')).toBeInTheDocument();
    expect(screen.getByText('g4care-dev · TPU')).toBeInTheDocument();
  });
});
