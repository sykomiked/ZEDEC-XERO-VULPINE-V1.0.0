// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Field names are the cross-SDK names (SDK_DESIGN section 4).
// ignore_for_file: constant_identifier_names
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'native.dart' as n;
import 'truth.dart';

/// Base of a [Hexagram], numbered by its line pattern (`ZXV_HEX_BASE_*`).
/// NOT numbered like [Truth] (TRUE is base 3 but Truth 1): convert only with
/// [Hexagram.toTruth] and [Hexagram.fromTruth].
enum HexBase {
  FALSE(0),
  NEITHER(1),
  BOTH(2),
  TRUE(3);

  const HexBase(this.code);
  final int code;

  static HexBase fromCode(int c) => c >= 0 && c < 4 ? values[c] : throw n.usage(n.eArg, 'base $c is not 0..3');
}

/// Polarity of the evidence (`ZXV_HEX_POL_*`). FORBIDDEN is absorbing.
enum HexPolarity {
  ENTROPIC(0),
  NEUTRAL(1),
  FORBIDDEN(2),
  NEGENTROPIC(3);

  const HexPolarity(this.code);
  final int code;

  static HexPolarity fromCode(int c) => c >= 0 && c < 4 ? values[c] : throw n.usage(n.eArg, 'polarity $c is not 0..3');
}

/// Lifecycle phase (`ZXV_HEX_PHASE_*`); the cycle is POTENTIAL, EMERGING, MANIFEST, DISSOLVING.
enum HexPhase {
  POTENTIAL(0),
  EMERGING(1),
  DISSOLVING(2),
  MANIFEST(3);

  const HexPhase(this.code);
  final int code;

  static HexPhase fromCode(int c) => c >= 0 && c < 4 ? values[c] : throw n.usage(n.eArg, 'phase $c is not 0..3');

  /// The next phase on the cycle (`zxv_hex_phase_next`).
  HexPhase next() => fromCode(_u8((o) => zxvLib.hexPhaseNext(code, o)));
}

/// What happens next to tagged work (`ZXV_HEX_ACT_*`).
enum HexAction {
  PROCEED(0),
  WAIT(1),
  REFUSE(2),
  ESCALATE(3),
  ROLLBACK(4);

  const HexAction(this.code);
  final int code;

  static HexAction fromCode(int c) => c >= 0 && c < 5 ? values[c] : throw n.usage(n.eArg, 'action $c is not 0..4');

  /// libzxv's name, e.g. 'rollback'.
  String get actionName => zxvLib.hexActionName(code).toDartString();
}

int _u8(int Function(Pointer<Uint8>) f) => using((a) {
  final o = a<Uint8>();
  n.check(f(o));
  return o.value;
});

int _i32(int Function(Pointer<Int32>) f) => using((a) {
  final o = a<Int32>();
  n.check(f(o));
  return o.value;
});

/// The 64-state hexagram tag of ABI 1.3: an immutable value packing a
/// [HexBase], a [HexPolarity] and a [HexPhase] as
/// `tag = base << 4 | polarity << 2 | phase` (0..63). Every rule is computed
/// by libzxv.
///
/// ```dart
/// var h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
/// h.action;                      // HexAction.PROCEED
/// h = h.advance();               // EMERGING
/// h.step(HexPhase.DISSOLVING);   // ZxvRefusedException (ZXV_E_STATE): a jump
/// ```
final class Hexagram {
  const Hexagram._(this.tag);

  /// The byte value, 0..63.
  final int tag;

  /// A tag from its three fields.
  factory Hexagram.of(HexBase base, HexPolarity polarity, HexPhase phase) =>
      Hexagram._(_u8((o) => zxvLib.hexPack(base.code, polarity.code, phase.code, o)));

  /// The tag with this value; `ZXV_E_ARG` (USAGE) outside 0..63.
  factory Hexagram.fromTag(int tag) {
    n.u8(tag, 'tag'); // Dart would truncate 256 to 0 at the FFI boundary
    n.check(zxvLib.hexUnpack(tag, nullptr, nullptr, nullptr));
    return Hexagram._(tag);
  }

  /// A tag whose base is `truth` (`zxv_hex_from_lp`); an explicit conversion.
  factory Hexagram.fromTruth(Truth truth, HexPolarity polarity, HexPhase phase) =>
      Hexagram.of(HexBase.fromCode(_u8((o) => zxvLib.hexFromLp(truth.code, o))), polarity, phase);

  /// Polarity from uint32 evidence counts before and after: stale gives
  /// ENTROPIC; else the margin growing NEGENTROPIC, shrinking ENTROPIC,
  /// unchanged NEUTRAL. Never FORBIDDEN.
  static HexPolarity polarityFromEvidence(int pos0, int neg0, int pos1, int neg1, {required bool stale}) {
    n.u32(pos0, 'pos0');
    n.u32(neg0, 'neg0');
    n.u32(pos1, 'pos1');
    n.u32(neg1, 'neg1');
    return HexPolarity.fromCode(_u8((o) => zxvLib.hexPolarity(pos0, neg0, pos1, neg1, stale ? 1 : 0, o)));
  }

  HexBase get base => HexBase.fromCode(tag >> 4);
  HexPolarity get polarity => HexPolarity.fromCode((tag >> 2) & 3);
  HexPhase get phase => HexPhase.fromCode(tag & 3);
  bool get isForbidden => polarity == HexPolarity.FORBIDDEN;

  /// What happens next: the kernel's 64-entry table.
  HexAction get action => HexAction.fromCode(_i32((o) => zxvLib.hexAction(tag, o)));

  /// 'base/polarity/phase', e.g. 'true/negentropic/manifest'.
  String get name => zxvLib.hexName(tag).toDartString();

  /// The base as a [Truth] (`zxv_hex_to_lp`); an explicit conversion.
  Truth toTruth() => Truth.fromCode(_i32((o) => zxvLib.hexToLp(tag >> 4, o)));

  /// The same or the next phase only; a jump is REFUSED (`ZXV_E_STATE`).
  Hexagram step(HexPhase phase) => Hexagram._(_u8((o) => zxvLib.hexStep(tag, phase.code, o)));

  /// One step along the cycle; cannot fail.
  Hexagram advance() => step(phase.next());

  /// New base and polarity, same phase; REFUSED to un-forbid a forbidden tag.
  Hexagram withState(HexBase base, HexPolarity polarity) =>
      Hexagram._(_u8((o) => zxvLib.hexSet(tag, base.code, polarity.code, o)));

  /// Polarity FORBIDDEN (absorbing).
  Hexagram forbid() => Hexagram._(_u8((o) => zxvLib.hexForbid(tag, o)));

  /// Four-valued AND on the base, worst polarity, least advanced phase.
  Hexagram and(Hexagram other) => Hexagram._(_u8((o) => zxvLib.hexAnd(tag, other.tag, o)));

  /// Four-valued OR on the base, worst polarity, least advanced phase.
  Hexagram or(Hexagram other) => Hexagram._(_u8((o) => zxvLib.hexOr(tag, other.tag, o)));

  /// Negation of the base; polarity and phase kept.
  Hexagram not() => Hexagram._(_u8((o) => zxvLib.hexNot(tag, o)));

  @override
  bool operator ==(Object other) => other is Hexagram && other.tag == tag;

  @override
  int get hashCode => tag;

  @override
  String toString() => 'Hexagram($tag $name)';
}
