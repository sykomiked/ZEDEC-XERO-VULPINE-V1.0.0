// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'context.dart';
import 'errors.dart';
import 'native.dart' as n;

/// One currency slot: its accounts and current peg (`pegDen == 0` until a peg is set).
final class VHubCurrency {
  const VHubCurrency(
    this.slot,
    this.issuerAccount,
    this.poolAccount,
    this.vfloatAccount,
    this.pegNum,
    this.pegDen,
    this.pegEpoch,
  );
  final int slot, issuerAccount, poolAccount, vfloatAccount, pegNum, pegDen, pegEpoch;

  /// True once a peg is set.
  bool get hasPeg => pegDen != 0;

  @override
  String toString() =>
      'VHubCurrency(slot=$slot, issuer=$issuerAccount, pool=$poolAccount, vfloat=$vfloatAccount, '
      'peg=$pegNum/$pegDen@$pegEpoch)';
}

/// An account's DEBIT, CREDIT and EQUITY (= DEBIT - CREDIT).
typedef VHubBalance = ({int debit, int credit, int equity});

/// What a swap for `want` received would cost: the amount to send, the V-Bills in between, and what arrives.
typedef VHubQuote = ({int amount, int vbills, int received});

/// What a swap moved.
typedef VHubSwap = ({int vbills, int received});

/// The V-Bill hub: test currencies pegged to the V-Bill by ML-DSA-87 signed
/// pegs, exact quotes and swaps through the V-Bill, and per-sender limit
/// rules. A demo hub: test currencies only (ISO 4217 codes are refused) and
/// no real money; its ledger lives in process memory.
///
/// Creating, adding currencies and rules, setting pegs, opening and funding
/// accounts and swapping need `LEDGER_POST`; quotes, balances, currency info
/// and [check] need `LEDGER_READ`. Every call passes [capability]. The hub
/// belongs to the context. Amounts are uint64 values in an `int`; account
/// ids are uint32 values.
final class VHub {
  VHub._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle('V-Bill hub', p, zxv15.vhubDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability every call passes.
  final Capability capability;
  late final n.NativeHandle _h;

  n.ContextState get _st => capability.context.state;

  static const int maxCurrencies = 8;
  static const int maxRules = 4;
  static const int limitSenders = 16;
  static const int codeMax = 12;
  static const int amountMax = 1 << 59;

  /// The V-Bill issuer's account.
  static const int vbillIssuer = 0;

  /// An issuer's ML-DSA-87 public key size.
  static const int issuerKeyBytes = 2592;

  /// An ML-DSA-87 peg signature size.
  static const int signatureBytes = 4627;

  /// A peg message size.
  static const int pegMessageBytes = 40;

  /// A hub; `seed` (32 bytes, or null for random) derives every posting's keys. Needs `LEDGER_POST`.
  factory VHub.create(Capability cap, {List<int>? seed}) {
    if (seed != null) n.requireLength(seed, 32, 'seed');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.vhubCreate(cap.handle.ptr, n.optBytes(a, seed), o), cap.context.state);
      return VHub._(cap, o.value);
    });
  }

  /// The 40-byte message an issuer signs (ML-DSA-87, empty context) to set a peg. Pure.
  static Uint8List pegMessage(int slot, int num, int den, int epoch) {
    n.u8(slot, 'slot');
    return using((a) {
      final o = a<Uint8>(pegMessageBytes);
      n.check(zxv15.vhubPegMsg(slot, num, den, epoch, o));
      return n.bytesOut(o, pegMessageBytes);
    });
  }

  Pointer<Void> get _c => capability.handle.ptr;

  /// Adds a currency at `slot` with its issuer's key and initial funding; returns its info.
  VHubCurrency addCurrency(
    int slot,
    String code,
    int minorUnits,
    List<int> issuerKey,
    int poolFunding,
    int vbillBacking,
  ) {
    n.u8(slot, 'slot');
    n.u8(minorUnits, 'minorUnits');
    n.requireLength(issuerKey, issuerKeyBytes, 'issuerKey');
    using((a) {
      final cs = n.cstr(a, code, 'code');
      final k = n.bytesIn(a, issuerKey);
      n.check(zxv15.vhubAddCcy(_c, _h.ptr, slot, cs, minorUnits, k, poolFunding, vbillBacking), _st);
    });
    return currency(slot);
  }

  /// The currency at `slot`.
  VHubCurrency currency(int slot) {
    n.u8(slot, 'slot');
    return using((a) {
      final u = a<Uint32>(3), p = a<Uint64>(3);
      n.check(zxv15.vhubCcyInfo(_c, _h.ptr, slot, u, u + 1, u + 2, p, p + 1, p + 2), _st);
      return VHubCurrency(slot, u[0], u[1], u[2], p[0], p[1], p[2]);
    });
  }

  /// Sets a peg with the issuer's signature over [pegMessage]. [PegException] for a bad or stale peg.
  void setPeg(int slot, int num, int den, int epoch, List<int> signature) {
    n.u8(slot, 'slot');
    n.requireLength(signature, signatureBytes, 'signature');
    using((a) => n.check(zxv15.vhubSetPeg(_c, _h.ptr, slot, num, den, epoch, n.bytesIn(a, signature)), _st));
  }

  /// Opens an account in `slot`'s currency for `owner` (uint32); returns its id.
  int openAccount(int slot, int owner) {
    n.u8(slot, 'slot');
    n.u32(owner, 'owner');
    return using((a) {
      final o = a<Uint32>();
      n.check(zxv15.vhubOpenAccount(_c, _h.ptr, slot, owner, o), _st);
      return o.value;
    });
  }

  /// Issues `amount` into `account` from its currency's issuer.
  void issue(int account, int amount) {
    n.u32(account, 'account');
    n.check(zxv15.vhubIssue(_c, _h.ptr, account, amount), _st);
  }

  /// An account's balance.
  VHubBalance balance(int account) {
    n.u32(account, 'account');
    return using((a) {
      final d = a<Uint64>(), c = a<Uint64>(), e = a<Int64>();
      n.check(zxv15.vhubBalance(_c, _h.ptr, account, d, c, e), _st);
      return (debit: d.value, credit: c.value, equity: e.value);
    });
  }

  /// Adds a per-sender limit rule on swaps out of `slot` (per rule epoch).
  void addLimit(int slot, int limit) {
    n.u8(slot, 'slot');
    n.check(zxv15.vhubAddLimit(_c, _h.ptr, slot, limit), _st);
  }

  /// Starts a new rule epoch (every sender back at zero); returns it.
  int nextEpoch() => using((a) {
    final o = a<Uint64>();
    n.check(zxv15.vhubNextEpoch(_c, _h.ptr, o), _st);
    return o.value;
  });

  /// What receiving `want` in `toSlot` costs in `fromSlot`.
  VHubQuote quote(int fromSlot, int toSlot, int want) {
    n.u8(fromSlot, 'fromSlot');
    n.u8(toSlot, 'toSlot');
    return using((a) {
      final o = a<Uint64>(3);
      n.check(zxv15.vhubQuote(_c, _h.ptr, fromSlot, toSlot, want, o, o + 1, o + 2), _st);
      return (amount: o[0], vbills: o[1], received: o[2]);
    });
  }

  /// Swaps `amount` from `sender` (in `fromSlot`) to `receiver` (in `toSlot`) at `tick`.
  VHubSwap swap(int fromSlot, int toSlot, int sender, int receiver, int amount, int tick) {
    n.u8(fromSlot, 'fromSlot');
    n.u8(toSlot, 'toSlot');
    n.u32(sender, 'sender');
    n.u32(receiver, 'receiver');
    return using((a) {
      final o = a<Uint64>(2);
      n.check(zxv15.vhubSwap(_c, _h.ptr, fromSlot, toSlot, sender, receiver, amount, tick, o, o + 1), _st);
      return (vbills: o[0], received: o[1]);
    });
  }

  /// Checks A = L + E on every account and DEBIT == CREDIT per asset; returns the number of postings checked.
  int check() => using((a) {
    final o = a<Uint64>();
    n.check(zxv15.vhubCheck(_c, _h.ptr, o), _st);
    return o.value;
  });

  bool get isClosed => _h.isClosed;

  /// Frees the hub. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
