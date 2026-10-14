// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Exact amounts, the same rules as the Mac app (macos/studio Money.swift):
// what the user types becomes whole minor units with integer arithmetic only;
// more decimals than the currency has are refused, never rounded. 64-bit
// amounts from zxvd arrive as decimal strings and are read with BigInt, so
// nothing passes through a double.

/// ISO 4217 currencies whose minor unit is not 2 decimals; everything else
/// (and private codes such as VFV) uses 2.
const _exponents = {
  'BHD': 3, 'IQD': 3, 'JOD': 3, 'KWD': 3, 'LYD': 3, 'OMR': 3, 'TND': 3, //
  'BIF': 0, 'CLP': 0, 'DJF': 0, 'GNF': 0, 'ISK': 0, 'JPY': 0, 'KMF': 0, 'KRW': 0,
  'PYG': 0, 'RWF': 0, 'UGX': 0, 'UYI': 0, 'VND': 0, 'VUV': 0, 'XAF': 0, 'XOF': 0, 'XPF': 0,
  'CLF': 4, 'UYW': 4,
};

int exponentOf(String code) => _exponents[code.toUpperCase()] ?? 2;

final _maxI64 = BigInt.parse('9223372036854775807');

sealed class ParseResult {}

class Parsed extends ParseResult {
  Parsed(this.minor);
  final BigInt minor;
}

class ParseFailed extends ParseResult {
  ParseFailed(this.why);
  final String why;
}

/// "1,250.5" in a 2-decimal currency -> 125050.
ParseResult parseAmount(String text, String currency) {
  final exp = exponentOf(currency);
  final t = text.trim().replaceAll(',', '');
  if (t.isEmpty) return ParseFailed('Enter an amount like 125.00.');
  final parts = t.split('.');
  final digits = RegExp(r'^[0-9]*$');
  if (parts.length > 2 || parts[0].isEmpty || !digits.hasMatch(parts[0])) {
    return ParseFailed('Enter an amount like 125.00.');
  }
  var frac = parts.length == 2 ? parts[1] : '';
  if (parts.length == 2 && (frac.isEmpty || !digits.hasMatch(frac))) {
    return ParseFailed('Enter an amount like 125.00.');
  }
  while (frac.length > exp && frac.endsWith('0')) {
    frac = frac.substring(0, frac.length - 1);
  }
  if (frac.length > exp) {
    return ParseFailed('Refused: $currency has $exp decimal places. zxvd never rounds.');
  }
  final v = BigInt.parse(parts[0] + frac.padRight(exp, '0'));
  if (v == BigInt.zero) return ParseFailed('The amount must be at least one minor unit.');
  if (v > _maxI64) return ParseFailed('That amount is too large.');
  return Parsed(v);
}

/// The amount_minor param: a number while exact in JSON doubles, else a string.
Object amountParam(BigInt minor) =>
    minor <= BigInt.from(9007199254740991) ? minor.toInt() : minor.toString();

/// 125050 USD -> "1,250.50 USD"; negatives get a minus sign. A value that is
/// not a decimal integer is shown as it came.
String formatMinor(String minor, String currency, {bool withCode = true}) {
  final v = BigInt.tryParse(minor);
  if (v == null) return minor;
  final exp = exponentOf(currency);
  final neg = v.isNegative;
  final s = v.abs().toString().padLeft(exp + 1, '0');
  final whole = groupThousands(s.substring(0, s.length - exp));
  final out = exp > 0 ? '$whole.${s.substring(s.length - exp)}' : whole;
  return '${neg ? '−' : ''}$out${withCode ? ' $currency' : ''}';
}

String groupThousands(String digits) {
  final b = StringBuffer();
  for (var i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 == 0) b.write(',');
    b.write(digits[i]);
  }
  return b.toString();
}

String grouped(num n) => n < 0 ? '-${groupThousands((-n).toString())}' : groupThousands(n.toString());
