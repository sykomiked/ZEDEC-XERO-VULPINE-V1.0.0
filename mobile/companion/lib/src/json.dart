// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A forgiving view over zxvd's JSON results (api/zxvd.openrpc.json; the
// swarm.state shape is state_json in kernel/arch/hosted/zxv_host.c). A field
// that is missing or of another type reads as empty, so a newer zxvd with
// more fields, or an older one with fewer, never breaks a screen.

class J {
  const J(this.raw);
  final Object? raw;

  static const empty = J(null);

  Map<String, dynamic> get _m => raw is Map<String, dynamic> ? raw as Map<String, dynamic> : const {};

  bool get isNull => raw == null;
  J operator [](String k) => J(_m[k]);

  String str(String k, [String d = '']) {
    final v = _m[k];
    return v is String ? v : (v is num || v is bool ? '$v' : d);
  }

  int integer(String k, [int d = 0]) {
    final v = _m[k];
    if (v is int) return v;
    if (v is String) return int.tryParse(v) ?? d;
    return d;
  }

  bool boolean(String k, [bool d = false]) {
    final v = _m[k];
    return v is bool ? v : d;
  }

  List<J> list(String k) {
    final v = _m[k];
    return v is List ? v.map(J.new).toList() : const [];
  }

  List<String> strings(String k) {
    final v = _m[k];
    return v is List ? v.whereType<String>().toList() : const [];
  }
}
