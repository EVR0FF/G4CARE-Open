//==============================================================================
// G4CARE - bundle.ts — Cloud shared: bundle
//
// Cloud shared: bundle
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import * as tar from 'tar';
import { mkdir, mkdtemp, readFile, writeFile, copyFile, rm } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';

import type { BundleFile, MetaInfo } from './types.js';
import { stringifyManifest, parseManifest, sha256File, fileSize } from './manifest.js';
import { scanDependencies } from './scan.js';

export const MANIFEST_NAME = 'manifest.toml';

/// Pack `files` (paths relative to `baseDir`) plus a generated manifest into a
/// gzipped `.g4c` tarball at `outPath`. Returns the generated file list.
export async function packBundle(
  baseDir: string,
  files: string[],
  meta: MetaInfo,
  outPath: string,
): Promise<BundleFile[]> {
  const stage = await mkdtemp(join(tmpdir(), 'g4c-pack-'));
  try {
    const list: BundleFile[] = [];
    for (const f of files) {
      const src = resolve(baseDir, f);
      const dst = resolve(stage, f);
      await mkdir(dirname(dst), { recursive: true });
      await copyFile(src, dst);
      list.push({ path: f, sha256: await sha256File(src), size: await fileSize(src) });
    }

    await writeFile(join(stage, MANIFEST_NAME), stringifyManifest(meta, list));
    await tar.c({ gzip: true, cwd: stage, file: outPath }, ['.']);
    return list;
  } finally {
    await rm(stage, { recursive: true, force: true });
  }
}

/// Extract a `.g4c` tarball into `outDir`.
export async function unpackBundle(tarPath: string, outDir: string): Promise<void> {
  await mkdir(outDir, { recursive: true });
  await tar.x({ cwd: outDir, file: tarPath });
}

/// Read the manifest out of an existing `.g4c` tarball.
export async function readManifestFromBundle(
  tarPath: string,
): Promise<{ meta: MetaInfo; files: BundleFile[] }> {
  const stage = await mkdtemp(join(tmpdir(), 'g4c-manifest-'));
  try {
    await tar.x({
      cwd: stage,
      file: tarPath,
      filter: (p: string) => p === MANIFEST_NAME || p === `./${MANIFEST_NAME}`,
    });
    const text = await readFile(join(stage, MANIFEST_NAME), 'utf8');
    return parseManifest(text);
  } finally {
    await rm(stage, { recursive: true, force: true });
  }
}
