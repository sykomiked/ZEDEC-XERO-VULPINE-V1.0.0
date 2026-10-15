// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Vino Embed (libzxv ABI 1.6, group "vino").
//
// What an app or game needs to take part in Vino without ever holding
// anyone's value or keys. Two lanes, which never meet:
//
//  - PLAY: a sandbox inside the app (PlaySandbox) with its own play units,
//    issuers, holders and vouchers, in process memory only. Play units are
//    not money: nothing here takes a play sandbox and something live, so a
//    play unit can never be converted into, redeemed for, or moved into live
//    value.
//  - LIVE: real Vino, held in the user's own ZXV wallet, never in the app.
//    The app builds a PaymentRequest; the user approves it in their wallet,
//    which signs a receipt (VinoReceipt.sign, wallet side); the app checks
//    the receipt offline (VinoReceipt.verify). The app never sees a balance,
//    a key or a custody account.
//
// There is no interest, yield or return of any kind anywhere in this group.
import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings16.dart';
import 'context.dart';
import 'crypto.dart';
import 'errors.dart';
import 'native.dart' as n;

/// Sizes and limits of the vino group (`ZXV_VINO_*`, ABI 1.6).
abstract final class Vino {
  /// Size of an encoded [PaymentRequest] (`ZXV_VINO_REQ_BYTES`).
  static const int requestBytes = 160;

  /// Size of a request id: SHA-256 of the encoded request.
  static const int requestIdBytes = 32;

  /// Longest memo, in bytes of printable ASCII 0x20..0x7e (`ZXV_VINO_MEMO_MAX`).
  static const int memoMax = 64;

  /// Size of the message a wallet signs to approve a request (`ZXV_VINO_RCPT_MSG_BYTES`).
  static const int receiptMessageBytes = 88;

  /// Size of a wallet's ML-DSA-87 public key.
  static const int walletPublicKeyBytes = 2592;

  /// Size of a wallet's ML-DSA-87 secret key.
  static const int walletSecretKeyBytes = 4896;

  /// Size of a receipt signature (ML-DSA-87).
  static const int receiptSignatureBytes = 4627;

  /// Largest amount, face or backing step, 2^59 (`ZXV_VINO_AMOUNT_MAX`).
  static const int amountMax = 1 << 59;

  /// Most accounts (issuers plus holders) in one play sandbox.
  static const int maxAccounts = 256;

  /// Most active (unredeemed) vouchers in one play sandbox.
  static const int maxVouchers = 4096;

  /// Request flag: the purchase has a paid chance outcome (`ZXV_VINO_REQ_CHANCE`); refused on live.
  static const int flagChance = 0x1;

  /// Every request flag this ABI defines; any other bit is refused.
  static const int flagsKnown = 0x1;
}

/// The rail a play voucher is on. A voucher is on exactly one active rail at
/// a time, so one coin can never be spent on both. Switching moves nothing.
enum VinoRail {
  /// The note in the holder's hand (`ZXV_VINO_RAIL_BEARER`, 0).
  bearer(0),

  /// Its twin on the play ledger (`ZXV_VINO_RAIL_GHOST`, 1).
  ghost(1);

  const VinoRail(this.code);

  /// The `ZXV_VINO_RAIL_*` code.
  final int code;

  /// The rail for a code; a [ZxvUsageException] for an unknown one.
  static VinoRail fromCode(int code) =>
      values.firstWhere((r) => r.code == code, orElse: () => n.badCode('vino rail', code));
}

/// Which lane a payment request is for. The two lanes never meet.
enum VinoLane {
  /// Play units inside an app's sandbox; not money (`ZXV_VINO_LANE_PLAY`, 1).
  play(1),

  /// Real Vino from the user's own wallet, in VFV minor units (`ZXV_VINO_LANE_LIVE`, 2).
  live(2);

  const VinoLane(this.code);

  /// The `ZXV_VINO_LANE_*` code.
  final int code;

  /// The lane for a code; a [ZxvUsageException] for an unknown one.
  static VinoLane fromCode(int code) =>
      values.firstWhere((l) => l.code == code, orElse: () => n.badCode('vino lane', code));
}

/// A play voucher's state.
enum VoucherState {
  /// Held and spendable (`ZXV_VINO_V_ACTIVE`, 1).
  active(1),

  /// Returned to its issuer and retired; nothing was paid out (`ZXV_VINO_V_REDEEMED`, 2).
  redeemed(2);

  const VoucherState(this.code);

  /// The `ZXV_VINO_V_*` code.
  final int code;

  /// The state for a code; a [ZxvUsageException] for an unknown one.
  static VoucherState fromCode(int code) =>
      values.firstWhere((s) => s.code == code, orElse: () => n.badCode('voucher state', code));
}

/// One play voucher, as [PlaySandbox.voucher] reports it.
final class PlayVoucher {
  const PlayVoucher(this.id, this.face, this.issuer, this.holder, this.rail, this.state);

  /// Its id (> 0, never reused in its sandbox).
  final int id;

  /// Its Fibonacci face, in play units.
  final int face;

  /// The issuer account it came from.
  final int issuer;

  /// The holder account that holds it (or last held it, once redeemed).
  final int holder;

  /// Its active rail.
  final VinoRail rail;

  /// Active or redeemed.
  final VoucherState state;

  @override
  bool operator ==(Object other) =>
      other is PlayVoucher &&
      id == other.id &&
      face == other.face &&
      issuer == other.issuer &&
      holder == other.holder &&
      rail == other.rail &&
      state == other.state;

  @override
  int get hashCode => Object.hash(id, face, issuer, holder, rail, state);

  @override
  String toString() =>
      'PlayVoucher(id=$id, face=$face, issuer=$issuer, holder=$holder, rail=${rail.name}, state=${state.name})';
}

/// One account of a play sandbox, as [PlaySandbox.account] reports it.
final class PlayAccount {
  const PlayAccount(this.id, this.owner, this.isIssuer, this.backing, this.faces, this.count);

  /// The account id.
  final int id;

  /// The owner id given when it was opened.
  final int owner;

  /// True for an issuer, false for a holder.
  final bool isIssuer;

  /// An issuer's play backing; 0 for a holder.
  final int backing;

  /// An issuer's outstanding (sum of the faces of its active vouchers), or
  /// the sum of the faces a holder holds.
  final int faces;

  /// How many active vouchers the issuer issued, or the holder holds.
  final int count;

  @override
  bool operator ==(Object other) =>
      other is PlayAccount &&
      id == other.id &&
      owner == other.owner &&
      isIssuer == other.isIssuer &&
      backing == other.backing &&
      faces == other.faces &&
      count == other.count;

  @override
  int get hashCode => Object.hash(id, owner, isIssuer, backing, faces, count);

  @override
  String toString() =>
      'PlayAccount(id=$id, owner=$owner, isIssuer=$isIssuer, backing=$backing, faces=$faces, count=$count)';
}

/// A play-lane sandbox (`zxv_vino_play`): issuers, holders and Fibonacci-face
/// vouchers, all in play units, in process memory only.
///
/// **Play units are not money.** They come from nowhere ([back] is a
/// faucet), are worth nothing, are lost when the sandbox is closed, and can
/// never become live value: no call takes a sandbox and anything live.
///
/// How it works:
///  - An issuer posts play backing ([back]) and may issue only while
///    `10 * backing >= 18 * outstanding` (1.8x coverage, exact integers). An
///    issue or a withdrawal ([unback]) that would break this is a
///    [CoverageException].
///  - A voucher has one face from the Fibonacci ladder (1, 2, 3, 5, 8, 13,
///    ...; others are [NotADenominationException]), one issuer, one holder
///    and one active [VinoRail]. A [transfer] must name the active rail
///    ([RailException]).
///  - [redeem] returns a voucher to its issuer and retires it: the issuer's
///    outstanding falls by its face, and nothing is paid out to anyone. No
///    call turns a voucher into backing, play units, or anything else, and
///    there is no interest, yield or return of any kind.
///
/// Every call needs [Permission.VINO] on [capability] (else
/// [CapabilityDeniedException]); a halted context gives [ZxvFatalError].
/// Account ids are uint32; voucher ids, faces and amounts are uint64 values
/// in an `int`. Close it before its context (closing the context closes it
/// too); use after close throws [StateError].
final class PlaySandbox {
  PlaySandbox._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle('play sandbox', p, zxv16.vinoPlayDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability every call passes.
  final Capability capability;
  late final n.NativeHandle _h;

  n.ContextState get _st => capability.context.state;
  Pointer<Void> get _c => capability.handle.ptr;

  /// An empty sandbox on `cap`'s context. Needs [Permission.VINO]
  /// ([CapabilityDeniedException] otherwise).
  factory PlaySandbox.create(Capability cap) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxv16.vinoPlayCreate(cap.handle.ptr, o), cap.context.state);
    return PlaySandbox._(cap, o.value);
  });

  /// Opens an account for `owner` (a uint32 id of your choosing; owners need
  /// not be distinct): an issuer if `isIssuer`, else a holder. Returns the
  /// new account id (they count up from 0). `ZXV_E_CAPACITY` once
  /// [Vino.maxAccounts] are open.
  int open(int owner, {required bool isIssuer}) {
    n.u32(owner, 'owner');
    return using((a) {
      final o = a<Uint32>();
      n.check(zxv16.vinoPlayOpen(_c, _h.ptr, owner, isIssuer ? 1 : 0, o), _st);
      return o.value;
    });
  }

  /// Opens an issuer account for `owner`; returns its id. See [open].
  int openIssuer(int owner) => open(owner, isIssuer: true);

  /// Opens a holder account for `owner`; returns its id. See [open].
  int openHolder(int owner) => open(owner, isIssuer: false);

  /// Posts `amount` play units of backing to `issuer`. This is the play
  /// faucet: play units come from nowhere and are worth nothing.
  ///
  /// Throws [ZxvUsageException] `ZXV_E_ARG` if `issuer` is a holder;
  /// `ZXV_E_NOT_FOUND` for no such account; `ZXV_E_RANGE` for amount 0,
  /// above [Vino.amountMax], or backing past 2^62.
  void back(int issuer, int amount) {
    n.u32(issuer, 'issuer');
    n.check(zxv16.vinoPlayBack(_c, _h.ptr, issuer, amount), _st);
  }

  /// Withdraws `amount` of `issuer`'s play backing, only while it stays at
  /// 1.8x coverage ([CoverageException]); more than its backing is
  /// [InsufficientFundsException]. A refusal changes nothing.
  void unback(int issuer, int amount) {
    n.u32(issuer, 'issuer');
    n.check(zxv16.vinoPlayUnback(_c, _h.ptr, issuer, amount), _st);
  }

  /// Issues one voucher of Fibonacci `face` from `issuer` to `holder` on
  /// `rail`; returns its id (> 0, never reused in this sandbox).
  ///
  /// Throws [NotADenominationException] (not a ladder face),
  /// [CoverageException] (the issuer would fall below 1.8x),
  /// [ZxvUsageException] `ZXV_E_ARG` (issuer not an issuer, holder not a
  /// holder), `ZXV_E_NOT_FOUND`, `ZXV_E_CAPACITY` ([Vino.maxVouchers] active).
  int issue(int issuer, int holder, int face, VinoRail rail) {
    n.u32(issuer, 'issuer');
    n.u32(holder, 'holder');
    return using((a) {
      final o = a<Uint64>();
      n.check(zxv16.vinoIssue(_c, _h.ptr, issuer, holder, face, rail.code, o), _st);
      return o.value;
    });
  }

  /// Moves active `voucher` from holder `from` to holder `to`, spending it on
  /// `rail`, which must be its active rail ([RailException]); the rail does
  /// not change. `ZXV_E_STATE` if it was redeemed; `ZXV_E_ARG` if `from` is
  /// not its holder, `to` not a holder, or `from == to`; `ZXV_E_NOT_FOUND`.
  void transfer(int voucher, int from, int to, VinoRail rail) {
    n.u32(from, 'from');
    n.u32(to, 'to');
    n.check(zxv16.vinoTransfer(_c, _h.ptr, voucher, from, to, rail.code), _st);
  }

  /// Switches active `voucher`, by its `holder`, to `rail` (bearer <->
  /// ghost). Nothing moves. Already on `rail` is a [RailException];
  /// `ZXV_E_ARG` if `holder` is not its holder.
  void setRail(int voucher, int holder, VinoRail rail) {
    n.u32(holder, 'holder');
    n.check(zxv16.vinoSetRail(_c, _h.ptr, voucher, holder, rail.code), _st);
  }

  /// Redeems active `voucher`, by its `holder`, to its issuer: it is retired
  /// and the issuer's outstanding falls by its face. **Nothing is paid out**:
  /// no play units, no backing, no live value. `ZXV_E_STATE` if already
  /// redeemed; `ZXV_E_ARG` if `holder` is not its holder.
  void redeem(int voucher, int holder) {
    n.u32(holder, 'holder');
    n.check(zxv16.vinoRedeem(_c, _h.ptr, voucher, holder), _st);
  }

  /// A voucher's face, issuer, holder, rail and state. `ZXV_E_NOT_FOUND` for an unknown id.
  PlayVoucher voucher(int id) => using((a) {
    final face = a<Uint64>();
    final who = a<Uint32>(2);
    final rs = a<Int32>(2);
    n.check(zxv16.vinoVoucherInfo(_c, _h.ptr, id, face, who, who + 1, rs, rs + 1), _st);
    return PlayVoucher(id, face.value, who[0], who[1], VinoRail.fromCode(rs[0]), VoucherState.fromCode(rs[1]));
  });

  /// An account's owner, kind and totals. `ZXV_E_NOT_FOUND` for an unknown id.
  PlayAccount account(int id) {
    n.u32(id, 'account');
    return using((a) {
      final u = a<Uint32>(2);
      final iss = a<Int32>();
      final t = a<Uint64>(2);
      n.check(zxv16.vinoAccountInfo(_c, _h.ptr, id, u, iss, t, t + 1, u + 1), _st);
      return PlayAccount(id, u[0], iss.value != 0, t[0], t[1], u[1]);
    });
  }

  /// The ids of the active vouchers `account` issued (an issuer) or holds (a
  /// holder), ascending. Redeemed vouchers are not listed.
  List<int> vouchers(int account) {
    n.u32(account, 'account');
    return using((a) {
      final count = a<Size>();
      // Two calls: ask for the count, then fill an exact buffer.
      var st = zxv16.vinoVouchers(_c, _h.ptr, account, nullptr, 0, count);
      if (st == 0) return <int>[];
      if (st != n.eBuffer) n.check(st, _st);
      final cap = count.value;
      final ids = a<Uint64>(cap);
      st = zxv16.vinoVouchers(_c, _h.ptr, account, ids, cap, count);
      n.check(st, _st);
      return List<int>.generate(count.value, (i) => ids[i]);
    });
  }

  /// Re-checks every invariant: each active voucher has a ladder face, one
  /// rail, an issuer and a holder; each issuer's outstanding equals the sum
  /// of its active faces and `10 * backing >= 18 * outstanding`; each
  /// holder's total equals what it holds. Returns normally when all hold;
  /// otherwise [ZxvFatalError] (`ZXV_E_INVARIANT`, the context is poisoned).
  void check() => n.check(zxv16.vinoPlayCheck(_c, _h.ptr), _st);

  /// True once closed.
  bool get isClosed => _h.isClosed;

  /// Frees the sandbox; every play unit and voucher in it is gone. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// A payment request (both lanes): a plain value the app builds and the
/// user's wallet approves. Its canonical form is [Vino.requestBytes] bytes
/// ([encode]); its id is SHA-256 of those bytes ([id]).
///
/// `amount` is in VFV minor units for [VinoLane.live] and play units for
/// [VinoLane.play] (play units are not money). Times are seconds of the
/// caller's clock (Unix time by convention); a wallet approves only within
/// `[createdAt, expiresAt]`. A live request may not mark a paid chance outcome
/// ([Vino.flagChance]: loot boxes, paid draws): encoding it is a
/// [ChanceException]. The raffle is a separate module, not a payment request.
///
/// Building one checks only the byte lengths; [encode] applies every rule.
/// Byte lists are copied in and out, so an instance never changes. `amount`,
/// `createdAt` and `expiresAt` are uint64 values in an `int` (bits kept).
final class PaymentRequest {
  /// A request for `lane`. `merchant` is the payee's 32-byte id (e.g. SHA-256
  /// of its key), `nonce` 16 bytes unique per request, `amount` 1 ..
  /// [Vino.amountMax], `expiresAt` > `createdAt`, `memo` 0..64 printable
  /// ASCII characters, `flags` the `ZXV_VINO_REQ_*` bits.
  ///
  /// Throws [ZxvUsageException] (`ZXV_E_ARG`) for a wrong-length merchant or nonce.
  PaymentRequest({
    required VinoLane lane,
    required List<int> merchant,
    required List<int> nonce,
    required this.amount,
    required this.createdAt,
    required this.expiresAt,
    this.memo = '',
    this.flags = 0,
  }) : _lane = lane.code,
       _merchant = _fixed(merchant, 32, 'merchant'),
       _nonce = _fixed(nonce, 16, 'nonce');

  PaymentRequest._raw(
    this._lane,
    List<int> merchant,
    List<int> nonce,
    this.amount,
    this.createdAt,
    this.expiresAt,
    this.memo,
    this.flags,
  ) : _merchant = _fixed(merchant, 32, 'merchant'),
      _nonce = _fixed(nonce, 16, 'nonce');

  static Uint8List _fixed(List<int> b, int len, String what) {
    n.requireLength(b, len, what);
    return Uint8List.fromList(b);
  }

  final int _lane;
  final Uint8List _merchant, _nonce;

  /// 1 .. [Vino.amountMax]: VFV minor units (live) or play units (play).
  final int amount;

  /// When the request was made.
  final int createdAt;

  /// After this time it can no longer be approved (> [createdAt]).
  final int expiresAt;

  /// 0..64 printable ASCII characters (0x20..0x7e).
  final String memo;

  /// `ZXV_VINO_REQ_*` bits ([Vino.flagChance]); unknown bits are refused.
  final int flags;

  /// The lane: play or live.
  VinoLane get lane => VinoLane.fromCode(_lane);

  /// The payee's 32-byte id (a copy).
  Uint8List get merchant => Uint8List.fromList(_merchant);

  /// The 16-byte nonce (a copy).
  Uint8List get nonce => Uint8List.fromList(_nonce);

  /// True if [flags] marks a paid chance outcome; allowed on play only.
  bool get hasChanceOutcome => flags & Vino.flagChance != 0;

  /// The canonical [Vino.requestBytes]-byte record. Pure.
  ///
  /// Throws [LaneException] (lane not play or live), [ChanceException] (a
  /// live request with [Vino.flagChance]), and `ZXV_E_RANGE` (amount 0 or
  /// above [Vino.amountMax], `expiresAt <= createdAt`), `ZXV_E_FORMAT`
  /// (unknown flag, memo not printable ASCII), `ZXV_E_TOO_LONG` (memo above
  /// 64 bytes).
  Uint8List encode() => _encode().$1;

  /// The request id: SHA-256 of [encode]'s bytes (32 bytes). Same refusals as [encode].
  Uint8List id() => _encode().$2;

  (Uint8List, Uint8List) _encode() => using((a) {
    final memoP = memo.isEmpty ? nullptr.cast<Utf8>() : n.cstr(a, memo, 'memo');
    final out = a<Uint8>(Vino.requestBytes);
    final id = a<Uint8>(Vino.requestIdBytes);
    n.check(
      zxv16.vinoRequestEncode(
        _lane,
        flags,
        n.bytesIn(a, _merchant),
        n.bytesIn(a, _nonce),
        amount,
        createdAt,
        expiresAt,
        memoP,
        out,
        id,
      ),
    );
    return (n.bytesOut(out, Vino.requestBytes), n.bytesOut(id, Vino.requestIdBytes));
  });

  /// Decodes and validates request bytes: the same rules as [encode], plus
  /// the magic, version and zero padding (`ZXV_E_FORMAT`). Pure.
  /// `ZXV_E_ARG` if `bytes` is not [Vino.requestBytes] long.
  static PaymentRequest decode(List<int> bytes) => _decode(bytes).$1;

  /// The id (SHA-256) of valid request bytes; refuses invalid ones as [decode] does.
  static Uint8List idOf(List<int> bytes) => _decode(bytes).$2;

  static (PaymentRequest, Uint8List) _decode(List<int> bytes) {
    n.requireLength(bytes, Vino.requestBytes, 'request');
    return using((a) {
      final lane = a<Int32>();
      final flags = a<Uint32>();
      final merchant = a<Uint8>(32), nonce = a<Uint8>(16), id = a<Uint8>(32);
      final t = a<Uint64>(3);
      final memo = a<Uint8>(Vino.memoMax + 1);
      n.check(
        zxv16.vinoRequestDecode(n.bytesIn(a, bytes), lane, flags, merchant, nonce, t, t + 1, t + 2, memo.cast(), id),
      );
      var end = 0;
      while (end < Vino.memoMax && memo[end] != 0) {
        end++;
      }
      final req = PaymentRequest._raw(
        lane.value,
        merchant.asTypedList(32),
        nonce.asTypedList(16),
        t[0],
        t[1],
        t[2],
        ascii.decode(memo.asTypedList(end)),
        flags.value,
      );
      return (req, n.bytesOut(id, 32));
    });
  }

  @override
  bool operator ==(Object other) =>
      other is PaymentRequest &&
      _lane == other._lane &&
      n.sameBytes(_merchant, other._merchant) &&
      n.sameBytes(_nonce, other._nonce) &&
      amount == other.amount &&
      createdAt == other.createdAt &&
      expiresAt == other.expiresAt &&
      memo == other.memo &&
      flags == other.flags;

  @override
  int get hashCode =>
      Object.hash(_lane, Object.hashAll(_merchant), Object.hashAll(_nonce), amount, createdAt, expiresAt, memo, flags);

  @override
  String toString() =>
      'PaymentRequest(lane=$_lane, amount=${_u64(amount)}, createdAt=${_u64(createdAt)}, '
      'expiresAt=${_u64(expiresAt)}, memo="$memo", flags=$flags)';

  static String _u64(int v) => BigInt.from(v).toUnsigned(64).toString();
}

/// A request with a raw lane code (any int), so tests can replay refusals of
/// lanes the [VinoLane] enum cannot name. Internal: not exported.
PaymentRequest paymentRequestWithLaneCode(
  int laneCode, {
  required List<int> merchant,
  required List<int> nonce,
  required int amount,
  required int createdAt,
  required int expiresAt,
  String memo = '',
  int flags = 0,
}) => PaymentRequest._raw(laneCode, merchant, nonce, amount, createdAt, expiresAt, memo, flags);

/// Receipts: the user's wallet approving a [PaymentRequest] (both lanes).
///
/// A receipt is the wallet's ML-DSA-87 signature (empty FIPS 204 context)
/// over [Vino.receiptMessageBytes] bytes: the tag `"zxv/vino-rcpt/v1"` || the
/// request id || SHA-256(wallet public key) || `approvedAt` (8 bytes,
/// little-endian). A receipt for an expired request, or approved after it,
/// is an [ExpiredException].
///
/// The app's half is [verify] (pure and offline: no capability, no network).
/// [sign] is the wallet's call and needs [Permission.CRYPTO_SIGN]; an app
/// never holds the user's key. ML-DSA-87 here is the reference
/// implementation, not FIPS 140-3 validated.
abstract final class VinoReceipt {
  /// The [Vino.receiptMessageBytes] bytes a wallet signs to approve request
  /// `requestId` (32 bytes) with public key `walletPublicKey` (2592 bytes)
  /// at `approvedAt`. Pure.
  static Uint8List message(List<int> requestId, List<int> walletPublicKey, int approvedAt) {
    n.requireLength(requestId, Vino.requestIdBytes, 'requestId');
    n.requireLength(walletPublicKey, Vino.walletPublicKeyBytes, 'walletPublicKey');
    return using((a) {
      final out = a<Uint8>(Vino.receiptMessageBytes);
      n.check(zxv16.vinoReceiptMsg(n.bytesIn(a, requestId), n.bytesIn(a, walletPublicKey), approvedAt, out));
      return n.bytesOut(out, Vino.receiptMessageBytes);
    });
  }

  /// Wallet side: approves `request` (its [Vino.requestBytes] encoded bytes)
  /// at `approvedAt`, which must lie within the request's
  /// `[createdAt, expiresAt]` ([ExpiredException]), and signs with the
  /// wallet's ML-DSA-87 key. Returns the [Vino.receiptSignatureBytes]-byte
  /// signature (hedged, so it differs on every call; each one verifies).
  ///
  /// `cap` needs [Permission.CRYPTO_SIGN]. The secret key stays in
  /// [SecretKey] native memory: libzxv reads it in place, and nothing here
  /// copies or keeps it. Any refusal of decoding the request is thrown too.
  static Uint8List sign(
    Capability cap,
    List<int> request,
    SecretKey walletSecretKey,
    List<int> walletPublicKey,
    int approvedAt,
  ) {
    n.requireLength(request, Vino.requestBytes, 'request');
    n.requireLength(walletPublicKey, Vino.walletPublicKeyBytes, 'walletPublicKey');
    if (walletSecretKey.size != Vino.walletSecretKeyBytes) {
      throw n.usage(n.eArg, 'a wallet secret key is ${Vino.walletSecretKeyBytes} bytes, got ${walletSecretKey.size}');
    }
    return using((a) {
      final sig = a<Uint8>(Vino.receiptSignatureBytes);
      n.check(
        zxv16.vinoReceiptSign(
          cap.handle.ptr,
          n.bytesIn(a, request),
          walletSecretKey.pointer,
          n.bytesIn(a, walletPublicKey),
          approvedAt,
          sig,
        ),
        cap.context.state,
      );
      return n.bytesOut(sig, Vino.receiptSignatureBytes);
    });
  }

  /// App side, offline: returns normally if `signature` is the wallet's
  /// approval of `request` at `approvedAt`, with `approvedAt` inside the
  /// request's window and `now` not past its expiry. Pure: no capability.
  ///
  /// Throws [VerificationFailedException] (not this wallet's signature over
  /// this request and time), [ExpiredException], or any refusal of decoding
  /// the request.
  static void verify(List<int> request, List<int> walletPublicKey, int approvedAt, List<int> signature, int now) {
    n.requireLength(request, Vino.requestBytes, 'request');
    n.requireLength(walletPublicKey, Vino.walletPublicKeyBytes, 'walletPublicKey');
    n.requireLength(signature, Vino.receiptSignatureBytes, 'signature');
    using((a) {
      n.check(
        zxv16.vinoReceiptVerify(
          n.bytesIn(a, request),
          n.bytesIn(a, walletPublicKey),
          approvedAt,
          n.bytesIn(a, signature),
          now,
        ),
      );
    });
  }

  /// [verify] as a bool: false for a bad signature or an expired request;
  /// every other error still throws.
  static bool isValid(List<int> request, List<int> walletPublicKey, int approvedAt, List<int> signature, int now) {
    try {
      verify(request, walletPublicKey, approvedAt, signature, now);
      return true;
    } on VerificationFailedException {
      return false;
    } on ExpiredException {
      return false;
    }
  }
}

/// What [SpendMeter.tryAdmit] decided: whether the amount was counted, and
/// what is left today after the call (unchanged when refused).
typedef Admission = ({bool admitted, int left});

/// A per-device daily spend counter (`zxv_vino_meter`) for a deployment that
/// sets a daily cap. It counts what one device approved per day index and
/// refuses an amount that would pass the cap.
///
/// It is a counter, not a legal check, and makes no exemption claim.
///
/// Needs [Permission.VINO] on [capability]. Close it before its context; use
/// after close throws [StateError].
final class SpendMeter {
  SpendMeter._(this.capability, this.dailyCap, Pointer<Void> p) {
    _h = n.NativeHandle('spend meter', p, zxv16.vinoMeterDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability every call passes.
  final Capability capability;

  /// The most one device may approve per day.
  final int dailyCap;
  late final n.NativeHandle _h;

  /// A meter with a cap of `dailyCap` per day (1 .. [Vino.amountMax];
  /// `ZXV_E_RANGE` otherwise). Needs [Permission.VINO].
  factory SpendMeter.create(Capability cap, int dailyCap) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxv16.vinoMeterCreate(cap.handle.ptr, dailyCap, o), cap.context.state);
    return SpendMeter._(cap, dailyCap, o.value);
  });

  /// Counts `amount` against day `day` (e.g. Unix time ~/ 86400) and returns
  /// what is left that day. A day after the last one starts a fresh count.
  ///
  /// Throws [LimitException] (the day's total would pass the cap; nothing was
  /// counted) and `ZXV_E_RANGE` (`day` before the last day seen, i.e. the
  /// clock went back; amount 0 or above [Vino.amountMax]).
  int admit(int day, int amount) {
    final r = _admit(day, amount);
    n.check(r.$1, capability.context.state);
    return r.$2;
  }

  /// [admit] without an exception for the cap; other errors still throw.
  Admission tryAdmit(int day, int amount) {
    final (st, left) = _admit(day, amount);
    if (st == n.eLimit) return (admitted: false, left: left);
    n.check(st, capability.context.state);
    return (admitted: true, left: left);
  }

  (int, int) _admit(int day, int amount) => using((a) {
    final left = a<Uint64>();
    final st = zxv16.vinoMeterAdmit(capability.handle.ptr, _h.ptr, day, amount, left);
    return (st, left.value);
  });

  /// True once closed.
  bool get isClosed => _h.isClosed;

  /// Frees the meter. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
