//==============================================================================
// G4CARE - migrate.ts — Cloud backend: migrations
//
// Cloud backend: migrations
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { readdir, readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Pool } from 'pg';

export async function migrate(pool: Pool): Promise<void> {
  const here = dirname(fileURLToPath(import.meta.url));
  const dir = join(here, '..', '..', 'migrations');
  const files = (await readdir(dir)).filter((f) => f.endsWith('.sql')).sort();
  for (const f of files) {
    const sql = await readFile(join(dir, f), 'utf8');
    // Strip "--" comments first (a comment may contain ";" which would break a
    // naive split), then execute each statement separately.
    const cleaned = sql.split('\n').map((l) => l.replace(/--.*$/, '')).join('\n');
    for (const stmt of cleaned.split(';').map((s) => s.trim()).filter(Boolean)) {
      await pool.query(stmt);
    }
  }
}

export async function migrateDatabase(connectionString: string): Promise<void> {
  const pool = new Pool({ connectionString });
  try {
    await migrate(pool);
  } finally {
    await pool.end();
  }
}
