// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 freight vectors: headers, erasure coding, sets, op streams.
import 'dart:convert';
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final v = sec('freight');

  Matcher status(int st) => throwsA(isA<ZxvException>().having((e) => e.status, 'status', st));

  test('headers', () {
    for (final e in arr(v['header_serialize'])) {
      final c = obj(e);
      final h = expectStatus(
        c['status'],
        () => FreightHeader.of(
          k: i(c['k']),
          flags: i(c['flags']),
          payloadLength: i(c['payload_len']),
          seq: i(c['seq']),
          seqCount: i(c['seq_count']),
          freightId: u64(c['freight_id']),
          expandLimit: u64(c['expand_limit']),
          payloadSha256: hexOf(c['payload_sha256']),
        ),
      );
      if (h == null) continue;
      expect(toHex(h.toBytes()), c['header']);
      final p = FreightHeader.parse(h.toBytes());
      expect(p, h);
      expect(p.freightId, u64(c['freight_id']));
      expect(p.seqCount, i(c['seq_count']));
      expect(FreightHeader.isValid(h.toBytes()), isTrue);
    }
    expect(FreightHeader.isValid(Uint8List(64)), isFalse);
    expect(FreightHeader.isValid(null), isFalse);
    for (final e in arr(v['header_init_reject'])) {
      final c = obj(e);
      final flags = c.containsKey('flags') ? i(c['flags']) : 0;
      final limit = c.containsKey('expand_limit') ? u64(c['expand_limit']) : 0;
      expectStatus(c['status'], () => FreightHeader.forPayload(0, i(c['k']), flags, limit, Uint8List(i(c['len']))));
    }
  });

  test('erasure codec', () {
    for (final e in arr(v['codec'])) {
      final c = obj(e);
      final payload = hexOf(c['payload']);
      final h = FreightHeader.forPayload(u64(c['freight_id']), i(c['k']), 0, 0, payload);
      expect(toHex(h.toBytes()), c['header']);
      final packets = Freight.encode(h, payload);
      expect(toHex(packets), c['packets']);
      for (final x in arr(c['decode'])) {
        final dc = obj(x);
        final pk = [for (final r in arr(dc['rows'])) Freight.packet(packets, i(r))];
        if (dc.containsKey('flip')) {
          final f = obj(dc['flip']);
          final p = pk[i(f['index_in_rows'])];
          final at = i(f['byte']);
          p[at] ^= hexOf(f['xor'])[0];
        }
        final out = expectStatus(dc['status'], () => Freight.decode(h, Freight.packets(pk)));
        if (out != null) expect(out, payload);
      }
    }
    final h = FreightHeader.forPayload(1, 10, 0, 0, ramp(50));
    final few = Freight.packets([Freight.packet(Freight.encode(h, ramp(50)), 3)]);
    expect(
      () => Freight.decode(h, few),
      throwsA(isA<ErasureException>().having((e) => Zxv.statusClass(e.status), 'class', StatusClass.refused)),
    );
  });

  test('sets and join', () {
    for (final e in arr(v['set_count'])) {
      final c = obj(e);
      final n = expectStatus(c['status'], () => Freight.setCount(u64(c['len']), i(c['k'])));
      if (n != null) expect(n, i(c['count']));
    }
    final sp = obj(v['split']);
    final data = ramp(i(sp['len']));
    final k = i(sp['k']);
    final id = u64(sp['freight_id']);
    final join = FreightJoin.create(data.length);
    addTearDown(join.close);
    for (final x in arr(sp['members']).reversed) {
      // any order
      final m = obj(x);
      final mem = expectStatus(m['status'], () => Freight.split(id, k, 0, 0, data, i(m['seq'])));
      if (mem == null) continue;
      expect(toHex(mem.header.toBytes()), m['header']);
      expect(toHex(Hashes.sha3_256(mem.packets)), m['packets_sha3_256']);
      join.addMember(mem);
    }
    expect(join.status(), (received: 3, count: 3, complete: true, total: data.length));
    expect(join.result(), data);
    // A joined result larger than one freight.
    final big = ramp(10000);
    final count = Freight.setCount(big.length, 168);
    final j2 = FreightJoin.create(big.length);
    addTearDown(j2.close);
    expect(j2.result, status(ZxvStatus.eState));
    for (var q = 0; q < count; q++) {
      j2.addMember(Freight.split(9, 168, 0, 0, big, q));
    }
    expect(j2.result(), big);
  });

  test('op streams', () {
    for (final e in arr(v['ops_encode'])) {
      final c = obj(e);
      final input = utf8b(c['utf8']);
      final sha = b(c['with_sha']);
      final ops = OpStream.encode(input, withSha: sha);
      expect(toHex(ops), c['ops']);
      expect(OpStream.bound(input.length, withSha: sha), u64(c['bound']));
      expect(OpStream.expand(ops, OpStream.opsMax), input);
      expect(OpStream.peek(ops), (total: input.length, hasSha: sha));
    }
    final ws = arr(v['ops_writer']);
    final w0 = obj(ws[0]);
    final w = OpStreamWriter.create(u64(w0['declared_total']), null, 4096);
    addTearDown(w.close);
    for (final x in arr(w0['ops_list'])) {
      final op = obj(x);
      switch (op['op']) {
        case 'literal':
          w.literal(utf8b(op['utf8']));
        case 'copy':
          w.copy(u64(op['distance']), u64(op['len']));
        default:
          w.seed(Generator.fromCode(i(op['gen'])), hexOf(op['params']), u64(op['len']));
      }
    }
    final ops = w.finish();
    expect(toHex(ops), w0['ops']);
    final out = OpStream.expand(ops, 1 << 20);
    expect(toHex(out), w0['expanded']);
    expect(OpStream.peek(ops), (total: u64(w0['peek_total']), hasSha: b(w0['peek_has_sha'])));
    expectStatus(w0['expand_limit_too_small'], () => OpStream.expand(ops, out.length - 1));
    expectStatus(w0['expand_truncated'], () => OpStream.expand(ops.sublist(0, ops.length - 1), 1 << 20));

    final w1 = obj(ws[1]);
    final ops1 = hexOf(w1['ops']);
    Uint8List? cid;
    final wr = OpStreamWriter.create(u64(w1['declared_total']), null, 1024);
    addTearDown(wr.close);
    for (final x in arr(w1['ops_list'])) {
      final op = obj(x);
      if (op['op'] == 'literal') {
        wr.literal(utf8b(op['utf8']));
      } else {
        cid = hexOf(op['cid']);
        wr.cidRef(cid, u64(op['len']));
      }
    }
    expect(wr.finish(), ops1);
    expectStatus(w1['expand_without_resolver'], () => OpStream.expand(ops1, 100));
    final want = cid!;
    var asked = 0;
    final res = OpStream.expand(
      ops1,
      100,
      resolver: (id, len) {
        asked++;
        return toHex(id) == toHex(want) ? utf8b('abc') : null;
      },
    );
    expect(utf8.decode(res), w1['expanded_with_resolver_utf8']);
    expect(asked, 1, reason: 'the resolver is asked once per CID');
    // A resolver that answers null: NOT_FOUND, as with no resolver.
    expect(() => OpStream.expand(ops1, 100, resolver: (_, _) => null), status(ZxvStatus.eNotFound));
    // A resolver that throws: the exception never crosses C and is rethrown here as is.
    final boom = StateError('resolver failed');
    expect(() => OpStream.expand(ops1, 100, resolver: (_, _) => throw boom), throwsA(same(boom)));
    // A resolver that answers the wrong length fails the expansion.
    expect(() => OpStream.expand(ops1, 100, resolver: (_, _) => Uint8List(1)), throwsA(isA<ZxvUsageException>()));
    expect(utf8.decode(OpStream.expand(ops1, 100, resolver: (_, _) => utf8b('abc'))), '<abc>');
  });

  test('generators', () {
    for (final e in arr(v['gen_run'])) {
      final c = obj(e);
      final out = expectStatus(
        c['status'],
        () => OpStream.generate(Generator.fromCode(i(c['gen'])), hexOf(c['params']), i(c['len'])),
      );
      if (out != null) expect(toHex(out), c['out']);
    }
  });
}
