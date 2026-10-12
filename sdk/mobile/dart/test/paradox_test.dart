// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// ABI 1.5 paradox vectors: every step runs in order on ONE book (log
// capacity 64, registry 16).
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'v15.dart';
import 'vectors.dart';

void main() {
  final v = sec('paradox');

  Hexagram tag(Object? e) => Hexagram.fromTag(i(e));
  ParadoxKind kind(Object? e) => ParadoxKind.fromCode(i(e));
  List<int> longs(Object? e) => [for (final x in arr(e)) i64(x)];
  List<int> ints(Object? e) => [for (final x in arr(e)) i(x)];
  Uint8List id(Object? e) => fill(s(e).codeUnitAt(0), 32);

  void heads(ParadoxBook bk, Object? h) {
    for (final log in ParadoxLog.values) {
      final w = obj(obj(h)[log.name]);
      expect(bk.logCount(log), i(w['count']), reason: log.name);
      expect(toHex(bk.logHead(log)), w['head'], reason: log.name);
    }
  }

  test('pure functions', () {
    for (final e in arr(v['signature'])) {
      final c = obj(e);
      expect(toHex(Paradox.signature(utf8b(c['utf8']))), c['sig']);
    }
    final kn = arr(v['kind_names']);
    for (final k in ParadoxKind.values) {
      expect(Paradox.kindName(k), kn[k.code]);
    }
    final on = arr(v['outcome_names']);
    for (final o in Outcome.values) {
      expect(Paradox.outcomeName(o), on[o.code]);
    }
    final sn = arr(v['node_state_names']);
    for (final st in NodeState.values) {
      expect(Paradox.nodeStateName(st), sn[st.code]);
    }
    final kot = ints(v['kind_of_tag']);
    for (var t = 0; t < 64; t++) {
      expect(Paradox.kindOfTag(Hexagram.fromTag(t)).code, kot[t], reason: 'tag $t');
    }
    final act = arr(v['action']);
    for (final k in ParadoxKind.values) {
      final row = ints(act[k.code]);
      for (var t = 0; t < 64; t++) {
        final h = Hexagram.fromTag(t);
        if (row[t] < 0) {
          expect(
            () => Paradox.action(k, h),
            throwsA(isA<ZxvUsageException>().having((e) => e.status, 'status', ZxvStatus.eArg)),
          );
        } else {
          expect(Paradox.action(k, h), ParadoxAction.fromCode(row[t]));
        }
      }
    }
    final oa = ints(v['outcome_action']);
    for (final o in Outcome.values) {
      expect(Paradox.outcomeAction(o).code, oa[o.code]);
    }
  });

  void constructive(Capability cap, ParadoxBook bk) {
    final c = obj(v['constructive']);
    final lo = longs(c['lo']), hi = longs(c['hi']), init = longs(c['initial']);
    final plain = ParadoxPolicy.create(cap, lo, hi, init);
    final money = ParadoxPolicy.create(cap, lo, hi, init, flags: ParadoxFlag.money);
    expect(plain.bounds().hi, hi);
    expect(money.bounds().flags, ParadoxFlag.money);
    for (final e in arr(c['steps'])) {
      final st = obj(e);
      final p = st['policy'] == 'money' ? money : plain;
      final synth = st['synth'] == null ? null : longs(st['synth']);
      final out = expectStatus(
        st['status'],
        () => bk.constructive(p, kind(st['kind']), tag(st['tag']), synth, i(st['tension'])),
      );
      if (out != null) expect(out, Outcome.fromCode(i(st['outcome'])));
      final vals = p.values();
      expect(vals.values, longs(st['values']));
      expect(vals.previous, longs(st['prev']));
      expect(vals.armed, b(st['armed']));
      expect(vals.tensionAt, i(st['tension_at']));
      if (st.containsKey('proposal')) expect(toHex(p.proposal(synth!)), st['proposal']);
    }
    plain.close();
    money.close();
  }

  void destructive(ParadoxBook bk) {
    for (final e in arr(v['destructive'])) {
      final c = obj(e);
      final d = Detection(
        steps: i(c['steps']),
        stepLimit: i(c['step_limit']),
        spent: i(c['spent']),
        budget: i(c['budget']),
        trace: [for (final x in arr(c['trace'])) u64(x)],
        invariantOk: b(c['invariant_ok']),
      );
      final pat = s(c['pattern_utf8']);
      final pattern = pat.isEmpty ? null : utf8b(pat);
      final state = utf8b(c['state_utf8']);
      final r = bk.destructive(
        kind(c['kind']),
        tag(c['tag']),
        d,
        state,
        utf8b(c['checkpoint_utf8']),
        pattern,
        i(c['submitter']),
      );
      expect(r.outcome, Outcome.fromCode(i(c['outcome'])));
      expect(toHex(r.state), c['state_after']);
      expect(state, utf8b(c['state_utf8']), reason: "the caller's list is not modified");
      if (c.containsKey('repair_proof')) {
        final p = r.proof!;
        expect(toHex(p.bytes), c['repair_proof']);
        expectStatus(c['repair_verify'], p.verify);
        final bad = p.bytes..[0] ^= 1;
        expectStatus(c['repair_verify_flipped_byte0'], () => RepairProof.of(bad).verify());
        expect(RepairProof.of(bad).isValid(), isFalse);
        expect(p.signature, Paradox.signature(pattern));
        expect(p.quarantineIndex, 0);
      } else {
        expect(r.proof, isNull);
      }
    }
  }

  void neutral(Capability cap, ParadoxBook bk) {
    final c = obj(v['neutral']);
    final br = arr(c['branches']);
    final b0 = hexOf(br[0]), b1 = hexOf(br[1]);
    final h = ParadoxHold.create(cap, i(c['capacity']), i(c['decisive']));
    h.add(b0);
    h.add(b1);
    h.evidence(b0, positive: true);
    h.evidence(b1, positive: true);
    for (final e in arr(c['steps'])) {
      final st = obj(e);
      if (!st.containsKey('kind')) continue;
      for (var k = 0; k < i(st['add_for_branch0']); k++) {
        h.evidence(b0, positive: true);
      }
      final r = expectStatus(
        st['status'],
        () => bk.neutral(h, kind(st['kind']), tag(st['tag']), ownerPick: i(st['owner_pick'])),
      );
      if (r != null) expect(r, (outcome: Outcome.fromCode(i(st['outcome'])), settled: i(st['settled'])));
    }
    expect(h.info().settled, 0);
    expect(h.branch(0).id, b0);
    expect(h.branch(0).positive, 3);
    h.close();
  }

  void special(ParadoxBook bk) {
    final c = obj(v['special']);
    final sg = Paradox.signature(utf8b(c['sig_utf8']));
    expect(toHex(sg), c['sig']);
    final f = bk.special(kind(c['kind']), tag(c['tag']), sg, hexOf(c['subject']));
    expect(f, (outcome: Outcome.fromCode(i(c['outcome'])), record: i(c['rec'])));
    final dc = obj(c['draft']), dm = obj(c['draft_money']);
    final draft = bk.draft(f.record, kind(dc['kind']), i(dc['reason']), i(dc['flags']));
    expect(toHex(draft.bytes), dc['bytes']);
    final money = bk.draft(f.record, kind(dm['kind']), i(dm['reason']), i(dm['flags']));
    expect(toHex(money.bytes), dm['bytes']);
    expect(money.flags, ParadoxFlag.money);
    for (final e in arr(c['review'])) {
      final r = obj(e);
      final d = r['draft'] == 'draft_money' ? money : draft;
      final out = expectStatus(
        r['status'],
        () => bk.review(d, ownerOk: b(r['owner_ok']), consensusOk: b(r['consensus_ok'])),
      );
      if (out != null) expect(out, Outcome.fromCode(i(r['outcome'])));
    }
    expect(bk.registryCount(), i(c['registry_count_after']));
  }

  void graph(Capability cap, ParadoxBook bk, Map<String, dynamic> g) {
    final graph = ParadoxGraph.create(cap, i(g['step_limit']));
    for (final e in arr(g['nodes'])) {
      final nd = obj(e);
      expectStatus(
        nd['status'],
        () => graph.addNode(id(nd['id']), kind(nd['kind']), tag(nd['tag']), i(nd['mag']), i(nd['dir'])),
      );
    }
    for (final e in arr(g['deps'])) {
      final d = obj(e);
      expectStatus(d['status'], () => graph.addDependency(id(d['id']), id(d['dep'])));
    }
    for (final e in arr(g['passes'])) {
      final p = obj(e);
      if (p['status'] == 'ZXV_E_PARADOX_STEP') {
        expect(
          () => bk.pass(graph),
          throwsA(isA<StepLimitException>().having((x) => x.status, 'status', ZxvStatus.eParadoxStep)),
        );
      } else {
        final out = expectStatus(p['status'], () => bk.pass(graph));
        if (out != null) expect(out, Outcome.fromCode(i(p['outcome'])));
      }
      final info = graph.info();
      expect(info.passes, i(p['pass']));
      expect(info.totalMag, i(p['total_mag']));
      expect(info.totalDir, i(p['total_dir']));
      expect(info.escalated, b(p['escalated']));
      expect(toHex(info.chain), p['chain']);
      for (final MapEntry(:key, :value) in obj(p['states']).entries) {
        expect(graph.state(fill(key.codeUnitAt(0), 32)), NodeState.fromCode(i(value)), reason: key);
      }
    }
    expect(graph.node(0).state, isNot(NodeState.open));
    graph.close();
  }

  test('one book', () {
    final sigL = Paradox.signature(utf8b('loop:A->B->A'));
    final ctx = ZxvContext.create();
    addTearDown(ctx.close);
    final cap = root(ctx);
    final bk = ParadoxBook.create(cap, 64, 16);
    heads(bk, v['heads_fresh']);
    for (final e in arr(v['classify'])) {
      final c = obj(e);
      if (c.containsKey('registry_add')) {
        final ra = obj(c['registry_add']);
        final idx = bk.registryAdd(sigL, kind(ra['kind']), i(ra['reason']), i(ra['flags']), ownerOk: b(ra['owner_ok']));
        expect(idx, i(ra['index']));
        continue;
      }
      Evidence? ev;
      if (c['evidence'] != null) {
        final x = ints(c['evidence']);
        ev = (pos0: x[0], neg0: x[1], pos1: x[2], neg1: x[3], stale: b(c['stale']));
      }
      final k = bk.classify(tag(c['tag']), evidence: ev, signature: c['sig'] != null ? sigL : null);
      expect(k, Classification(kind(c['kind']), tag(c['out_tag']), i(c['reg_index'])));
    }
    for (final e in arr(v['registry'])) {
      final c = obj(e);
      final idx = expectStatus(
        c['status'],
        () => bk.registryAdd(
          Paradox.signature(utf8b(c['utf8'])),
          kind(c['kind']),
          0,
          i(c['flags']),
          ownerOk: b(c['owner_ok']),
        ),
      );
      if (idx != null) expect(idx, i(c['index']));
    }
    expect(
      () => bk.registryAdd(Uint8List(32), ParadoxKind.neutral, 0, ParadoxFlag.device, ownerOk: false),
      throwsA(isA<OwnerApprovalRequiredException>()),
    );
    heads(bk, v['heads_after_classify']);
    expect(bk.registryFind(sigL), 0);
    expect(bk.registryFind(Uint8List(32)), isNull);
    expect(bk.registryEntry(0).source, Paradox.srcSeed);
    expect(ParadoxBook.classifyTag(Hexagram.fromTag(0)).registryIndex, -1);

    constructive(cap, bk);
    destructive(bk);
    neutral(cap, bk);
    special(bk);
    for (final g in arr(v['graphs'])) {
      graph(cap, bk, obj(g));
    }

    heads(bk, v['heads_final']);
    final logs = obj(v['logs']);
    for (final log in ParadoxLog.values) {
      final recs = arr(logs[log.name]);
      for (var k = 0; k < recs.length; k++) {
        final r = bk.logRecord(log, k);
        expect(toHex(r), recs[k], reason: '$log $k');
        if (log == ParadoxLog.quarantine) {
          expect(ParadoxQuarantineRecord.parse(r).seq, k);
        } else {
          expect(ParadoxLogRecord.parse(r).seq, k);
        }
      }
      expect(
        () => bk.logRecord(log, recs.length),
        throwsA(isA<ZxvException>().having((e) => e.status, 'status', ZxvStatus.eNotFound)),
      );
    }
    final lv = arr(v['log_verify']);
    for (final log in ParadoxLog.values) {
      expectStatus(lv[log.code], () => bk.logVerify(log));
    }
  });
}
