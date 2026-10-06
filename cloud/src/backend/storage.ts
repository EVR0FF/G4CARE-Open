//==============================================================================
// G4CARE cloud — object storage abstraction (filesystem implementation).
//
// Production deployments can swap this for an S3-compatible backend (MinIO,
// SeaweedFS, AWS, …) without changing
// the server code (same put/get/delete contract).
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
