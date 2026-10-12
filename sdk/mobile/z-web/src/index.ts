// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// @zedec/zxv-web: ZXV in the browser, off the main thread. The page talks
// to a Web Worker that runs the WebAssembly build of @zedec/zxv; this
// module (the page side) imports no Node built-ins and never runs libzxv
// itself.

import { AssetCache, type AssetSpec, type GetOptions, VerifiedBytes } from './cache.js';
import { type WasmInput, ZxvWebClient } from './client.js';
import type { WorkerLike } from './protocol.js';

export * from './client.js';
export * from './cache.js';
export * from './errors.js';
export * from './logic.js';
export { Cap } from '@zedec/zxv/wasm';
export { sha3_256, sha3_256Hex, toHex, isDigestHex } from './sha3.js';
export {
  encodeRational, decodeRational, isWireRational, encodeInt, decodeInt, assertNoFloats, I64_MAX,
} from './wire.js';
export type { WireRational, WireInt } from './wire.js';
export { PROTOCOL } from './protocol.js';
export type {
  Ops, Op, Request, Reply, CancelRequest, Endpoint, WorkerLike, Handle, WireTruth, WireStop, WireToken, WireGenerateOptions,
} from './protocol.js';

/** Options of {@link createZxvWeb}. */
export interface CreateOptions {
  /** The wasm module: where to fetch it and its SHA3-256 (through `cache`), or bytes with their digest. */
  wasm: AssetSpec | WasmInput;
  /** A worker to use. Default: a module Worker on `workerUrl`. */
  worker?: WorkerLike;
  /** Default: `new URL('./worker.js', import.meta.url)` (this package's worker entry). */
  workerUrl?: string | URL;
  /** The offline cache (default: a new {@link AssetCache}). */
  cache?: AssetCache;
  signal?: AbortSignal;
}

/**
 * Start a worker, load and check the wasm module, and connect. The wasm is
 * served from the offline cache when a verified copy is there.
 */
export async function createZxvWeb(options: CreateOptions): Promise<ZxvWebClient> {
  const get: GetOptions = options.signal ? { signal: options.signal } : {};
  const w = options.wasm;
  const wasm: WasmInput = w instanceof VerifiedBytes || 'bytes' in w ? w : await (options.cache ?? new AssetCache()).get(w, get);
  let worker = options.worker;
  if (!worker) {
    const W = (globalThis as { Worker?: new (url: string | URL, o: { type: 'module' }) => WorkerLike }).Worker;
    if (!W) throw new Error('no Worker here: pass options.worker');
    worker = new W(options.workerUrl ?? new URL('./worker.js', import.meta.url), { type: 'module' });
  }
  return ZxvWebClient.connect(worker, wasm, get);
}
