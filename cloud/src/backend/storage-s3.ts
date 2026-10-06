//==============================================================================
// G4CARE cloud — S3-compatible object storage (MinIO, SeaweedFS, AWS, …).
//
// Implements the same ObjectStore contract as FileStorage, so the server can
// swap between local files and S3-compatible storage without changing routes.
//==============================================================================

import { Client } from 'minio';
import type { ObjectStore } from './storage.js';

export interface S3Options {
  endPoint: string;
  port: number;
  useSSL: boolean;
  accessKey: string;
  secretKey: string;
  bucket: string;
}

export class S3Storage implements ObjectStore {
  private readonly client: Client;

  constructor(private readonly opts: S3Options) {
    this.client = new Client({
      endPoint: opts.endPoint,
      port: opts.port,
      useSSL: opts.useSSL,
      accessKey: opts.accessKey,
      secretKey: opts.secretKey,
    });
  }

  private async ensureBucket(): Promise<void> {
    const exists = await this.client.bucketExists(this.opts.bucket);
    if (!exists) await this.client.makeBucket(this.opts.bucket);
  }

  async put(id: string, data: Buffer): Promise<void> {
    await this.ensureBucket();
    await this.client.putObject(this.opts.bucket, id, data, data.length);
  }

  async get(id: string): Promise<Buffer> {
    const stream = await this.client.getObject(this.opts.bucket, id);
    const chunks: Buffer[] = [];
    for await (const chunk of stream) chunks.push(chunk as Buffer);
    return Buffer.concat(chunks);
  }

  async delete(id: string): Promise<void> {
    await this.client.removeObject(this.opts.bucket, id);
  }
}
