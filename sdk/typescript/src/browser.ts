// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv for browsers, Workers, Deno and Bun: the WebAssembly backend
// only. No Node built-ins are imported. Generation runs synchronously on
// the calling thread, so call it from a Web Worker to keep a page
// responsive.

import { setPlatform } from './internal/platform.js';

async function fetchBytes(where: URL | string): Promise<Uint8Array> {
  const r = await fetch(where);
  if (!r.ok) throw new Error(`@zedec/zxv: cannot fetch ${String(where)}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

setPlatform({
  name: 'browser',
  wasmBytes: async (where) => fetchBytes(where ?? new URL('../../wasm/zxv.wasm', import.meta.url)),
  readFile: fetchBytes,
});

export * from './api.js';
