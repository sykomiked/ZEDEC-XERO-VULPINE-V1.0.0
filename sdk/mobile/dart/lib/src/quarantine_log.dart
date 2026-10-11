// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'context.dart';
import 'hexagram.dart';
import 'native.dart' as n;

/// One quarantine record. Byte lists are copies.
final class QuarantineRecord {
  QuarantineRecord(this.seq, this.tag, this.reason, this.submitter, this.subject, this.prev, this.hash);
  final int seq;
  final Hexagram tag;
  final int reason;

  /// uint32 submitter id.
  final int submitter;

  /// 32-byte digest of the quarantined work.
  final Uint8List subject;

  /// The previous record's hash (zeros for the first).
  final Uint8List prev;

  /// This record's SHA-256 chain hash.
  final Uint8List hash;
}

/// The ABI 1.3 quarantine log: an append-only, SHA-256 hash-chained record of
/// forbidden work, read for audit and never replayed.
///
/// Creating and appending need `ATTEST` and pass [capability] on every call.
/// A full log is REFUSED (`ZXV_E_CAPACITY`) and records nothing: halt the
/// context ([ZxvContext.halt]) or stop the work. [verify] throws
/// [ZxvFatalError] (and poisons the context) on a broken chain. The log
/// belongs to the context (R6): it is closed with it, or by [close].
final class QuarantineLog {
  QuarantineLog._(this.capability, Pointer<Void> p) {
    handle = n.NativeHandle(
      'quarantine log',
      p,
      zxvLib.hexlogDestroy,
      capability.context.state,
      capability.context.handle,
    );
    n.handleFinalizer.attach(this, handle, detach: this);
  }

  static const int maxRecords = 65536;

  final Capability capability;
  late final n.NativeHandle handle;

  n.ContextState get _st => capability.context.state;

  /// A log of at most `capacity` records (1..65536). Needs `ATTEST`.
  factory QuarantineLog.create(Capability cap, int capacity) {
    n.u32(capacity, 'capacity');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxvLib.hexlogCreate(cap.handle.ptr, capacity, o), cap.context.state);
      return QuarantineLog._(cap, o.value);
    });
  }

  /// Quarantines one record and returns its sequence number. Only forbidden
  /// tags are accepted (`ZXV_E_ARG`). `reason` is 0..255, `submitter` a
  /// uint32, `subject` 32 bytes.
  int append(Hexagram tag, int reason, int submitter, List<int> subject) {
    n.u8(reason, 'reason');
    n.u32(submitter, 'submitter');
    n.requireLength(subject, 32, 'subject');
    return using((a) {
      final o = a<Uint64>();
      n.check(
        zxvLib.hexlogAppend(capability.handle.ptr, handle.ptr, tag.tag, reason, submitter, n.bytesIn(a, subject), o),
        _st,
      );
      return o.value;
    });
  }

  int count() => using((a) {
    final o = a<Uint32>();
    n.check(zxvLib.hexlogCount(handle.ptr, o), _st);
    return o.value;
  });

  /// The chain head: the last record's hash, 32 zeros when empty.
  Uint8List head() => using((a) {
    final o = a<Uint8>(32);
    n.check(zxvLib.hexlogHead(handle.ptr, o), _st);
    return n.bytesOut(o, 32);
  });

  /// Record `index`, oldest first.
  QuarantineRecord record(int index) {
    n.u32(index, 'index');
    return using((a) {
      final seq = a<Uint64>();
      final tag = a<Uint8>();
      final reason = a<Uint8>();
      final who = a<Uint32>();
      final s = a<Uint8>(32), p = a<Uint8>(32), h = a<Uint8>(32);
      n.check(zxvLib.hexlogAt(handle.ptr, index, seq, tag, reason, who, s, p, h), _st);
      return QuarantineRecord(
        seq.value,
        Hexagram.fromTag(tag.value),
        reason.value,
        who.value,
        n.bytesOut(s, 32),
        n.bytesOut(p, 32),
        n.bytesOut(h, 32),
      );
    });
  }

  /// Recomputes every link; a mismatch poisons the context and throws [ZxvFatalError].
  void verify() => n.check(zxvLib.hexlogVerify(handle.ptr), _st);

  void close() {
    n.handleFinalizer.detach(this);
    handle.close();
  }
}
