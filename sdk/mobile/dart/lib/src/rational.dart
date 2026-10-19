// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'native.dart' as n;

/// An exact rational `numerator / denominator` with 64-bit parts: the
/// kernel's `rat_t`. Immutable; always reduced with a positive denominator,
/// so `==` is value equality.
///
/// Arithmetic runs in libzxv and never rounds: a result that does not fit
/// throws [ZxvUsageException] (`ZXV_E_RANGE`).
///
/// No member takes a `double` except [Rational.fromDoubleExact], whose name
/// says what it does: `0.1` becomes `3602879701896397/36028797018963968`.
/// For one tenth write `Rational.parse('0.1')` or `Rational.of(1, 10)`.
final class Rational implements Comparable<Rational> {
  const Rational._(this.numerator, this.denominator);

  /// The reduced numerator (carries the sign).
  final int numerator;

  /// The reduced denominator, always > 0.
  final int denominator;

  static const Rational zero = Rational._(0, 1);
  static const Rational one = Rational._(1, 1);

  /// The reduced rational; `ZXV_E_ARG` for a zero denominator.
  factory Rational.of(int numerator, [int denominator = 1]) =>
      _out((o) => zxvLib.ratNormalize(numerator, denominator, o, o + 1));

  /// Exact text: `'12.75'`, `'-0.1'`, `'3/4'`, `'42'`. Exponents, spaces,
  /// `'NaN'` and a leading `'.'` are `ZXV_E_FORMAT`.
  factory Rational.parse(String text) => using((a) {
    final s = n.cstr(a, text, 'text');
    return _out((o) => zxvLib.ratParse(s, o, o + 1));
  });

  /// THE FLOAT BOUNDARY: the exact value of an IEEE-754 double (its raw
  /// bits go to `zxv_rat_from_ieee754`). NaN and infinity throw
  /// [FloatRejectedException]; a value needing more than 63 bits `ZXV_E_RANGE`.
  factory Rational.fromDoubleExact(double value) {
    final b = ByteData(8)..setFloat64(0, value, Endian.little);
    return Rational.fromIeee754Bits(b.getUint64(0, Endian.little));
  }

  /// The exact rational of raw IEEE-754 binary64 bits (from a file or the wire).
  factory Rational.fromIeee754Bits(int bits) => _out((o) => zxvLib.ratFromIeee754(bits, o, o + 1));

  static Rational _out(int Function(Pointer<Int64> o) f) => using((a) {
    final o = a<Int64>(2);
    n.check(f(o));
    return Rational._(o[0], o[1]);
  });

  Rational _bin(Rational b, int Function(int, int, int, int, Pointer<Int64>, Pointer<Int64>) f) =>
      _out((o) => f(numerator, denominator, b.numerator, b.denominator, o, o + 1));

  Rational operator +(Rational other) => _bin(other, zxvLib.ratAdd);
  Rational operator -(Rational other) => _bin(other, zxvLib.ratSub);
  Rational operator *(Rational other) => _bin(other, zxvLib.ratMul);

  /// Exact division; `ZXV_E_ARG` for division by zero.
  Rational operator /(Rational other) => _bin(other, zxvLib.ratDiv);
  Rational operator -() => zero - this;

  bool operator <(Rational other) => compareTo(other) < 0;
  bool operator <=(Rational other) => compareTo(other) <= 0;
  bool operator >(Rational other) => compareTo(other) > 0;
  bool operator >=(Rational other) => compareTo(other) >= 0;

  int get sign => numerator.sign;
  bool get isInteger => denominator == 1;
  Rational abs() => sign < 0 ? -this : this;

  @override
  int compareTo(Rational other) => using((a) {
    final o = a<Int32>();
    n.check(zxvLib.ratCmp(numerator, denominator, other.numerator, other.denominator, o));
    return o.value;
  });

  /// Q16.16 (value * 65536) as libzxv rounds it; `ZXV_E_RANGE` outside int32.
  int toQ16() => using((a) {
    final o = a<Int32>();
    n.check(zxvLib.ratToQ16(numerator, denominator, o));
    return o.value;
  });

  /// `'n/d'`, or `'n'` for an integer.
  String toFractionString() => denominator == 1 ? '$numerator' : '$numerator/$denominator';

  /// libzxv's canonical text: a terminating decimal (`'0.75'`), else `'1/3'`.
  @override
  String toString() => n.text((b, c, l) => zxvLib.ratFormat(numerator, denominator, b, c, l), null);

  @override
  bool operator ==(Object other) =>
      other is Rational && other.numerator == numerator && other.denominator == denominator;

  @override
  int get hashCode => Object.hash(numerator, denominator);
}
