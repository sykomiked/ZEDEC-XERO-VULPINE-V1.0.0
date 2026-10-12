// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'bindings15.dart';
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

  /// A zeroed key of `size` bytes for native code to fill. Internal.
  factory SecretKey.allocate(int size) => SecretKey._(size);

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

/// An ML-DSA-87 key pair; close the secret key when done.
final class MlDsa87KeyPair {
  MlDsa87KeyPair(this.publicKey, this.secretKey);

  /// The 2592-byte public key.
  final Uint8List publicKey;
  final SecretKey secretKey;
  void close() => secretKey.close();
}

/// FIPS 204 ML-DSA-87 through a capability that needs `CRYPTO_SIGN` (the
/// V-Bill hub's peg signatures use it). The 4896-byte secret key stays in
/// [SecretKey] memory; a seed for deterministic key generation is a 32-byte
/// [SecretKey] too, so it never sits on the Dart heap.
final class MlDsa87 {
  MlDsa87(this.capability);
  final Capability capability;

  static const int publicKeyBytes = 2592;
  static const int secretKeyBytes = 4896;
  static const int signatureBytes = 4627;

  /// The longest context string.
  static const int contextMax = 255;

  n.ContextState get _st => capability.context.state;

  /// A fresh key pair from the OS random source, or the one a 32-byte `seed`
  /// determines (FIPS 204 ML-DSA.KeyGen_internal).
  MlDsa87KeyPair generate({SecretKey? seed}) {
    if (seed != null && seed.size != 32) throw n.usage(n.eArg, 'an ML-DSA-87 seed is 32 bytes, got ${seed.size}');
    final sk = SecretKey._(secretKeyBytes);
    try {
      return using((a) {
        final pk = a<Uint8>(publicKeyBytes);
        final c = capability.handle.ptr;
        n.check(
          seed == null
              ? zxv15.mldsa87Keygen(c, pk, sk.pointer)
              : zxv15.mldsa87KeygenSeed(c, seed.pointer, pk, sk.pointer),
          _st,
        );
        return MlDsa87KeyPair(n.bytesOut(pk, publicKeyBytes), sk);
      });
    } catch (_) {
      sk.close();
      rethrow;
    }
  }

  /// A hedged (randomized) signature of `message` under `context` (at most 255 bytes).
  Uint8List sign(SecretKey secretKey, List<int> message, {List<int>? context}) =>
      _sign(false, secretKey, message, context);

  /// The deterministic signature of `message` under `context`.
  Uint8List signDeterministic(SecretKey secretKey, List<int> message, {List<int>? context}) =>
      _sign(true, secretKey, message, context);

  Uint8List _sign(bool det, SecretKey sk, List<int> message, List<int>? context) {
    _context(context);
    if (sk.size != secretKeyBytes) {
      throw n.usage(n.eArg, 'an ML-DSA-87 secret key is $secretKeyBytes bytes, got ${sk.size}');
    }
    return using((a) {
      final sig = a<Uint8>(signatureBytes);
      final c = capability.handle.ptr;
      final m = n.bytesIn(a, message);
      final x = n.optBytes(a, context);
      final cl = context?.length ?? 0;
      n.check(
        det
            ? zxv15.mldsa87SignDeterministic(c, sk.pointer, m, message.length, x, cl, sig)
            : zxv15.mldsa87Sign(c, sk.pointer, m, message.length, x, cl, sig),
        _st,
      );
      return n.bytesOut(sig, signatureBytes);
    });
  }

  /// Returns normally if `signature` is valid; [VerificationFailedException] if not.
  void verify(List<int> publicKey, List<int> message, List<int> signature, {List<int>? context}) {
    n.requireLength(publicKey, publicKeyBytes, 'publicKey');
    n.requireLength(signature, signatureBytes, 'signature');
    _context(context);
    using((a) {
      n.check(
        zxv15.mldsa87Verify(
          capability.handle.ptr,
          n.bytesIn(a, publicKey),
          n.bytesIn(a, message),
          message.length,
          n.optBytes(a, context),
          context?.length ?? 0,
          n.bytesIn(a, signature),
        ),
        _st,
      );
    });
  }

  /// [verify] as a bool: false only for an invalid signature; other errors still throw.
  bool isValid(List<int> publicKey, List<int> message, List<int> signature, {List<int>? context}) {
    try {
      verify(publicKey, message, signature, context: context);
      return true;
    } on VerificationFailedException {
      return false;
    }
  }

  void _context(List<int>? context) {
    if (context != null && context.length > contextMax) {
      throw n.usage(n.eArg, 'an ML-DSA context string is at most $contextMax bytes');
    }
  }
}
