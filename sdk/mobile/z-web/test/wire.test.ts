// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The exact wire encodings, the float refusals, and the Truth / Hexagram
// helpers against vectors.json. Also: the page module graph has no Node
// built-ins.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { test } from 'node:test';

import {
  assertNoFloats, decodeInt, decodeRational, encodeInt, encodeRational, fromWireRational, Hexagram, hexagramFromWire, hexagramToWire,
  I64_MAX, isWireRational, Rational, toRational, toWireRational, Truth, truthFromWire, truthToWire, ZxvWebError,
} from '../src/index.js';
import { pkgRoot, V } from './helpers.js';

const float = { kind: 'FloatRejectedError', status: -24, statusName: 'ZXV_E_FLOAT', statusClass: 'USAGE' };
const format = { status: -22, statusName: 'ZXV_E_FORMAT' };

function throwsLike(f: () => unknown, fields: Record<string, unknown>): void {
  assert.throws(f, (e: unknown) => {
    assert.ok(e instanceof ZxvWebError, String(e));
    for (const [k, v] of Object.entries(fields)) assert.equal((e as unknown as Record<string, unknown>)[k], v, k);
    return true;
  });
}

test('rationals travel as reduced "num/den"', () => {
  assert.equal(encodeRational(6n, 4n), '3/2');
  assert.equal(encodeRational(3n, -6n), '-1/2');
  assert.equal(encodeRational(0n, 7n), '0/1');
  assert.equal(encodeRational(5n, 1n), '5/1');
  assert.equal(encodeRational(I64_MAX, 1n), `${I64_MAX}/1`);
  assert.deepEqual(decodeRational('-7/3'), { num: -7n, den: 3n });
  assert.deepEqual(decodeRational(`${I64_MAX}/${I64_MAX - 1n}`), { num: I64_MAX, den: I64_MAX - 1n });
  throwsLike(() => encodeRational(I64_MAX + 1n, 1n), { status: -8 });
  throwsLike(() => encodeRational(1n, 0n), format);
});

test('only the canonical form is accepted', () => {
  for (const bad of ['2/4', '1/0', '-0/1', '01/2', '1/02', '1/-2', '+1/2', '0.5', '1/2 ', ' 1/2', '1', '1/2/3', '1e3/1', 'NaN', '',
    `${I64_MAX + 1n}/1`, `1/${I64_MAX + 1n}`, '0x10/1']) {
    assert.ok(!isWireRational(bad), bad);
    throwsLike(() => decodeRational(bad), format);
  }
  throwsLike(() => decodeRational(0.5), float);
  throwsLike(() => decodeRational(2), float);
  throwsLike(() => decodeRational(null), format);
});

test('64-bit integers travel as decimal strings; numbers must be safe integers', () => {
  assert.equal(encodeInt(250_050), '250050');
  assert.equal(encodeInt(-12), '-12');
  assert.equal(encodeInt(2n ** 60n), '1152921504606846976');
  for (const bad of [0.1, 1.5, NaN, Infinity, -Infinity, 2 ** 53, -(2 ** 53), 1e21]) throwsLike(() => encodeInt(bad), float);
  assert.equal(decodeInt('9223372036854775807'), I64_MAX);
  assert.equal(decodeInt('-9223372036854775808'), -I64_MAX - 1n);
  assert.equal(decodeInt('18446744073709551615', 'u', true), (1n << 64n) - 1n);
  for (const bad of ['9223372036854775808', '-0', '01', '1.0', '1e3', ' 1', '', '+1']) throwsLike(() => decodeInt(bad), format);
  throwsLike(() => decodeInt('-1', 'u', true), format);
  throwsLike(() => decodeInt(42), float);
});

test('assertNoFloats finds a float anywhere in a message', () => {
  assertNoFloats({ id: 1, a: [1, 2, { b: '0.5' }], c: new Float64Array([0.5]), d: new ArrayBuffer(8) });
  throwsLike(() => assertNoFloats({ id: 1, a: [1, { b: 0.5 }] }), { ...float });
  throwsLike(() => assertNoFloats({ x: NaN }), float);
  throwsLike(() => assertNoFloats({ x: 2 ** 60 }), float);
  assert.throws(() => assertNoFloats({ x: { a: 0.1 } }), /message\.x\.a/);
});

test('page values convert exactly; floats never do', () => {
  assert.equal(toWireRational('0.1'), '1/10');
  assert.equal(toWireRational('12.75'), '51/4');
  assert.equal(toWireRational('-3/9'), '-1/3');
  assert.equal(toWireRational(7), '7/1');
  assert.equal(toWireRational(7n), '7/1');
  assert.equal(toWireRational(Rational.of(2n, 6n)), '1/3');
  // the one deliberate float entry: the double's exact value
  assert.equal(toWireRational(Rational.fromNumberExact(0.1)), '3602879701896397/36028797018963968');
  throwsLike(() => toRational(0.1), float);
  throwsLike(() => toRational(Number.MAX_SAFE_INTEGER + 2), float);
  assert.throws(() => toRational('1e3'));
  assert.throws(() => toRational('0.1f'));
  assert.ok(fromWireRational('51/4').equals(Rational.parse('12.75')));
  assert.equal(fromWireRational('1/3').toString(), '1/3');
});

test('Truth on the wire, with the kernel tables of vectors.json', () => {
  for (const t of Truth.values) assert.equal(truthFromWire(truthToWire(t)), t);
  assert.throws(() => truthFromWire('true'));
  assert.throws(() => truthFromWire(1));
  const names = V.lpres.names;
  for (let a = 0; a < 4; a++) {
    const ta = truthFromWire(names[a]);
    assert.equal(ta.not(), truthFromWire(names[V.lpres.not[a]!]));
    for (let b = 0; b < 4; b++) {
      const tb = truthFromWire(names[b]);
      assert.equal(ta.and(tb), truthFromWire(names[V.lpres.and[a]![b]!]), `${names[a]} AND ${names[b]}`);
      assert.equal(ta.or(tb), truthFromWire(names[V.lpres.or[a]![b]!]), `${names[a]} OR ${names[b]}`);
    }
  }
  // BOTH is never an implicit boolean
  assert.throws(() => Truth.BOTH.require());
  assert.equal(Truth.BOTH.toBoolean({ onBoth: false, onNeither: false }), false);
});

test('Hexagram tags on the wire, every tag of vectors.json', () => {
  assert.equal(V.hex.tags.length, 64);
  for (const t of V.hex.tags) {
    const h = hexagramFromWire(t.tag);
    assert.equal(hexagramToWire(h), t.tag);
    assert.equal(h.name, t.name);
    assert.equal(h.base, t.base);
    assert.equal(h.polarity, t.polarity);
    assert.equal(h.phase, t.phase);
    assert.equal(h.action, t.action);
    assert.equal(h.not().tag, V.hex.not[t.tag]);
    assert.equal(h.and(Hexagram.fromTag(5)).tag, V.hex.and[t.tag]![5]);
  }
  for (const bad of [64, -1, 1.5, '3', null]) assert.throws(() => hexagramFromWire(bad));
});

test('the page module graph has no Node built-ins', () => {
  const dist = join(pkgRoot, 'dist', 'src');
  const seen = new Set<string>();
  const visit = (file: string): void => {
    if (seen.has(file)) return;
    seen.add(file);
    const src = readFileSync(file, 'utf8');
    for (const m of src.matchAll(/(?:import|export)[^'"]*from\s*['"]([^'"]+)['"]|import\(\s*['"]([^'"]+)['"]\s*\)/g)) {
      const spec = (m[1] ?? m[2])!;
      if (spec === '@zedec/zxv/wasm') continue; // the SDK's browser entry (its own test checks its graph)
      assert.ok(spec.startsWith('.'), `${file} imports ${spec}`);
      visit(resolve(dirname(file), spec));
    }
  };
  visit(join(dist, 'index.js'));
  visit(join(dist, 'worker.js'));
  assert.ok(seen.size >= 9, String(seen.size));
  assert.ok(![...seen].some((f) => /node(-worker)?\.js$/.test(f)), 'the worker_threads adapter is not reachable');
});
