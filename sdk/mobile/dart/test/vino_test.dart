// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.6 vino: every status_abi16 and vino vector, plus lifetimes,
// capabilities, the poisoned context and the README examples.
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/src/native.dart' as native;
import 'package:zxv/src/vino.dart' show paymentRequestWithLaneCode;
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

String u64s(int v) => BigInt.from(v).toUnsigned(64).toString();

PaymentRequest requestOf(Map<String, dynamic> c) => paymentRequestWithLaneCode(
  i(c['lane']),
  merchant: hexOf(c['merchant']),
  nonce: hexOf(c['nonce']),
  amount: u64(c['amount']),
  createdAt: u64(c['created_at']),
  expiresAt: u64(c['expires_at']),
  memo: s(c['memo']),
  flags: i(c['flags']),
);

void main() {
  final v = sec('vino');

  // ---- statuses, features, capability bits ----

  test('status_abi16 vectors', () {
    for (final e in arr(vectors['status_abi16'])) {
      final c = obj(e);
      final st = i(c['code']);
      expect(Zxv.statusName(st), c['name']);
      expect(Zxv.statusClass(st), StatusClass.fromCode(i(c['class'])));
    }
  });

  test('every new status maps to its typed exception', () {
    final want = <int, (Type, bool Function(Object))>{
      ZxvStatus.eRail: (RailException, (e) => e is ZxvRefusedException),
      ZxvStatus.eCoverage: (CoverageException, (e) => e is ZxvRefusedException),
      ZxvStatus.eLane: (LaneException, (e) => e is ZxvUsageException),
      ZxvStatus.eExpired: (ExpiredException, (e) => e is ZxvRefusedException),
      ZxvStatus.eChance: (ChanceException, (e) => e is ZxvRefusedException),
    };
    for (final MapEntry(key: st, value: (exact, base)) in want.entries) {
      final e = native.raise(st, 'probe', null);
      expect(e.runtimeType, exact, reason: 'status $st');
      expect(base(e), isTrue);
      final z = e as ZxvException;
      expect(z.status, st);
      expect(z.statusName, Zxv.statusName(st));
    }
    expect(
      [ZxvStatus.eRail, ZxvStatus.eCoverage, ZxvStatus.eLane, ZxvStatus.eExpired, ZxvStatus.eChance],
      [-46, -47, -48, -49, -50],
    );
    // -51..-59 are reserved for the vino group: unknown here, so SYSTEM.
    for (var st = -51; st >= -59; st--) {
      expect(native.raise(st, 'probe', null), isA<ZxvSystemException>());
    }
  });

  test('feature, capability bit and the 1.6 mask', () {
    expect(Zxv.version().minor, greaterThanOrEqualTo(6));
    expect(Zxv.hasFeature2(Feature2.vino), isTrue);
    expect(Feature2.vino.bit, 0x100);
    expect(Permission.VINO.bit, 1 << 15);
    expect(Permission.all1_6, (1 << 16) - 1);
    expect(Permission.mask(Permission.all), Permission.all1_6);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(ctx.rootCapability({Permission.VINO}).info().permissions, {Permission.VINO});
  });

  // ---- payment requests ----

  test('request vectors', () {
    for (final e in arr(v['requests'])) {
      final c = obj(e);
      final req = requestOf(c);
      final bytes = expectStatus(c['status'], req.encode);
      if (bytes == null) continue;
      expect(toHex(bytes), c['bytes']);
      expect(toHex(req.id()), c['id']);
      expect(toHex(PaymentRequest.idOf(bytes)), c['id']);
      // Round trip: decode gives back the same value, memo and 64-bit fields exact.
      final back = PaymentRequest.decode(bytes);
      expect(back, req);
      expect(back.hashCode, req.hashCode);
      expect(back.memo, c['memo']);
      expect(u64s(back.expiresAt), c['expires_at']);
      expect(u64s(back.amount), c['amount']);
      expect(back.lane.code, i(c['lane']));
    }
    final r1 = PaymentRequest.decode(hexOf(obj(arr(v['requests'])[1])['bytes']));
    expect(r1.lane, VinoLane.play);
    expect(r1.hasChanceOutcome, isTrue, reason: 'a chance outcome is allowed on the play lane');
  });

  test('typed request refusals and immutability', () {
    final m = Uint8List(32)..fillRange(0, 32, 1);
    final nn = Uint8List(16)..fillRange(0, 16, 2);
    PaymentRequest live({String memo = '', int flags = 0, List<int>? merchant}) => PaymentRequest(
      lane: VinoLane.live,
      merchant: merchant ?? m,
      nonce: nn,
      amount: 5,
      createdAt: 1,
      expiresAt: 2,
      memo: memo,
      flags: flags,
    );
    expect(() => live(flags: Vino.flagChance).encode(), throwsA(isA<ChanceException>()));
    expect(
      () => paymentRequestWithLaneCode(0, merchant: m, nonce: nn, amount: 5, createdAt: 1, expiresAt: 2).encode(),
      throwsA(isA<LaneException>()),
    );
    expect(
      () => live(memo: 'café').encode(),
      throwsA(isA<ZxvUsageException>().having((e) => e.status, 'status', ZxvStatus.eFormat)),
    );
    expect(
      () => live(memo: 'a\u0000b').encode(),
      throwsA(isA<ZxvUsageException>().having((e) => e.status, 'status', ZxvStatus.eArg)),
    );
    expect(() => live(merchant: Uint8List(31)), throwsA(isA<ZxvUsageException>()));
    expect(() => PaymentRequest.decode(Uint8List(159)), throwsA(isA<ZxvUsageException>()));
    final r = live(memo: 'x');
    r.merchant[0] = 9;
    m[0] = 9;
    expect(r.merchant[0], 1);
  });

  test('decode vectors', () {
    final good = hexOf(obj(arr(v['requests'])[0])['bytes']);
    expect(toHex(PaymentRequest.decode(good).encode()), toHex(good));
    for (final e in arr(v['decode'])) {
      final c = obj(e);
      final b = Uint8List.fromList(good)..[i(c['offset'])] = i(c['value']);
      expectStatus(c['status'], () => PaymentRequest.decode(b));
    }
  });

  // ---- receipts ----

  test('receipt vectors', () {
    final r = obj(v['receipt']);
    final request = hexOf(obj(arr(v['requests'])[0])['bytes']);
    final requestId = PaymentRequest.idOf(request);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final dsa = MlDsa87(cap);
    final seed = SecretKey.fromBytes(hexOf(r['seed']));
    final wallet = dsa.generate(seed: seed);
    seed.close();
    addTearDown(wallet.close);
    expect(toHex(Hashes.sha3_256(wallet.publicKey)), r['pk_sha3_256']);
    final approvedAt = u64(r['approved_at']);
    final msg = VinoReceipt.message(requestId, wallet.publicKey, approvedAt);
    expect(toHex(msg), r['msg']);
    final sig = dsa.signDeterministic(wallet.secretKey, msg, context: const []);
    expect(toHex(sig), r['sig']);
    for (final e in arr(r['checks'])) {
      final c = obj(e);
      final sg = Uint8List.fromList(sig);
      final flip = i(c['flip_sig']);
      if (flip >= 0) sg[flip] ^= 1;
      final at = u64(c['approved_at']), now = u64(c['now']);
      expectStatus(c['status'], () => VinoReceipt.verify(request, wallet.publicKey, at, sg, now));
      expect(VinoReceipt.isValid(request, wallet.publicKey, at, sg, now), c['status'] == 'ZXV_OK');
    }
    // The wallet's own call: a hedged signature that verifies offline.
    final live = VinoReceipt.sign(cap, request, wallet.secretKey, wallet.publicKey, approvedAt);
    VinoReceipt.verify(request, wallet.publicKey, approvedAt, live, approvedAt + 1);
    expect(
      () => VinoReceipt.sign(cap, request, wallet.secretKey, wallet.publicKey, 1700000601),
      throwsA(isA<ExpiredException>()),
    );
    expect(
      () => VinoReceipt.verify(request, wallet.publicKey, approvedAt + 1, live, approvedAt + 1),
      throwsA(isA<VerificationFailedException>()),
    );
    final app = without(cap, {Permission.CRYPTO_SIGN});
    expect(
      () => VinoReceipt.sign(app, request, wallet.secretKey, wallet.publicKey, approvedAt),
      throwsA(isA<CapabilityDeniedException>()),
    );
    final short = SecretKey.fromBytes(Uint8List(32));
    addTearDown(short.close);
    expect(
      () => VinoReceipt.sign(cap, request, short, wallet.publicKey, approvedAt),
      throwsA(isA<ZxvUsageException>()),
    );
    expect(
      () => VinoReceipt.verify(request, wallet.publicKey, approvedAt, live.sublist(0, 10), approvedAt),
      throwsA(isA<ZxvUsageException>()),
    );
  });

  // ---- the play lane ----

  test('play scenario', () {
    final p = obj(v['play']);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final sb = PlaySandbox.create(ctx.rootCapability({Permission.VINO}));
    for (final e in arr(p['steps'])) {
      final st = obj(e);
      final status = st['status'];
      int acct(String k) => i(st[k]);
      VinoRail rail() => VinoRail.fromCode(i(st['rail']));
      switch (st['op']) {
        case 'open':
          final kind = i(st['is_issuer']);
          if (kind == 0 || kind == 1) {
            final id = expectStatus(status, () => sb.open(acct('owner'), isIssuer: kind == 1));
            if (id != null) expect(id, i(st['account']));
          } else {
            // A bool cannot carry is_issuer = 2, so the ABI's ZXV_E_ARG for it is unreachable here.
            expect(status, 'ZXV_E_ARG');
          }
        case 'back':
          expectStatus(status, () => sb.back(acct('issuer'), u64(st['amount'])));
        case 'unback':
          expectStatus(status, () => sb.unback(acct('issuer'), u64(st['amount'])));
        case 'issue':
          final id = expectStatus(status, () => sb.issue(acct('issuer'), acct('holder'), u64(st['face']), rail()));
          if (id != null) expect(id, u64(st['id']));
        case 'transfer':
          expectStatus(status, () => sb.transfer(u64(st['voucher']), acct('from'), acct('to'), rail()));
        case 'set_rail':
          expectStatus(status, () => sb.setRail(u64(st['voucher']), acct('holder'), rail()));
        case 'redeem':
          expectStatus(status, () => sb.redeem(u64(st['voucher']), acct('holder')));
        default:
          fail('unknown op ${st['op']}');
      }
    }
    for (final e in arr(p['accounts'])) {
      final a = obj(e);
      final id = i(a['account']);
      expect(
        sb.account(id),
        PlayAccount(id, i(a['owner']), i(a['is_issuer']) == 1, u64(a['backing']), u64(a['faces']), i(a['count'])),
      );
      expect(sb.vouchers(id), [for (final x in arr(a['vouchers'])) u64(x)]);
    }
    for (final e in arr(p['vouchers'])) {
      final x = obj(e);
      final id = u64(x['id']);
      expect(
        sb.voucher(id),
        PlayVoucher(
          id,
          u64(x['face']),
          i(x['issuer']),
          i(x['holder']),
          VinoRail.fromCode(i(x['rail'])),
          VoucherState.fromCode(i(x['state'])),
        ),
      );
    }
    sb.check();
    // Typed refusals on the same sandbox.
    expect(() => sb.transfer(2, 2, 1, VinoRail.bearer), throwsA(isA<RailException>()));
    expect(() => sb.setRail(2, 2, VinoRail.ghost), throwsA(isA<RailException>()));
    expect(() => sb.issue(0, 1, 1, VinoRail.bearer), throwsA(isA<CoverageException>()));
    expect(() => sb.issue(0, 1, 4, VinoRail.bearer), throwsA(isA<NotADenominationException>()));
    expect(() => sb.voucher(99), throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eNotFound)));
    expect(() => sb.vouchers(77), throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eNotFound)));
    expect(() => sb.account(-1), throwsA(isA<ZxvUsageException>()));
    expect(() => sb.openHolder(1 << 32), throwsA(isA<ZxvUsageException>()), reason: 'a uint32 is range-checked');
  });

  test('voucher lists of any length', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final sb = PlaySandbox.create(ctx.rootCapability({Permission.VINO}));
    final iss = sb.openIssuer(1);
    final holder = sb.openHolder(2);
    final other = sb.openHolder(3);
    expect(sb.vouchers(holder), isEmpty);
    sb.back(iss, 1000000);
    final ids = [for (var k = 1; k <= 300; k++) sb.issue(iss, k % 3 == 0 ? other : holder, 1, VinoRail.ghost)];
    expect(sb.vouchers(iss), ids);
    expect(sb.vouchers(holder), [
      for (var k = 0; k < ids.length; k++)
        if ((k + 1) % 3 != 0) ids[k],
    ]);
    expect(sb.vouchers(other).length, 100);
    sb.redeem(ids[0], holder);
    expect(sb.vouchers(iss), isNot(contains(ids[0])), reason: 'redeemed vouchers are not listed');
    expect(sb.voucher(ids[0]).state, VoucherState.redeemed);
    expect(sb.account(iss).faces, 299);
    expect(sb.account(iss).backing, 1000000, reason: 'redeeming pays nothing out');
    sb.check();
  });

  // ---- the meter ----

  test('meter scenario', () {
    final m = obj(v['meter']);
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = ctx.rootCapability({Permission.VINO});
    final meter = SpendMeter.create(cap, u64(m['daily_cap']));
    expect(meter.dailyCap, 100);
    for (final e in arr(m['steps'])) {
      final st = obj(e);
      final day = u64(st['day']), amount = u64(st['amount']);
      switch (st['status']) {
        case 'ZXV_OK':
          expect(meter.tryAdmit(day, amount), (admitted: true, left: u64(st['left'])));
        case 'ZXV_E_LIMIT':
          expect(meter.tryAdmit(day, amount), (admitted: false, left: u64(st['left'])));
        default:
          expectStatus(st['status'], () => meter.tryAdmit(day, amount));
      }
    }
    final small = SpendMeter.create(cap, 10);
    expect(small.admit(1, 6), 4);
    expect(() => small.admit(1, 5), throwsA(isA<LimitException>()));
    expect(small.admit(1, 4), 0);
    expect(() => small.admit(0, 1), throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eRange)));
    for (final bad in [0, Vino.amountMax + 1]) {
      expect(
        () => SpendMeter.create(cap, bad),
        throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eRange)),
      );
    }
  });

  // ---- lifetimes, capabilities, poisoned context ----

  test('use after close is refused', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = ctx.rootCapability({Permission.VINO});
    final sb = PlaySandbox.create(cap);
    final meter = SpendMeter.create(cap, 10);
    sb.openIssuer(1);
    sb.close();
    sb.close(); // idempotent
    expect(() => sb.account(0), throwsA(isA<StateError>().having((e) => e.message, 'message', contains('closed'))));
    meter.close();
    meter.close();
    expect(() => meter.admit(1, 1), throwsA(isA<StateError>()));
    expect(sb.isClosed && meter.isClosed, isTrue);
  });

  test('closing the context closes them', () {
    final ctx = ZxvContext.create();
    final cap = ctx.rootCapability({Permission.VINO});
    final sb = PlaySandbox.create(cap);
    final meter = SpendMeter.create(cap, 10);
    ctx.close();
    expect(sb.isClosed, isTrue);
    expect(meter.isClosed, isTrue);
    expect(sb.check, throwsA(isA<StateError>()));
  });

  test('a capability without VINO is refused', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final r = root(ctx);
    final noVino = without(r, {Permission.VINO});
    expect(
      () => PlaySandbox.create(noVino),
      throwsA(isA<CapabilityDeniedException>().having((e) => e.status, 'status', ZxvStatus.eCap)),
    );
    expect(() => SpendMeter.create(noVino, 10), throwsA(isA<CapabilityDeniedException>()));
    // Pure calls need no capability at all.
    final req = PaymentRequest(
      lane: VinoLane.live,
      merchant: Uint8List(32),
      nonce: Uint8List(16),
      amount: 1,
      createdAt: 1,
      expiresAt: 2,
    );
    expect(req.encode().length, Vino.requestBytes);
    // A sandbox whose capability is revoked later refuses every call.
    final narrow = r.attenuate({Permission.VINO});
    final sb = PlaySandbox.create(narrow);
    sb.openIssuer(1);
    narrow.revoke();
    expect(() => sb.account(0), throwsA(isA<CapabilityDeniedException>()));
    expect(() => sb.openHolder(2), throwsA(isA<CapabilityDeniedException>()));
  });

  test('a halted context fails closed', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = ctx.rootCapability({Permission.VINO});
    final sb = PlaySandbox.create(cap);
    final meter = SpendMeter.create(cap, 10);
    final iss = sb.openIssuer(1);
    ctx.halt('test: poisoned context');
    expect(() => sb.back(iss, 5), throwsA(isA<ZxvFatalError>().having((e) => e.status, 'status', ZxvStatus.ePoisoned)));
    expect(ctx.isPoisoned, isTrue);
    expect(() => sb.account(iss), throwsA(isA<ZxvFatalError>()));
    expect(() => meter.admit(1, 1), throwsA(isA<ZxvFatalError>()));
    expect(() => PlaySandbox.create(cap), throwsA(isA<ZxvFatalError>()));
    expect(sb.isClosed, isFalse, reason: 'a poisoned handle can still be closed');
  });

  test('enums round trip', () {
    for (final r in VinoRail.values) {
      expect(VinoRail.fromCode(r.code), r);
    }
    for (final l in VinoLane.values) {
      expect(VinoLane.fromCode(l.code), l);
    }
    for (final s in VoucherState.values) {
      expect(VoucherState.fromCode(s.code), s);
    }
    expect(() => VinoRail.fromCode(2), throwsA(isA<ZxvUsageException>()));
  });

  // ---- the README examples, as written there ----

  test('README: a play-lane shop', () {
    final ctx = ZxvContext.create();
    final cap = ctx.rootCapability({Permission.VINO});
    final play = PlaySandbox.create(cap);
    final bank = play.openIssuer(1);
    final player = play.openHolder(100);
    final shop = play.openHolder(200);
    play.back(bank, 18); // play units: not money
    final coin = play.issue(bank, player, 8, VinoRail.bearer); // 10*18 >= 18*8
    play.transfer(coin, player, shop, VinoRail.bearer); // spend on its active rail
    play.redeem(coin, shop); // retired; nothing is paid out
    expect(play.voucher(coin).state, VoucherState.redeemed);
    expect(play.account(bank).faces, 0);
    ctx.close(); // closes the capability and the sandbox too
  });

  test('README: a live request, the wallet receipt and the offline check', () {
    const now = 1700000000;
    final request = PaymentRequest(
      lane: VinoLane.live,
      merchant: List.filled(32, 7),
      nonce: List.filled(16, 9),
      amount: 1250,
      createdAt: now,
      expiresAt: now + 600,
      memo: 'Sword of Dawn',
    );
    final bytes = request.encode(); // 160 bytes; request.id() is its SHA-256

    // Wallet side (the user's wallet, its own capability with CRYPTO_SIGN):
    final walletCtx = ZxvContext.create();
    final walletCap = walletCtx.rootCapability({Permission.CRYPTO_SIGN});
    final wallet = MlDsa87(walletCap).generate();
    final sig = VinoReceipt.sign(walletCap, bytes, wallet.secretKey, wallet.publicKey, now + 30);

    // App side: pure and offline, no capability.
    VinoReceipt.verify(bytes, wallet.publicKey, now + 30, sig, now + 60);
    expect(PaymentRequest.decode(bytes), request);

    wallet.close();
    walletCtx.close();
  });
}
