// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Shared test fixtures: the conformance vectors, the fixtures directory and
// both backends.

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { Zxv } from '../src/index.js';

/** sdk/typescript */
export const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** sdk/conformance/fixtures */
export const fixtures = join(pkgRoot, '..', 'conformance', 'fixtures');

/** A vector integer: big ones arrive as decimal strings (see below). */
export type Int = number | string;
type Rat = [Int, Int];

/** The parts of vectors.json the tests read. */
export interface Vectors {
  abi: [number, number, number];
  status: { code: number; name: string; class: number }[];
  rational_ops: { a: Rat; b: Rat; add: Rat; sub: Rat; mul: Rat; div: Rat; cmp: number }[];
  rational_parse: { text: string; status: number; value?: Rat; format?: string }[];
  ieee754: { bits: string; status: number; value?: Rat }[];
  q16: { value: Rat; status: number; q16: number }[];
  lpres: { names: string[]; not: number[]; and: number[][]; or: number[][] };
  ubh168: { trunk: number; shell: number; sync: number; tag: number; s_plus: [number, number, number]; s_minus: [number, number]; frame: string; truth: number }[];
  ubh168_idle: string[];
  hash: { utf8: string; keccak256: string; sha3_256: string; shake256_64: string }[];
  ed25519: { sk: string; pk: string; msg_utf8: string; sig: string };
  mldsa87: { seed: string; ctx_utf8: string; msg_utf8: string; pk_sha3_256: string; sk_sha3_256: string; sig_deterministic_sha3_256: string };
  mlkem1024: { d: string; z: string; m: string; ek_sha3_256: string; ct_sha3_256: string; ss: string };
  swarm: { levels: number; tokens_per_cycle: number; models: [number, number][]; mood: [number, number]; feelings: [number, number, number][];
    allotments: [number, number, number][]; level_capacity: number[] };
  generate: { model: string; tokenizer: string; prompt: string; n_ctx: number; prompt_tokens: number[];
    greedy: { max_tokens: number; tokens: number[] };
    sampled: { max_tokens: number; temperature_q16: number; top_k: number; top_p_q16: number; repeat_penalty_q16: number; repeat_last_n: number;
      seed: number; tokens: number[] } };
  evm_state: { channel_id: string; nonce: number; addresses: string[]; balances: number[]; encoding: string; digest: string };
  phase_cycle: number;
  hex: {
    base_names: string[]; polarity_names: string[]; phase_names: string[]; action_names: string[];
    lp_to_base: number[]; base_to_lp: number[]; phase_next: number[];
    tags: { tag: number; name: string; base: number; polarity: number; phase: number; action: number }[];
    not: number[]; and: number[][]; or: number[][]; step: number[][]; set: number[][];
    polarity: { pos0: number; neg0: number; pos1: number; neg1: number; stale: boolean; polarity: number }[];
    qlog: { domain: string; head: string;
      records: { seq: number; tag: number; reason: number; submitter: number; subject: string; prev: string; hash: string }[] };
  };
}

// JSON.parse would round integers above 2^53 (998244359987710471 is one):
// quote every integer of 16+ digits first, and read vector integers with BigInt().
const raw = readFileSync(join(pkgRoot, '..', 'conformance', 'vectors.json'), 'utf8')
  .replace(/([[,:]\s*)(-?\d{16,})(?=\s*[\],}])/g, '$1"$2"');
/** The answer key. */
export const V: Vectors = JSON.parse(raw) as Vectors;

/** A Rational-shaped pair from the vectors, as bigints. */
export const big = (r: Rat): [bigint, bigint] => [BigInt(r[0]), BigInt(r[1])];

/** The prompt fixture text. */
export const promptText = readFileSync(join(fixtures, V.generate.prompt), 'utf8');

const cache = new Map<string, Promise<Zxv>>();

/** The loaded library for a backend (one per process). */
export function load(kind: 'native' | 'wasm'): Promise<Zxv> {
  let p = cache.get(kind);
  if (!p) cache.set(kind, (p = Zxv.load({ backend: kind })));
  return p;
}

/** Both backends, in a stable order. */
export const BACKENDS = ['native', 'wasm'] as const;
