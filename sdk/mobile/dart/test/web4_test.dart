// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 Web 4 vectors: agents, cards, directory, digests, manifests,
// consent, envelopes.
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final v = sec('web4');
  const sig = Web4.signatureBytes;

  String body(Uint8List signed) => toHex(signed.sublist(0, signed.length - sig));

  test('web4', () {
    expect(sig, i(v['sig_bytes']));
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final dir = AgentDirectory.create(8);
    addTearDown(dir.close);
    final keys = <String, AgentKey>{};

    for (final e in arr(v['agents'])) {
      final a = obj(e);
      final nodeName = a['node'] as String?;
      final node = nodeName == null ? null : keys[nodeName]!;
      final seed = SecretKey.fromBytes(fill(hexOf(a['seed_byte'])[0], 32));
      final k = AgentKey.create(cap, s(a['name']), u64(a['now_ms']), seed: seed, node: node);
      seed.close();
      keys[s(a['name'])] = k;
      expect(toHex(k.agentId), a['agent_id']);
      expect(toHex(k.peerId), a['peer_id']);
      expect(toHex(Hashes.sha3_256(k.publicKey)), a['pk_sha3_256']);
      final card = k.card();
      final cb = card.bytes;
      expect(cb.length, i(a['card_len']));
      expect(toHex(Hashes.sha3_256(cb.sublist(0, cb.length - sig))), a['card_signed_body_sha3_256']);
      final vf = expectStatus(a['card_verify'], card.verify)!;
      expect(vf.name, a['name']);
      expect(vf.agentId, k.agentId);
      expect(vf.isSelfBound, node == null);
      cb[cb.length - 1] ^= 1;
      expectStatus(a['card_verify_flipped_last_byte'], () => AgentCard.of(cb).verify());
      dir.add(card);
      expect(
        k.toString().contains(toHex(k.publicKey).substring(0, 64)),
        isFalse,
        reason: 'toString shows no key bytes',
      );
    }
    expect(dir.count(), 4);
    final alice = keys['alice']!, bob = keys['bob']!, human = keys['human']!;
    expect(dir.find(bob.agentId), bob.card());
    expect(dir.find(Uint8List(32)), isNull);

    // action digests
    final ad = obj(v['action_digest']);
    final from = hexOf(ad['from']), to = hexOf(ad['to']);
    for (final e in arr(ad['cases'])) {
      final c = obj(e);
      final payload = c.containsKey('payload_utf8') ? utf8b(c['payload_utf8']) : ramp(i(c['payload_ramp_len']));
      final d = expectStatus(c['status'], () => Web4.actionDigest(s(c['tool']), from, to, u64(c['req_id']), payload));
      if (d != null) expect(toHex(d), c['digest']);
    }

    // manifest
    final mf = obj(v['manifest']);
    final m = ToolManifest.create(i(mf['version']), u64(mf['issued_ms']), u64(mf['expires_ms']));
    addTearDown(m.close);
    for (final e in arr(mf['tools'])) {
      final t = obj(e);
      expectStatus(
        t['status'],
        () => m.addTool((
          name: s(t['name']),
          priceVfv: u64(t['price_vfv']),
          ratePerMin: i(t['rate_per_min']),
          burst: i(t['burst']),
          flags: i(t['flags']),
          maxInput: i(t['max_input']),
        )),
      );
    }
    expectStatus(
      mf['add_duplicate'],
      () => m.addTool((name: 'pay', priceVfv: 1, ratePerMin: 1, burst: 1, flags: 0, maxInput: 0)),
    );
    expectStatus(mf['encode_unsigned'], m.encode);
    alice.sign(m);
    final enc = m.encode();
    expect(enc.length, i(mf['encoding_len']));
    expect(body(enc), mf['encoding_signed_body']);
    expect(m.toJson(), mf['json']);
    expect([for (final t in m.tools()) t.name], ['pay', 'profile.read', 'stream.feed']);
    expect(m.find('profile.read'), 1);
    expect(m.find('nope'), isNull);
    expect(m.info().signed, isTrue);
    final d = expectStatus(mf['decode'], () => ToolManifest.decode(enc))!;
    addTearDown(d.close);
    for (final e in arr(mf['verify'])) {
      final c = obj(e);
      final pk = keys[s(c['key'])]!.publicKey;
      expectStatus(c['status'], () => d.verify(pk, u64(c['now_ms'])));
    }

    // consent
    final cs = obj(v['consent']);
    final action = hexOf(obj(arr(ad['cases'])[1])['digest']);
    final nonce = hexOf(cs['nonce']);
    final issued = u64(cs['issued_ms']), expires = u64(cs['expires_ms']);
    final tok = human.issueConsent(
      alice.agentId,
      action,
      i(cs['scope']),
      u64(cs['max_vfv']),
      issued,
      expires,
      nonce: nonce,
    );
    expect(tok.bytes.length, i(cs['token_len']));
    expect(body(tok.bytes), cs['token_signed_body']);
    expect(tok.humanId, human.agentId);
    expect(tok.scopeBits, ConsentScope.money);
    final nonce2 = Uint8List.fromList(nonce)..[0] ^= 0xFF;
    final tok2 = human.issueConsent(alice.agentId, action, ConsentScope.money, 1000, issued, expires, nonce: nonce2);
    final log = ConsentLog.create(16);
    addTearDown(log.close);
    final checks = arr(cs['check']);
    for (var k = 0; k < checks.length; k++) {
      final c = obj(checks[k]);
      final second = k == checks.length - 1;
      final now = u64(c['now_ms']);
      if (second) log.revoke(tok2, now);
      final t = second ? tok2 : tok;
      final agent = keys[s(c['agent'])]!.agentId;
      expectStatus(
        c['status'],
        () => log.check(t, human.publicKey, agent, action, i(c['need']), u64(c['amount']), now),
      );
    }
    expect(
      () => log.check(null, human.publicKey, alice.agentId, action, ConsentScope.money, 1, issued),
      throwsA(isA<ConsentException>()),
    );

    // envelopes
    final envs = arr(v['envelopes']);
    final e0 = obj(envs[0]), e1 = obj(envs[1]);
    final req = alice.seal(
      EnvelopeKind.fromCode(i(e0['kind'])),
      bob.agentId,
      u64(e0['req_id']),
      s(e0['tool']),
      u64(e0['now_ms']),
      chunk: i(e0['chunk']),
      flags: i(e0['flags']),
      payload: utf8b(e0['payload_utf8']),
    );
    expect(req.bytes.length, i(e0['env_len']));
    expect(body(req.bytes), e0['env_signed_body']);
    expectStatus(e0['verify_sender'], () => req.verify(alice.publicKey));
    expectStatus(e0['verify_other'], () => req.verify(bob.publicKey));
    expect(req.isValid(alice.publicKey), isTrue);
    expect(req.isValid(bob.publicKey), isFalse);
    expect(req.tool, 'pay');
    expect(req.payload(), utf8b('amount=100'));
    expect(req.consent(), isNull);
    final rsp = bob.reply(
      req,
      EnvelopeKind.fromCode(i(e1['kind'])),
      u64(e1['now_ms']),
      chunk: i(e1['chunk']),
      flags: i(e1['flags']),
      payload: utf8b(e1['payload_utf8']),
    );
    expect(rsp.bytes.length, i(e1['env_len']));
    expect(body(rsp.bytes), e1['env_signed_body']);
    expectStatus(e1['verify_sender'], () => rsp.verify(bob.publicKey));
    expect(rsp.isFinal, isTrue);
    expect(Envelope.of(rsp.bytes).kind, EnvelopeKind.response);

    // a request with a consent token attached
    final withConsent = alice.seal(EnvelopeKind.request, bob.agentId, 8, 'pay', issued, consent: tok2);
    expect(withConsent.consent(), tok2);
    expect(withConsent.flags & Envelope.flagConsent, isNot(0));
  });

  test('signing needs CRYPTO_SIGN', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    expect(
      () => AgentKey.create(without(root(ctx), {Permission.CRYPTO_SIGN}), 'a', 1),
      throwsA(isA<CapabilityDeniedException>()),
    );
  });
}
