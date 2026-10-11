// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { FloatRejectedError, Status, usage } from './errors.js';
import { type IntLike, toBig } from './internal/check.js';

const I64_MAX = (1n << 63n) - 1n;

function gcd(a: bigint, b: bigint): bigint {
  if (a < 0n) a = -a;
  if (b < 0n) b = -b;
  while (b) [a, b] = [b, a % b];
  return a === 0n ? 1n : a;
}

/** Reduce and range-check exactly as the kernel's rational.c does. */
function make(num: bigint, den: bigint, op: string): Rational {
  if (den === 0n) throw usage(`${op}: zero denominator`, Status.E_ARG);
  if (num === 0n) return new Rational(0n, 1n, TOKEN);
  if (den < 0n) {
    num = -num;
    den = -den;
  }
  const g = gcd(num, den);
  num /= g;
  den /= g;
  if (num > I64_MAX || num < -I64_MAX || den > I64_MAX) {
    throw usage(`${op} overflows 64-bit numerator/denominator`, Status.E_RANGE);
  }
  return new Rational(num, den, TOKEN);
}

const TOKEN = Symbol('Rational');
const SYNTAX = /^[+-]?[0-9]+(?:[./][0-9]+)?$/;

/**
 * An exact rational number: the kernel's `rat_t` (orbital_compat IR
 * numeric normal form), with bigint parts.
 *
 * Immutable, always reduced, denominator > 0, and both parts within
 * int64 (|num| <= 2^63 - 1) exactly like `zxv_rat_*`: an operation whose
 * exact result does not fit throws {@link ZxvUsageError} with status
 * ZXV_E_RANGE instead of rounding. The arithmetic runs in TypeScript with
 * bigint and is cross-checked against the C library by the test suite, so
 * it is available before (and without) loading a backend.
 *
 * Construct from integers ({@link Rational.of}), exact text
 * ({@link Rational.parse}), or, on purpose, from a float
 * ({@link Rational.fromNumberExact}). There is no other way in.
 */
export class Rational {
  /** Numerator (sign carrier). */
  readonly num: bigint;
  /** Denominator, always > 0. */
  readonly den: bigint;

  /** @internal Use {@link Rational.of}. */
  constructor(num: bigint, den: bigint, token: symbol) {
    if (token !== TOKEN) throw usage('use Rational.of, Rational.parse or Rational.fromNumberExact');
    this.num = num;
    this.den = den;
    Object.freeze(this);
  }

  /** 0 */
  static readonly ZERO: Rational = new Rational(0n, 1n, TOKEN);
  /** 1 */
  static readonly ONE: Rational = new Rational(1n, 1n, TOKEN);

  /**
   * `num / den` from integers (bigint, or integer numbers; a fractional
   * number throws {@link FloatRejectedError}). Reduced; a zero denominator
   * is ZXV_E_ARG.
   */
  static of(num: IntLike, den: IntLike = 1n): Rational {
    const n = toBig('num', num);
    const d = toBig('den', den);
    if (n > I64_MAX || n < -I64_MAX || d > I64_MAX || d < -I64_MAX) {
      throw usage('numerator and denominator must fit int64 (and not be INT64_MIN)', Status.E_RANGE);
    }
    return make(n, d, 'Rational.of');
  }

  /**
   * Parse exact text: `"12.75"`, `"-0.1"`, `"3/4"`, `"42"`, `"+5"`.
   * Grammar `[+-]?D+ ( '.' D+ | '/' D+ )?`; no spaces, no exponent, no
   * "NaN" (ZXV_E_FORMAT); `"1/0"` is ZXV_E_ARG; a value that does not fit
   * int64 is ZXV_E_RANGE. Same rules as `zxv_rat_parse`.
   */
  static parse(text: string): Rational {
    if (typeof text !== 'string' || !SYNTAX.test(text)) {
      throw usage(`"${String(text).slice(0, 40)}" is not an exact number (use 12.75 or 3/4)`, Status.E_FORMAT);
    }
    const neg = text.startsWith('-');
    const body = text.replace(/^[+-]/, '');
    const range = () => usage(`"${text.slice(0, 40)}" does not fit 64-bit numerator/denominator`, Status.E_RANGE);
    const slash = body.indexOf('/');
    if (slash >= 0) {
      const n = BigInt(body.slice(0, slash));
      const d = BigInt(body.slice(slash + 1));
      if (n > I64_MAX) throw range();
      if (d === 0n) throw usage(`"${text.slice(0, 40)}" has a zero denominator`, Status.E_ARG);
      if (d > I64_MAX) throw range();
      return wrapRange(() => make(neg ? -n : n, d, 'parse'), range);
    }
    const dot = body.indexOf('.');
    const ip = BigInt(dot >= 0 ? body.slice(0, dot) : body);
    if (ip > I64_MAX) throw range();
    if (dot < 0) return wrapRange(() => make(neg ? -ip : ip, 1n, 'parse'), range);
    let frac = body.slice(dot + 1);
    // Beyond 18 places only trailing zeros are exact in int64 (kernel rule).
    if (frac.length > 18) {
      if (/[^0]/.test(frac.slice(18))) throw range();
      frac = frac.slice(0, 18);
    }
    const scale = 10n ** BigInt(frac.length);
    const num = ip * scale + BigInt(frac);
    return wrapRange(() => make(neg ? -num : num, scale, 'parse'), range);
  }

  /**
   * THE float boundary: the exact value of an IEEE-754 double. Every finite
   * double is exactly m * 2^e, so `fromNumberExact(0.1)` is
   * 3602879701896397/36028797018963968, not 1/10: the drift is made
   * visible instead of hidden. NaN and infinities throw
   * {@link FloatRejectedError}; a value whose exact form needs more than 63
   * bits (e.g. 1e-300) throws ZXV_E_RANGE. Same result as
   * `zxv_rat_from_ieee754` on the raw bits.
   */
  static fromNumberExact(x: number): Rational {
    if (typeof x !== 'number') throw usage('fromNumberExact takes a number');
    const dv = new DataView(new ArrayBuffer(8));
    dv.setFloat64(0, x, false);
    return Rational.fromIeee754Bits(dv.getBigUint64(0, false));
  }

  /** The exact rational of raw IEEE-754 binary64 bits (see {@link Rational.fromNumberExact}). */
  static fromIeee754Bits(bits: bigint): Rational {
    const b = BigInt.asUintN(64, toBig('bits', bits));
    const neg = b >> 63n !== 0n;
    const exp = Number((b >> 52n) & 0x7ffn);
    const frac = b & ((1n << 52n) - 1n);
    if (exp === 0x7ff) throw new FloatRejectedError(frac ? 'NaN has no rational value' : 'infinity has no rational value');
    let m: bigint;
    let e: number;
    if (exp === 0) {
      if (frac === 0n) return Rational.ZERO;
      m = frac;
      e = -1074;
    } else {
      m = frac | (1n << 52n);
      e = exp - 1075;
    }
    while ((m & 1n) === 0n) {
      m >>= 1n;
      e++;
    }
    if (e >= 0) {
      if (e > 62 || m > I64_MAX >> BigInt(e)) throw usage("the double's exact value exceeds int64", Status.E_RANGE);
      return new Rational((neg ? -1n : 1n) * (m << BigInt(e)), 1n, TOKEN);
    }
    if (-e > 62) throw usage("the double's exact denominator exceeds 2^62", Status.E_RANGE);
    return new Rational((neg ? -1n : 1n) * m, 1n << BigInt(-e), TOKEN);
  }

  /** Q16.16 fixed point (`value * 65536`, rounded half away from zero), as `zxv_rat_to_q16`. */
  static fromQ16(q16: IntLike): Rational {
    return make(toBig('q16', q16), 65536n, 'fromQ16');
  }

  /** this + b */
  add(b: Rational | IntLike | string): Rational {
    const o = R(b);
    return make(this.num * o.den + o.num * this.den, this.den * o.den, 'add');
  }

  /** this - b */
  sub(b: Rational | IntLike | string): Rational {
    const o = R(b);
    return make(this.num * o.den - o.num * this.den, this.den * o.den, 'sub');
  }

  /** this * b */
  mul(b: Rational | IntLike | string): Rational {
    const o = R(b);
    return make(this.num * o.num, this.den * o.den, 'mul');
  }

  /** this / b; division by zero is ZXV_E_ARG. */
  div(b: Rational | IntLike | string): Rational {
    const o = R(b);
    if (o.num === 0n) throw usage('division by zero', Status.E_ARG);
    return make(this.num * o.den, this.den * o.num, 'div');
  }

  /** -this */
  neg(): Rational {
    return new Rational(-this.num, this.den, TOKEN);
  }

  /** |this| */
  abs(): Rational {
    return this.num < 0n ? this.neg() : this;
  }

  /** -1, 0 or +1. Exact; no epsilon. */
  cmp(b: Rational | IntLike | string): -1 | 0 | 1 {
    const o = R(b);
    const l = this.num * o.den;
    const r = o.num * this.den;
    return l < r ? -1 : l > r ? 1 : 0;
  }

  /** Exact equality. */
  equals(b: Rational | IntLike | string): boolean {
    return this.cmp(b) === 0;
  }

  /** True if the denominator is 1. */
  isInteger(): boolean {
    return this.den === 1n;
  }

  /** Sign: -1, 0 or +1. */
  sign(): -1 | 0 | 1 {
    return this.num < 0n ? -1 : this.num > 0n ? 1 : 0;
  }

  /**
   * Round to Q16.16 (half away from zero), as `zxv_rat_to_q16`; the
   * engine's sampling parameters use it. ZXV_E_RANGE outside int32.
   */
  toQ16(): number {
    const scaled = this.num * 65536n;
    let q = scaled / this.den; // truncates toward zero
    let rem = scaled % this.den;
    if (rem < 0n) rem = -rem;
    if (2n * rem >= this.den) q += scaled < 0n ? -1n : 1n;
    if (q > 2147483647n || q < -2147483648n) throw usage('value is outside Q16.16', Status.E_RANGE);
    return Number(q);
  }

  /**
   * Exact text, as `zxv_rat_format`: a terminating decimal when one exists
   * with at most 18 places (`"12.75"`), otherwise `"n/d"` (`"1/3"`).
   * Never rounds. `Rational.parse(r.toString())` equals `r`.
   */
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
    const places = Math.max(twos, fives);
    let body: string;
    if (d === 1n && places <= 18) {
      const scale = 10n ** BigInt(places);
      const scaled = n * (scale / this.den);
      const ip = scaled / scale;
      const fp = scaled % scale;
      body = ip.toString();
      if (places > 0 && fp !== 0n) body += '.' + fp.toString().padStart(places, '0').replace(/0+$/, '');
    } else {
      body = `${n}/${this.den}`;
    }
    return (neg ? '-' : '') + body;
  }

  /** `"num/den"` always (e.g. `"3/1"`). */
  toFraction(): string {
    return `${this.num}/${this.den}`;
  }

  /** JSON form: the exact text of {@link Rational.toString}. */
  toJSON(): string {
    return this.toString();
  }

  /** Arithmetic on a Rational must be explicit: `+r`, `r * 2` and `r < 1` throw. */
  valueOf(): never {
    throw new TypeError('Rational has no implicit numeric value: use add/mul/cmp, or toString()');
  }

  /** Type guard. */
  static isRational(v: unknown): v is Rational {
    return v instanceof Rational;
  }
}

function R(v: Rational | IntLike | string): Rational {
  return v instanceof Rational ? v : typeof v === 'string' ? Rational.parse(v) : Rational.of(v);
}

function wrapRange(f: () => Rational, range: () => Error): Rational {
  try {
    return f();
  } catch {
    throw range();
  }
}

/** Accept a Rational or an integer where an exact amount is expected. */
export function toRational(name: string, v: unknown): Rational {
  if (v instanceof Rational) return v;
  if (typeof v === 'bigint' || typeof v === 'number') return Rational.of(toBig(name, v));
  if (typeof v === 'string') return Rational.parse(v);
  throw usage(`${name} must be a Rational, a bigint, an integer or exact decimal text`);
}
