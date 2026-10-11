// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

import 'vectors.dart';

void main() {
  final hex = obj(vectors['hex']);

  test('all 64 tags match the vectors', () {
    final tags = arr(hex['tags']).map(obj).toList();
    expect(tags, hasLength(64));
    for (final e in tags) {
      final h = Hexagram.fromTag(e['tag'] as int);
      expect(h.name, e['name']);
      expect(h.base.code, e['base']);
      expect(h.polarity.code, e['polarity']);
      expect(h.phase.code, e['phase']);
      expect(h.action.code, e['action']);
      expect(Hexagram.of(h.base, h.polarity, h.phase), h);
      expect(h.isForbidden, h.polarity == HexPolarity.FORBIDDEN);
    }
    final names = arr(hex['action_names']);
    for (var i = 0; i < names.length; i++) {
      expect(HexAction.fromCode(i).actionName, names[i]);
    }
  });

  test('not, and, or tables (8,192 pairs)', () {
    var checked = 0;
    for (var a = 0; a < 64; a++) {
      final ha = Hexagram.fromTag(a);
      expect(ha.not().tag, arr(hex['not'])[a]);
      for (var b = 0; b < 64; b++) {
        final hb = Hexagram.fromTag(b);
        expect(ha.and(hb).tag, arr(arr(hex['and'])[a])[b]);
        expect(ha.or(hb).tag, arr(arr(hex['or'])[a])[b]);
        checked += 2;
      }
    }
    expect(checked, 8192);
  });

  test('step and set refuse exactly what the vectors refuse', () {
    for (var t = 0; t < 64; t++) {
      final h = Hexagram.fromTag(t);
      for (var p = 0; p < 4; p++) {
        final want = arr(arr(hex['step'])[t])[p] as int;
        if (want < 0) {
          expect(
            () => h.step(HexPhase.fromCode(p)),
            throwsA(isA<ZxvRefusedException>().having((x) => x.status, 'status', -9)),
          );
        } else {
          expect(h.step(HexPhase.fromCode(p)).tag, want);
        }
      }
      for (var bp = 0; bp < 16; bp++) {
        final want = arr(arr(hex['set'])[t])[bp] as int;
        final base = HexBase.fromCode(bp >> 2), pol = HexPolarity.fromCode(bp & 3);
        if (want < 0) {
          expect(
            () => h.withState(base, pol),
            throwsA(isA<ZxvRefusedException>().having((x) => x.status, 'status', -9)),
          );
        } else {
          expect(h.withState(base, pol).tag, want);
        }
      }
      expect(h.advance().phase, h.phase.next());
      expect(h.forbid().isForbidden, isTrue);
      expect(h.forbid().advance().isForbidden, isTrue);
    }
  });

  test('phase cycle and polarity cases', () {
    final next = arr(hex['phase_next']);
    for (var i = 0; i < 4; i++) {
      expect(HexPhase.fromCode(i).next().code, next[i]);
    }
    for (final e in arr(hex['polarity']).map(obj)) {
      final p = Hexagram.polarityFromEvidence(
        e['pos0'] as int,
        e['neg0'] as int,
        e['pos1'] as int,
        e['neg1'] as int,
        stale: e['stale'] as bool,
      );
      expect(p.code, e['polarity']);
    }
    expect(() => Hexagram.polarityFromEvidence(-1, 0, 0, 0, stale: false), throwsA(isA<ZxvUsageException>()));
    expect(() => Hexagram.polarityFromEvidence(1 << 32, 0, 0, 0, stale: false), throwsA(isA<ZxvUsageException>()));
  });

  test('the truth bridge is explicit, never numeric', () {
    for (final t in Truth.values) {
      final h = Hexagram.fromTruth(t, HexPolarity.NEUTRAL, HexPhase.POTENTIAL);
      expect(h.base.code, arr(hex['lp_to_base'])[t.code]);
      expect(h.toTruth(), t);
    }
    for (final b in HexBase.values) {
      expect(Hexagram.of(b, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).toTruth().code, arr(hex['base_to_lp'])[b.code]);
    }
    expect(Hexagram.fromTruth(Truth.TRUE, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).base, HexBase.TRUE);
    expect(HexBase.TRUE.code, 3);
    expect(Truth.TRUE.code, 1);
  });

  test('out-of-range tags are usage errors, never truncated', () {
    // Dart truncates an int passed as Uint8 (256 -> 0): the SDK checks first.
    for (final bad in [64, 199, 255, 256, 320, -1, -57]) {
      expect(
        () => Hexagram.fromTag(bad),
        throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', -1)),
        reason: '$bad',
      );
    }
    final h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
    expect(h.action, HexAction.PROCEED);
    expect(h.advance().phase, HexPhase.EMERGING);
  });
}
