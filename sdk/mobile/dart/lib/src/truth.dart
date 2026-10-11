// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// The four names are the cross-SDK names of SDK_DESIGN section 4 (and `true`
// and `false` are reserved words in Dart).
// ignore_for_file: constant_identifier_names
import 'native.dart' as n;

/// Four-valued (Belnap) logic: the kernel's `lpres` states, numbered as the
/// kernel numbers them (`ZXV_LP_*`).
///
/// There is no conversion to `bool` without a policy: [BOTH] is a held
/// contradiction (not an error, not null) and [NEITHER] is "not yet known".
/// Use [toBool] with both answers spelled out, [isCertainlyTrue], or
/// [requireTrue]. [and], [or] and [not] follow libzxv's tables (the tests
/// check this copy against `zxv_lp_*` and `vectors.json`).
///
/// The kernel plans an orthogonal 2-bit `mode`; it will arrive as a separate
/// accessor, never by renumbering these values.
enum Truth {
  /// S0: unknown, unattested, pending. Code 0.
  NEITHER(0),

  /// S+: attested present and valid. Code 1.
  TRUE(1),

  /// S-: attested absent or invalid. Code 2.
  FALSE(2),

  /// Contradictory evidence, held. Code 3.
  BOTH(3);

  const Truth(this.code);

  /// The kernel's numeric value.
  final int code;

  static const _not = [0, 2, 1, 3];
  static const _and = [
    [0, 0, 2, 0],
    [0, 1, 2, 3],
    [2, 2, 2, 2],
    [0, 3, 2, 3],
  ];
  static const _or = [
    [0, 1, 0, 3],
    [1, 1, 1, 1],
    [0, 1, 2, 3],
    [3, 1, 3, 3],
  ];

  /// The value for a kernel code; `ZXV_E_ARG` for anything but 0..3.
  static Truth fromCode(int code) {
    if (code < 0 || code > 3) throw n.usage(n.eArg, 'not a four-valued logic state: $code');
    return values[code];
  }

  /// Both kinds of evidence give BOTH, only positive TRUE, only negative FALSE, none NEITHER.
  static Truth fromEvidence(int positive, int negative) {
    if (positive < 0 || negative < 0) throw n.usage(n.eArg, 'evidence counts must be >= 0');
    return positive > 0 ? (negative > 0 ? BOTH : TRUE) : (negative > 0 ? FALSE : NEITHER);
  }

  Truth not() => values[_not[code]];
  Truth and(Truth other) => values[_and[code][other.code]];
  Truth or(Truth other) => values[_or[code][other.code]];

  bool get isCertainlyTrue => this == TRUE;
  bool get isCertainlyFalse => this == FALSE;
  bool get isCertain => this == TRUE || this == FALSE;

  /// Collapses to a bool with an explicit policy for the two non-classical states.
  bool toBool({required bool onBoth, required bool onNeither}) => switch (this) {
    TRUE => true,
    FALSE => false,
    BOTH => onBoth,
    NEITHER => onNeither,
  };

  /// Returns only for TRUE; otherwise throws what `error` builds (a [StateError] by default).
  void requireTrue([Object Function(Truth)? error]) {
    if (this != TRUE) throw error?.call(this) ?? StateError('required TRUE, was $name');
  }
}
