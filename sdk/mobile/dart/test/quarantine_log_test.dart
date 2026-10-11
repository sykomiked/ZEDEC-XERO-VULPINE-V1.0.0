// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'vectors.dart';

void main() {
  final qlog = obj(obj(vectors['hex'])['qlog']);

  test('the vector chain is reproduced', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final log = QuarantineLog.create(ctx.rootCapability({Permission.ATTEST}), 16);
    expect(log.head(), Uint8List(32));
    for (final r in arr(qlog['records']).map(obj)) {
      final seq = log.append(
        Hexagram.fromTag(r['tag'] as int),
        r['reason'] as int,
        r['submitter'] as int,
        unhex(r['subject'] as String),
      );
      expect(seq, r['seq']);
    }
    expect(log.count(), 3);
    expect(toHex(log.head()), qlog['head']);
    log.verify();
    final records = arr(qlog['records']).map(obj).toList();
    for (var i = 0; i < records.length; i++) {
      final rec = log.record(i);
      expect(toHex(rec.hash), records[i]['hash']);
      expect(toHex(rec.prev), records[i]['prev']);
      expect(rec.submitter, records[i]['submitter']);
      expect(rec.tag.tag, records[i]['tag']);
    }
  });

  test('refusals and the ATTEST capability', () {
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final all = ctx.rootCapability(Permission.all);
    final noAttest = all.attenuate({Permission.LEDGER_READ});
    expect(() => QuarantineLog.create(noAttest, 4), throwsA(isA<CapabilityDeniedException>()));
    final log = QuarantineLog.create(all, 1);
    final forbidden = Hexagram.of(HexBase.TRUE, HexPolarity.FORBIDDEN, HexPhase.MANIFEST);
    final fine = Hexagram.of(HexBase.TRUE, HexPolarity.NEUTRAL, HexPhase.MANIFEST);
    expect(
      () => log.append(fine, 0, 0, Uint8List(32)),
      throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', -1)),
    );
    expect(() => log.append(forbidden, 256, 0, Uint8List(32)), throwsA(isA<ZxvUsageException>()));
    expect(() => log.append(forbidden, 0, -1, Uint8List(32)), throwsA(isA<ZxvUsageException>()));
    expect(() => log.append(forbidden, 0, 0, Uint8List(31)), throwsA(isA<ZxvUsageException>()));
    log.append(forbidden, 1, 7, Uint8List(32));
    expect(
      () => log.append(forbidden, 1, 7, Uint8List(32)),
      throwsA(isA<ZxvRefusedException>().having((x) => x.status, 'status', -5)),
    );
    ctx.halt('quarantine log full');
    expect(() => log.count(), throwsA(isA<ZxvFatalError>()));
  });
}
