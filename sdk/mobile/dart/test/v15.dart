// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Helpers for the ABI 1.5 vector tests: 64-bit numbers in the vectors are
// decimal strings.
import 'dart:convert';
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'vectors.dart';

Map<String, dynamic> sec(String name) => obj(vectors[name]);

/// A uint64 given as a decimal string (or a small JSON number), as int bits.
int u64(Object? e) => e is String ? BigInt.parse(e).toSigned(64).toInt() : (e as num).toInt();

/// A signed 64-bit value given as a decimal string (or a JSON number).
int i64(Object? e) => e is String ? int.parse(e) : (e as num).toInt();

int i(Object? e) => (e as num).toInt();
String s(Object? e) => e as String;
bool b(Object? e) => e as bool;
String? str(Object? e) => e as String?;

Uint8List utf8b(Object? e) => Uint8List.fromList(utf8.encode(e as String));
Uint8List hexOf(Object? e) => unhex(e as String);
Uint8List ramp(int n) => Uint8List.fromList(List.generate(n, (x) => x & 0xFF));
Uint8List fill(int value, int n) => Uint8List(n)..fillRange(0, n, value);

final Map<String, int> _codes = {
  for (final t in ['status', 'status_abi15', 'status_abi16'])
    for (final e in arr(vectors[t])) obj(e)['name'] as String: i(obj(e)['code']),
};

/// The code of a status name, from the vectors' status tables.
int code(String name) => _codes[name] ?? (throw ArgumentError(name));

/// Runs `f` expecting `status`: ZXV_OK returns its value; any other status
/// must throw a [ZxvException] with that code (and returns null).
T? expectStatus<T>(Object? status, T Function() f) {
  final st = status as String? ?? 'ZXV_OK';
  if (st == 'ZXV_OK') return f();
  try {
    f();
  } on ZxvException catch (e) {
    expect(e.status, code(st), reason: 'expected $st, got ${e.statusName}: ${e.detail}');
    return null;
  }
  fail('expected $st, but the call succeeded');
}

Capability root(ZxvContext ctx) => ctx.rootCapability(Permission.all);

/// `cap` without the given permissions.
Capability without(Capability cap, Set<Permission> drop) => cap.attenuate(Permission.all.difference(drop));
