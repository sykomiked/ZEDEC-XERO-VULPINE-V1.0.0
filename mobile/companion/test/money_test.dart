// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/src/money.dart';

BigInt? ok(String t, String c) => switch (parseAmount(t, c)) { Parsed(:final minor) => minor, _ => null };
String? why(String t, String c) => switch (parseAmount(t, c)) { ParseFailed(:final why) => why, _ => null };

void main() {
  test('amounts become exact minor units', () {
    expect(ok('125.50', 'USD'), BigInt.from(12550));
    expect(ok('1,250.5', 'USD'), BigInt.from(125050));
    expect(ok('1.500', 'USD'), BigInt.from(150)); // trailing zeros carry no value
    expect(ok('7', 'JPY'), BigInt.from(7));
    expect(ok('1.234', 'KWD'), BigInt.from(1234));
    expect(ok('9223372036854775.807', 'KWD'), BigInt.parse('9223372036854775807'));
  });

  test('more decimals than the currency has are refused, never rounded', () {
    expect(why('1.005', 'USD'), contains('2 decimal places'));
    expect(why('1.5', 'JPY'), contains('0 decimal places'));
  });

  test('bad input is refused', () {
    for (final t in ['', 'abc', '-5', '1e3', '1.', '.5', '1.2.3', '+1', ' 1 2']) {
      expect(ok(t, 'USD'), isNull, reason: t);
    }
    expect(why('0.00', 'USD'), contains('at least one minor unit'));
    expect(why('92233720368547758.08', 'USD'), contains('too large'));
  });

  test('amount_minor is a number while exact in a double, else a string', () {
    expect(amountParam(BigInt.from(9007199254740991)), 9007199254740991);
    expect(amountParam(BigInt.parse('9007199254740992')), '9007199254740992');
  });

  test('formatting reads decimal strings exactly', () {
    expect(formatMinor('125050', 'USD'), '1,250.50 USD');
    expect(formatMinor('-3', 'USD'), '−0.03 USD');
    expect(formatMinor('9223372036854775807', 'USD'), '92,233,720,368,547,758.07 USD');
    expect(formatMinor('1234', 'KWD', withCode: false), '1.234');
    expect(formatMinor('1000', 'JPY'), '1,000 JPY');
    expect(formatMinor('x', 'USD'), 'x');
    expect(grouped(1234567), '1,234,567');
    expect(grouped(-1234), '-1,234');
  });
}
