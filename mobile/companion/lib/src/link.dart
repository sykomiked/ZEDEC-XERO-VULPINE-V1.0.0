// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The phone side of zxv-phone/1 (kernel/arch/hosted/zxvd_phone.h L2-L3):
// an X25519 handshake keyed by the pairing secret, then JSON-RPC requests in
// ChaCha20-Poly1305 frames, one answer per request, in order.
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';

import 'pairing.dart';

/// A JSON-RPC error from zxvd (or a broken link, code 0).
class RpcError implements Exception {
  RpcError(this.code, this.message);
  final int code;
  final String message;

  /// zxvd_phone.h A2/A3: the method is not allowed from this phone.
  bool get notAllowed => code == -32012;

  @override
  String toString() => notAllowed
      ? (message.contains('allow_post')
          ? 'This phone was paired read-only. Pair it again on the Mac with "Allow money and network changes" to do this.'
          : 'Only the Mac can do this.')
      : message;
}

const _proto = 'zxv-phone/1';
const maxRequest = 16384; // ZXVD_RPC_MAX_LINE
const maxResponse = 4 << 20; // ZXVD_PHONE_MAX_OUT

Uint8List _nonce(int seq) {
  final n = Uint8List(12);
  for (var i = 0; i < 8; i++) {
    n[4 + i] = (seq >> (56 - 8 * i)) & 0xff;
  }
  return n;
}

Uint8List _be32(int v) =>
    Uint8List.fromList([(v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff]);

bool _sameBytes(List<int> a, List<int> b) {
  if (a.length != b.length) return false;
  var d = 0;
  for (var i = 0; i < a.length; i++) {
    d |= a[i] ^ b[i];
  }
  return d == 0;
}

/// Bytes arriving on a socket, read as exact-length chunks.
class _Reader {
  _Reader(Stream<Uint8List> s) {
    _sub = s.listen(_on, onError: (Object e) => _fail(e), onDone: () => _fail(null));
  }

  late final StreamSubscription<Uint8List> _sub;
  final _chunks = <Uint8List>[];
  int _avail = 0, _want = 0;
  Completer<Uint8List>? _waiter;
  Object? _closed;

  void _on(Uint8List d) {
    _chunks.add(d);
    _avail += d.length;
    _deliver();
  }

  void _fail(Object? e) {
    _closed = e ?? const SocketException('zxvd closed the connection');
    final w = _waiter;
    _waiter = null;
    w?.completeError(_closed!);
  }

  void _deliver() {
    final w = _waiter;
    if (w == null) return;
    if (_avail < _want) {
      if (_closed != null) _fail(_closed);
      return;
    }
    final out = Uint8List(_want);
    var o = 0;
    while (o < _want) {
      final c = _chunks.first;
      final take = c.length < _want - o ? c.length : _want - o;
      out.setRange(o, o + take, c);
      o += take;
      if (take == c.length) {
        _chunks.removeAt(0);
      } else {
        _chunks[0] = Uint8List.sublistView(c, take);
      }
    }
    _avail -= _want;
    _waiter = null;
    w.complete(out);
  }

  Future<Uint8List> read(int n) {
    if (_waiter != null) throw StateError('one read at a time');
    _want = n;
    final w = _waiter = Completer<Uint8List>();
    _deliver();
    return w.future;
  }

  Future<void> cancel() => _sub.cancel();
}

/// One open, authenticated connection to zxvd.
class PhoneLink {
  PhoneLink._(this._socket, this._reader, this._kOut, this._kIn);

  final Socket _socket;
  final _Reader _reader;
  final SecretKey _kOut, _kIn;
  int _seqOut = 0, _seqIn = 0, _nextId = 1;
  Future<void> _queue = Future.value();
  bool _closed = false;

  static final _aead = Chacha20.poly1305Aead();

  /// Connects and runs the handshake. Throws [HandshakeError] when zxvd
  /// does not prove it holds the pairing key (a wrong or revoked pairing, or
  /// something else on that address).
  static Future<PhoneLink> connect(Pairing p,
      {Duration timeout = const Duration(seconds: 8)}) async {
    final socket = await Socket.connect(p.host, p.port, timeout: timeout);
    socket.setOption(SocketOption.tcpNoDelay, true);
    final reader = _Reader(socket);
    try {
      final x = X25519();
      final eph = await x.newKeyPair();
      final epub = (await eph.extractPublicKey()).bytes;
      socket.add([...ascii.encode('ZXP1'), ...p.id, ...epub]);
      final rep = await reader.read(64).timeout(timeout);
      final es = rep.sublist(0, 32);
      final dh = await (await x.sharedSecretKey(
              keyPair: eph, remotePublicKey: SimplePublicKey(es, type: KeyPairType.x25519)))
          .extractBytes();
      if (dh.every((b) => b == 0)) throw HandshakeError('zxvd sent a weak key');
      final th =
          (await Sha256().hash([...ascii.encode(_proto), ...p.id, ...epub, ...es])).bytes;
      final okm = await (Hkdf(hmac: Hmac.sha256(), outputLength: 96)).deriveKey(
          secretKey: SecretKey([...dh, ...th]),
          nonce: p.key,
          info: ascii.encode('$_proto keys'));
      final k = await okm.extractBytes();
      final want = (await Hmac.sha256().calculateMac(
              [...ascii.encode('server confirm'), ...th],
              secretKey: SecretKey(k.sublist(64, 96))))
          .bytes;
      if (!_sameBytes(want, rep.sublist(32, 64))) {
        throw HandshakeError('this pairing is not known to zxvd (revoked, or paired again)');
      }
      return PhoneLink._(socket, reader, SecretKey(k.sublist(0, 32)), SecretKey(k.sublist(32, 64)));
    } catch (_) {
      await reader.cancel();
      socket.destroy();
      rethrow;
    }
  }

  bool get isClosed => _closed;

  /// One JSON-RPC call; the result value, or an [RpcError]. Calls are
  /// queued: zxvd answers one request at a time, in order.
  Future<dynamic> call(String method, [Map<String, dynamic>? params, Duration? timeout]) {
    final c = Completer<dynamic>();
    _queue = _queue.then((_) async {
      try {
        c.complete(await _call(method, params, timeout ?? const Duration(seconds: 30)));
      } catch (e, st) {
        c.completeError(e, st);
      }
    });
    return c.future;
  }

  Future<dynamic> _call(String method, Map<String, dynamic>? params, Duration timeout) async {
    if (_closed) throw RpcError(0, 'not connected');
    final id = _nextId++;
    final req = utf8.encode(jsonEncode({
      'jsonrpc': '2.0',
      'id': id,
      'method': method,
      'params': ?params,
    }));
    if (req.length > maxRequest) throw RpcError(-32600, 'request too long');
    try {
      final hdr = _be32(req.length);
      final box = await _aead.encrypt(req, secretKey: _kOut, nonce: _nonce(_seqOut++), aad: hdr);
      _socket.add([...hdr, ...box.cipherText, ...box.mac.bytes]);
      final rh = await _reader.read(4).timeout(timeout);
      final n = (rh[0] << 24) | (rh[1] << 16) | (rh[2] << 8) | rh[3];
      if (n > maxResponse) throw const FormatException('answer too long');
      final body = await _reader.read(n + 16).timeout(timeout);
      final pt = await _aead.decrypt(
          SecretBox(body.sublist(0, n), nonce: _nonce(_seqIn++), mac: Mac(body.sublist(n))),
          secretKey: _kIn,
          aad: rh);
      final r = jsonDecode(utf8.decode(pt)) as Map<String, dynamic>;
      if (r['id'] != id) throw const FormatException('answer out of order');
      final err = r['error'];
      if (err is Map) throw RpcError(err['code'] as int, err['message'] as String);
      return r['result'];
    } on RpcError {
      rethrow;
    } catch (e) {
      // anything else leaves the stream unusable: a later answer could not
      // be matched to its request
      close();
      throw RpcError(0, 'connection lost ($e)');
    }
  }

  void close() {
    if (_closed) return;
    _closed = true;
    _reader.cancel();
    _socket.destroy();
  }
}

class HandshakeError implements Exception {
  HandshakeError(this.message);
  final String message;
  @override
  String toString() => message;
}
