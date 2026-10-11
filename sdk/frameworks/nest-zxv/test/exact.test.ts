// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Exact values at the HTTP boundary: pipes, the body parser's float check,
// bigint serialization.

import 'reflect-metadata';

import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { BadRequestException } from '@nestjs/common';
import { Rational } from '@zedec/zxv';

import { ExactBodyPipe, findInexactJsonNumber, ParseRationalPipe, toExactJson, toMinorUnits, toRational } from '../src/index.js';

const bad = (f: () => unknown) => assert.throws(f, BadRequestException);

describe('toRational / ParseRationalPipe', () => {
  it('reads exact text, integers and Rationals', () => {
    assert.equal(toRational('0.1').toString(), '0.1');
    assert.ok(toRational('0.1').equals(Rational.of(1n, 10n)));
    assert.equal(toRational('1/3').toString(), '1/3');
    assert.equal(toRational('-12.75').toString(), '-12.75');
    assert.equal(toRational('+5').toString(), '5');
    assert.equal(toRational(42).toString(), '42');
    assert.equal(toRational(-7n).toString(), '-7');
    const r = Rational.of(2n, 3n);
    assert.equal(toRational(r), r);
    const pipe = new ParseRationalPipe();
    assert.equal(pipe.transform('0.25', { type: 'query', data: 'a' }).toString(), '0.25');
  });

  it('refuses floats and anything inexact or malformed', () => {
    for (const v of [0.1, 1.5, Number.NaN, Number.POSITIVE_INFINITY, 2 ** 53, '1e-1', '1E3', ' 1', '1 ', '0x10', '1/0', 'NaN', '', '.5', '5.',
      '99999999999999999999', true, null, undefined, {}, ['1'], '1,5']) {
      bad(() => toRational(v, 'price'));
    }
  });

  it('names the field and never echoes native detail', () => {
    try {
      toRational('1e-1', 'price');
      assert.fail('expected a refusal');
    } catch (e) {
      assert.ok(e instanceof BadRequestException);
      const body = e.getResponse() as { message: string };
      assert.match(body.message, /^price: /);
      assert.doesNotMatch(body.message, /ZXV_E_/);
    }
  });
});

describe('toMinorUnits', () => {
  it('reads whole minor units exactly', () => {
    assert.equal(toMinorUnits(150_00), 15000n);
    assert.equal(toMinorUnits('9007199254740993'), 9007199254740993n);
    assert.equal(toMinorUnits('-5'), -5n);
    assert.equal(toMinorUnits(12n), 12n);
  });

  it('refuses fractions, exponents and out-of-range values', () => {
    for (const v of [1.5, 2 ** 53, '1.0', '1e3', '9223372036854775808', 1n << 63n, 'ten', '', null, undefined]) bad(() => toMinorUnits(v, 'amountMinor'));
  });
});

describe('ExactBodyPipe', () => {
  const Quote = new ExactBodyPipe({ price: 'rational', quantity: 'rational', reference: 'string', amountMinor: 'minorUnits' });

  it('transforms a body to exact values', () => {
    const q = Quote.transform({ price: '12.75', quantity: '1/3', reference: 'Q-1', amountMinor: 425 });
    assert.equal(q.price.mul(q.quantity).toString(), '4.25');
    assert.equal(q.amountMinor, 425n);
    assert.equal(q.reference, 'Q-1');
    assert.ok(Object.isFrozen(q));
  });

  it('refuses unknown, missing and inexact fields', () => {
    const ok = { price: '1', quantity: '1', reference: 'r', amountMinor: 1 };
    bad(() => Quote.transform({ ...ok, extra: 1 }));
    bad(() => Quote.transform({ ...ok, price: undefined }));
    bad(() => Quote.transform({ ...ok, price: 12.75 }));
    bad(() => Quote.transform({ ...ok, amountMinor: 1.5 }));
    bad(() => Quote.transform({ ...ok, reference: '' }));
    bad(() => Quote.transform([ok]));
    bad(() => Quote.transform('{}'));
    bad(() => Quote.transform(null));
  });
});

describe('findInexactJsonNumber', () => {
  it('finds fractions, exponents and unsafe integers outside strings', () => {
    assert.equal(findInexactJsonNumber('{"a":0.1}'), '0.1');
    assert.equal(findInexactJsonNumber('{"a":1.0}'), '1.0');
    assert.equal(findInexactJsonNumber('{"a":1e3}'), '1e3');
    assert.equal(findInexactJsonNumber('{"a":-2E-2}'), '-2E-2');
    assert.equal(findInexactJsonNumber('[1,[2,{"b":[3.5]}]]'), '3.5');
    assert.equal(findInexactJsonNumber('{"a":9007199254740993}'), '9007199254740993');
    assert.equal(findInexactJsonNumber('{"a":1.00000000000000001}'), '1.00000000000000001');
  });

  it('accepts exact text, safe integers and numbers inside strings', () => {
    assert.equal(findInexactJsonNumber('{"a":"0.1","b":42,"c":-7,"d":"1e3","e":"say \\"1.5\\""}'), undefined);
    assert.equal(findInexactJsonNumber('{"a":9007199254740991,"b":[0,-0]}'), undefined);
    assert.equal(findInexactJsonNumber('{"k\\\\":1}'), undefined);
  });
});

describe('toExactJson', () => {
  it('turns bigints into exact strings, deeply, and leaves Rationals to toJSON', () => {
    const out = toExactJson({ id: 9007199254740993n, list: [1n, { n: -2n }], r: Rational.parse('1/3'), s: 'x', k: 3 });
    assert.equal(JSON.stringify(out), '{"id":"9007199254740993","list":["1",{"n":"-2"}],"r":"1/3","s":"x","k":3}');
  });
});
