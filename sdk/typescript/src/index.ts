// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv for Node.js: the Node-API addon backend (default) and the
// WebAssembly backend, behind one typed API.

import { readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { loadNative } from './internal/native.js';
import { setPlatform } from './internal/platform.js';

const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');

async function read(where: URL | string): Promise<Uint8Array> {
  if (where instanceof URL && where.protocol !== 'file:') return new Uint8Array(await (await fetch(where)).arrayBuffer());
  if (typeof where === 'string' && /^https?:\/\//.test(where)) return read(new URL(where));
  return new Uint8Array(await readFile(where));
}

setPlatform({
  name: 'node',
  loadNative: (o) => loadNative({ ...(o.libraryPath ? { libraryPath: o.libraryPath } : {}), ...(o.addonPath ? { addonPath: o.addonPath } : {}) }),
  wasmBytes: async (where) => read(where ?? process.env['ZXV_WASM_PATH'] ?? join(pkgRoot, 'wasm', 'zxv.wasm')),
  readFile: read,
});

export * from './api.js';
