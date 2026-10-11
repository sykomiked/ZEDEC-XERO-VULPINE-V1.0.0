// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Status checks, text and string helpers, and the handle wrapper. Internal.
import 'dart:convert';
import 'dart:ffi';
import 'dart:io' show exit, stderr;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'errors.dart';

const int eArg = -1;
const int eBuffer = -2;
const int eRange = -8;
const int eCap = -17;
const int ePoisoned = -18;
const int uint32Max = 0xFFFFFFFF;

/// What happens when a context halts.
enum FatalPolicy {
  /// Throw [ZxvFatalError]. The default.
  throwError,

  /// Print the reason and end the process at once with `exit(70)`: no
  /// `finally` blocks run. The only form no Dart code can catch. On iOS,
  /// ending the app yourself is against Apple's guidelines; prefer
  /// [throwError] there and stop the work.
  abort,
}

/// The fail-closed state of one context, shared by its handles.
final class ContextState {
  ContextState(this.policy);
  final FatalPolicy policy;
  String? deadReason;

  void markDead(String why) => deadReason ??= why;
}

String statusName(int status) => zxvLib.statusName(status).toDartString();

StatusClass statusClass(int status) => StatusClass.fromCode(zxvLib.statusClass(status));

/// Throws for a negative status; marks `ctx` dead on FATAL.
void check(int status, [ContextState? ctx]) {
  if (status == 0) return;
  throw raise(status, lastError(), ctx);
}

Object raise(int status, String detail, ContextState? ctx) {
  final name = statusName(status);
  switch (statusClass(status)) {
    case StatusClass.fatal:
      return fatal(status, name, detail, ctx);
    case StatusClass.usage:
      return status == -24 ? FloatRejectedException(detail) : ZxvUsageException(status, name, detail);
    case StatusClass.refused:
      return switch (status) {
        -17 => CapabilityDeniedException(status, name, detail),
        -19 => BudgetExhaustedException(status, name, detail),
        -6 => InsufficientFundsException(status, name, detail),
        -21 => VerificationFailedException(status, name, detail),
        -3 => NotImplementedException(status, name, detail),
        _ => ZxvRefusedException(status, name, detail),
      };
    default:
      return ZxvSystemException(status, name, detail);
  }
}

ZxvFatalError fatal(int status, String name, String detail, ContextState? ctx) {
  ctx?.markDead('$name: $detail');
  if (ctx?.policy == FatalPolicy.abort) {
    stderr.writeln('ZXV FATAL (FatalPolicy.abort): $name: $detail');
    exit(70);
  }
  return ZxvFatalError(status, name, detail);
}

ZxvUsageException usage(int status, String detail) => ZxvUsageException(status, statusName(status), detail);

/// The calling thread's `zxv_last_error` (R7).
String lastError() {
  try {
    return text((buf, cap, len) => zxvLib.lastError(buf, cap, len), null);
  } on Object {
    return '';
  }
}

/// The R4 two-call text pattern.
String text(int Function(Pointer<Utf8> buf, int cap, Pointer<Size> len) call, ContextState? ctx) {
  final len = calloc<Size>();
  try {
    final st = call(nullptr, 0, len);
    if (st == 0) return '';
    if (st != eBuffer) check(st, ctx);
    final n = len.value;
    final buf = calloc<Uint8>(n + 1);
    try {
      check(call(buf.cast(), n + 1, len), ctx);
      return utf8.decode(buf.asTypedList(len.value));
    } finally {
      calloc.free(buf);
    }
  } finally {
    calloc.free(len);
  }
}

/// A NUL-terminated UTF-8 copy in `arena` (R5). Refuses an embedded NUL.
Pointer<Utf8> cstr(Arena arena, String s, String what) {
  final b = utf8.encode(s);
  if (b.contains(0)) throw usage(eArg, '$what contains a NUL character');
  final p = arena<Uint8>(b.length + 1);
  p.asTypedList(b.length).setAll(0, b);
  p[b.length] = 0;
  return p.cast();
}

/// A native copy of `bytes` in `arena` (never NULL, even when empty).
Pointer<Uint8> bytesIn(Arena arena, List<int> bytes) {
  final p = arena<Uint8>(bytes.isEmpty ? 1 : bytes.length);
  p.asTypedList(bytes.length).setAll(0, bytes);
  return p;
}

Uint8List bytesOut(Pointer<Uint8> p, int n) => Uint8List.fromList(p.asTypedList(n));

/// Dart truncates an int passed as Uint8 (256 becomes 0): check first.
int u8(int v, String what) {
  if (v < 0 || v > 255) throw usage(eArg, '$what $v is not 0..255');
  return v;
}

int u32(int v, String what) {
  if (v < 0 || v > uint32Max) throw usage(eRange, '$what $v is not a uint32');
  return v;
}

void requireLength(List<int> b, int n, String what) {
  if (b.length != n) throw usage(eArg, '$what must be $n bytes, got ${b.length}');
}

/// One native handle with a safe lifetime (ABI R6). Children are closed
/// before their parent, and a parent keeps a strong reference to each child
/// handle, so a child can never be destroyed after its parent. A Dart
/// isolate runs one call at a time, so no call can be in flight on a handle
/// while it is closed.
final class NativeHandle {
  NativeHandle(this.kind, this._ptr, this._destroy, this.owner, this._parent) {
    final p = _parent;
    if (p != null) {
      if (p.isClosed) {
        _destroy(_ptr);
        throw StateError('${p.kind} is closed');
      }
      p._children.add(this);
    }
  }

  final String kind;
  final Pointer<Void> _ptr;
  final void Function(Pointer<Void>) _destroy;
  final ContextState? owner;
  final NativeHandle? _parent;
  final Set<NativeHandle> _children = {};
  bool isClosed = false;

  /// The pointer for one call; refuses a closed handle or a dead context.
  Pointer<Void> get ptr => use();

  Pointer<Void> use({bool allowDead = false}) {
    if (isClosed) throw StateError('this $kind is closed');
    final why = allowDead ? null : owner?.deadReason;
    if (why != null) throw ZxvFatalError(ePoisoned, 'ZXV_E_POISONED', 'the context halted earlier: $why');
    return _ptr;
  }

  /// Destroys children, then this handle. Idempotent.
  void close() {
    if (isClosed) return;
    isClosed = true;
    for (final c in _children.toList()) {
      c.close();
    }
    _destroy(_ptr);
    _parent?._children.remove(this);
  }
}

/// The GC backstop: closes a handle tree when its wrapper is collected.
/// Dart does not promise to run finalizers at isolate exit; close explicitly.
final Finalizer<NativeHandle> handleFinalizer = Finalizer((h) => h.close());
