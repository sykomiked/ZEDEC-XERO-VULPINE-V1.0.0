// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 statuses, features and capability bits; handle lifetimes, the
// poisoned context and capability refusals.
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/src/native.dart' as native;
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  test('status_abi15 vectors', () {
    for (final e in arr(vectors['status_abi15'])) {
      final c = obj(e);
      final st = i(c['code']);
      expect(Zxv.statusName(st), c['name']);
      expect(Zxv.statusClass(st), StatusClass.fromCode(i(c['class'])));
    }
  });

  test('every new status maps to its typed exception', () {
    final want = <int, (Type, bool Function(Object))>{
      ZxvStatus.eDenom: (NotADenominationException, (e) => e is ZxvUsageException),
      ZxvStatus.eTreaty: (TreatyException, (e) => e is ZxvUsageException),
      ZxvStatus.eCid: (CidException, (e) => e is ZxvRefusedException),
      ZxvStatus.eConsent: (ConsentException, (e) => e is ZxvRefusedException),
      ZxvStatus.eErasure: (ErasureException, (e) => e is ZxvRefusedException),
      ZxvStatus.ePeg: (PegException, (e) => e is ZxvRefusedException),
      ZxvStatus.eLimit: (LimitException, (e) => e is ZxvRefusedException),
      ZxvStatus.eParadoxStep: (StepLimitException, (e) => e is ZxvRefusedException),
      ZxvStatus.eParadoxOwner: (OwnerApprovalRequiredException, (e) => e is ZxvRefusedException),
    };
    for (final MapEntry(key: st, value: (exact, base)) in want.entries) {
      final e = native.raise(st, 'probe', null);
      expect(e.runtimeType, exact, reason: 'status $st');
      expect(base(e), isTrue);
      final z = e as ZxvException;
      expect(z.status, st);
      expect(z.statusName, Zxv.statusName(st));
    }
    expect(ZxvStatus.eDenom, -32);
    expect(ZxvStatus.eParadoxOwner, -40);
    // -41..-45 are reserved by the ABI: unknown here, so SYSTEM.
    for (var st = -41; st >= -45; st--) {
      expect(native.raise(st, 'probe', null), isA<ZxvSystemException>());
    }
  });

  test('features2 and the 1.5 capability bits', () {
    expect(Zxv.version().minor, greaterThanOrEqualTo(5));
    for (final f in Feature2.values) {
      expect(Zxv.hasFeature2(f), isTrue, reason: '$f is built in');
    }
    expect(Zxv.features2(), Feature2.values.toSet());
    expect(Permission.AIPI.bit, 1 << 12);
    expect(Permission.MINT.bit, 1 << 13);
    expect(Permission.PARADOX.bit, 1 << 14);
    expect(Permission.HW_MATRIX.bit, 1 << 10);
    expect(Permission.HW_RECONF.bit, 1 << 11);
    expect(Permission.mask(Permission.all), (1 << 15) - 1);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(root(ctx).info().permissions, Permission.all, reason: 'a 1.5 library keeps the new bits');
  });

  test('use after close is refused', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final t = Treaty.create();
    final node = AipiNode.create(cap, 1 << 16, 8);
    final w = TokenWallet.create(cap, 1, Octane.plain);
    final hub = VHub.create(cap);
    final join = FreightJoin.create(100);
    final wr = OpStreamWriter.create(1, null, 64);
    final key = AgentKey.create(cap, 'a', 1);
    final dir = AgentDirectory.create(1);
    final m = ToolManifest.create(1, 0, 10);
    final log = ConsentLog.create(1);
    final book = ParadoxBook.create(cap, 4, 4);
    final pol = ParadoxPolicy.create(cap, [0], [1], [0]);
    final hold = ParadoxHold.create(cap, 2, 1);
    final graph = ParadoxGraph.create(cap, 1);
    final all = <(void Function(), Object? Function())>[
      (t.close, t.name),
      (node.close, node.info),
      (w.close, w.info),
      (hub.close, () => hub.currency(0)),
      (join.close, join.status),
      (wr.close, () => wr.copy(1, 1)),
      (key.close, key.card),
      (dir.close, dir.count),
      (m.close, m.info),
      (log.close, () => log.prune(0)),
      (book.close, book.registryCount),
      (pol.close, pol.values),
      (hold.close, hold.info),
      (graph.close, graph.info),
    ];
    for (final (close, use) in all) {
      close();
      close(); // idempotent
      expect(use, throwsA(isA<StateError>().having((e) => e.message, 'message', contains('closed'))));
    }
    expect(t.isClosed && node.isClosed && graph.isClosed, isTrue);
  });

  test('closing the context closes its handles', () {
    final ctx = ZxvContext.create();
    final cap = root(ctx);
    final hub = VHub.create(cap);
    final key = AgentKey.create(cap, 'a', 1);
    ctx.close();
    expect(hub.isClosed, isTrue);
    expect(key.isClosed, isTrue);
    expect(hub.check, throwsStateError);
  });

  test('a poisoned context fails closed', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final hub = VHub.create(cap);
    final w = TokenWallet.create(cap, 1, Octane.plain);
    w.mint(10);
    ctx.halt('test: poisoned context');
    expect(() => hub.currency(0), throwsA(isA<ZxvFatalError>().having((e) => e.status, 'status', ZxvStatus.ePoisoned)));
    expect(ctx.isPoisoned, isTrue);
    // Once marked dead, every handle of the context refuses before reaching C.
    expect(w.info, throwsA(isA<ZxvFatalError>()));
    expect(() => w.mint(1), throwsA(isA<ZxvFatalError>()));
    expect(() => TokenWallet.create(cap, 2, Octane.plain), throwsA(isA<ZxvFatalError>()));
    expect(hub.isClosed, isFalse, reason: 'a poisoned handle can still be closed');
    hub.close();
    w.close();
  });

  test('capability refusals', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final r = root(ctx);
    final noAipi = without(r, {Permission.AIPI});
    final noMint = without(r, {Permission.MINT});
    final noParadox = without(r, {Permission.PARADOX});
    expect(
      () => AipiNode.create(noAipi, 1 << 16, 8),
      throwsA(isA<CapabilityDeniedException>().having((e) => e.status, 'status', ZxvStatus.eCap)),
    );
    expect(() => TokenWallet.create(noMint, 1, Octane.plain), throwsA(isA<CapabilityDeniedException>()));
    expect(() => ParadoxBook.create(noParadox, 4, 4), throwsA(isA<CapabilityDeniedException>()));
    expect(() => ParadoxHold.create(noParadox, 2, 1), throwsA(isA<CapabilityDeniedException>()));
    expect(() => ParadoxGraph.create(noParadox, 1), throwsA(isA<CapabilityDeniedException>()));
    expect(() => ParadoxPolicy.create(noParadox, [0], [1], [0]), throwsA(isA<CapabilityDeniedException>()));
    final w = TokenWallet.create(noAipi, 1, Octane.plain);
    w.mint(5);
    expect(w.value(), 5);
    // A capability of another context is refused.
    final other = ZxvContext.create();
    addTearDown(other.close);
    final mine = TokenWallet.create(r, 1, Octane.plain);
    final theirs = TokenWallet.create(root(other), 1, Octane.plain);
    mine.mint(10);
    expect(
      () => mine.transfer(theirs, 1),
      throwsA(isA<CapabilityDeniedException>().having((e) => e.status, 'status', ZxvStatus.eCap)),
    );
  });

  test('ML-DSA-87 signs and verifies', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final dsa = MlDsa87(cap);
    final kp = dsa.generate();
    addTearDown(kp.close);
    expect(kp.publicKey.length, MlDsa87.publicKeyBytes);
    expect(kp.secretKey.size, MlDsa87.secretKeyBytes);
    final msg = utf8b('peg');
    final sig = dsa.sign(kp.secretKey, msg);
    expect(dsa.isValid(kp.publicKey, msg, sig), isTrue);
    expect(dsa.isValid(kp.publicKey, utf8b('pег'), sig), isFalse);
    final det = dsa.signDeterministic(kp.secretKey, msg, context: utf8b('ctx'));
    expect(dsa.isValid(kp.publicKey, msg, det, context: utf8b('ctx')), isTrue);
    expect(dsa.isValid(kp.publicKey, msg, det), isFalse);
    expect(() => dsa.verify(kp.publicKey, msg, det), throwsA(isA<VerificationFailedException>()));
    // A seed is a SecretKey; the caller's copy is wiped, and the same seed gives the same key.
    final seedBytes = fill(7, 32);
    final seed = SecretKey.fromBytes(seedBytes);
    addTearDown(seed.close);
    expect(seedBytes, Uint8List(32));
    final a = dsa.generate(seed: seed), b2 = dsa.generate(seed: seed);
    expect(a.publicKey, b2.publicKey);
    a.close();
    b2.close();
    expect(() => MlDsa87(without(cap, {Permission.CRYPTO_SIGN})).generate(), throwsA(isA<CapabilityDeniedException>()));
    final short = SecretKey.fromBytes(Uint8List(32));
    addTearDown(short.close);
    expect(() => dsa.sign(short, const []), throwsA(isA<ZxvUsageException>()));
    expect(() => dsa.sign(kp.secretKey, const [], context: Uint8List(256)), throwsA(isA<ZxvUsageException>()));
  });
}
