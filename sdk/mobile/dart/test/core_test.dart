// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:test/test.dart';
import 'package:zxv/src/bindings.dart';
import 'package:zxv/zxv.dart';

import 'vectors.dart';

void main() {
  test('version and features', () {
    final v = Zxv.version();
    expect(v.major, 1);
    expect(v.minor, greaterThanOrEqualTo(3));
    expect(arr(vectors['abi'])[0], v.major);
    expect(Zxv.hasFeature(Feature.hex), isTrue);
    expect(Zxv.features(), contains(Feature.rational));
    expect(Zxv.hasFeature(Feature.lora), isFalse);
  });

  test('status names and classes', () {
    for (final e in arr(vectors['status']).map(obj)) {
      final code = e['code'] as int;
      expect(Zxv.statusName(code), e['name']);
      expect(Zxv.statusClass(code), StatusClass.fromCode(e['class'] as int));
    }
    expect(Zxv.statusClass(-12345), StatusClass.system);
  });

  test('rational ops', () {
    Rational r(Object? p) => Rational.of(arr(p)[0] as int, arr(p)[1] as int);
    for (final e in arr(vectors['rational_ops']).map(obj)) {
      final a = r(e['a']), b = r(e['b']);
      expect(a + b, r(e['add']));
      expect(a - b, r(e['sub']));
      expect(a * b, r(e['mul']));
      expect(a / b, r(e['div']));
      expect(a.compareTo(b), e['cmp']);
    }
  });

  test('rational parse and format', () {
    for (final e in arr(vectors['rational_parse']).map(obj)) {
      final text = e['text'] as String;
      final status = e['status'] as int;
      if (status == 0) {
        final r = Rational.parse(text);
        expect([r.numerator, r.denominator], e['value']);
        expect(r.toString(), e['format']);
      } else {
        expect(
          () => Rational.parse(text),
          throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', status)),
          reason: text,
        );
      }
    }
  });

  test('ieee754 is the only float door', () {
    for (final e in arr(vectors['ieee754']).map(obj)) {
      final bits = BigInt.parse(e['bits'] as String, radix: 16).toSigned(64).toInt(); // the same 64-bit pattern
      final status = e['status'] as int;
      if (status == 0) {
        final want = Rational.of(arr(e['value'])[0] as int, arr(e['value'])[1] as int);
        expect(Rational.fromIeee754Bits(bits), want);
      } else if (status == -24) {
        expect(() => Rational.fromIeee754Bits(bits), throwsA(isA<FloatRejectedException>()));
      } else {
        expect(
          () => Rational.fromIeee754Bits(bits),
          throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', status)),
        );
      }
    }
    expect(Rational.fromDoubleExact(0.1).toFractionString(), '3602879701896397/36028797018963968');
    expect(() => Rational.fromDoubleExact(double.nan), throwsA(isA<FloatRejectedException>()));
    expect(() => Rational.fromDoubleExact(double.infinity), throwsA(isA<FloatRejectedException>()));
  });

  test('q16', () {
    for (final e in arr(vectors['q16']).map(obj)) {
      final r = Rational.of(arr(e['value'])[0] as int, arr(e['value'])[1] as int);
      if (e['status'] == 0) {
        expect(r.toQ16(), e['q16']);
      } else {
        expect(() => r.toQ16(), throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', e['status'])));
      }
    }
  });

  test('rational extras', () {
    expect(Rational.parse('0.1') + Rational.parse('0.2'), Rational.of(3, 10));
    expect(Rational.of(2, 4), Rational.of(1, 2));
    expect(Rational.of(-1, 3).abs(), Rational.of(1, 3));
    expect(Rational.of(1, 3) < Rational.of(1, 2), isTrue);
    expect(() => Rational.of(1, 0), throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', -1)));
    expect(
      () => Rational.of(0x7FFFFFFFFFFFFFFF) + Rational.one,
      throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', -8)),
    );
    expect(() => Rational.parse('1\u00002'), throwsA(isA<ZxvUsageException>()));
  });

  test('truth tables match libzxv and the vectors', () {
    final lp = obj(vectors['lpres']);
    expect(Truth.values.map((t) => t.name).toList(), lp['names']);
    using((a) {
      final o = a<Int32>();
      for (final x in Truth.values) {
        expect(x.not().code, arr(lp['not'])[x.code]);
        expect(zxvLib.lpNot(x.code, o), 0);
        expect(o.value, x.not().code);
        for (final y in Truth.values) {
          expect(x.and(y).code, arr(arr(lp['and'])[x.code])[y.code]);
          expect(x.or(y).code, arr(arr(lp['or'])[x.code])[y.code]);
          expect(zxvLib.lpAnd(x.code, y.code, o), 0);
          expect(o.value, x.and(y).code);
          expect(zxvLib.lpOr(x.code, y.code, o), 0);
          expect(o.value, x.or(y).code);
        }
      }
    });
  });

  test('truth needs an explicit policy to become a bool', () {
    expect(Truth.TRUE.isCertainlyTrue, isTrue);
    expect(Truth.BOTH.isCertainlyTrue, isFalse);
    expect(Truth.BOTH.toBool(onBoth: true, onNeither: false), isTrue);
    expect(Truth.NEITHER.toBool(onBoth: true, onNeither: false), isFalse);
    expect(() => Truth.BOTH.requireTrue(), throwsStateError);
    expect(Truth.fromEvidence(1, 1), Truth.BOTH);
    expect(() => Truth.fromCode(4), throwsA(isA<ZxvUsageException>()));
  });

  test('hashes', () {
    for (final e in arr(vectors['hash']).map(obj)) {
      final data = (e['utf8'] as String).codeUnits;
      expect(toHex(Hashes.sha3_256(data)), e['sha3_256']);
      expect(toHex(Hashes.keccak256(data)), e['keccak256']);
      expect(toHex(Hashes.shake256(data, 64)), e['shake256_64']);
    }
  });
}
