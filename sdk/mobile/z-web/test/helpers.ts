// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Shared test fixtures: the conformance vectors, the fixture model, the
// wasm module built by sdk/typescript (scripts/build-wasm.mjs), and a
// client on a real worker thread. Nothing here touches the network.

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { sha3_256Hex, ZxvWebClient } from '../src/index.js';
import { startNodeWorker } from '../src/node.js';

/** sdk/mobile/z-web */
export const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const sdkRoot = resolve(pkgRoot, '..', '..');
/** sdk/conformance/fixtures */
export const fixtures = join(sdkRoot, 'conformance', 'fixtures');
/** The wasm module of @zedec/zxv. */
export const wasmPath = join(sdkRoot, 'typescript', 'wasm', 'zxv.wasm');

type Int = number | string;

/** The parts of vectors.json these tests read. */
export interface Vectors {
  lpres: { names: string[]; not: number[]; and: number[][]; or: number[][] };
  hash: { utf8: string; sha3_256: string }[];
  generate: {
    model: string; tokenizer: string; prompt: string; n_ctx: number; prompt_tokens: number[];
    greedy: { max_tokens: number; tokens: number[] };
    sampled: { max_tokens: number; temperature_q16: number; top_k: number; top_p_q16: number; repeat_penalty_q16: number; repeat_last_n: number;
      seed: Int; tokens: number[] };
  };
  hex: { tags: { tag: number; name: string; base: number; polarity: number; phase: number; action: number }[]; and: number[][]; not: number[] };
}

// JSON.parse would round integers above 2^53: quote every 16+ digit integer first.
const raw = readFileSync(join(sdkRoot, 'conformance', 'vectors.json'), 'utf8')
  .replace(/([[,:]\s*)(-?\d{16,})(?=\s*[\],}])/g, '$1"$2"');
/** The answer key. */
export const V: Vectors = JSON.parse(raw) as Vectors;

/** A file with its SHA3-256, as the client wants it. */
export function asset(path: string): { bytes: Uint8Array; sha3_256: string } {
  const bytes = new Uint8Array(readFileSync(path));
  return { bytes, sha3_256: sha3_256Hex(bytes) };
}

/** The wasm module with its digest. */
export const wasm = asset(wasmPath);

/** A client on a fresh worker thread. */
export function connect(): Promise<ZxvWebClient> {
  return ZxvWebClient.connect(startNodeWorker(), wasm);
}

/** Expect `p` to reject with an error matching `fields`. */
export async function rejectsWith(p: Promise<unknown>, fields: Record<string, unknown>): Promise<Error> {
  try {
    await p;
  } catch (e) {
    for (const [k, v] of Object.entries(fields)) {
      const got = (e as Record<string, unknown>)[k];
      if (got !== v) throw new Error(`expected ${k} = ${String(v)}, got ${String(got)} (${String((e as Error).message)})`);
    }
    return e as Error;
  }
  throw new Error('expected a rejection');
}
