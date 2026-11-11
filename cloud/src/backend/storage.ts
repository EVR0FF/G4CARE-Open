//==============================================================================
// G4CARE - storage.ts — Cloud backend: storage
//
// Cloud backend: storage
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { mkdir, writeFile, readFile, rm } from 'node:fs/promises';
import { join } from 'node:path';

export interface ObjectStore {
  put(id: string, data: Buffer): Promise<void>;
  get(id: string): Promise<Buffer>;
  delete(id: string): Promise<void>;
}

export class FileStorage implements ObjectStore {
  constructor(private readonly root: string) {}

  async ensure(): Promise<void> {
    await mkdir(this.root, { recursive: true });
  }

  private key(id: string): string {
    return join(this.root, id);
  }

  async put(id: string, data: Buffer): Promise<void> {
    await this.ensure();
    await writeFile(this.key(id), data);
  }

  async get(id: string): Promise<Buffer> {
    return readFile(this.key(id));
  }

  async delete(id: string): Promise<void> {
    await rm(this.key(id), { force: true });
  }
}
