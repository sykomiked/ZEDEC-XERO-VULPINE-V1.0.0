// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

void main() {
  test('a first ledger', () {
    final ctx = ZxvContext.create();
    try {
      final root = ctx.rootCapability(Permission.all);
      ctx.enableCurrency(root, 'NGN');
      final ledger = Ledger(root);
      final bank = ledger.openAccount('bank', 'NGN', AccountKind.issuer);
      final alice = ledger.openAccount('alice', 'NGN');
      final bob = ledger.openAccount('bob', 'NGN');
      final e1 = ledger.post(bank, alice, 10000, 'NGN', 'fund-1');
      final e2 = ledger.post(alice, bob, 2550, 'NGN', 'pay-1');
      expect(e2, greaterThan(e1));
      expect(ledger.balance(alice), const Balance(10000, 2550, 7450));
      expect(ledger.accountCount(), 3);
      expect(ledger.account(alice.id), alice);
      final j = ledger.journal(alice);
      expect(j.map((e) => e.reference), ['fund-1', 'pay-1']);
      expect(j.map((e) => e.signedMinor), [10000, -2550]);
      expect(
        () => ledger.post(bob, alice, 1000000, 'NGN', 'too-much'),
        throwsA(isA<InsufficientFundsException>().having((x) => x.status, 'status', -6)),
      );
      expect(() => ledger.post(alice, bob, 1, 'NGN', 'pay-1'), throwsA(isA<ZxvUsageException>()));
      ledger.check();
    } finally {
      ctx.close();
    }
  });

  test('capabilities are checked on every call', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final root = ctx.rootCapability(Permission.all);
    ctx.enableCurrency(root, 'USD');
    final a = Ledger(root).openAccount('a', 'USD', AccountKind.issuer);
    final b = Ledger(root).openAccount('b', 'USD');
    final reader = root.attenuate({Permission.LEDGER_READ});
    expect(reader.info().permissions, {Permission.LEDGER_READ});
    expect(reader.allows({Permission.LEDGER_READ}), isTrue);
    expect(reader.allows({Permission.LEDGER_POST}), isFalse);
    expect(
      () => Ledger(reader).post(a, b, 1, 'USD', 'r1'),
      throwsA(isA<CapabilityDeniedException>().having((x) => x.status, 'status', -17)),
    );
    expect(Ledger(reader).balance(b).equityMinor, 0);
    reader.setAttestation(Truth.BOTH);
    expect(() => Ledger(reader).balance(b), throwsA(isA<CapabilityDeniedException>()));
    expect(reader.info().attestation, Truth.BOTH);
    reader.setAttestation(Truth.TRUE);
    Ledger(reader).balance(b);
    root.revoke();
    expect(reader.info().revoked, isTrue);
    expect(() => Ledger(reader).balance(b), throwsA(isA<CapabilityDeniedException>()));
    expect(() => reader.require({Permission.LEDGER_READ}), throwsA(isA<CapabilityDeniedException>()));
  });

  test('a capability from another context is refused', () {
    final c1 = ZxvContext.create(), c2 = ZxvContext.create();
    addTearDown(c1.close);
    addTearDown(c2.close);
    final foreign = c2.rootCapability(Permission.all);
    expect(() => c1.enableCurrency(foreign, 'EUR'), throwsA(isA<CapabilityDeniedException>()));
  });

  test('halt is fatal and cannot be swallowed as an Exception', () {
    final ctx = ZxvContext.create();
    final root = ctx.rootCapability(Permission.all);
    ctx.enableCurrency(root, 'NGN');
    final ledger = Ledger(root);
    final a = ledger.openAccount('a', 'NGN');
    expect(ctx.isPoisoned, isFalse);
    ctx.halt('test kill switch');
    expect(ctx.isPoisoned, isTrue);
    Object? caught;
    try {
      try {
        ledger.balance(a);
      } on Exception catch (e) {
        fail('swallowed as an Exception: $e');
      }
    } on ZxvFatalError catch (e) {
      caught = e;
    }
    expect(caught, isA<ZxvFatalError>().having((x) => x.status, 'status', -18));
    expect(caught, isNot(isA<Exception>()));
    expect(() => ledger.openAccount('b', 'NGN'), throwsA(isA<ZxvFatalError>()));
    expect(() => root.attenuate({Permission.LEDGER_READ}), throwsA(isA<ZxvFatalError>()));
    root.revoke(); // only takes authority away: still allowed
    ctx.close();
  });

  test('closing the context closes its children first', () {
    final ctx = ZxvContext.create();
    final root = ctx.rootCapability(Permission.all);
    final child = root.attenuate({Permission.LEDGER_READ});
    final grandchild = child.attenuate({Permission.LEDGER_READ});
    final log = QuarantineLog.create(root, 4);
    final budgets = Budgets.create(root);
    ctx.close();
    for (final op in <void Function()>[
      () => root.info(),
      () => child.info(),
      () => grandchild.info(),
      () => log.count(),
      () => budgets.budget('x', Resource.tokens, Rational.one),
    ]) {
      expect(op, throwsStateError);
    }
    ctx.close();
    grandchild.close();
  });

  test('bad arguments are usage errors', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(() => ctx.rootCapability(Permission.all, processId: -1), throwsA(isA<ZxvUsageException>()));
    expect(() => ctx.rootCapability(Permission.all, identity: List.filled(20, 0)), throwsA(isA<ZxvUsageException>()));
    final root = ctx.rootCapability(Permission.all, processId: 0xFFFFFFFF, identity: List.filled(21, 0));
    expect(root.info().processId, 0xFFFFFFFF);
    expect(() => ctx.enableCurrency(root, 'USD', minorUnits: 256), throwsA(isA<ZxvUsageException>()));
    expect(() => ctx.enableCurrency(root, 'NOPE'), throwsA(isA<ZxvUsageException>()));
    expect(() => Ledger(root).balanceById(-1), throwsA(isA<ZxvUsageException>()));
  });
}
