// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// An exact rational for React and React Native. It follows @zedec/zxv's
// Rational (reduced, den > 0, both parts within int64 like the kernel's
// rat_t, overflow throws instead of rounding, same text grammar) but has no
// dependency at all, so it loads in Hermes/Metro where the SDK's wasm
// loader cannot. The tests cross-check it against @zedec/zxv's Rational.
//
// Floats never come in: a fractional, NaN, infinite or unsafe-integer
// number throws. `toDisplay()` rounds for showing a value, and returns a
// string so the rounded value cannot flow back in by accident.

/** 2^63 - 1. */
export const I64_MAX = (1n << 63n) - 1n;

const SYNTAX = /^[+-]?[0-9]+(?:\.[0-9]+|\/[0-9]+)?$/;
const WIRE = /^(-?)(0|[1-9][0-9]{0,18})\/([1-9][0-9]{0,18})$/;

/** A number that is not exact (float, NaN, infinity, beyond 2^53). */
export class FloatRejectedError extends TypeError {
  readonly status = -24;
  readonly statusName = 'ZXV_E_FLOAT';
  constructor(what: string, value: unknown) {
    super(`${what}: ${String(value)} is not a safe integer; use a bigint, exact text ("12.75", "3/4") or an ExactRational`);
    this.name = 'FloatRejectedError';
  }
}

/** A value outside int64, or malformed text. */
export class RationalError extends RangeError {
  readonly statusName: 'ZXV_E_RANGE' | 'ZXV_E_FORMAT' | 'ZXV_E_ARG';
  constructor(message: string, statusName: 'ZXV_E_RANGE' | 'ZXV_E_FORMAT' | 'ZXV_E_ARG') {
    super(message);
    this.name = 'RationalError';
    this.statusName = statusName;
  }
}

function gcd(a: bigint, b: bigint): bigint {
  a = a < 0n ? -a : a;
  b = b < 0n ? -b : b;
  while (b !== 0n) [a, b] = [b, a % b];
  return a;
}

const TOKEN = Symbol('ExactRational');

/** Anything that converts to an {@link ExactRational} exactly. */
export type RationalInput = ExactRational | string | bigint | number | { readonly num: bigint; readonly den: bigint };

/** An immutable exact rational. */
export class ExactRational {
  readonly num: bigint;
  readonly den: bigint;

  private constructor(num: bigint, den: bigint, token: symbol) {
    if (token !== TOKEN) throw new TypeError('use ExactRational.of / parse / from');
    this.num = num;
    this.den = den;
    Object.freeze(this);
  }

  static readonly ZERO = new ExactRational(0n, 1n, TOKEN);
  static readonly ONE = new ExactRational(1n, 1n, TOKEN);

  /** num/den, reduced. Throws RationalError beyond int64 or for den 0. */
  static of(num: bigint | number, den: bigint | number = 1n): ExactRational {
    let n = toBig('num', num);
    let d = toBig('den', den);
    if (d === 0n) throw new RationalError('zero denominator', 'ZXV_E_ARG');
    if (d < 0n) {
      n = -n;
      d = -d;
    }
    const g = gcd(n, d);
    if (g > 1n) {
      n /= g;
      d /= g;
    }
    if (n > I64_MAX || n < -I64_MAX || d > I64_MAX) throw new RationalError(`${n}/${d} does not fit int64`, 'ZXV_E_RANGE');
    return new ExactRational(n, d, TOKEN);
  }

  /** Exact text: "12.75", "-0.1", "3/4", "42", "+5" (no exponent, no spaces). */
  static parse(text: string): ExactRational {
    if (typeof text !== 'string' || !SYNTAX.test(text)) {
      throw new RationalError(`"${String(text).slice(0, 40)}" is not an exact number (use 12.75 or 3/4)`, 'ZXV_E_FORMAT');
    }
    const neg = text.startsWith('-');
    const body = text.replace(/^[+-]/, '');
    const slash = body.indexOf('/');
    if (slash >= 0) {
      const n = BigInt(body.slice(0, slash));
      const d = BigInt(body.slice(slash + 1));
      if (n > I64_MAX || d > I64_MAX) throw new RationalError(`"${text}" does not fit int64`, 'ZXV_E_RANGE');
      return ExactRational.of(neg ? -n : n, d);
    }
    const dot = body.indexOf('.');
    const ip = BigInt(dot >= 0 ? body.slice(0, dot) : body);
    if (ip > I64_MAX) throw new RationalError(`"${text}" does not fit int64`, 'ZXV_E_RANGE');
    if (dot < 0) return ExactRational.of(neg ? -ip : ip);
    let frac = body.slice(dot + 1);
    if (frac.length > 18) {
      if (/[^0]/.test(frac.slice(18))) throw new RationalError(`"${text}" does not fit int64`, 'ZXV_E_RANGE');
      frac = frac.slice(0, 18);
    }
    const scale = 10n ** BigInt(frac.length);
    const num = ip * scale + BigInt(frac);
    return ExactRational.of(neg ? -num : num, scale);
  }

  /** The canonical wire form "num/den" (zxvd, zxv-web). Anything else is refused. */
  static fromWire(text: string): ExactRational {
    const m = typeof text === 'string' ? WIRE.exec(text) : null;
    if (!m) throw new RationalError(`${JSON.stringify(text)} is not a "num/den" rational`, 'ZXV_E_FORMAT');
    const abs = BigInt(m[2]!);
    const den = BigInt(m[3]!);
    if ((m[1] === '-' && abs === 0n) || abs > I64_MAX || den > I64_MAX || gcd(abs, den) !== 1n) {
      throw new RationalError(`${JSON.stringify(text)} is not a canonical "num/den" rational`, 'ZXV_E_FORMAT');
    }
    return new ExactRational(m[1] === '-' ? -abs : abs, den, TOKEN);
  }

  /** Convert any {@link RationalInput}; a float throws {@link FloatRejectedError}. */
  static from(x: RationalInput, what = 'value'): ExactRational {
    if (x instanceof ExactRational) return x;
    if (typeof x === 'bigint') return ExactRational.of(x);
    if (typeof x === 'number') return ExactRational.of(toBig(what, x));
    if (typeof x === 'string') return ExactRational.parse(x);
    // a Rational of @zedec/zxv (or anything with bigint num/den) converts exactly
    const r = x as { num?: unknown; den?: unknown };
    if (r && typeof r.num === 'bigint' && typeof r.den === 'bigint') return ExactRational.of(r.num, r.den);
    throw new TypeError(`${what}: expected an ExactRational, exact text, a bigint or a safe integer`);
  }

  /** "num/den", always (the wire form). */
  toWire(): string {
    return `${this.num}/${this.den}`;
  }

  add(b: RationalInput): ExactRational {
    const o = ExactRational.from(b);
    return ExactRational.of(this.num * o.den + o.num * this.den, this.den * o.den);
  }
  sub(b: RationalInput): ExactRational {
    const o = ExactRational.from(b);
    return ExactRational.of(this.num * o.den - o.num * this.den, this.den * o.den);
  }
  mul(b: RationalInput): ExactRational {
    const o = ExactRational.from(b);
    return ExactRational.of(this.num * o.num, this.den * o.den);
  }
  div(b: RationalInput): ExactRational {
    const o = ExactRational.from(b);
    if (o.num === 0n) throw new RationalError('division by zero', 'ZXV_E_ARG');
    return ExactRational.of(this.num * o.den, this.den * o.num);
  }
  /** -1, 0 or 1. */
  cmp(b: RationalInput): -1 | 0 | 1 {
    const o = ExactRational.from(b);
    const l = this.num * o.den;
    const r = o.num * this.den;
    return l < r ? -1 : l > r ? 1 : 0;
  }
  equals(b: RationalInput): boolean {
    return this.cmp(b) === 0;
  }
  sign(): -1 | 0 | 1 {
    return this.num < 0n ? -1 : this.num > 0n ? 1 : 0;
  }

  /** Exact text that never rounds: "12.75" when the decimal terminates, "1/3" otherwise. */
  toString(): string {
    const neg = this.num < 0n;
    const n = neg ? -this.num : this.num;
    let d = this.den;
    let twos = 0;
    let fives = 0;
    while (d % 2n === 0n) {
      d /= 2n;
      twos++;
    }
    while (d % 5n === 0n) {
      d /= 5n;
      fives++;
    }
    let body: string;
    if (d === 1n) {
      const places = Math.max(twos, fives);
      const scaled = (n * 10n ** BigInt(places)) / this.den;
      const s = scaled.toString().padStart(places + 1, '0');
      body = places === 0 ? s : `${s.slice(0, -places)}.${s.slice(-places)}`.replace(/\.?0+$/, '');
    } else {
      body = `${n}/${this.den}`;
    }
    return (neg ? '-' : '') + body;
  }

  /**
   * A rounded decimal for display only (half to even), e.g.
   * `ExactRational.parse('2/3').toDisplay(3)` is "0.667". Never feed it back.
   */
  toDisplay(fractionDigits = 2): string {
    if (!Number.isSafeInteger(fractionDigits) || fractionDigits < 0 || fractionDigits > 30) throw new RangeError('fractionDigits: 0..30');
    const scale = 10n ** BigInt(fractionDigits);
    const neg = this.num < 0n;
    const n = (neg ? -this.num : this.num) * scale;
    let q = n / this.den;
    const r = n % this.den;
    if (2n * r > this.den || (2n * r === this.den && q % 2n === 1n)) q += 1n;
    const s = q.toString().padStart(fractionDigits + 1, '0');
    const text = fractionDigits === 0 ? s : `${s.slice(0, -fractionDigits)}.${s.slice(-fractionDigits)}`;
    return (neg && q !== 0n ? '-' : '') + text;
  }

  toJSON(): string {
    return this.toWire();
  }

  /** Arithmetic and comparison operators would go through a float: refused. */
  valueOf(): never {
    throw new TypeError('ExactRational has no number value: use add/cmp/toDisplay');
  }
}

function toBig(what: string, v: bigint | number): bigint {
  if (typeof v === 'bigint') return v;
  if (typeof v === 'number' && Number.isSafeInteger(v)) return BigInt(v);
  throw new FloatRejectedError(what, v);
}

/** An integer input: bigint, a safe-integer number, or a decimal string. Returns its decimal string. */
export function intToWire(v: bigint | number | string, what = 'integer'): string {
  if (typeof v === 'bigint') return v.toString();
  if (typeof v === 'number') return toBig(what, v).toString();
  if (typeof v === 'string' && /^-?(0|[1-9][0-9]{0,18})$/.test(v) && v !== '-0') return v;
  throw new RationalError(`${what}: ${JSON.stringify(String(v))} is not an integer`, 'ZXV_E_FORMAT');
}

/** A decimal-string 64-bit integer from the wire. */
export function intFromWire(v: unknown, what = 'integer'): bigint {
  if (typeof v === 'string' && /^-?(0|[1-9][0-9]{0,18})$/.test(v) && v !== '-0') {
    const b = BigInt(v);
    if (b <= I64_MAX && b >= -I64_MAX - 1n) return b;
  }
  if (typeof v === 'number') throw new FloatRejectedError(`${what} (64-bit integers travel as decimal strings)`, v);
  throw new RationalError(`${what}: ${JSON.stringify(v)} is not a 64-bit decimal integer`, 'ZXV_E_FORMAT');
}
