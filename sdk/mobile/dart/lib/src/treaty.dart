// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'errors.dart';
import 'native.dart' as n;

/// The rule a treaty broke (`ZXV_TREATY_F_*`).
enum TreatyFault {
  /// The call gave no fault code.
  unknown(-1),

  /// The treaty validates.
  none(0),

  /// The record or text does not start with the treaty magic.
  magic(2),

  /// Sutra syntax error.
  syntax(3),

  /// Bad name.
  name(4),

  /// Bad asset code.
  asset(5),

  /// Unknown operation.
  op(6),

  /// Party or obligation count out of range.
  count(7),

  /// A PIC S9(18) number is malformed or out of range.
  number(8),

  /// Bad party (id, duplicate, deposit).
  party(9),

  /// Bad weight.
  weight(10),

  /// Bad claim.
  claim(11),

  /// Bad obligation.
  owe(12),

  /// Record padding is not spaces.
  pad(13);

  const TreatyFault(this.code);
  final int code;

  /// The fault for a code ([unknown] for codes not listed).
  static TreatyFault fromCode(int code) => values.firstWhere((f) => f.code == code, orElse: () => unknown);
}

/// A treaty's operation (`ZXV_TREATY_OP_*`).
enum TreatyOp {
  /// No operation set.
  none(0),

  /// Multilateral netting.
  net(1),

  /// Pro-rata settlement of claims from deposits.
  setl(2);

  const TreatyOp(this.code);
  final int code;

  /// The operation for a code ([none] for unknown codes).
  static TreatyOp fromCode(int code) => values.firstWhere((o) => o.code == code, orElse: () => none);
}

/// A quotient and remainder.
typedef PicDivision = ({int quotient, int remainder});

/// COBOL PIC S9(18) arithmetic exactly as the treaty engine does it: a result
/// beyond +-999,999,999,999,999,999 is `ZXV_E_RANGE`, never wrapped. Pure.
abstract final class Pic18 {
  /// The largest PIC S9(18) magnitude.
  static const int max = 999999999999999999;

  static int add(int a, int b) => _one(zxv15.treatyPicAdd, a, b);
  static int subtract(int a, int b) => _one(zxv15.treatyPicSub, a, b);
  static int multiply(int a, int b) => _one(zxv15.treatyPicMul, a, b);

  /// `a / b` truncated toward zero, and the remainder.
  static PicDivision divide(int a, int b) => using((m) {
    final q = m<Int64>(), r = m<Int64>();
    n.check(zxv15.treatyPicDiv(a, b, q, r));
    return (quotient: q.value, remainder: r.value);
  });

  static int _one(int Function(int, int, Pointer<Int64>) f, int a, int b) => using((m) {
    final o = m<Int64>();
    n.check(f(a, b, o));
    return o.value;
  });
}

/// One treaty party. Weights are an exact fraction `weightNum / weightDen`.
typedef TreatyParty = ({String id, int deposit, int claim, int weightNum, int weightDen});

/// One obligation: party `from` owes party `to` the `amount`.
typedef TreatyObligation = ({int from, int to, int amount});

/// A treaty: parties, deposits, claims, weights and obligations, read from
/// Sutra text or a 2542-byte COBOL copybook record, validated, and run
/// (netting or pro-rata settlement) into a [TreatyResult]. Pure: no context
/// and no capability.
///
/// Close it; a [Finalizer] backstop frees a dropped one.
final class Treaty {
  Treaty._(Pointer<Void> p) {
    _h = n.NativeHandle('treaty', p, zxv15.treatyDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// The size of a copybook record.
  static const int recordBytes = 2542;

  /// The size of a result record.
  static const int resultRecordBytes = 2422;

  /// The longest Sutra text.
  static const int sutraMax = 3843;
  static const int maxParties = 16;
  static const int maxObligations = 64;

  /// An empty treaty.
  factory Treaty.create() => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxv15.treatyCreate(o));
    return Treaty._(o.value);
  });

  /// A treaty parsed from Sutra text; [TreatyException] carries the fault and offset.
  factory Treaty.parse(String sutra) => Treaty.create().._closeOnError((t) => t.load(sutra));

  /// A treaty parsed from a copybook record.
  factory Treaty.parseRecord(List<int> record) => Treaty.create().._closeOnError((t) => t.loadRecord(record));

  void _closeOnError(void Function(Treaty) f) {
    try {
      f(this);
    } catch (_) {
      close();
      rethrow;
    }
  }

  /// Replaces the contents with the parsed Sutra text.
  void load(String sutra) => using((a) {
    final off = a<Uint32>(), f = a<Int32>()..value = -1;
    _checkParse(zxv15.treatyParseSutra(_h.ptr, n.cstr(a, sutra, 'sutra'), off, f), off, f);
  });

  /// Replaces the contents with the parsed copybook record.
  void loadRecord(List<int> record) => using((a) {
    final off = a<Uint32>(), f = a<Int32>()..value = -1;
    _checkParse(zxv15.treatyParseRecord(_h.ptr, n.bytesIn(a, record), record.length, off, f), off, f);
  });

  void _checkParse(int st, Pointer<Uint32> off, Pointer<Int32> f) {
    if (st == n.eTreaty) {
      throw TreatyException(
        st,
        n.statusName(st),
        n.lastError(),
        fault: TreatyFault.fromCode(f.value),
        offset: off.value,
      );
    }
    n.check(st);
  }

  /// Empties the treaty.
  void clear() => n.check(zxv15.treatyClear(_h.ptr));

  /// The canonical Sutra text.
  String toSutra() => _withFault(() => n.text((b, c, l) => zxv15.treatyEmitSutra(_h.ptr, b, c, l), null));

  /// The 2542-byte copybook record.
  Uint8List toRecord() => _withFault(() => n.bytes((b, c, l) => zxv15.treatyEmitRecord(_h.ptr, b, c, l), null));

  T _withFault<T>(T Function() f) {
    try {
      return f();
    } on TreatyException catch (e) {
      throw TreatyException(e.status, e.statusName, e.detail, fault: fault());
    }
  }

  /// Throws [TreatyException] (with its [TreatyFault]) unless the treaty validates.
  void validate() {
    final f = fault();
    if (f != TreatyFault.none) {
      throw TreatyException(n.eTreaty, n.statusName(n.eTreaty), 'the treaty does not validate: ${f.name}', fault: f);
    }
  }

  /// The first rule the treaty breaks, or [TreatyFault.none].
  TreatyFault fault() => using((a) {
    final f = a<Int32>();
    final st = zxv15.treatyValidate(_h.ptr, f);
    if (st != 0 && st != n.eTreaty) n.check(st);
    return TreatyFault.fromCode(f.value);
  });

  /// True if both treaties hold the same content.
  bool contentEquals(Treaty other) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.treatyEqual(_h.ptr, other._h.ptr, o));
    return o.value != 0;
  });

  /// The treaty's name.
  String name() => n.text((b, c, l) => zxv15.treatyName(_h.ptr, b, c, l), null);

  /// The asset code.
  String asset() => n.text((b, c, l) => zxv15.treatyAsset(_h.ptr, b, c, l), null);

  /// The operation.
  TreatyOp op() => TreatyOp.fromCode(_header().$1);

  int partyCount() => _header().$2;
  int obligationCount() => _header().$3;

  (int, int, int) _header() => using((a) {
    final op = a<Int32>(), p = a<Uint32>(), o = a<Uint32>();
    n.check(zxv15.treatyHeader(_h.ptr, op, p, o));
    return (op.value, p.value, o.value);
  });

  /// Party `index`.
  TreatyParty party(int index) {
    final i = n.index(index, 'index');
    final id = n.text((b, c, l) => zxv15.treatyPartyId(_h.ptr, i, b, c, l), null);
    return using((a) {
      final d = a<Int64>(), c = a<Int64>(), wn = a<Uint32>(), wd = a<Uint32>();
      n.check(zxv15.treatyParty(_h.ptr, i, d, c, wn, wd));
      return (id: id, deposit: d.value, claim: c.value, weightNum: wn.value, weightDen: wd.value);
    });
  }

  /// Every party, in order.
  List<TreatyParty> parties() => List.generate(partyCount(), party);

  /// Obligation `index`.
  TreatyObligation obligation(int index) {
    final i = n.index(index, 'index');
    return using((a) {
      final f = a<Uint32>(), t = a<Uint32>(), amt = a<Int64>();
      n.check(zxv15.treatyOwe(_h.ptr, i, f, t, amt));
      return (from: f.value, to: t.value, amount: amt.value);
    });
  }

  /// Every obligation, in order.
  List<TreatyObligation> obligations() => List.generate(obligationCount(), obligation);

  /// Runs the treaty's operation. The result is a plain value; nothing to close.
  TreatyResult run() {
    final r = _withFault(
      () => using((a) {
        final o = a<Pointer<Void>>();
        n.check(zxv15.treatyRun(_h.ptr, o));
        return o.value;
      }),
    );
    try {
      return TreatyResult._read(r);
    } finally {
      zxv15.treatyResultDestroy(r);
    }
  }

  bool get isClosed => _h.isClosed;

  /// Frees the treaty. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }

  @override
  String toString() =>
      isClosed ? 'Treaty[closed]' : 'Treaty[${name()} ${asset()} ${op().name}, ${partyCount()} parties]';
}

/// One party's result.
typedef TreatyPartyResult = ({int net, int balance, int dividend});

/// One settlement leg.
typedef TreatyLeg = ({int from, int to, int amount});

/// What [Treaty.run] produced: per-party results, settlement legs, fees and the result record.
final class TreatyResult {
  TreatyResult._(this.fees, this.parties, this.legs, this._record);

  /// Fees taken, in minor units.
  final int fees;

  /// Per-party results, in party order.
  final List<TreatyPartyResult> parties;

  /// The settlement legs.
  final List<TreatyLeg> legs;
  final Uint8List _record;

  static TreatyResult _read(Pointer<Void> r) => using((a) {
    final np = a<Uint32>(), nl = a<Uint32>(), fees = a<Int64>();
    n.check(zxv15.treatyResultSummary(r, np, nl, fees));
    final x = a<Int64>(), y = a<Int64>(), z = a<Int64>();
    final f = a<Uint32>(), t = a<Uint32>();
    final ps = List<TreatyPartyResult>.generate(np.value, (i) {
      n.check(zxv15.treatyResultParty(r, i, x, y, z));
      return (net: x.value, balance: y.value, dividend: z.value);
    }, growable: false);
    final ls = List<TreatyLeg>.generate(nl.value, (i) {
      n.check(zxv15.treatyResultLeg(r, i, f, t, x));
      return (from: f.value, to: t.value, amount: x.value);
    }, growable: false);
    final rec = n.bytes((b, c, l) => zxv15.treatyResultRecord(r, b, c, l), null);
    return TreatyResult._(fees.value, List.unmodifiable(ps), List.unmodifiable(ls), rec);
  });

  /// A copy of the 2422-byte result record.
  Uint8List toRecord() => Uint8List.fromList(_record);

  @override
  String toString() => 'TreatyResult(fees=$fees, parties=$parties, legs=$legs)';
}
