// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'native.dart' as n;

/// The four buckets the assurance fee splits into.
enum FeeBucket { reserveFloor, vbillDividend, infraBounty, regenCapital }

/// The units a fee policy prices.
enum FeeUnit { money, compute, bandwidth, storage }

/// An exact ratio of two uint64 values (bits in an `int`).
typedef FeeRatio = ({int num, int den});

/// The default fee policy: bucket percentages, whether fees below the
/// minimum are allowed, credit rates per unit, the multiplier and the
/// commons cap, all exact ratios of uint64 values (no floats).
final class FeePolicy {
  const FeePolicy(this.percent, this.allowBelow, this.creditRates, this.multiplier, this.commonsCap);

  /// The percentage for each [FeeBucket], in order.
  final List<int> percent;
  final bool allowBelow;

  /// The credit rate for each [FeeUnit], in order.
  final List<FeeRatio> creditRates;
  final FeeRatio multiplier;
  final FeeRatio commonsCap;

  /// The credit rate for `unit`.
  FeeRatio creditRate(FeeUnit unit) => creditRates[unit.index];

  @override
  String toString() =>
      'FeePolicy(percent=$percent, allowBelow=$allowBelow, creditRates=$creditRates, '
      'multiplier=$multiplier, commonsCap=$commonsCap)';
}

/// A fee and the remainder to carry into the next charge.
typedef FeeCharge = ({int fee, int remainder});

/// The four parts of a fee.
final class FeeSplit {
  const FeeSplit(this.reserveFloor, this.vbillDividend, this.infraBounty, this.regenCapital);
  final int reserveFloor, vbillDividend, infraBounty, regenCapital;

  /// The part for `bucket`.
  int part(FeeBucket bucket) => switch (bucket) {
    FeeBucket.reserveFloor => reserveFloor,
    FeeBucket.vbillDividend => vbillDividend,
    FeeBucket.infraBounty => infraBounty,
    FeeBucket.regenCapital => regenCapital,
  };

  @override
  bool operator ==(Object other) =>
      other is FeeSplit &&
      other.reserveFloor == reserveFloor &&
      other.vbillDividend == vbillDividend &&
      other.infraBounty == infraBounty &&
      other.regenCapital == regenCapital;

  @override
  int get hashCode => Object.hash(reserveFloor, vbillDividend, infraBounty, regenCapital);

  @override
  String toString() => 'FeeSplit($reserveFloor, $vbillDividend, $infraBounty, $regenCapital)';
}

/// The 0.08889% assurance fee (8889 / 10,000,000) on a gross amount, rounded
/// exactly, with a carried remainder for long runs, and its four-way split.
/// Amounts are uint64 values in an `int`. Pure.
abstract final class AssuranceFee {
  /// The fee rate numerator.
  static const int num = 8889;

  /// The fee rate denominator.
  static const int den = 10000000;

  /// The fee on `gross`, rounded down.
  static int fee(int gross) => using((a) {
    final o = a<Uint64>();
    n.check(zxv15.feeAssure(gross, o));
    return o.value;
  });

  /// The fee on `gross` with `remainderIn` carried from earlier charges.
  static FeeCharge charge(int remainderIn, int gross) => using((a) {
    final f = a<Uint64>(), r = a<Uint64>();
    n.check(zxv15.feeAssureCarry(remainderIn, gross, f, r));
    return (fee: f.value, remainder: r.value);
  });

  /// The four-way split of `fee` (the parts sum to it).
  static FeeSplit split(int fee) => using((a) {
    final o = a<Uint64>(4);
    n.check(zxv15.feeSplit(fee, o));
    return FeeSplit(o[0], o[1], o[2], o[3]);
  });

  /// libzxv's default policy.
  static FeePolicy defaultPolicy() => using((a) {
    final pct = a<Uint32>(4), allow = a<Int32>();
    final rn = a<Uint64>(4), rd = a<Uint64>(4);
    final mn = a<Uint64>(), md = a<Uint64>(), cn = a<Uint64>(), cd = a<Uint64>();
    n.check(zxv15.feePolicyDefault(pct, allow, rn, rd, mn, md, cn, cd));
    return FeePolicy(
      List.unmodifiable([for (var i = 0; i < 4; i++) pct[i]]),
      allow.value != 0,
      List.unmodifiable([for (var i = 0; i < 4; i++) (num: rn[i], den: rd[i])]),
      (num: mn.value, den: md.value),
      (num: cn.value, den: cd.value),
    );
  });
}
