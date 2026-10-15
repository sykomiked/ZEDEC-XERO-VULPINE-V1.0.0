// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// The declarations in lib/src/bindings.dart (hand-written),
// lib/src/bindings15.dart (ABI 1.5, generated then checked here) and
// lib/src/bindings16.dart (ABI 1.6 vino) must match
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
    'uint16_t': 'Uint16',
    'size_t': 'Size',
    'void': 'Void',
    'char': 'Utf8',
  };
  // The one function-pointer type of the ABI (a CIDREF resolver).
  if (t == 'zxv_freight_resolve_fn' && stars == 0) return 'Pointer<NativeFunction<FreightResolveC>>';
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

String _params(String list) => list.trim() == 'void'
    ? ''
    : splitTop(list.replaceAll('\n', ' '))
          .map((p) {
            final arr = p.contains('[');
            var q = p.replaceAll(RegExp(r'\[[^\]]*\]'), '').trim();
            q = q.replaceFirst(RegExp(r'\w+$'), '').trim(); // drop the parameter name
            return ffiType(arr ? '$q *' : q);
          })
          .join(', ');

/// Checks every lookupFunction<...>('zxv_name') in `file` against `protos`; returns the names.
Set<String> _check(String file, Map<String, String> protos) {
  final src = File(file).readAsStringSync().replaceAll(RegExp(r'\s+'), ' ');
  final names = <String>{};
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
    expect(names.add(name), isTrue, reason: '$name is declared twice in $file');
  }
  // Every lookup in the file was parsed and checked (none skipped by the pattern).
  expect(names.length, 'lookupFunction<'.allMatches(src).length);
  return names;
}

void main() {
  final header = File('../../libzxv/include/zxv_api.h').readAsStringSync();

  test('every bindings.dart, bindings15.dart and bindings16.dart declaration matches zxv_api.h', () {
    final protos = <String, String>{};
    for (final m in RegExp(r'ZXV_API\s+([^;(]+?)\s*\b(zxv_\w+)\s*\(([^)]*)\)\s*;').allMatches(header)) {
      protos[m[2]!] = '${ffiType(m[1]!)} Function(${_params(m[3]!)})';
    }
    expect(protos.length, greaterThan(350));

    final base = _check('lib/src/bindings.dart', protos);
    final v15 = _check('lib/src/bindings15.dart', protos);
    final v16 = _check('lib/src/bindings16.dart', protos);
    expect(base.length, greaterThan(70));
    expect(base.intersection(v15), isEmpty, reason: 'a function is bound once');
    expect(base.union(v15).intersection(v16), isEmpty, reason: 'a function is bound once');
    final fnRe = RegExp(r'ZXV_API\s+[^;(]+?\s*\b(zxv_\w+)\s*\(');
    // Every ABI 1.5 function (zxv_features2 up to the vino group) and ML-DSA-87 is bound.
    final from = header.indexOf('ZXV_API int32_t zxv_features2(');
    final vinoAt = header.indexOf('/* ===== vino (ABI 1.6)');
    final vinoEnd = header.indexOf('/* ===== end vino ===== */');
    expect(vinoAt, greaterThan(from));
    final abi15 = {for (final m in fnRe.allMatches(header.substring(from, vinoAt))) m[1]!};
    // Every ABI 1.6 vino function is bound, and only those.
    final abi16 = {for (final m in fnRe.allMatches(header.substring(vinoAt, vinoEnd))) m[1]!};
    expect(abi16.length, 21);
    expect(v16, abi16);
    expect(abi15.length, greaterThan(190));
    expect(abi15.difference(v15), isEmpty, reason: 'unbound ABI 1.5 functions');
    for (final f in ['keygen', 'keygen_seed', 'sign', 'sign_deterministic', 'verify']) {
      expect(v15, contains('zxv_mldsa87_$f'));
    }
    expect(v15.length, abi15.length + 5);
  });

  test('FreightResolveC matches zxv_freight_resolve_fn', () {
    final m = RegExp(r'typedef\s+(\w+)\s*\(\s*\*\s*zxv_freight_resolve_fn\s*\)\s*\(([^)]*)\)\s*;').firstMatch(header)!;
    final want = '${ffiType(m[1]!)} Function(${_params(m[2]!)})';
    final src = File('lib/src/bindings15.dart').readAsStringSync().replaceAll(RegExp(r'\s+'), ' ');
    final got = RegExp(r'typedef FreightResolveC = ([^;]+);').firstMatch(src)![1]!;
    expect(got.replaceAll(' ', ''), want.replaceAll(' ', ''));
  });
}
