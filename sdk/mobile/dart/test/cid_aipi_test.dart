// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 CID and AIPI vectors, plus the offline AIPI node.
import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final c0 = sec('cid');
  final a0 = sec('aipi');

  test('compute and text', () {
    for (final e in arr(c0['compute'])) {
      final c = obj(e);
      final cid = Cid.compute(u64(c['codec']), utf8b(c['utf8']));
      expect(cid.toHex(), c['cid']);
      expect(cid.toString(), c['text_default']);
      expect(cid.toText(CidBase.base58btc), c['text_58btc']);
      expect(cid.version, i(c['version']));
      expect(cid.multihash, i(c['hash']));
      expect(cid.codec, u64(c['codec']));
      expect(toHex(cid.digest), c['digest']);
      expect(cid.matches(utf8b(c['utf8'])), isTrue);
    }
  });

  test('unixfs', () {
    for (final e in arr(c0['unixfs'])) {
      final c = obj(e);
      final data = c.containsKey('ramp') ? ramp(i(c['len'])) : utf8b(c['utf8']);
      final cid = Cid.unixfs(data);
      expect(cid.toHex(), c['cid']);
      expect(cid.toString(), c['text_default']);
      if (c.containsKey('codec')) expect(cid.codec, u64(c['codec']));
    }
  });

  test('parse text', () {
    for (final e in arr(c0['text'])) {
      final c = obj(e);
      final cid = Cid.parse(s(c['text']));
      expect(cid.toHex(), c['cid']);
      expect(cid.toString(), c['text_default']);
      if (c['text_58btc'] == 'ZXV_E_CID') {
        expect(() => cid.toText(CidBase.base58btc), throwsA(isA<CidException>()));
      } else {
        expect(cid.toText(CidBase.base58btc), c['text_58btc']);
      }
      expect(cid.version, i(c['version']));
      expect(cid.codec, u64(c['codec']));
      expect(toHex(cid.digest), c['digest']);
      expect(cid.toV1().toHex(), c['v1']);
      expect(cid.toV1().toString(), c['v1_text']);
      expect(toHex(cid.providerKey()), c['provider_key']);
      expect(Cid.of(cid.bytes), cid);
      expect(Cid.of(cid.bytes).hashCode, cid.hashCode);
    }
    for (final e in arr(c0['text_reject'])) {
      final c = obj(e);
      expectStatus(c['status'], () => Cid.parse(s(c['text'])));
    }
  });

  test('verify', () {
    for (final e in arr(c0['verify'])) {
      final c = obj(e);
      expectStatus(c['status'], () => Cid.of(hexOf(c['cid'])).verify(utf8b(c['utf8'])));
    }
    expect(Cid.raw(utf8b('abc')).matches(utf8b('abd')), isFalse);
  });

  Cid moduleCid() {
    final m = Cid.raw(utf8b(a0['module_utf8']));
    expect(m.toHex(), a0['module_cid']);
    return m;
  }

  test('function records', () {
    final module = moduleCid();
    final contract = Cid.raw(utf8b(a0['contract_utf8']));
    expect(contract.toHex(), a0['contract_cid']);
    for (final e in arr(a0['fn'])) {
      final c = obj(e);
      final f = expectStatus(
        c['status'],
        () => FunctionRecord.encode(
          intent: Intent.fromCode(i(c['intent'])),
          verify: VerifyMode.fromCode(i(c['verify'])),
          privacy: Privacy.fromCode(i(c['privacy'])),
          determinism: Determinism.fromCode(i(c['determinism'])),
          quorumN: i(c['quorum_n']),
          quorumK: i(c['quorum_k']),
          fuelMax: u64(c['fuel_max']),
          module: module,
          contract: b(c['contract']) ? contract : null,
          entry: s(c['entry']),
          checker: str(c['checker']),
          certifier: str(c['certifier']),
        ),
      );
      if (f == null) continue;
      expect(toHex(f.bytes), c['record']);
      expect(f.cid.toHex(), c['fn_cid']);
      expect(f.cid.toText(CidBase.base58btc), c['fn_cid_text']);
      final d = FunctionRecord.decode(f.bytes);
      expect(d, f);
      expect(d.entry, c['entry']);
      expect(d.fuelMax, u64(c['fuel_max']));
      expect(d.contract != null, b(c['contract']));
    }
    final built = FunctionRecord.encode(
      module: module,
      entry: 'main',
      fuelMax: 1000000,
      verify: VerifyMode.checker,
      checker: 'check',
    );
    expect(toHex(built.bytes), obj(arr(a0['fn'])[0])['record']);
    expect(
      () => FunctionRecord.decode([1, 2, 3]),
      throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eFormat)),
    );
  });

  test('call and claim records', () {
    final module = moduleCid();
    final fn = Cid.of(hexOf(obj(arr(a0['fn'])[0])['fn_cid']));
    final calls = arr(a0['call']);
    for (final e in calls) {
      final c = obj(e);
      final target = c['fn'] == 'module_cid' ? module : fn;
      final input = utf8b(c['input_utf8']);
      final r = expectStatus(c['status'], () => CallRecord.encode(target, input));
      if (r == null) {
        expect(() => CallRecord.cidOf(target, input), throwsA(isA<CidException>()));
        continue;
      }
      expect(toHex(r.bytes), c['record']);
      expect(r.cid.toHex(), c['call_cid']);
      expect(r.cid.toText(CidBase.base58btc), c['call_cid_text']);
      expect(CallRecord.cidOf(fn, input), r.cid);
      final d = CallRecord.decode(r.bytes);
      expect(d.function, fn);
      expect(d.input, input);
    }
    for (final e in arr(a0['claim'])) {
      final c = obj(e);
      final call = Cid.of(hexOf(obj(calls[1])['call_cid']));
      final r = ClaimRecord.encode(call, hexOf(c['executor']), utf8b(c['output_utf8']), utf8b(c['cert_utf8']));
      expect(toHex(r.bytes), c['record']);
      final d = ClaimRecord.decode(r.bytes);
      expect(d.call, call);
      expect(d.executor, hexOf(c['executor']));
      expect(d.output, utf8b(c['output_utf8']));
      expect(d.certificate, utf8b(c['cert_utf8']));
    }
  });

  test('offline node', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final key = SecretKey.fromBytes(fill(0x5A, 32));
    addTearDown(key.close);
    final node = AipiNode.create(cap, 1 << 20, 64, nodeKey: key);
    final keyless = AipiNode.create(cap, AipiNode.storeMin, 4);
    final mod = utf8b(a0['module_utf8']);
    final m = node.put(mod);
    expect(m.toHex(), a0['module_cid']);
    expect(node.put(mod), m, reason: 'putting the same bytes again is a no-op');
    expect(node.get(m), mod);
    final big = ramp(10000);
    expect(node.get(node.put(big)), big);
    final secret = utf8b('private notes');
    final sc = node.putPrivate(secret);
    expect(node.get(sc), secret);
    Matcher status(int st) => throwsA(isA<ZxvException>().having((e) => e.status, 'status', st));
    expect(() => node.put(secret), status(ZxvStatus.eState));
    expect(() => keyless.putPrivate(secret), status(ZxvStatus.eState));
    expect(() => node.get(Cid.raw(utf8b('never stored'))), status(ZxvStatus.eNotFound));
    final f = FunctionRecord.encode(module: m, entry: 'main', fuelMax: 10);
    final fc = node.publish(f);
    expect(fc, f.cid);
    expect(node.loadFunction(fc), f);
    expect(node.plan(fc, utf8b('x')), Route.local);
    final orphan = FunctionRecord.encode(module: Cid.raw(utf8b('elsewhere')), entry: 'main', fuelMax: 10);
    expect(node.plan(node.publish(orphan), null), Route.unavailable);
    final info = node.info();
    expect(info.blocks, 6, reason: 'four public blocks; a private block takes two entries');
    expect(info.storeUsed, greaterThan(10000));
    expect(info.getsVerified, greaterThanOrEqualTo(4));
    expect(info.forbidden, 0);
  });

  test('the node needs the AIPI capability', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final r = root(ctx);
    expect(
      () => AipiNode.create(without(r, {Permission.AIPI}), 1 << 16, 8),
      throwsA(isA<CapabilityDeniedException>().having((e) => e.status, 'status', ZxvStatus.eCap)),
    );
    final node = AipiNode.create(r, 1 << 16, 8);
    expect(node.info().blocks, 0);
    node.close();
  });
}
