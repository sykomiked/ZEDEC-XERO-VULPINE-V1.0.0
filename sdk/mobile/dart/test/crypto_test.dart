// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'vectors.dart';

void main() {
  final v = obj(vectors['ed25519']);

  test('ed25519 vector', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final ed = Ed25519(ctx.rootCapability({Permission.CRYPTO_SIGN}));
    final seed = unhex(v['sk'] as String);
    final sk = SecretKey.fromBytes(seed);
    addTearDown(sk.close);
    expect(seed, Uint8List(32), reason: "the caller's copy is wiped");
    expect(toHex(ed.publicKey(sk)), v['pk']);
    final msg = (v['msg_utf8'] as String).codeUnits;
    final sig = ed.sign(sk, msg);
    expect(toHex(sig), v['sig']);
    ed.verify(unhex(v['pk'] as String), msg, sig);
    sig[0] ^= 1;
    expect(ed.isValid(unhex(v['pk'] as String), msg, sig), isFalse);
    expect(() => ed.verify(unhex(v['pk'] as String), msg, sig), throwsA(isA<VerificationFailedException>()));
    expect(toHex(sk.exportBytesUnsafe()), v['sk']);
  });

  test('generated keys sign; close wipes', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final ed = Ed25519(ctx.rootCapability({Permission.CRYPTO_SIGN}));
    final kp = ed.generate();
    final sig = ed.sign(kp.secretKey, const []);
    expect(ed.isValid(kp.publicKey, const [], sig), isTrue);
    kp.close();
    expect(kp.secretKey.isClosed, isTrue);
    expect(() => ed.sign(kp.secretKey, [1]), throwsStateError);
    kp.close();
  });

  test('signing needs CRYPTO_SIGN', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final ed = Ed25519(ctx.rootCapability({Permission.LEDGER_READ}));
    final sk = SecretKey.fromBytes(List.generate(32, (i) => i));
    addTearDown(sk.close);
    expect(() => ed.sign(sk, [1]), throwsA(isA<CapabilityDeniedException>()));
    expect(() => ed.generate(), throwsA(isA<CapabilityDeniedException>()));
  });
}
