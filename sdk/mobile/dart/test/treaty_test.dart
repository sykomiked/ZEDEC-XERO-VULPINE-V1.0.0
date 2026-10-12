// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 treaty vectors: Sutra text, copybook records, PIC S9(18), runs.
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final v = sec('treaty');

  test('sizes', () {
    expect(Treaty.recordBytes, i(v['rec_len']));
    expect(Treaty.resultRecordBytes, i(v['result_len']));
  });

  test('Sutra and record round trip', () {
    for (final e in arr(v['sutra'])) {
      final c = obj(e);
      final t = Treaty.parse(s(c['sutra']));
      addTearDown(t.close);
      final rec = t.toRecord();
      expect(toHex(rec), c['record']);
      expect(t.toSutra(), c['reemit']);
      final r = Treaty.parseRecord(rec);
      expect(t.contentEquals(r), isTrue);
      r.close();
      expect(t.name(), c['name']);
      expect(t.asset(), c['asset']);
      expect(t.op().code, i(c['op']));
      expect(t.partyCount(), i(c['party_count']));
      expect(t.obligationCount(), i(c['owe_count']));
      final ps = t.parties();
      final want = arr(c['parties']);
      for (var k = 0; k < want.length; k++) {
        final p = obj(want[k]);
        final w = arr(p['weight']);
        expect(ps[k], (
          id: s(p['id']),
          deposit: u64(p['deposit']),
          claim: u64(p['claim']),
          weightNum: i(w[0]),
          weightDen: i(w[1]),
        ));
      }
      final owes = arr(c['owes']);
      for (var k = 0; k < owes.length; k++) {
        final w = obj(owes[k]);
        expect(t.obligation(k), (from: i(w['from']), to: i(w['to']), amount: u64(w['amount'])));
      }
    }
  });

  test('Sutra refusals carry the fault and offset', () {
    for (final e in arr(v['reject'])) {
      final c = obj(e);
      try {
        Treaty.parse(s(c['sutra']));
        fail('accepted ${c['sutra']}');
      } on TreatyException catch (x) {
        expect(x.status, ZxvStatus.eTreaty);
        expect(x.fault.code, i(c['fault']), reason: s(c['sutra']));
        expect(x.offset, u64(c['offset']));
      }
    }
  });

  test('record refusals', () {
    final base = hexOf(obj(arr(v['sutra'])[0])['record']);
    for (final e in arr(v['reject_record'])) {
      final c = obj(e);
      final r = Uint8List.fromList(base);
      r[i(c['patch_offset'])] = hexOf(c['patch_byte'])[0];
      try {
        Treaty.parseRecord(r);
        fail('accepted a patched record');
      } on TreatyException catch (x) {
        expect(x.fault.code, i(c['fault']));
        expect(x.offset, u64(c['offset']));
      }
    }
  });

  test('PIC S9(18)', () {
    for (final e in arr(v['pic'])) {
      final c = obj(e);
      final a = i64(c['a']), b2 = i64(c['b']);
      final st = c['status'];
      switch (s(c['op'])) {
        case 'add':
          final r = expectStatus(st, () => Pic18.add(a, b2));
          if (r != null) expect(r, i64(c['out']));
        case 'sub':
          final r = expectStatus(st, () => Pic18.subtract(a, b2));
          if (r != null) expect(r, i64(c['out']));
        case 'mul':
          final r = expectStatus(st, () => Pic18.multiply(a, b2));
          if (r != null) expect(r, i64(c['out']));
        default:
          final r = expectStatus(st, () => Pic18.divide(a, b2));
          if (r != null) expect(r, (quotient: i64(c['out']), remainder: i64(c['rem'])));
      }
    }
  });

  test('run', () {
    for (final e in arr(v['run'])) {
      final c = obj(e);
      final t = Treaty.parse(s(c['sutra']));
      addTearDown(t.close);
      final r = expectStatus(c['status'], t.run);
      if (r == null) continue;
      expect(r.parties.length, i(c['parties']));
      expect(r.legs.length, i(c['legs']));
      expect(r.fees, u64(c['fees']));
      final ps = arr(c['party']);
      for (var k = 0; k < ps.length; k++) {
        final p = obj(ps[k]);
        expect(r.parties[k], (net: i64(p['net']), balance: u64(p['balance']), dividend: u64(p['dividend'])));
      }
      final ls = arr(c['leg']);
      for (var k = 0; k < ls.length; k++) {
        final g = obj(ls[k]);
        expect(r.legs[k], (from: i(g['from']), to: i(g['to']), amount: u64(g['amount'])));
      }
      expect(r.toRecord(), hexOf(c['result_record']));
    }
    final poor = Treaty.parse('TREATY POOR USD SETL\nPARTY A 0 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n');
    addTearDown(poor.close);
    expect(poor.run, throwsA(isA<InsufficientFundsException>()));
  });

  test('validate and fault', () {
    final t = Treaty.create();
    addTearDown(t.close);
    expect(t.fault(), isNot(TreatyFault.none), reason: 'an empty treaty does not validate');
    expect(t.validate, throwsA(isA<TreatyException>()));
    t.load(s(obj(arr(v['sutra'])[0])['sutra']));
    expect(t.fault(), TreatyFault.none);
    t.validate();
    t.clear();
    expect(t.partyCount(), 0);
  });
}
