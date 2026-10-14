// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'context.dart';
import 'errors.dart';
import 'hexagram.dart';
import 'native.dart' as n;

Never _unknown(String what, int code) => throw n.usage(n.eArg, 'unknown $what $code');

/// The kind of a paradox (`ZXV_PDX_*`).
enum ParadoxKind {
  constructive(0),
  destructive(1),
  neutral(2),
  special(3),
  none(4);

  const ParadoxKind(this.code);
  final int code;

  static ParadoxKind fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => _unknown('ParadoxKind', code));
}

/// What to do with a paradox.
enum ParadoxAction {
  none(0),
  synthesize(1),
  contain(2),
  hold(3),
  record(4),
  rollback(5);

  const ParadoxAction(this.code);
  final int code;

  static ParadoxAction fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => _unknown('ParadoxAction', code));
}

/// An operator's outcome.
enum Outcome {
  applied(0),
  kept(1),
  proposalForOwner(2),
  rolledBack(3),
  contained(4),
  deferred(5),
  settled(6),
  recorded(7),
  accepted(8),
  pending(9),
  resolved(10),
  escalateToOwner(11),
  refused(12),
  logFull(13);

  const Outcome(this.code);
  final int code;

  static Outcome fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => _unknown('Outcome', code));
}

/// A graph node's state.
enum NodeState {
  open(0),
  clear(1),
  contained(2),
  rolledBack(3),
  tainted(4),
  sandboxed(5),
  waiting(6),
  held(7),
  blocked(8),
  applied(9);

  const NodeState(this.code);
  final int code;

  static NodeState fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => _unknown('NodeState', code));
}

/// The book's three hash-chained logs.
enum ParadoxLog {
  ops(0),
  frontier(1),
  quarantine(2);

  const ParadoxLog(this.code);
  final int code;

  static ParadoxLog fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => _unknown('ParadoxLog', code));
}

/// Registry flags: entries with these need the owner's approval.
abstract final class ParadoxFlag {
  static const int money = 1;
  static const int device = 2;
}

/// The trip bits of a destructive detection.
abstract final class Trip {
  static const int steps = 1;
  static const int budget = 2;
  static const int cycle = 4;
  static const int invariant = 8;
  static const int forbidden = 16;
}

/// Evidence for a classification: positive and negative counts of two
/// observations (uint32), and staleness.
typedef Evidence = ({int pos0, int neg0, int pos1, int neg1, bool stale});

/// A classification: the kind, the (possibly re-tagged) hexagram and the registry index (-1 for none).
final class Classification {
  const Classification(this.kind, this.tag, this.registryIndex);
  final ParadoxKind kind;
  final Hexagram tag;
  final int registryIndex;

  /// What to do.
  ParadoxAction action() => Paradox.action(kind, tag);

  @override
  bool operator ==(Object other) =>
      other is Classification && other.kind == kind && other.tag == tag && other.registryIndex == registryIndex;

  @override
  int get hashCode => Object.hash(kind, tag, registryIndex);

  @override
  String toString() => 'Classification(${kind.name}, $tag, registryIndex=$registryIndex)';
}

int _le32(Uint8List b, int o) => ByteData.sublistView(b).getUint32(o, Endian.little);
int _le64(Uint8List b, int o) => ByteData.sublistView(b).getUint64(o, Endian.little);
Uint8List _sl(Uint8List b, int o, int len) => Uint8List.fromList(b.sublist(o, o + len));

/// One 144-byte operator or frontier log record.
final class ParadoxLogRecord {
  ParadoxLogRecord._(Uint8List r)
    : seq = _le64(r, 0),
      op = r[8],
      kind = r[9],
      tag = r[10],
      outcome = r[11],
      aux = _le32(r, 12),
      _a = _sl(r, 16, 32),
      _b = _sl(r, 48, 32),
      _p = _sl(r, 80, 32),
      _h = _sl(r, 112, 32);

  static const int bytes = 144;

  final int seq, op, kind, tag, outcome, aux;
  final Uint8List _a, _b, _p, _h;

  factory ParadoxLogRecord.parse(List<int> r) {
    n.requireLength(r, bytes, 'record');
    return ParadoxLogRecord._(Uint8List.fromList(r));
  }

  Uint8List get a => Uint8List.fromList(_a);
  Uint8List get b => Uint8List.fromList(_b);
  Uint8List get prev => Uint8List.fromList(_p);
  Uint8List get hash => Uint8List.fromList(_h);
}

/// One 110-byte paradox quarantine log record.
final class ParadoxQuarantineRecord {
  ParadoxQuarantineRecord._(Uint8List r)
    : seq = _le64(r, 0),
      tag = r[8],
      reason = r[9],
      submitter = _le32(r, 10),
      _s = _sl(r, 14, 32),
      _p = _sl(r, 46, 32),
      _h = _sl(r, 78, 32);

  static const int bytes = 110;

  final int seq, tag, reason, submitter;
  final Uint8List _s, _p, _h;

  factory ParadoxQuarantineRecord.parse(List<int> r) {
    n.requireLength(r, bytes, 'record');
    return ParadoxQuarantineRecord._(Uint8List.fromList(r));
  }

  Uint8List get subject => Uint8List.fromList(_s);
  Uint8List get prev => Uint8List.fromList(_p);
  Uint8List get hash => Uint8List.fromList(_h);
}

/// A 160-byte repair proof from a containment. A value; [verify] checks it.
final class RepairProof {
  RepairProof._(this._b);
  final Uint8List _b;

  static const int bytesLength = 160;

  factory RepairProof.of(List<int> bytes) {
    n.requireLength(bytes, bytesLength, 'proof');
    return RepairProof._(Uint8List.fromList(bytes));
  }

  Uint8List get bytes => Uint8List.fromList(_b);
  int get trips => _le32(_b, 0);
  int get steps => _le32(_b, 4);
  int get spent => _le32(_b, 8);
  int get traceLength => _le32(_b, 12);
  int get submitter => _le32(_b, 16);
  int get stateLength => _le32(_b, 20);
  Uint8List get stateFoundHash => _sl(_b, 24, 32);
  Uint8List get stateRestoredHash => _sl(_b, 56, 32);
  Uint8List get signature => _sl(_b, 88, 32);
  Uint8List get hash => _sl(_b, 120, 32);
  int get quarantineIndex => _le64(_b, 152);

  int _verify() => using((a) => zxv15.paradoxRepairVerify(n.bytesIn(a, _b)));

  /// Returns normally if the proof's hash is consistent; [VerificationFailedException] if not.
  void verify() => n.check(_verify());

  /// [verify] as a bool.
  bool isValid() {
    final st = _verify();
    if (st != 0 && st != -21) n.check(st);
    return st == 0;
  }

  @override
  bool operator ==(Object other) => other is RepairProof && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'RepairProof[trips=$trips, quarantine=$quarantineIndex]';
}

/// A 71-byte draft classification for review. A value.
final class Draft {
  Draft._(this._b);
  final Uint8List _b;

  static const int bytesLength = 71;

  factory Draft.of(List<int> bytes) {
    n.requireLength(bytes, bytesLength, 'draft');
    return Draft._(Uint8List.fromList(bytes));
  }

  Uint8List get bytes => Uint8List.fromList(_b);
  int get record => _le32(_b, 0);
  Uint8List get recordHash => _sl(_b, 4, 32);
  Uint8List get signature => _sl(_b, 36, 32);
  int get kindCode => _b[68];
  int get reason => _b[69];
  int get flags => _b[70];

  @override
  bool operator ==(Object other) => other is Draft && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'Draft[rec=$record, kind=$kindCode, reason=$reason, flags=$flags]';
}

/// The paradox engine's pure functions.
abstract final class Paradox {
  static const int hashBytes = 32;
  static const int logMax = 65536;
  static const int registryMax = 65536;
  static const int policyMax = 64;
  static const int holdMax = 64;
  static const int graphMax = 64;
  static const int maxDeps = 4;
  static const int updatesMax = 256;
  static const int traceMax = 4096;

  /// A registry entry's source when it came from the seed table (UINT64_MAX bits).
  static const int srcSeed = -1;

  /// The 32-byte paradox signature of `data`.
  static Uint8List signature(List<int>? data) {
    final d = data ?? const <int>[];
    return using((a) {
      final o = a<Uint8>(32);
      n.check(zxv15.paradoxSignature(n.bytesIn(a, d), d.length, o));
      return n.bytesOut(o, 32);
    });
  }

  /// The kind a hexagram tag implies.
  static ParadoxKind kindOfTag(Hexagram tag) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.paradoxKindOfTag(tag.tag, o));
    return ParadoxKind.fromCode(o.value);
  });

  /// What to do with a paradox of `kind` tagged `tag`.
  static ParadoxAction action(ParadoxKind kind, Hexagram tag) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.paradoxAction(kind.code, tag.tag, o));
    return ParadoxAction.fromCode(o.value);
  });

  /// The hexagram action an outcome maps to.
  static HexAction outcomeAction(Outcome outcome) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.paradoxOutcomeAction(outcome.code, o));
    return HexAction.fromCode(o.value);
  });

  /// libzxv's name for a kind.
  static String kindName(ParadoxKind kind) => zxv15.paradoxKindName(kind.code).toDartString();

  /// libzxv's name for an outcome.
  static String outcomeName(Outcome outcome) => zxv15.paradoxOutcomeName(outcome.code).toDartString();

  /// libzxv's name for a node state.
  static String nodeStateName(NodeState state) => zxv15.paradoxNodeStateName(state.code).toDartString();

  static Classification _classify(
    Pointer<Void> book,
    Hexagram tag,
    Evidence? evidence,
    List<int>? signature,
    n.ContextState? st,
  ) {
    if (signature != null) n.requireLength(signature, 32, 'signature');
    return using((a) {
      Pointer<Uint32> ev = nullptr;
      if (evidence != null) {
        ev = a<Uint32>(4);
        final vs = [evidence.pos0, evidence.neg0, evidence.pos1, evidence.neg1];
        for (var i = 0; i < 4; i++) {
          ev[i] = n.u32(vs[i], 'evidence');
        }
      }
      final kind = a<Int32>(), t = a<Uint8>(), idx = a<Int32>();
      n.check(
        zxv15.paradoxClassify(
          book,
          tag.tag,
          ev,
          evidence?.stale == true ? 1 : 0,
          n.optBytes(a, signature),
          kind,
          t,
          idx,
        ),
        st,
      );
      return Classification(ParadoxKind.fromCode(kind.value), Hexagram.fromTag(t.value), idx.value);
    });
  }
}

/// One registry entry; `source` is [Paradox.srcSeed] for seed entries, else the ops record that added it.
final class RegistryEntry {
  RegistryEntry._(this._s, this.kind, this.reason, this.flags, this.source);
  final Uint8List _s;
  final ParadoxKind kind;
  final int reason, flags, source;

  Uint8List get signature => Uint8List.fromList(_s);
}

/// What a destructive detection saw. All values are uint32.
final class Detection {
  Detection({
    required this.steps,
    required this.stepLimit,
    required this.spent,
    required this.budget,
    List<int>? trace,
    required this.invariantOk,
  }) : _t = trace == null ? null : Uint64List.fromList(trace);

  final int steps, stepLimit, spent, budget;
  final bool invariantOk;
  final Uint64List? _t;

  /// A copy of the trace (uint64 bits), or null.
  Uint64List? get trace => _t == null ? null : Uint64List.fromList(_t);
}

/// A containment: the outcome, the state after it, and the repair proof when CONTAINED.
final class Containment {
  Containment._(this.outcome, this._s, this.proof);
  final Outcome outcome;
  final Uint8List _s;
  final RepairProof? proof;

  Uint8List get state => Uint8List.fromList(_s);
}

/// A neutral operator's outcome and the settled branch (-1 for none).
typedef HoldOutcome = ({Outcome outcome, int settled});

/// A special operator's outcome and the frontier record (UINT32_MAX when none).
typedef FrontierOutcome = ({Outcome outcome, int record});

/// A paradox book: the registry of known paradox signatures and the three
/// hash-chained logs (operators, frontier, quarantine) every operator
/// writes. Writing a log or the registry, applying a policy update or
/// creating a handle needs `PARADOX` and passes [capability]; reading and
/// classifying need none. Nothing here signs. The book belongs to the
/// context; a full log is `ZXV_E_CAPACITY` (halt the context).
final class ParadoxBook {
  ParadoxBook._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle('paradox book', p, zxv15.paradoxDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability every write passes.
  final Capability capability;
  late final n.NativeHandle _h;

  n.ContextState get _st => capability.context.state;
  Pointer<Void> get _c => capability.handle.ptr;

  /// A book with logs of `logCapacity` records and a registry of `registryCapacity` entries. Needs `PARADOX`.
  factory ParadoxBook.create(Capability cap, int logCapacity, int registryCapacity) {
    n.u32(logCapacity, 'logCapacity');
    n.u32(registryCapacity, 'registryCapacity');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.paradoxCreate(cap.handle.ptr, logCapacity, registryCapacity, o), cap.context.state);
      return ParadoxBook._(cap, o.value);
    });
  }

  /// Classifies without a book (no registry lookup). Pure.
  static Classification classifyTag(Hexagram tag, {Evidence? evidence}) =>
      Paradox._classify(nullptr, tag, evidence, null, null);

  /// Classifies `tag` with optional evidence and a registry lookup of `signature`.
  Classification classify(Hexagram tag, {Evidence? evidence, List<int>? signature}) =>
      Paradox._classify(_h.ptr, tag, evidence, signature, _st);

  /// Adds a registry entry; a MONEY or DEVICE flag needs `ownerOk`
  /// ([OwnerApprovalRequiredException] otherwise). Returns its index. Needs `PARADOX`.
  int registryAdd(List<int> signature, ParadoxKind kind, int reason, int flags, {required bool ownerOk}) {
    n.requireLength(signature, 32, 'signature');
    n.u8(reason, 'reason');
    n.u8(flags, 'flags');
    return using((a) {
      final o = a<Uint32>();
      n.check(
        zxv15.paradoxRegistryAdd(_c, _h.ptr, n.bytesIn(a, signature), kind.code, reason, flags, ownerOk ? 1 : 0, o),
        _st,
      );
      return o.value;
    });
  }

  /// The index of `signature` in the registry, or null.
  int? registryFind(List<int> signature) {
    n.requireLength(signature, 32, 'signature');
    return using((a) {
      final o = a<Uint32>();
      final st = zxv15.paradoxRegistryFind(_h.ptr, n.bytesIn(a, signature), o);
      if (st == n.eNotFound) return null;
      n.check(st, _st);
      return o.value;
    });
  }

  /// The number of registry entries.
  int registryCount() => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.paradoxRegistryCount(_h.ptr, o), _st);
    return o.value;
  });

  /// Registry entry `index`.
  RegistryEntry registryEntry(int index) {
    final i = n.index(index, 'index');
    return using((a) {
      final s = a<Uint8>(32), kind = a<Int32>(), reason = a<Uint8>(), flags = a<Uint8>(), src = a<Uint64>();
      n.check(zxv15.paradoxRegistryAt(_h.ptr, i, s, kind, reason, flags, src), _st);
      return RegistryEntry._(n.bytesOut(s, 32), ParadoxKind.fromCode(kind.value), reason.value, flags.value, src.value);
    });
  }

  /// The number of records in `log`.
  int logCount(ParadoxLog log) => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.paradoxLogCount(_h.ptr, log.code, o), _st);
    return o.value;
  });

  /// The chain head of `log` (zeros when empty).
  Uint8List logHead(ParadoxLog log) => using((a) {
    final o = a<Uint8>(32);
    n.check(zxv15.paradoxLogHead(_h.ptr, log.code, o), _st);
    return n.bytesOut(o, 32);
  });

  /// Record `index` of `log` (144 bytes, or 110 for the quarantine log).
  Uint8List logRecord(ParadoxLog log, int index) {
    final i = n.index(index, 'index');
    final h = _h.ptr;
    return n.bytes((b, c, l) => zxv15.paradoxLogRecord(h, log.code, i, b, c, l), _st);
  }

  /// Re-walks `log`'s chain; a break is FATAL (the context is poisoned).
  void logVerify(ParadoxLog log) => n.check(zxv15.paradoxLogVerify(_h.ptr, log.code), _st);

  /// The constructive operator: synthesize within `policy`'s bounds (or propose to the owner). Needs `PARADOX`.
  Outcome constructive(ParadoxPolicy policy, ParadoxKind kind, Hexagram tag, List<int>? synth, int tension) {
    n.u32(tension, 'tension');
    return using((a) {
      final o = a<Int32>();
      final s = synth == null ? nullptr : _i64(a, synth);
      n.check(
        zxv15.paradoxConstructive(_c, _h.ptr, policy._h.ptr, kind.code, tag.tag, s, synth?.length ?? 0, tension, o),
        _st,
      );
      return Outcome.fromCode(o.value);
    });
  }

  /// The destructive operator: contain a runaway, restoring `state` from
  /// `checkpoint` (same length) when contained. Needs `PARADOX`.
  Containment destructive(
    ParadoxKind kind,
    Hexagram tag,
    Detection detection,
    List<int> state,
    List<int> checkpoint,
    List<int>? pattern,
    int submitter,
  ) {
    if (state.length != checkpoint.length) throw n.usage(n.eArg, 'state and checkpoint differ in length');
    for (final v in [detection.steps, detection.stepLimit, detection.spent, detection.budget, submitter]) {
      n.u32(v, 'detection');
    }
    n.u32(state.length, 'state length');
    n.u32(pattern?.length ?? 0, 'pattern length');
    final trace = detection._t;
    n.u32(trace?.length ?? 0, 'trace length');
    return using((a) {
      final o = a<Int32>(), proof = a<Uint8>(RepairProof.bytesLength);
      final s = n.bytesIn(a, state);
      Pointer<Uint64> tr = nullptr;
      if (trace != null && trace.isNotEmpty) {
        tr = a<Uint64>(trace.length);
        tr.asTypedList(trace.length).setAll(0, trace);
      }
      n.check(
        zxv15.paradoxDestructive(
          _c,
          _h.ptr,
          kind.code,
          tag.tag,
          detection.steps,
          detection.stepLimit,
          detection.spent,
          detection.budget,
          tr,
          trace?.length ?? 0,
          detection.invariantOk ? 1 : 0,
          s,
          n.bytesIn(a, checkpoint),
          state.length,
          n.optBytes(a, pattern),
          pattern?.length ?? 0,
          submitter,
          o,
          proof,
        ),
        _st,
      );
      final out = Outcome.fromCode(o.value);
      return Containment._(
        out,
        n.bytesOut(s, state.length),
        out == Outcome.contained ? RepairProof.of(proof.asTypedList(RepairProof.bytesLength)) : null,
      );
    });
  }

  /// The neutral operator: hold the branches until evidence (or `ownerPick`, -1 for none) settles one. Needs `PARADOX`.
  HoldOutcome neutral(ParadoxHold hold, ParadoxKind kind, Hexagram tag, {int ownerPick = -1}) {
    if (ownerPick < -1 || ownerPick > 0x7FFFFFFF) throw n.usage(n.eArg, 'ownerPick $ownerPick');
    return using((a) {
      final o = a<Int32>(), s = a<Int32>();
      n.check(zxv15.paradoxNeutral(_c, _h.ptr, hold._h.ptr, kind.code, tag.tag, ownerPick, o, s), _st);
      return (outcome: Outcome.fromCode(o.value), settled: s.value);
    });
  }

  /// The special operator: record the paradox on the frontier log. Needs `PARADOX`.
  FrontierOutcome special(ParadoxKind kind, Hexagram tag, List<int> signature, List<int> subject) {
    n.requireLength(signature, 32, 'signature');
    n.requireLength(subject, 32, 'subject');
    return using((a) {
      final o = a<Int32>(), r = a<Uint32>();
      n.check(
        zxv15.paradoxSpecial(_c, _h.ptr, kind.code, tag.tag, n.bytesIn(a, signature), n.bytesIn(a, subject), o, r),
        _st,
      );
      return (outcome: Outcome.fromCode(o.value), record: r.value);
    });
  }

  /// A draft classification of frontier record `record`. Needs `PARADOX`.
  Draft draft(int record, ParadoxKind kind, int reason, int flags) {
    n.u32(record, 'record');
    n.u8(reason, 'reason');
    n.u8(flags, 'flags');
    return using((a) {
      final o = a<Uint8>(Draft.bytesLength);
      n.check(zxv15.paradoxDraft(_c, _h.ptr, record, kind.code, reason, flags, o), _st);
      return Draft.of(o.asTypedList(Draft.bytesLength));
    });
  }

  /// Reviews a draft; accepted drafts join the registry. Needs `PARADOX`.
  Outcome review(Draft draft, {required bool ownerOk, required bool consensusOk}) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.paradoxReview(_c, _h.ptr, n.bytesIn(a, draft._b), ownerOk ? 1 : 0, consensusOk ? 1 : 0, o), _st);
    return Outcome.fromCode(o.value);
  });

  /// One pass over `graph`; [StepLimitException] when its step limit is reached. Needs `PARADOX`.
  Outcome pass(ParadoxGraph graph) => using((a) {
    final o = a<Int32>();
    n.check(zxv15.paradoxGraphPass(_c, _h.ptr, graph._h.ptr, o), _st);
    return Outcome.fromCode(o.value);
  });

  bool get isClosed => _h.isClosed;

  /// Frees the book (and nothing else: policies, holds and graphs are separate handles). Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

Pointer<Int64> _i64(Arena a, List<int> v) {
  final p = a<Int64>(v.isEmpty ? 1 : v.length);
  p.asTypedList(v.length).setAll(0, v);
  return p;
}

/// A policy's current and previous values, whether armed, and the tension threshold.
final class PolicyValues {
  PolicyValues._(this._v, this._p, this.armed, this.tensionAt);
  final Int64List _v, _p;
  final bool armed;
  final int tensionAt;

  Int64List get values => Int64List.fromList(_v);
  Int64List get previous => Int64List.fromList(_p);
}

/// A policy's bounds and flags.
final class PolicyBounds {
  PolicyBounds._(this._lo, this._hi, this.flags);
  final Int64List _lo, _hi;
  final int flags;

  Int64List get lo => Int64List.fromList(_lo);
  Int64List get hi => Int64List.fromList(_hi);
}

/// A bounded policy the constructive operator may tune: `size` int64 values
/// with low and high bounds.
final class ParadoxPolicy {
  ParadoxPolicy._(Capability cap, Pointer<Void> p, this.size) : _st = cap.context.state {
    _h = n.NativeHandle('paradox policy', p, zxv15.paradoxPolicyDestroy, cap.context.state, cap.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  final n.ContextState _st;
  late final n.NativeHandle _h;
  final int size;

  /// A policy. Needs `PARADOX`.
  factory ParadoxPolicy.create(Capability cap, List<int> lo, List<int> hi, List<int> initial, {int flags = 0}) {
    if (hi.length != lo.length || initial.length != lo.length) {
      throw n.usage(n.eArg, 'lo, hi and initial must have the same length');
    }
    n.u32(flags, 'flags');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(
        zxv15.paradoxPolicyCreate(cap.handle.ptr, lo.length, _i64(a, lo), _i64(a, hi), _i64(a, initial), flags, o),
        cap.context.state,
      );
      return ParadoxPolicy._(cap, o.value, lo.length);
    });
  }

  /// The values.
  PolicyValues values() => using((a) {
    final v = a<Int64>(size == 0 ? 1 : size), p = a<Int64>(size == 0 ? 1 : size);
    final armed = a<Int32>(), at = a<Uint32>();
    n.check(zxv15.paradoxPolicyValues(_h.ptr, v, p, size, armed, at), _st);
    return PolicyValues._(
      Int64List.fromList(v.asTypedList(size)),
      Int64List.fromList(p.asTypedList(size)),
      armed.value != 0,
      at.value,
    );
  });

  /// The bounds.
  PolicyBounds bounds() => using((a) {
    final lo = a<Int64>(size == 0 ? 1 : size), hi = a<Int64>(size == 0 ? 1 : size), f = a<Uint32>();
    n.check(zxv15.paradoxPolicyBounds(_h.ptr, lo, hi, size, f), _st);
    return PolicyBounds._(Int64List.fromList(lo.asTypedList(size)), Int64List.fromList(hi.asTypedList(size)), f.value);
  });

  /// The proposal bytes the owner would review for `synth`.
  Uint8List proposal(List<int> synth) => using((a) {
    final h = _h.ptr;
    final s = _i64(a, synth);
    return n.bytes((b, c, l) => zxv15.paradoxPolicyProposal(h, s, synth.length, b, c, l), _st);
  });

  bool get isClosed => _h.isClosed;

  /// Frees the policy. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// A hold's branch count, settled branch (-1 for none) and decisive margin.
typedef HoldInfo = ({int count, int settled, int decisive});

/// One branch and its evidence counts.
final class HoldBranch {
  HoldBranch._(this._i, this.positive, this.negative);
  final Uint8List _i;
  final int positive, negative;

  Uint8List get id => Uint8List.fromList(_i);
}

/// The branches a neutral paradox holds, with evidence per branch.
final class ParadoxHold {
  ParadoxHold._(Capability cap, Pointer<Void> p) : _st = cap.context.state {
    _h = n.NativeHandle('paradox hold', p, zxv15.paradoxHoldDestroy, cap.context.state, cap.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  final n.ContextState _st;
  late final n.NativeHandle _h;

  /// A hold of at most `capacity` branches settling at a margin of `decisive`. Needs `PARADOX`.
  factory ParadoxHold.create(Capability cap, int capacity, int decisive) {
    n.u32(capacity, 'capacity');
    n.u32(decisive, 'decisive');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.paradoxHoldCreate(cap.handle.ptr, capacity, decisive, o), cap.context.state);
      return ParadoxHold._(cap, o.value);
    });
  }

  /// Adds a branch (a 32-byte id).
  void add(List<int> id) {
    n.requireLength(id, 32, 'id');
    using((a) => n.check(zxv15.paradoxHoldAdd(_h.ptr, n.bytesIn(a, id)), _st));
  }

  /// Adds one piece of evidence for or against a branch.
  void evidence(List<int> id, {required bool positive}) {
    n.requireLength(id, 32, 'id');
    using((a) => n.check(zxv15.paradoxHoldEvidence(_h.ptr, n.bytesIn(a, id), positive ? 1 : 0), _st));
  }

  /// Counts.
  HoldInfo info() => using((a) {
    final c = a<Uint32>(), s = a<Int32>(), d = a<Int32>();
    n.check(zxv15.paradoxHoldInfo(_h.ptr, c, s, d), _st);
    return (count: c.value, settled: s.value, decisive: d.value);
  });

  /// Branch `index`.
  HoldBranch branch(int index) {
    final i = n.index(index, 'index');
    return using((a) {
      final id = a<Uint8>(32), p = a<Uint32>(), q = a<Uint32>();
      n.check(zxv15.paradoxHoldBranch(_h.ptr, i, id, p, q), _st);
      return HoldBranch._(n.bytesOut(id, 32), p.value, q.value);
    });
  }

  bool get isClosed => _h.isClosed;

  /// Frees the hold. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// One graph node.
final class GraphNode {
  GraphNode._(this._i, this.kind, this.state);
  final Uint8List _i;
  final ParadoxKind kind;
  final NodeState state;

  Uint8List get id => Uint8List.fromList(_i);
}

/// A graph's nodes, passes, total magnitude and direction, whether escalated, and the pass chain hash.
final class GraphInfo {
  GraphInfo._(this.nodes, this.passes, this.totalMag, this.totalDir, this.escalated, this._c);
  final int nodes, passes, totalMag, totalDir;
  final bool escalated;
  final Uint8List _c;

  Uint8List get chain => Uint8List.fromList(_c);
}

/// A dependency graph of paradoxes resolved pass by pass under a step limit.
final class ParadoxGraph {
  ParadoxGraph._(Capability cap, Pointer<Void> p) : _st = cap.context.state {
    _h = n.NativeHandle('paradox graph', p, zxv15.paradoxGraphDestroy, cap.context.state, cap.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  final n.ContextState _st;
  late final n.NativeHandle _h;

  /// A graph resolving within `stepLimit` passes. Needs `PARADOX`.
  factory ParadoxGraph.create(Capability cap, int stepLimit) {
    n.u32(stepLimit, 'stepLimit');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.paradoxGraphCreate(cap.handle.ptr, stepLimit, o), cap.context.state);
      return ParadoxGraph._(cap, o.value);
    });
  }

  /// Adds a node; `mag` is uint32, `dir` is -1, 0 or +1.
  void addNode(List<int> id, ParadoxKind kind, Hexagram tag, int mag, int dir) =>
      _node(id, mag, dir, (h, p) => zxv15.paradoxGraphAddNode(h, p, kind.code, tag.tag, mag, dir));

  /// Makes `id` depend on `dep`.
  void addDependency(List<int> id, List<int> dep) {
    n.requireLength(id, 32, 'id');
    n.requireLength(dep, 32, 'dep');
    using((a) => n.check(zxv15.paradoxGraphAddDep(_h.ptr, n.bytesIn(a, id), n.bytesIn(a, dep)), _st));
  }

  /// Updates a node for the next pass.
  void update(List<int> id, ParadoxKind kind, Hexagram tag, int mag, int dir) =>
      _node(id, mag, dir, (h, p) => zxv15.paradoxGraphUpdate(h, p, kind.code, tag.tag, mag, dir));

  void _node(List<int> id, int mag, int dir, int Function(Pointer<Void>, Pointer<Uint8>) f) {
    n.requireLength(id, 32, 'id');
    n.u32(mag, 'mag');
    if (dir < -1 || dir > 1) throw n.usage(n.eArg, 'dir must be -1, 0 or +1');
    using((a) => n.check(f(_h.ptr, n.bytesIn(a, id)), _st));
  }

  /// The state of node `id`.
  NodeState state(List<int> id) {
    n.requireLength(id, 32, 'id');
    return using((a) {
      final o = a<Int32>();
      n.check(zxv15.paradoxGraphState(_h.ptr, n.bytesIn(a, id), o), _st);
      return NodeState.fromCode(o.value);
    });
  }

  /// Node `index`.
  GraphNode node(int index) {
    final i = n.index(index, 'index');
    return using((a) {
      final id = a<Uint8>(32), k = a<Int32>(), s = a<Int32>();
      n.check(zxv15.paradoxGraphNodeAt(_h.ptr, i, id, k, s), _st);
      return GraphNode._(n.bytesOut(id, 32), ParadoxKind.fromCode(k.value), NodeState.fromCode(s.value));
    });
  }

  /// Totals and the chain hash.
  GraphInfo info() => using((a) {
    final u = a<Uint32>(3), dir = a<Int32>(), esc = a<Int32>(), chain = a<Uint8>(32);
    n.check(zxv15.paradoxGraphInfo(_h.ptr, u, u + 1, u + 2, dir, esc, chain), _st);
    return GraphInfo._(u[0], u[1], u[2], dir.value, esc.value != 0, n.bytesOut(chain, 32));
  });

  bool get isClosed => _h.isClosed;

  /// Frees the graph. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
