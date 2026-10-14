// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 V-Bill hub vectors: one scenario on one hub, in order.
import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final v = sec('vhub');

  test('peg messages', () {
    for (final e in arr(v['peg_msg'])) {
      final c = obj(e);
      final msg = VHub.pegMessage(i(c['slot']), u64(c['num']), u64(c['den']), u64(c['epoch']));
      expect(toHex(msg), c['msg']);
    }
  });

  test('scenario', () {
    final sc = obj(v['scenario']);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final hub = VHub.create(cap, seed: hexOf(sc['hub_seed']));
    final dsa = MlDsa87(cap);
    final keys = <MlDsa87KeyPair>[];
    addTearDown(() {
      for (final k in keys) {
        k.close();
      }
    });
    for (final e in arr(sc['keys'])) {
      final ks = obj(e);
      final seed = SecretKey.fromBytes(hexOf(ks['seed']));
      final k = dsa.generate(seed: seed);
      seed.close();
      expect(toHex(Hashes.sha3_256(k.publicKey)), ks['pk_sha3_256']);
      keys.add(k);
    }
    for (final e in arr(sc['currencies'])) {
      final c = obj(e);
      final slot = i(c['slot']);
      final pk = keys[c.containsKey('issuer_key') ? i(c['issuer_key']) : 0].publicKey;
      final pool = c.containsKey('pool_funding') ? u64(c['pool_funding']) : 1;
      final backing = c.containsKey('vbill_backing') ? u64(c['vbill_backing']) : 1;
      final mu = c.containsKey('minor_units') ? i(c['minor_units']) : 2;
      final ci = expectStatus(c['status'], () => hub.addCurrency(slot, s(c['code']), mu, pk, pool, backing));
      if (ci == null) continue;
      expect(ci.issuerAccount, i(c['issuer_account']));
      expect(ci.poolAccount, i(c['pool_account']));
      expect(ci.vfloatAccount, i(c['vfloat_account']));
      expect(ci.hasPeg, isFalse);
    }
    for (final e in arr(sc['pegs'])) {
      final c = obj(e);
      final slot = i(c['slot']);
      final num = u64(c['num']), den = u64(c['den']), epoch = u64(c['epoch']);
      final sig = dsa.signDeterministic(
        keys[i(c['signer'])].secretKey,
        VHub.pegMessage(slot, num, den, epoch),
        context: const [],
      );
      expect(toHex(Hashes.sha3_256(sig)), c['sig_sha3_256']);
      expectStatus(c['status'], () => hub.setPeg(slot, num, den, epoch, sig));
    }
    final zta = hub.currency(0);
    expect(zta.pegNum, 350);
    expect(zta.pegEpoch, 1, reason: 'refused pegs changed nothing');
    for (final e in arr(sc['accounts'])) {
      final c = obj(e);
      final acct = hub.openAccount(i(c['slot']), i(c['owner']));
      expect(acct, i(c['account']));
      if (c.containsKey('issued')) hub.issue(acct, u64(c['issued']));
    }
    for (final e in arr(sc['quotes'])) {
      final c = obj(e);
      final q = expectStatus(c['status'], () => hub.quote(i(c['from']), i(c['to']), u64(c['want'])));
      if (q != null) expect(q, (amount: u64(c['amount']), vbills: u64(c['vbills']), received: u64(c['received'])));
    }
    for (final e in arr(sc['swaps'])) {
      final c = obj(e);
      if (c.containsKey('add_limit')) {
        final al = obj(c['add_limit']);
        hub.addLimit(i(al['slot']), u64(al['limit']));
      }
      final r = expectStatus(
        c['status'],
        () => hub.swap(i(c['from']), i(c['to']), i(c['sender']), i(c['receiver']), u64(c['amount']), u64(c['tick'])),
      );
      if (r != null) expect(r, (vbills: u64(c['vbills']), received: u64(c['received'])));
    }
    expect(hub.check(), u64(sc['postings']));
    for (final e in arr(sc['balances'])) {
      final c = obj(e);
      expect(hub.balance(i(c['account'])), (
        debit: u64(c['debit']),
        credit: u64(c['credit']),
        equity: i64(c['equity']),
      ));
    }
    // Typed refusals.
    expect(() => hub.swap(0, 1, 13, 14, 105000, 20), throwsA(isA<LimitException>()));
    expect(() => hub.quote(0, 3, 1), throwsA(isA<PegException>()));
    expect(hub.nextEpoch(), 1);
    expect(VHub.pegMessage(0, 350, 1, 1), VHub.pegMessage(0, 350, 1, 1));
    expect(hub.check(), greaterThanOrEqualTo(14));
    // Range checks happen before C: a slot is a uint8, an account a uint32.
    expect(() => hub.currency(256), throwsA(isA<ZxvUsageException>()));
    expect(() => hub.balance(-1), throwsA(isA<ZxvUsageException>()));
  });

  test('the hub needs LEDGER_POST', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(() => VHub.create(without(root(ctx), {Permission.LEDGER_POST})), throwsA(isA<CapabilityDeniedException>()));
  });
}
