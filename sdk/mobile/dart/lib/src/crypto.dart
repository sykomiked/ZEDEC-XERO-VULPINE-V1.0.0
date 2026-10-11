// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'context.dart';
import 'errors.dart';
import 'native.dart' as n;

/// SHA3-256, Keccak-256 (Ethereum) and SHAKE256. Pure: no capability.
abstract final class Hashes {
  static Uint8List sha3_256(List<int> data) => _h32(data, zxvLib.sha3_256);
  static Uint8List keccak256(List<int> data) => _h32(data, zxvLib.keccak256);

  static Uint8List shake256(List<int> data, int outLen) {
    if (outLen < 0) throw n.usage(n.eArg, 'outLen must be >= 0');
    return using((a) {
      final o = a<Uint8>(outLen == 0 ? 1 : outLen);
      n.check(zxvLib.shake256(n.bytesIn(a, data), data.length, o, outLen));
      return n.bytesOut(o, outLen);
    });
  }

  static Uint8List _h32(List<int> data, int Function(Pointer<Uint8>, int, Pointer<Uint8>) f) => using((a) {
    final o = a<Uint8>(32);
    n.check(f(n.bytesIn(a, data), data.length, o));
    return n.bytesOut(o, 32);
  });
}

final class _SecretBox {
  _SecretBox(this.ptr, this.size);
  Pointer<Uint8> ptr;
  final int size;

  void wipe() {
    if (ptr == nullptr) return;
    zxvLib.secureZero(ptr.cast(), size);
    calloc.free(ptr);
    ptr = nullptr;
  }
}

final Finalizer<_SecretBox> _secretFinalizer = Finalizer((b) => b.wipe());

/// Secret key material in native memory the Dart GC never moves or copies.
/// Wiped with `zxv_secure_zero` and freed by [close], and by a [Finalizer]
/// backstop if dropped unclosed (Dart does not promise finalizers run, e.g.
/// at isolate exit, so close it).
///
/// The only heap copy is [exportBytesUnsafe]. Not guaranteed: bytes you
/// passed in from the Dart heap ([SecretKey.fromBytes] zeroes a mutable
/// list, but the VM may have copied it), swap, and crash dumps.
final class SecretKey {
  SecretKey._(int size) : _box = _SecretBox(calloc<Uint8>(size), size) {
    _secretFinalizer.attach(this, _box, detach: this);
  }

  final _SecretBox _box;

  /// Length in bytes.
  int get size => _box.size;

  bool get isClosed => _box.ptr == nullptr;

  /// Copies `bytes` into native memory, then fills `bytes` with zeros if it
  /// is a modifiable list (pass a copy to keep yours).
  factory SecretKey.fromBytes(List<int> bytes) {
    final k = SecretKey._(bytes.length);
    k._box.ptr.asTypedList(bytes.length).setAll(0, bytes);
    try {
      bytes.fillRange(0, bytes.length, 0);
    } on UnsupportedError {
      // an unmodifiable list: nothing we can wipe
    }
    return k;
  }

  Pointer<Uint8> get pointer {
    if (isClosed) throw StateError('this secret key is closed');
    return _box.ptr;
  }

  /// A heap copy. UNSAFE: outside the SDK's control and never wiped.
  Uint8List exportBytesUnsafe() => n.bytesOut(pointer, size);

  /// Wipes and frees. Idempotent.
  void close() {
    _secretFinalizer.detach(this);
    _box.wipe();
  }
}

/// An Ed25519 key pair; close the secret key when done.
final class Ed25519KeyPair {
  Ed25519KeyPair(this.publicKey, this.secretKey);
  final Uint8List publicKey;
  final SecretKey secretKey;
  void close() => secretKey.close();
}

/// RFC 8032 Ed25519 through a capability that needs `CRYPTO_SIGN`.
final class Ed25519 {
  Ed25519(this.capability);
  final Capability capability;

  n.ContextState get _st => capability.context.state;

  /// A fresh key pair from the OS random source.
  Ed25519KeyPair generate() {
    final sk = SecretKey._(32);
    try {
      return using((a) {
        final pk = a<Uint8>(32);
        n.check(zxvLib.ed25519Keygen(capability.handle.ptr, pk, sk.pointer), _st);
        return Ed25519KeyPair(n.bytesOut(pk, 32), sk);
      });
    } catch (_) {
      sk.close();
      rethrow;
    }
  }

  Uint8List publicKey(SecretKey sk) {
    _size(sk);
    return using((a) {
      final pk = a<Uint8>(32);
      n.check(zxvLib.ed25519Public(capability.handle.ptr, sk.pointer, pk), _st);
      return n.bytesOut(pk, 32);
    });
  }

  /// A 64-byte signature.
  Uint8List sign(SecretKey sk, List<int> message) {
    _size(sk);
    return using((a) {
      final sig = a<Uint8>(64);
      n.check(zxvLib.ed25519Sign(capability.handle.ptr, sk.pointer, n.bytesIn(a, message), message.length, sig), _st);
      return n.bytesOut(sig, 64);
    });
  }

  /// Returns normally if valid; [VerificationFailedException] if not.
  void verify(List<int> publicKey, List<int> message, List<int> signature) {
    n.requireLength(publicKey, 32, 'publicKey');
    n.requireLength(signature, 64, 'signature');
    using(
      (a) => n.check(
        zxvLib.ed25519Verify(
          capability.handle.ptr,
          n.bytesIn(a, publicKey),
          n.bytesIn(a, message),
          message.length,
          n.bytesIn(a, signature),
        ),
        _st,
      ),
    );
  }

  /// [verify] as a bool: false only for an invalid signature.
  bool isValid(List<int> publicKey, List<int> message, List<int> signature) {
    try {
      verify(publicKey, message, signature);
      return true;
    } on VerificationFailedException {
      return false;
    }
  }

  void _size(SecretKey k) {
    if (k.size != 32) throw n.usage(n.eArg, 'an Ed25519 secret key is 32 bytes, got ${k.size}');
  }
}
