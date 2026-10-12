// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Permission names are the cross-SDK names (ZXV_CAP_*).
// ignore_for_file: constant_identifier_names
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'native.dart' as n;
import 'native.dart' show FatalPolicy;
import 'truth.dart';

export 'native.dart' show FatalPolicy;

/// A capability permission bit (`ZXV_CAP_*`, ABI R10): bit = 1 << index.
enum Permission {
  LEDGER_READ,
  LEDGER_POST,
  CRYPTO_SIGN,
  CRYPTO_KEM,
  AI_INFERENCE,
  BUDGET,
  PHASE,
  ATTEST,
  SWARM,
  EVM,

  /// Build and call the hardware emulation matrix (ABI 1.4; bit 10).
  HW_MATRIX,

  /// Load hardware images and contracts (ABI 1.4; bit 11).
  HW_RECONF,

  /// Publish and execute AIPI functions (ABI 1.5; bit 12).
  AIPI,

  /// Mint coins, change token octane (ABI 1.5; bit 13).
  MINT,

  /// Paradox operators and policies (ABI 1.5; bit 14).
  PARADOX;

  int get bit => 1 << index;

  static Set<Permission> get all => values.toSet();
  static int mask(Iterable<Permission> ps) => ps.fold(0, (m, p) => m | p.bit);
  static Set<Permission> fromMask(int m) => {
    for (final p in values)
      if (m & p.bit != 0) p,
  };
}

/// One ZXV platform instance: a ledger, its currencies, and the root of every
/// capability, budget registry and quarantine log made for it.
///
/// Close it. Closing it first closes everything made from it, children
/// before parents (ABI R6). If it is garbage collected unclosed, a
/// [Finalizer] closes it the same way, but Dart does not promise to run
/// finalizers (for example at isolate exit), so do not rely on it.
final class ZxvContext {
  ZxvContext._(Pointer<Void> p, this.fatalPolicy) : state = n.ContextState(fatalPolicy) {
    handle = n.NativeHandle('context', p, zxvLib.ctxDestroy, state, null);
    n.handleFinalizer.attach(this, handle, detach: this);
  }

  final FatalPolicy fatalPolicy;
  final n.ContextState state;
  late final n.NativeHandle handle;

  /// A new context: an empty ledger, no currencies enabled.
  factory ZxvContext.create({FatalPolicy policy = FatalPolicy.throwError}) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxvLib.ctxCreate(o));
    return ZxvContext._(o.value, policy);
  });

  /// A root capability; its attestation starts TRUE.
  ///
  /// `processId` is a uint32; `identity` is 21 bytes or null for zeros.
  Capability rootCapability(Set<Permission> permissions, {int processId = 0, List<int>? identity}) {
    n.u32(processId, 'processId');
    if (identity != null) n.requireLength(identity, 21, 'identity');
    return using((a) {
      final o = a<Pointer<Void>>();
      final id = identity == null ? nullptr : n.bytesIn(a, identity);
      n.check(zxvLib.capCreate(handle.ptr, processId, id, Permission.mask(permissions), o), state);
      return Capability._(this, o.value, handle);
    });
  }

  /// True if the context halted. A halted context refuses every call; it can only be closed.
  bool get isPoisoned {
    if (state.deadReason != null) return true;
    return using((a) {
      final o = a<Int32>();
      n.check(zxvLib.ctxIsPoisoned(handle.ptr, o), state);
      return o.value != 0;
    });
  }

  /// Halts the context on purpose (an operator kill switch; how tests reach
  /// the fatal path). `reason` is at most 150 bytes of UTF-8.
  void halt([String? reason]) => using((a) {
    final r = reason == null ? nullptr : n.cstr(a, reason, 'reason');
    n.check(zxvLib.ctxHalt(handle.ptr, r), state);
  });

  /// Enables an ISO 4217 currency. Needs `LEDGER_POST`.
  void enableCurrency(Capability cap, String alpha, {int? minorUnits}) => using((a) {
    final c = _own(cap).handle.ptr;
    final s = n.cstr(a, alpha, 'alpha');
    n.check(
      minorUnits == null
          ? zxvLib.ccyEnableCap(c, s)
          : zxvLib.ccyEnableWithMinorCap(c, s, n.u8(minorUnits, 'minorUnits')),
      state,
    );
  });

  /// Registers a platform-private unit (3 to 8 upper-case letters or digits). Needs `LEDGER_POST`.
  void registerPrivateCurrency(Capability cap, String code, int minorUnits) => using((a) {
    n.check(
      zxvLib.ccyRegisterPrivateCap(_own(cap).handle.ptr, n.cstr(a, code, 'code'), n.u8(minorUnits, 'minorUnits')),
      state,
    );
  });

  Capability _own(Capability cap) {
    if (!identical(cap.context, this)) throw n.raise(n.eCap, 'the capability belongs to a different context', state);
    return cap;
  }

  /// Closes everything made from this context, then the context. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    handle.close();
  }
}

/// Permissions, process id, attestation and revocation of a [Capability].
final class CapabilityInfo {
  const CapabilityInfo(this.permissions, this.processId, this.attestation, this.revoked);
  final Set<Permission> permissions;
  final int processId;
  final Truth attestation;
  final bool revoked;
}

/// A grant of permissions over one [ZxvContext]. Every object that makes
/// gated calls holds one and passes it on every call; libzxv checks it every
/// time. Hand narrower ones ([attenuate]) to code that needs less; there is
/// no global "current" capability.
///
/// Honest limit: in one process this is least privilege, not a sandbox.
final class Capability {
  Capability._(this.context, Pointer<Void> p, n.NativeHandle parent) {
    handle = n.NativeHandle('capability', p, zxvLib.capDestroy, context.state, parent);
    n.handleFinalizer.attach(this, handle, detach: this);
  }

  /// The context this capability belongs to (kept alive by this reference).
  final ZxvContext context;
  late final n.NativeHandle handle;

  n.ContextState get _st => context.state;

  /// A child with only `keep` (and only what this one has). Revoking this one kills it.
  Capability attenuate(Set<Permission> keep) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxvLib.capAttenuate(handle.ptr, Permission.mask(keep), o), _st);
    return Capability._(context, o.value, handle);
  });

  /// Revokes this capability and every descendant. Works on a halted context.
  void revoke() => n.check(zxvLib.capRevoke(handle.use(allowDead: true)), _st);

  /// Anything but TRUE blocks gated calls. Does not propagate to existing children.
  void setAttestation(Truth t) => n.check(zxvLib.capSetAttestation(handle.ptr, t.code), _st);

  CapabilityInfo info() => using((a) {
    final caps = a<Uint64>();
    final pid = a<Uint32>();
    final att = a<Int32>();
    final rev = a<Int32>();
    n.check(zxvLib.capInfo(handle.ptr, caps, pid, att, rev), _st);
    return CapabilityInfo(Permission.fromMask(caps.value), pid.value, Truth.fromCode(att.value), rev.value != 0);
  });

  /// True if a call needing all of `needed` would pass now.
  bool allows(Set<Permission> needed) {
    final st = zxvLib.capCheck(handle.ptr, Permission.mask(needed));
    if (st == n.eCap) return false;
    n.check(st, _st);
    return true;
  }

  /// Throws [CapabilityDeniedException] unless [allows].
  void require(Set<Permission> needed) => n.check(zxvLib.capCheck(handle.ptr, Permission.mask(needed)), _st);

  /// Destroys this capability and its children. Idempotent. Revokes nothing.
  void close() {
    n.handleFinalizer.detach(this);
    handle.close();
  }
}
