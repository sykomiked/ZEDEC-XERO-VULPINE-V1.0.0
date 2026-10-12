// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ExactRational against @zedec/zxv's Rational (itself cross-checked with
// libzxv's zxv_rat_*): same values, same text, same refusals. Also: the
// package's import graph is React and nothing else.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { Rational } from '@zedec/zxv';

import { ExactRational, FloatRejectedError, I64_MAX, intFromWire, intToWire, RationalError } from '../src/index.js';

// a small deterministic generator (no Math.random: reproducible failures)
let seed = 0x2545f4914f6cdd1dn;
function next(): bigint {
  seed ^= seed << 13n;
  seed &= (1n << 64n) - 1n;
  seed ^= seed >> 7n;
  seed ^= seed << 17n;
  seed &= (1n << 64n) - 1n;
  return seed;
}
function pick(): bigint {
  const r = next();
  switch (Number(r % 4n)) {
    case 0: return (r >> 8n) % 1000n - 500n;
    case 1: return (r >> 8n) % (1n << 31n) - (1n << 30n);
    case 2: return (r >> 1n) % I64_MAX * ((r & 1n) === 1n ? -1n : 1n);
    default: return (r >> 8n) % 97n + 1n;
  }
}

function both<T>(a: () => T, b: () => T): { ok: true; a: T; b: T } | { ok: false; aThrew: boolean; bThrew: boolean } {
  let ra: T | undefined;
  let rb: T | undefined;
  let ea = false;
  let eb = false;
  try { ra = a(); } catch { ea = true; }
  try { rb = b(); } catch { eb = true; }
  return !ea && !eb ? { ok: true, a: ra as T, b: rb as T } : { ok: false, aThrew: ea, bThrew: eb };
}

test('arithmetic, comparison and text equal @zedec/zxv Rational on 4000 random pairs', () => {
  let compared = 0;
  for (let i = 0; i < 4000; i++) {
    const n1 = pick();
    let d1 = pick();
    const n2 = pick();
    let d2 = pick();
    if (d1 === 0n) d1 = 1n;
    if (d2 === 0n) d2 = 1n;
    const x = ExactRational.of(n1, d1);
    const y = ExactRational.of(n2, d2);
    const X = Rational.of(n1, d1);
    const Y = Rational.of(n2, d2);
    assert.equal(x.toString(), X.toString());
    assert.equal(ExactRational.parse(X.toString()).toWire(), x.toWire());
    assert.equal(x.cmp(y), X.cmp(Y));
    for (const op of ['add', 'sub', 'mul', 'div'] as const) {
      if (op === 'div' && n2 === 0n) continue;
      const r = both(() => x[op](y).toWire(), () => { const z = X[op](Y); return `${z.num}/${z.den}`; });
      if (r.ok) {
        assert.equal(r.a, r.b, `${x} ${op} ${y}`);
        compared++;
      } else {
        assert.equal(r.aThrew, r.bThrew, `${x} ${op} ${y}: one overflowed, the other did not`);
      }
    }
  }
  assert.ok(compared > 4000, String(compared));
});

test('parse grammar and refusals match', () => {
  for (const t of ['12.75', '-0.1', '3/4', '42', '+5', '0.000000000000000001', '-6/4', '1.50', '0', '-0', '9223372036854775807',
    '0.1000000000000000000000']) {
    assert.equal(ExactRational.parse(t).toWire(), (() => { const r = Rational.parse(t); return `${r.num}/${r.den}`; })(), t);
  }
  for (const t of ['1e3', '0x10', '1/0', ' 1', '1.', '.5', 'NaN', '', '9223372036854775808', '1/9223372036854775808', '0.1234567890123456789']) {
    assert.throws(() => ExactRational.parse(t), RationalError, t);
    assert.throws(() => Rational.parse(t), t);
  }
});

test('floats are refused; the wire form is canonical only', () => {
  for (const f of [0.1, 1.5, NaN, Infinity, 2 ** 53]) assert.throws(() => ExactRational.from(f), FloatRejectedError);
  assert.equal(ExactRational.from(7).toWire(), '7/1');
  assert.equal(ExactRational.from(Rational.fromNumberExact(0.1)).toWire(), '3602879701896397/36028797018963968');
  assert.equal(ExactRational.fromWire('-7/3').toString(), '-7/3');
  for (const bad of ['2/4', '0.5', '-0/1', '1/-2', '01/2', '1', '1/0']) assert.throws(() => ExactRational.fromWire(bad), RationalError, bad);
  assert.throws(() => ExactRational.ONE.valueOf(), TypeError);
  assert.throws(() => +ExactRational.ONE, TypeError);
  assert.equal(JSON.stringify({ r: ExactRational.parse('2.5') }), '{"r":"5/2"}');
});

test('display rounding is half to even and returns text', () => {
  assert.equal(ExactRational.parse('2/3').toDisplay(3), '0.667');
  assert.equal(ExactRational.parse('0.125').toDisplay(2), '0.12');
  assert.equal(ExactRational.parse('0.375').toDisplay(2), '0.38');
  assert.equal(ExactRational.parse('-0.125').toDisplay(2), '-0.12');
  assert.equal(ExactRational.parse('-0.001').toDisplay(2), '0.00');
  assert.equal(ExactRational.parse('2.5').toDisplay(0), '2');
  assert.equal(ExactRational.parse('3.5').toDisplay(0), '4');
  assert.equal(ExactRational.parse('1/3').toDisplay(), '0.33');
});

test('the package imports nothing but React (it has to load in React Native)', () => {
  const src = resolve(dirname(fileURLToPath(import.meta.url)), '..', 'src'); // dist/src
  const seen = new Set<string>();
  const external = new Set<string>();
  const visit = (file: string): void => {
    if (seen.has(file)) return;
    seen.add(file);
    const text = readFileSync(file, 'utf8');
    assert.ok(!/import\.meta|process\.|require\(/.test(text), `${file} uses a Node- or bundler-only construct`);
    for (const m of text.matchAll(/(?:import|export)[^'"]*from\s*['"]([^'"]+)['"]/g)) {
      const spec = m[1]!;
      if (spec.startsWith('.')) visit(resolve(dirname(file), spec));
      else external.add(spec);
    }
  };
  visit(join(src, 'index.js'));
  assert.ok(seen.size >= 8, String(seen.size));
  assert.deepEqual([...external], ['react']);
});

test('integers on the wire', () => {
  assert.equal(intToWire(250_050), '250050');
  assert.equal(intToWire(2n ** 62n), '4611686018427387904');
  assert.equal(intToWire('-12'), '-12');
  assert.throws(() => intToWire(1.5), FloatRejectedError);
  assert.throws(() => intToWire('1.5'), RationalError);
  assert.throws(() => intToWire('01'), RationalError);
  assert.equal(intFromWire('-9223372036854775808'), -(2n ** 63n));
  assert.throws(() => intFromWire('9223372036854775808'), RationalError);
  assert.throws(() => intFromWire(5), FloatRejectedError);
});
