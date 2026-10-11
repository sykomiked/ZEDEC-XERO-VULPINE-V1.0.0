// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// The hand-written declarations in lib/src/bindings.dart must match
// zxv_api.h exactly (guides/writing-a-binding.md, "ABI drift"): a wrong
// width or a missing pointer would corrupt memory instead of failing.
import 'dart:io';

import 'package:test/test.dart';

const _typedefs = {
  'RatBinC': 'Int32 Function(Int64, Int64, Int64, Int64, Pointer<Int64>, Pointer<Int64>)',
  'RmagC': 'Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Int64, Int64)',
  'HashC': 'Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>)',
};

/// C type (no name) -> dart:ffi native type.
String ffiType(String c) {
  var t = c.replaceAll('const ', '').replaceAll(RegExp(r'\s+'), ' ').trim();
  final stars = '*'.allMatches(t).length;
  t = t.replaceAll('*', '').trim();
  const scalars = {
    'int32_t': 'Int32',
    'uint32_t': 'Uint32',
    'int64_t': 'Int64',
    'uint64_t': 'Uint64',
    'uint8_t': 'Uint8',
    'size_t': 'Size',
    'void': 'Void',
    'char': 'Utf8',
  };
  var base = scalars[t] ?? 'Void'; // opaque handles (zxv_ctx, ...) are Pointer<Void>
  if (stars == 0) return base;
  if (scalars[t] == null) {
    // a handle: zxv_ctx * -> Pointer<Void>, zxv_ctx ** -> Pointer<Pointer<Void>>
    base = 'Pointer<Void>';
    for (var i = 1; i < stars; i++) {
      base = 'Pointer<$base>';
    }
    return base;
  }
  for (var i = 0; i < stars; i++) {
    base = 'Pointer<$base>';
  }
  return base;
}

List<String> splitTop(String s) {
  final out = <String>[];
  var depth = 0, start = 0;
  for (var i = 0; i < s.length; i++) {
    final c = s[i];
    if (c == '<' || c == '(') depth++;
    if (c == '>' || c == ')') depth--;
    if (c == ',' && depth == 0) {
      out.add(s.substring(start, i).trim());
      start = i + 1;
    }
  }
  final last = s.substring(start).trim();
  if (last.isNotEmpty) out.add(last);
  return out;
}

void main() {
  test('every bindings.dart declaration matches zxv_api.h', () {
    final header = File('../../libzxv/include/zxv_api.h').readAsStringSync();
    final protos = <String, String>{};
    for (final m in RegExp(r'ZXV_API\s+([^;(]+?)\s*\b(zxv_\w+)\s*\(([^)]*)\)\s*;').allMatches(header)) {
      final ret = ffiType(m[1]!);
      final params = m[3]!.trim() == 'void'
          ? <String>[]
          : splitTop(m[3]!.replaceAll('\n', ' ')).map((p) {
              final arr = p.contains('[');
              var q = p.replaceAll(RegExp(r'\[[^\]]*\]'), '').trim();
              q = q.replaceFirst(RegExp(r'\w+$'), '').trim(); // drop the parameter name
              return ffiType(arr ? '$q *' : q);
            }).toList();
      protos[m[2]!] = '$ret Function(${params.join(', ')})';
    }
    expect(protos.length, greaterThan(150));

    final src = File('lib/src/bindings.dart').readAsStringSync().replaceAll(RegExp(r'\s+'), ' ');
    var checked = 0;
    // Scan each lookupFunction<...>('zxv_name') by bracket depth (formatting-proof).
    for (var at = src.indexOf('lookupFunction<'); at >= 0; at = src.indexOf('lookupFunction<', at + 1)) {
      var i = at + 'lookupFunction<'.length, depth = 1;
      final start = i;
      while (depth > 0) {
        final c = src[i++];
        if (c == '<') depth++;
        if (c == '>') depth--;
      }
      final typeArgs = src.substring(start, i - 1);
      final name = RegExp(r"^\(\s*'(zxv_\w+)'").firstMatch(src.substring(i))![1]!;
      var sig = splitTop(typeArgs).first.replaceAll('_I32', 'Int32');
      sig = _typedefs[sig] ?? sig;
      expect(protos, contains(name), reason: '$name is not in zxv_api.h');
      expect(sig.replaceAll(' ', '').replaceAll(',)', ')'), protos[name]!.replaceAll(' ', ''), reason: name);
      checked++;
    }
    // Every lookup in the file was parsed and checked (none skipped by the pattern).
    expect(checked, 'lookupFunction<'.allMatches(src).length);
    expect(checked, greaterThan(70));
  });
}
