// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Runtime argument checks at the public boundary. TypeScript types stop
// most mistakes at compile time; these stop the rest (plain JS callers,
// `any`, JSON input), and above all they refuse fractional numbers with
// FloatRejectedError instead of truncating them.

import { FloatRejectedError, Status, usage } from '../errors.js';

/** An integer argument: a bigint, or a number that is an integer. */
export type IntLike = bigint | number;

const I64_MIN = -(1n << 63n);
const I64_MAX = (1n << 63n) - 1n;
const U64_MAX = (1n << 64n) - 1n;

function floatRejected(name: string, v: number): FloatRejectedError {
  return new FloatRejectedError(
    `${name} = ${String(v)} is not an integer; ZXV takes no floats. ` +
      'Pass a bigint or an integer, a Rational, or convert on purpose with Rational.fromNumberExact(x).',
  );
}

/** Convert to bigint, refusing fractional numbers and unsafe integers. */
export function toBig(name: string, v: unknown): bigint {
  if (typeof v === 'bigint') return v;
  if (typeof v === 'number') {
    if (!Number.isFinite(v) || !Number.isInteger(v)) throw floatRejected(name, v);
    if (!Number.isSafeInteger(v)) {
      throw usage(`${name} = ${v} is beyond 2^53 and may already have been rounded; pass a bigint`, Status.E_RANGE);
    }
    return BigInt(v);
  }
  throw usage(`${name} must be a bigint or an integer number, not ${typeof v}`);
}

function inRange(name: string, x: bigint, lo: bigint, hi: bigint): bigint {
  if (x < lo || x > hi) throw usage(`${name} = ${x} is outside [${lo}, ${hi}]`, Status.E_RANGE);
  return x;
}

/** int64_t, as bigint. */
export const i64 = (name: string, v: unknown): bigint => inRange(name, toBig(name, v), I64_MIN, I64_MAX);
/** uint64_t, as bigint. */
export const u64 = (name: string, v: unknown): bigint => inRange(name, toBig(name, v), 0n, U64_MAX);
/** int32_t, as number. */
export const i32 = (name: string, v: unknown): number => Number(inRange(name, toBig(name, v), -(1n << 31n), (1n << 31n) - 1n));
/** uint32_t, as number. */
export const u32 = (name: string, v: unknown): number => Number(inRange(name, toBig(name, v), 0n, 0xffffffffn));
/** uint16_t, as number. */
export const u16 = (name: string, v: unknown): number => Number(inRange(name, toBig(name, v), 0n, 0xffffn));
/** uint8_t, as number. */
export const u8 = (name: string, v: unknown): number => Number(inRange(name, toBig(name, v), 0n, 0xffn));

/** A byte array, optionally of an exact length. */
export function bytes(name: string, v: unknown, len?: number): Uint8Array {
  if (!(v instanceof Uint8Array)) throw usage(`${name} must be a Uint8Array`);
  if (len !== undefined && v.length !== len) throw usage(`${name} must be ${len} bytes, got ${v.length}`);
  return v;
}

/** A string argument. */
export function str(name: string, v: unknown): string {
  if (typeof v !== 'string') throw usage(`${name} must be a string`);
  return v;
}

/** Lower-case hex of bytes. */
export function toHex(b: Uint8Array): string {
  let s = '';
  for (const x of b) s += x.toString(16).padStart(2, '0');
  return s;
}

/** Bytes of a hex string (even length, [0-9a-fA-F]). */
export function fromHex(h: string): Uint8Array {
  if (h.length % 2 !== 0 || /[^0-9a-fA-F]/.test(h)) throw usage(`not a hex string: ${h.slice(0, 40)}`, Status.E_FORMAT);
  const out = new Uint8Array(h.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(h.slice(2 * i, 2 * i + 2), 16);
  return out;
}
