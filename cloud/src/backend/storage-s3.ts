//==============================================================================
// G4CARE - storage-s3.ts — Cloud backend: S3 storage
//
// Cloud backend: S3 storage
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
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
