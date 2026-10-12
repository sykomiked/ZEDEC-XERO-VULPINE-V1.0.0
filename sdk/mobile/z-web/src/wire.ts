// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The exact encodings used on the page <-> worker wire. No float ever
// crosses it:
//   - a rational is the string "num/den", reduced, den > 0, both parts within
//     int64 (|num|, den <= 2^63 - 1, like the kernel's rat_t): "1/3", "-5/2",
//     "7/1", "0/1";
//   - a 64-bit integer (amounts, balances, entry ids, seeds, epochs,
//     capability bits) is a decimal string: "250050", "-12";
//   - any other number in a message must be a safe integer (ids, counts).
// Both ends refuse anything else; nothing is rounded. This module has no
// dependencies, so the worker and the page share it.

import { ZxvWebError } from './errors.js';

/** A rational on the wire: "num/den", reduced, den > 0. */
export type WireRational = string;
/** A 64-bit integer on the wire: a decimal string. */
export type WireInt = string;

/** 2^63 - 1, the bound of both parts of a kernel rational. */
export const I64_MAX = (1n << 63n) - 1n;
const U64_MAX = (1n << 64n) - 1n;

const RAT = /^(-?)(0|[1-9][0-9]{0,18})\/([1-9][0-9]{0,18})$/;
const INT = /^-?(0|[1-9][0-9]{0,19})$/;

function gcd(a: bigint, b: bigint): bigint {
  a = a < 0n ? -a : a;
  while (b !== 0n) [a, b] = [b, a % b];
  return a;
}

/** A float or an integer beyond 2^53 given as a JS number. */
export function floatRejected(what: string, value: unknown): ZxvWebError {
  return new ZxvWebError(
    `${what}: ${String(value)} is not a safe integer; pass a bigint, a decimal string or a "num/den" string (floats are never accepted)`,
    { name: 'FloatRejectedError', status: -24, statusName: 'ZXV_E_FLOAT', statusClass: 'USAGE' },
  );
}

/** A malformed wire value. */
export function wireFormat(what: string, value: unknown): ZxvWebError {
  return new ZxvWebError(`${what}: ${JSON.stringify(String(value).slice(0, 48))} is not a valid wire value`, {
    name: 'ZxvWebUsageError', status: -22, statusName: 'ZXV_E_FORMAT', statusClass: 'USAGE',
  });
}

/** Encode reduced parts as "num/den" (reduces; checks the int64 bounds). */
export function encodeRational(num: bigint, den: bigint): WireRational {
  if (typeof num !== 'bigint' || typeof den !== 'bigint') throw wireFormat('rational parts must be bigint', `${typeof num}/${typeof den}`);
  if (den === 0n) throw wireFormat('rational', `${num}/0`);
  if (den < 0n) {
    num = -num;
    den = -den;
  }
  const g = gcd(num, den);
  if (g > 1n) {
    num /= g;
    den /= g;
  }
  if (num > I64_MAX || num < -I64_MAX || den > I64_MAX) {
    throw new ZxvWebError(`rational ${num}/${den} does not fit int64`, {
      name: 'ZxvWebUsageError', status: -8, statusName: 'ZXV_E_RANGE', statusClass: 'USAGE',
    });
  }
  return `${num}/${den}`;
}

/**
 * Decode "num/den". Only the canonical form is accepted: reduced, den > 0,
 * no sign on zero, no leading zeros, no spaces, no decimals.
 */
export function decodeRational(text: unknown, what = 'rational'): { num: bigint; den: bigint } {
  if (typeof text !== 'string') {
    if (typeof text === 'number') throw floatRejected(what, text);
    throw wireFormat(what, text);
  }
  const m = RAT.exec(text);
  if (!m) throw wireFormat(what, text);
  const abs = BigInt(m[2]!);
  const den = BigInt(m[3]!);
  if (m[1] === '-' && abs === 0n) throw wireFormat(what, text);
  if (abs > I64_MAX || den > I64_MAX) throw wireFormat(what, text);
  if (gcd(abs, den) !== 1n) throw wireFormat(`${what} (not reduced)`, text);
  return { num: m[1] === '-' ? -abs : abs, den };
}

/** True if `text` is a canonical wire rational. */
export function isWireRational(text: unknown): text is WireRational {
  try {
    decodeRational(text);
    return true;
  } catch {
    return false;
  }
}

/** Encode an integer (bigint, or a safe-integer number) as a decimal string. */
export function encodeInt(value: bigint | number, what = 'integer'): WireInt {
  if (typeof value === 'bigint') return value.toString();
  if (typeof value === 'number' && Number.isSafeInteger(value)) return String(value);
  throw floatRejected(what, value);
}

/** Decode a decimal-string integer (signed 64-bit, or unsigned 64-bit with `unsigned`). */
export function decodeInt(text: unknown, what = 'integer', unsigned = false): bigint {
  if (typeof text !== 'string') {
    if (typeof text === 'number') throw floatRejected(`${what} (64-bit integers travel as decimal strings)`, text);
    throw wireFormat(what, text);
  }
  if (!INT.test(text) || text === '-0') throw wireFormat(what, text);
  const v = BigInt(text);
  if (unsigned ? v < 0n || v > U64_MAX : v > I64_MAX || v < -I64_MAX - 1n) throw wireFormat(what, text);
  return v;
}

/**
 * Check that every number in a message is a safe integer. Floats, NaN,
 * infinities and integers beyond 2^53 are refused wherever they appear.
 */
export function assertNoFloats(value: unknown, path = 'message', depth = 0): void {
  if (depth > 16) throw wireFormat(`${path} (nested too deeply)`, '...');
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw floatRejected(path, value);
    return;
  }
  if (typeof value !== 'object' || value === null) return;
  if (ArrayBuffer.isView(value) || value instanceof ArrayBuffer) return;
  if (typeof SharedArrayBuffer === 'function' && value instanceof SharedArrayBuffer) return;
  if (Array.isArray(value)) {
    value.forEach((v, i) => assertNoFloats(v, `${path}[${i}]`, depth + 1));
    return;
  }
  for (const [k, v] of Object.entries(value)) assertNoFloats(v, `${path}.${k}`, depth + 1);
}
