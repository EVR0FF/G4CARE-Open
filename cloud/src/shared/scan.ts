//==============================================================================
// G4CARE cloud — configuration dependency scanner.
//
// Walks a YAML config to find every file it references, so `g4care-cloud pack`
// can bundle a whole detector (config + INCLUDE blocks + GDML + macros + .so).
//==============================================================================

import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, resolve, relative, normalize } from 'node:path';
import { parse as parseYaml } from 'yaml';

type AnyRecord = Record<string, unknown>;

function getString(obj: AnyRecord | undefined, ...path: string[]): string | undefined {
  let cur: unknown = obj;
  for (const k of path) {
    if (cur && typeof cur === 'object') cur = (cur as AnyRecord)[k];
    else return undefined;
  }
  return typeof cur === 'string' ? cur : undefined;
}

function asRecord(v: unknown): AnyRecord | undefined {
  return v !== null && typeof v === 'object' ? (v as AnyRecord) : undefined;
}

/// Collect all file references from a parsed config document.
function collectFileRefs(doc: AnyRecord): string[] {
  const refs: string[] = [];
  const push = (v: unknown) => {
    if (typeof v === 'string' && v.trim()) refs.push(v.trim());
  };

  // INCLUDE (modular config) — scalar or sequence.
  if (Array.isArray(doc.INCLUDE)) doc.INCLUDE.forEach(push);
  else push(doc.INCLUDE);

  // MACROS — scalar, sequence of scalars, or sequence of { file: ... }.
  if (Array.isArray(doc.MACROS)) {
    for (const m of doc.MACROS) {
      if (typeof m === 'string') push(m);
      else push(getString(asRecord(m), 'file'));
    }
  } else {
    push(doc.MACROS);
  }

  // Fixed dotted keys.
  push(getString(doc, 'GEOMETRY', 'FILE'));
  push(getString(doc, 'GEOMETRY', 'WORLD', 'file'));
  push(getString(doc, 'GEOMETRY', 'LIBRARY'));
  push(getString(doc, 'PHYSICS', 'BASIC', 'MACRO'));
  push(getString(doc, 'CROSS_SECTION', 'BEB_FILE'));

  // GEOMETRY.OBJECTS.*.file (per-object GDML).
  const objects = asRecord(asRecord(doc.GEOMETRY)?.OBJECTS);
  if (objects) {
    for (const obj of Object.values(objects)) push(getString(asRecord(obj), 'file'));
  }

  // PHYSICS.ADVANCED.*.{MACRO, LIBRARY}.
  const advanced = asRecord(asRecord(doc.PHYSICS)?.ADVANCED);
  if (advanced) {
    for (const mod of Object.values(advanced)) {
      const m = asRecord(mod);
      push(getString(m, 'MACRO'));
      push(getString(m, 'LIBRARY'));
    }
  }

  return refs;
}

/// Recursively collect the entry file + all referenced files, as paths relative
/// to `baseDir`. Missing references are silently skipped.
export async function scanDependencies(
  entryPath: string,
  baseDir: string,
  seen: Set<string> = new Set(),
  strict = false,
): Promise<string[]> {
  const abs = resolve(entryPath);
  if (seen.has(abs)) return [];
  seen.add(abs);

  const found: string[] = [normalize(relative(baseDir, abs))];

  let doc: AnyRecord | undefined;
  try {
    const parsed = parseYaml(await readFile(abs, 'utf8'));
    doc = asRecord(parsed);
  } catch {
    doc = undefined;
  }
  if (!doc) return found;

  for (const ref of collectFileRefs(doc)) {
    const refAbs = resolve(dirname(abs), ref);
    if (!existsSync(refAbs)) {
      if (strict) {
        throw new Error(`referenced file not found: "${ref}" (referenced by ${normalize(relative(baseDir, abs))})`);
      }
      console.warn(`scan: skipping missing referenced file "${ref}" (referenced by ${normalize(relative(baseDir, abs))})`);
      continue;
    }
    const rel = normalize(relative(baseDir, refAbs));
    const isYaml = /\.ya?ml$/i.test(refAbs);

    if (isYaml) {
      for (const sub of await scanDependencies(refAbs, baseDir, seen, strict)) {
        if (!found.includes(sub)) found.push(sub);
      }
    } else if (!found.includes(rel)) {
      found.push(rel);
    }
  }

  return found;
}
