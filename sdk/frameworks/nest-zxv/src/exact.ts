// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { IncomingMessage, ServerResponse } from 'node:http';

import { type ArgumentMetadata, BadRequestException, Injectable, type PipeTransform } from '@nestjs/common';
import { Rational } from '@zedec/zxv';

const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);
const INT64_MAX = (1n << 63n) - 1n;
const INTEGER = /^[+-]?\d{1,19}$/;

/** Why a value was refused. The text is ours: it never echoes native detail. */
function refuse(field: string, why: string): BadRequestException {
  return new BadRequestException({ statusCode: 400, error: 'Bad Request', message: `${field}: ${why}` });
}

const FLOAT_HINT = 'a JSON number with a fraction or exponent is not exact; send it as a string such as "0.1" or "1/3"';

/**
 * An exact rational from a request value: a string in the SDK's exact
 * syntax (`"12.75"`, `"-0.1"`, `"3/4"`, `"42"`), a `bigint`, a `Rational`,
 * or a safe-integer `number`. A fractional `number` (a JSON float that
 * already became binary) is refused, as is NaN, infinity, a number beyond
 * 2^53 (already rounded), a string with an exponent, spaces or a zero
 * denominator, and anything else. Throws `BadRequestException` (400).
 */
export function toRational(value: unknown, field = 'value'): Rational {
  if (Rational.isRational(value)) return value;
  if (typeof value === 'bigint') return guard(() => Rational.of(value), field);
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw refuse(field, Number.isInteger(value) ? 'an integer beyond 2^53 is not exact as a JSON number; send it as a string' : FLOAT_HINT);
    return Rational.of(value);
  }
  if (typeof value === 'string') return guard(() => Rational.parse(value), field);
  throw refuse(field, value === undefined || value === null ? 'is required' : 'must be an exact number as a string, e.g. "12.75" or "1/3"');
}

function guard(make: () => Rational, field: string): Rational {
  try {
    return make();
  } catch {
    throw refuse(field, 'is not an exact number (use 12.75, -0.1, 3/4 or 42; no exponent, no spaces; numerator and denominator within 64 bits)');
  }
}

/**
 * Exact integer minor units (kobo, cents) as a bigint, from a string of
 * digits (`"150000"`) or a safe-integer `number`. A fraction, an exponent
 * or a value outside int64 is refused with `BadRequestException`.
 */
export function toMinorUnits(value: unknown, field = 'value'): bigint {
  if (typeof value === 'bigint') {
    if (value > INT64_MAX || value < -INT64_MAX) throw refuse(field, 'does not fit 64 bits');
    return value;
  }
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw refuse(field, Number.isInteger(value) ? 'an integer beyond 2^53 is not exact as a JSON number; send it as a string' : 'must be whole minor units (e.g. 150000 kobo), not a fraction');
    return BigInt(value);
  }
  if (typeof value === 'string' && INTEGER.test(value)) {
    const v = BigInt(value);
    if (v > INT64_MAX || v < -INT64_MAX) throw refuse(field, 'does not fit 64 bits');
    return v;
  }
  throw refuse(field, value === undefined || value === null ? 'is required' : 'must be whole minor units: an integer, or a string of digits');
}

/**
 * Pipe for one value: `@Query('a', ParseRationalPipe) a: Rational`,
 * `@Body('price', ParseRationalPipe)`, `@Param(...)`. See {@link toRational}.
 */
@Injectable()
export class ParseRationalPipe implements PipeTransform<unknown, Rational> {
  transform(value: unknown, metadata: ArgumentMetadata): Rational {
    return toRational(value, metadata.data ?? metadata.type);
  }
}

/** Field kinds of an {@link ExactBodyPipe} schema. */
export type ExactFieldKind = 'rational' | 'minorUnits' | 'string';

/** The value type a field kind becomes. */
export type ExactFieldValue<K extends ExactFieldKind> = K extends 'rational' ? Rational : K extends 'minorUnits' ? bigint : string;

/** The typed body an {@link ExactBodyPipe} produces. */
export type ExactBody<S extends Readonly<Record<string, ExactFieldKind>>> = { readonly [P in keyof S]: ExactFieldValue<S[P]> };

/**
 * Validates and transforms a JSON body against a small schema: every field
 * is required, unknown fields are refused (400), `rational` fields become
 * `Rational` ({@link toRational}), `minorUnits` fields become `bigint`
 * ({@link toMinorUnits}), `string` fields must be non-empty strings.
 *
 * ```ts
 * const Quote = new ExactBodyPipe({ price: 'rational', quantity: 'rational' });
 * @Post('quote') quote(@Body(Quote) q: ExactBody<typeof Quote.schema>) { ... }
 * ```
 *
 * Pair it with {@link exactJson}, which refuses JSON floats before parsing
 * (a float such as `1.00000000000000001` has already become `1` by the time
 * a pipe sees it).
 */
export class ExactBodyPipe<const S extends Readonly<Record<string, ExactFieldKind>>> implements PipeTransform<unknown, ExactBody<S>> {
  readonly schema: S;

  constructor(schema: S) {
    this.schema = Object.freeze({ ...schema });
  }

  transform(value: unknown): ExactBody<S> {
    if (value === null || typeof value !== 'object' || Array.isArray(value)) {
      throw refuse('body', 'must be a JSON object');
    }
    const input = value as Record<string, unknown>;
    for (const key of Object.keys(input)) {
      if (!Object.hasOwn(this.schema, key)) throw refuse(key, 'is not a known field');
    }
    const out: Record<string, unknown> = {};
    for (const [key, kind] of Object.entries(this.schema)) {
      const v = input[key];
      switch (kind) {
        case 'rational': out[key] = toRational(v, key); break;
        case 'minorUnits': out[key] = toMinorUnits(v, key); break;
        case 'string':
          if (typeof v !== 'string' || v.length === 0) throw refuse(key, 'must be a non-empty string');
          out[key] = v;
          break;
        default: throw refuse(key, 'has an unknown field kind');
      }
    }
    return Object.freeze(out) as ExactBody<S>;
  }
}

/**
 * The first JSON number token in `text` that is not exact as a JS number:
 * one with a fraction or exponent (`0.1`, `1.0`, `1e3`), or an integer
 * beyond 2^53. `undefined` when there is none. Strings are skipped, so
 * `"0.1"` (exact text) is fine. Malformed JSON is left to `JSON.parse`.
 */
export function findInexactJsonNumber(text: string): string | undefined {
  let i = 0;
  const n = text.length;
  while (i < n) {
    const c = text.charCodeAt(i);
    if (c === 0x22) {
      // a string: skip to its closing quote, honouring escapes
      i++;
      while (i < n) {
        const d = text.charCodeAt(i);
        if (d === 0x5c) i += 2;
        else if (d === 0x22) break;
        else i++;
      }
      i++;
      continue;
    }
    if (c === 0x2d || (c >= 0x30 && c <= 0x39)) {
      let j = i + 1;
      while (j < n && /[0-9eE+\-.]/.test(text[j] ?? '')) j++;
      const token = text.slice(i, j);
      if (/[.eE]/.test(token)) return token;
      if (/^-?\d+$/.test(token)) {
        const v = BigInt(token);
        if (v > MAX_SAFE || v < -MAX_SAFE) return token;
      }
      i = j;
      continue;
    }
    i++;
  }
  return undefined;
}

/** Options of {@link exactJson}. */
export interface ExactJsonOptions {
  /** Largest body in bytes; default 100 KiB (413 above it). */
  readonly limit?: number;
}

type Request = IncomingMessage & { body?: unknown; _body?: boolean };
type Next = (err?: unknown) => void;

function problem(res: ServerResponse, status: number, code: string, title: string, detail: string): void {
  const body = JSON.stringify({ type: `urn:zxv:problem:${code}`, title, status, code: code.toUpperCase(), detail });
  res.statusCode = status;
  res.setHeader('content-type', 'application/problem+json');
  res.setHeader('cache-control', 'no-store');
  res.end(body);
}

/**
 * A JSON body parser (Express/Connect middleware) that refuses JSON floats
 * before parsing: a body with `0.1`, `1.0`, `1e3` or an integer beyond 2^53
 * anywhere (outside strings) is answered 400 `urn:zxv:problem:json_float`.
 * Use it instead of the default parser:
 *
 * ```ts
 * const app = await NestFactory.create(AppModule, { bodyParser: false });
 * app.use(exactJson());
 * ```
 *
 * Requests that are not `application/json` (or `+json`), or whose body was
 * already parsed, pass through untouched.
 */
export function exactJson(options: ExactJsonOptions = {}): (req: Request, res: ServerResponse, next: Next) => void {
  const limit = options.limit ?? 100 * 1024;
  return (req, res, next) => {
    const type = String(req.headers['content-type'] ?? '').split(';')[0]?.trim().toLowerCase() ?? '';
    if (req._body === true || !(type === 'application/json' || type.endsWith('+json'))) {
      next();
      return;
    }

    const chunks: Buffer[] = [];
    let size = 0;
    let done = false;
    req.on('data', (chunk: Buffer) => {
      if (done) return;
      size += chunk.length;
      if (size > limit) {
        done = true;
        problem(res, 413, 'body_too_large', 'Payload too large', `the body exceeds ${limit} bytes`);
        req.resume();
        return;
      }
      chunks.push(chunk);
    });
    req.on('error', (err) => {
      if (done) return;
      done = true;
      next(err);
    });
    req.on('end', () => {
      if (done) return;
      done = true;
      const text = Buffer.concat(chunks).toString('utf8');
      if (text.trim().length === 0) {
        req.body = {};
        req._body = true;
        next();
        return;
      }
      const inexact = findInexactJsonNumber(text);
      if (inexact !== undefined) {
        problem(res, 400, 'json_float', 'Inexact number', FLOAT_HINT);
        return;
      }
      try {
        req.body = JSON.parse(text) as unknown;
      } catch {
        problem(res, 400, 'json_syntax', 'Malformed JSON', 'the body is not valid JSON');
        return;
      }
      req._body = true;
      next();
    });
  };
}

/**
 * Converts every `bigint` in a response value to its exact decimal string
 * (JSON has no 64-bit integers and `JSON.stringify` throws on bigint).
 * `Rational` already serializes as its exact text through `toJSON`.
 */
export function toExactJson(value: unknown): unknown {
  if (typeof value === 'bigint') return value.toString();
  if (Array.isArray(value)) return value.map(toExactJson);
  if (value !== null && typeof value === 'object' && !Rational.isRational(value) && Object.getPrototypeOf(value) === Object.prototype) {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(value)) out[k] = toExactJson(v);
    return out;
  }
  return value;
}
