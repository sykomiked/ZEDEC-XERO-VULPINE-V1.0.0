// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

/// sdk/conformance/vectors.json (ZXV_VECTORS overrides the path).
final Map<String, dynamic> vectors =
    jsonDecode(File(Platform.environment['ZXV_VECTORS'] ?? '../../conformance/vectors.json').readAsStringSync())
        as Map<String, dynamic>;

Map<String, dynamic> obj(Object? o) => o as Map<String, dynamic>;
List<dynamic> arr(Object? o) => o as List<dynamic>;

Uint8List unhex(String s) =>
    Uint8List.fromList([for (var i = 0; i < s.length; i += 2) int.parse(s.substring(i, i + 2), radix: 16)]);

String toHex(List<int> b) => b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();
