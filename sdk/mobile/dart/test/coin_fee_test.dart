// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 coin, token and assurance-fee vectors, plus the token wallet.
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final c0 = sec('coin');
  final f0 = sec('fee');

  TokenHolding holding(Object? sparse) {
    final c = List<int>.filled(Tokens.tiers, 0);
    for (final p in arr(sparse)) {
      c[i(arr(p)[0])] = u64(arr(p)[1]);
    }
    return TokenHolding.of(c);
  }

  Matcher status(int st) => throwsA(isA<ZxvException>().having((e) => e.status, 'status', st));

  test('the coin ladder', () {
    expect(Coins.rungs, i(c0['rungs']));
    expect(Coins.topFace, u64(c0['top_face']));
    for (final e in arr(c0['face'])) {
      final c = obj(e);
      final f = expectStatus(c['status'], () => Coins.face(i(c['rung'])));
      if (f != null) expect(f, u64(c['face']));
    }
    for (final e in arr(c0['rung_of'])) {
      final c = obj(e);
      final r = expectStatus(c['status'], () => Coins.rung(u64(c['face'])));
      if (r != null) expect(r, i(c['rung']));
    }
    expect(() => Coins.rung(4), throwsA(isA<NotADenominationException>()));
  });

  test('split', () {
    for (final e in arr(c0['split'])) {
      final c = obj(e);
      final amount = u64(c['amount']);
      final count = expectStatus(c['status'], () => Coins.coinCount(amount));
      if (count == null) continue;
      expect(count, u64(c['count']));
      if (c.containsKey('rungs')) {
        expect(Coins.split(amount), [for (final r in arr(c['rungs'])) i(r)]);
      } else if (count < 100000) {
        expect(Coins.split(amount).length, count);
      }
    }
    // 3,811,988,588 coins fit no list: counted, and split() refuses with RANGE.
    expect(() => Coins.split(7000000000000000000), status(ZxvStatus.eRange));
    expect(() => Coins.split(7000000000000000000), throwsA(isA<ZxvUsageException>()));
  });

  test('token faces and holdings', () {
    for (final e in arr(c0['token_face'])) {
      final c = obj(e);
      final f = expectStatus(c['status'], () => Tokens.face(i(c['tier'])));
      if (f == null) continue;
      expect(f, u64(c['face']));
      expect(Tokens.isSuper(i(c['tier'])), b(c['super']));
    }
    for (final e in arr(c0['token_tier'])) {
      final c = obj(e);
      final t = expectStatus(c['status'], () => Tokens.tier(u64(c['face'])));
      if (t != null) expect(t, i(c['tier']));
    }
    for (final e in arr(c0['token_value'])) {
      final c = obj(e);
      final h = holding(c['counts']);
      final v = expectStatus(c['status'], h.value);
      if (v == null) continue;
      expect(v, u64(c['value']));
      expect(h.pieces(), u64(c['pieces']));
    }
    for (final e in arr(c0['token_canonical'])) {
      final c = obj(e);
      final h = expectStatus(c['status'], () => TokenHolding.canonical(u64(c['value']), i(c['octane'])));
      if (h == null) continue;
      expect(h, holding(c['counts']));
      expect(h.pieces(), u64(c['pieces']));
      expect(h.isCanonical(i(c['octane'])), isTrue);
    }
    for (final e in arr(c0['token_is_canonical'])) {
      final c = obj(e);
      expect(holding(c['counts']).isCanonical(i(c['octane'])), b(c['canonical']));
    }
    for (final e in arr(c0['token_compress'])) {
      final c = obj(e);
      expect(holding(c['counts']).compress(i(c['octane'])), holding(c['out']));
    }
    for (final e in arr(c0['token_fuse_split'])) {
      final c = obj(e);
      final input = holding(c['counts']);
      final tier = i(c['tier']);
      final out = c['op'] == 'fuse'
          ? expectStatus(c['status'], () => input.fuse(i(c['octane']), tier))
          : expectStatus(c['status'], () => input.split(tier));
      expect(out ?? input, holding(c['out']), reason: 'a refused change leaves the holding as it was');
    }
    expect(TokenHolding.empty().withCount(3, 2).count(3), 2);
    expect(() => TokenHolding.of([1, 2]), throwsA(isA<ZxvUsageException>()));
  });

  test('Fibonacci codes', () {
    for (final e in arr(c0['token_code'])) {
      final c = obj(e);
      final v = u64(c['value']);
      final bits = expectStatus(c['status'], () => FibonacciCode.bits(v));
      if (bits == null) {
        expectStatus(c['status'], () => FibonacciCode.encode([v]));
        continue;
      }
      expect(bits, i(c['bits']));
      final enc = FibonacciCode.encode([v]);
      expect(toHex(enc.bytes), c['bytes']);
      expect(enc.bits, bits);
      expect(FibonacciCode.get(enc.bytes, 0), (value: v, nextBit: bits));
    }
    final st = obj(c0['token_code_stream']);
    final values = [for (final x in arr(st['values'])) u64(x)];
    final enc = FibonacciCode.encode(values);
    expect(enc.bits, u64(st['bits']));
    expect(toHex(enc.bytes), st['bytes']);
    expect(FibonacciCode.decode(enc.bytes, values.length), values);
    for (final e in arr(c0['token_code_get_reject'])) {
      final c = obj(e);
      expectStatus(c['status'], () => FibonacciCode.get(hexOf(c['bytes']), i(c['bit'])));
    }
    final tiny = Uint8List(1);
    expect(() => FibonacciCode.put(tiny, 0, 100), status(ZxvStatus.eBuffer));
    expect(tiny, Uint8List(1), reason: 'nothing written on a refusal');
  });

  test('token wallet', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final a = TokenWallet.create(cap, 7, Octane.racing);
    final w = TokenWallet.create(cap, 8, Octane.plain);
    a.mint(1000000);
    expect(a.info(), (modelId: 7, octane: Octane.racing, value: 1000000, pieces: 17));
    expect(a.holding(), TokenHolding.canonical(1000000, Octane.racing));
    a.transfer(w, 250);
    expect(a.value(), 999750);
    expect(w.info(), (modelId: 8, octane: Octane.plain, value: 250, pieces: 250));
    expect(() => w.spend(251), throwsA(isA<InsufficientFundsException>()));
    expect(w.value(), 250, reason: 'a refused spend changes nothing');
    w.spend(50);
    w.setOctane(Octane.regular);
    expect(w.holding(), TokenHolding.canonical(200, Octane.regular));
    expect(() => a.mint(-1), status(ZxvStatus.eRange), reason: '-1 is UINT64_MAX bits');
  });

  test('the wallet needs the MINT capability', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(
      () => TokenWallet.create(without(root(ctx), {Permission.MINT}), 1, Octane.plain),
      throwsA(isA<CapabilityDeniedException>().having((e) => e.status, 'status', ZxvStatus.eCap)),
    );
  });

  test('assurance fee', () {
    expect(AssuranceFee.num, u64(f0['num']));
    expect(AssuranceFee.den, u64(f0['den']));
    for (final e in arr(f0['assure'])) {
      final c = obj(e);
      expect(AssuranceFee.fee(u64(c['gross'])), u64(c['fee']));
    }
    for (final e in arr(f0['carry'])) {
      final c = obj(e);
      var rem = u64(c['rem_in']);
      for (final x in arr(c['steps'])) {
        final st = obj(x);
        final ch = AssuranceFee.charge(rem, u64(st['gross']));
        expect(ch, (fee: u64(st['fee']), remainder: u64(st['rem'])));
        rem = ch.remainder;
      }
    }
    for (final e in arr(f0['carry_reject'])) {
      final c = obj(e);
      expectStatus(c['status'], () => AssuranceFee.charge(u64(c['rem_in']), u64(c['gross'])));
    }
    String snake(String camel) => camel.replaceAllMapped(RegExp('[A-Z]'), (m) => '_${m[0]!.toLowerCase()}');
    final names = arr(f0['buckets']);
    for (final bk in FeeBucket.values) {
      expect(snake(bk.name), names[bk.index]);
    }
    for (final e in arr(f0['split'])) {
      final c = obj(e);
      final sp = AssuranceFee.split(u64(c['fee']));
      final parts = arr(c['parts']);
      for (final bk in FeeBucket.values) {
        expect(sp.part(bk), u64(parts[bk.index]));
      }
    }
    final d = obj(f0['policy_default']);
    final p = AssuranceFee.defaultPolicy();
    expect(p.percent, [for (final x in arr(d['pct'])) i(x)]);
    expect(p.allowBelow, i(d['allow_below']) != 0);
    final rates = arr(d['rate']);
    for (final un in FeeUnit.values) {
      final r = arr(rates[un.index]);
      expect(p.creditRate(un), (num: u64(r[0]), den: u64(r[1])));
    }
    final mu = arr(d['multiplier']), cc = arr(d['commons_cap']);
    expect(p.multiplier, (num: u64(mu[0]), den: u64(mu[1])));
    expect(p.commonsCap, (num: u64(cc[0]), den: u64(cc[1])));
    expect(p.allowBelow, isFalse);
  });
}
