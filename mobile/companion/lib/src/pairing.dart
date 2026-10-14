// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The pairing link zxvd's phone.pair returns (zxvd_phone.h P1):
//   zxv-phone:1?h=HOST&p=PORT&id=<32 hex>&k=<64 hex>&n=NAME
// It is this phone's only secret. It is kept in the iOS Keychain / Android
// Keystore and never shown again after pairing.
import 'dart:typed_data';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

class Pairing {
  Pairing({required this.host, required this.port, required this.id, required this.key, required this.name});

  final String host;
  final int port;
  final Uint8List id; // 16 bytes
  final Uint8List key; // 32 bytes
  final String name;

  String get idHex => _hex(id);

  /// Parses a pairing link; throws [FormatException] saying what is wrong.
  static Pairing parse(String text) {
    final t = text.trim();
    if (!t.startsWith('zxv-phone:1?')) {
      throw const FormatException('Not a ZXV pairing link. It starts with zxv-phone:1?');
    }
    final q = Uri.splitQueryString(t.substring('zxv-phone:1?'.length));
    final host = q['h'] ?? '';
    final port = int.tryParse(q['p'] ?? '');
    final id = _unhex(q['id'] ?? '', 16);
    final key = _unhex(q['k'] ?? '', 32);
    if (host.isEmpty) throw const FormatException('The link has no address (h=).');
    if (port == null || port < 1 || port > 65535) {
      throw const FormatException('The link has no valid port (p=).');
    }
    if (id == null) throw const FormatException('The link has no valid phone id (id=).');
    if (key == null) throw const FormatException('The link has no valid key (k=).');
    return Pairing(host: host, port: port, id: id, key: key, name: q['n'] ?? 'phone');
  }

  /// The same pairing reached at another address (the Mac's address changed).
  Pairing withHost(String h, int p) => Pairing(host: h, port: p, id: id, key: key, name: name);

  String toLink() =>
      'zxv-phone:1?h=$host&p=$port&id=${_hex(id)}&k=${_hex(key)}&n=${Uri.encodeQueryComponent(name)}';

  static String _hex(List<int> b) => b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();

  static Uint8List? _unhex(String s, int n) {
    if (s.length != 2 * n || !RegExp(r'^[0-9a-f]+$').hasMatch(s)) return null;
    final out = Uint8List(n);
    for (var i = 0; i < n; i++) {
      out[i] = int.parse(s.substring(2 * i, 2 * i + 2), radix: 16);
    }
    return out;
  }
}

/// Where the pairing is kept between launches.
abstract class PairingStore {
  Future<Pairing?> load();
  Future<void> save(Pairing p);
  Future<void> forget();
}

class SecurePairingStore implements PairingStore {
  static const _k = 'zxv.pairing.v1';
  final _s = const FlutterSecureStorage();

  @override
  Future<Pairing?> load() async {
    try {
      final v = await _s.read(key: _k);
      return v == null ? null : Pairing.parse(v);
    } catch (_) {
      return null;
    }
  }

  @override
  Future<void> save(Pairing p) => _s.write(key: _k, value: p.toLink());

  @override
  Future<void> forget() => _s.delete(key: _k);
}

class MemoryPairingStore implements PairingStore {
  MemoryPairingStore([this.p]);
  Pairing? p;
  @override
  Future<Pairing?> load() async => p;
  @override
  Future<void> save(Pairing x) async => p = x;
  @override
  Future<void> forget() async => p = null;
}
