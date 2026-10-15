// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Vino Embed (ABI 1.6, the "vino" group of zxv_api.h): what an app or game
// needs to take part in Vino without ever holding anyone's value or keys.
//
// Two lanes, which never meet:
//
//   PLAY  a sandbox inside the app ({@link VinoPlay}) with its own play
//         units, issuers, holders and Fibonacci-face vouchers, in process
//         memory only. Play units are not money: nothing in the ABI takes a
//         play handle and something live, so a play unit can never be
//         converted into, redeemed for, or moved into live value.
//   LIVE  real Vino, held in the user's own ZXV wallet, never in the app.
//         The app builds a {@link PaymentRequest}; the user approves it in
//         their wallet, which signs a {@link Receipt}; the app checks the
//         receipt offline. The app never sees a balance, a key or a
//         custody account.

import type { Capability, ZxvContext } from './context.js';
import { needSecret, type SecretKey } from './crypto.js';
import { Status, usage, ZxvSystemError } from './errors.js';
import { type Arg, type Ptr } from './internal/backend.js';
import { bytes, i32, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv} or a {@link ZxvContext}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

// ---- constants -------------------------------------------------------------

/** Bytes of an encoded payment request (`ZXV_VINO_REQ_BYTES`). */
export const VINO_REQUEST_BYTES = 160;
/** Bytes of a request id: SHA-256 of the encoded request. */
export const VINO_REQUEST_ID_BYTES = 32;
/** Longest memo, in printable ASCII characters (`ZXV_VINO_MEMO_MAX`). */
export const VINO_MEMO_MAX = 64;
/** Bytes of a merchant id in a request. */
export const VINO_MERCHANT_BYTES = 32;
/** Bytes of a request nonce. */
export const VINO_NONCE_BYTES = 16;
/** Bytes a wallet signs to approve a request (`ZXV_VINO_RCPT_MSG_BYTES`): tag, request id, key hash, time. */
export const VINO_RECEIPT_MSG_BYTES = 88;
/** Bytes of a wallet's ML-DSA-87 public key. */
export const VINO_WALLET_PK_BYTES = 2592;
/** Bytes of a wallet's ML-DSA-87 secret key. */
export const VINO_WALLET_SK_BYTES = 4896;
/** Bytes of a receipt signature (ML-DSA-87). */
export const VINO_SIG_BYTES = 4627;
/** Issuers plus holders one play sandbox can hold (`ZXV_VINO_MAX_ACCOUNTS`). */
export const VINO_MAX_ACCOUNTS = 256;
/** Active (unredeemed) vouchers one play sandbox can hold (`ZXV_VINO_MAX_VOUCHERS`). */
export const VINO_MAX_VOUCHERS = 4096;
/** Largest single amount, face or daily cap: 2^59 (`ZXV_VINO_AMOUNT_MAX`). */
export const VINO_AMOUNT_MAX = 1n << 59n;

/**
 * The two rails a voucher can be held on (`ZXV_VINO_RAIL_*`). A voucher is
 * on exactly ONE rail at a time, and a transfer must name that rail, so one
 * voucher can never be spent on both. Switching rails moves nothing.
 */
export const VinoRail = Object.freeze({
  /** The note in the holder's hand. */
  BEARER: 0,
  /** Its twin on the play ledger. */
  GHOST: 1,
} as const);
/** A {@link VinoRail} value. */
export type VinoRail = (typeof VinoRail)[keyof typeof VinoRail];

/** The two lanes of Vino Embed (`ZXV_VINO_LANE_*`). They never meet. */
export const VinoLane = Object.freeze({
  /** The in-app sandbox: play units, which are not money. */
  PLAY: 1,
  /** Real Vino, in the user's own wallet; the app only requests and checks receipts. */
  LIVE: 2,
} as const);
/** A {@link VinoLane} value. */
export type VinoLane = (typeof VinoLane)[keyof typeof VinoLane];

/** The kind of a play-sandbox account ({@link VinoPlay.openAccount}). */
export const VinoAccountKind = Object.freeze({
  /** Holds vouchers and spends them. */
  HOLDER: 0,
  /** Posts play backing and issues vouchers against it. */
  ISSUER: 1,
} as const);
/** A {@link VinoAccountKind} value. */
export type VinoAccountKind = (typeof VinoAccountKind)[keyof typeof VinoAccountKind];

/** The life of a voucher (`ZXV_VINO_V_*`). */
export const VinoVoucherState = Object.freeze({
  /** Held by a holder and spendable on its active rail. */
  ACTIVE: 1,
  /** Returned to its issuer and retired. It stays listed by id; nothing was paid out. */
  REDEEMED: 2,
} as const);
/** A {@link VinoVoucherState} value. */
export type VinoVoucherState = (typeof VinoVoucherState)[keyof typeof VinoVoucherState];

/** Flags of a payment request (`ZXV_VINO_REQ_*`). */
export const VinoRequestFlag = Object.freeze({
  /**
   * The purchase has a paid chance outcome (a loot box, a paid draw).
   * Allowed on the PLAY lane only: a LIVE request with it is refused with
   * {@link PaidChanceRefusedError}.
   */
  CHANCE: 0x1,
  /** Every flag this ABI knows; any other bit is ZXV_E_FORMAT. */
  KNOWN: 0x1,
} as const);

// ---- play lane -------------------------------------------------------------

/** One play-sandbox account ({@link VinoPlay.account}). */
export interface VinoAccountInfo {
  /** The account number (they count up from 0). */
  readonly account: number;
  /** The owner id given when it was opened. */
  readonly owner: number;
  /** True for an issuer, false for a holder. */
  readonly isIssuer: boolean;
  /** An issuer's posted play backing; always 0 for a holder. */
  readonly backing: bigint;
  /** An issuer: the faces it has outstanding. A holder: the faces it holds. */
  readonly faces: bigint;
  /** Active vouchers it issued (issuer) or holds (holder). */
  readonly count: number;
}

/** One voucher ({@link VinoPlay.voucher}). */
export interface VinoVoucherInfo {
  /** Its id (> 0, never reused in its sandbox). */
  readonly id: bigint;
  /** Its Fibonacci ladder face, in play units. */
  readonly face: bigint;
  /** The issuer account that issued it. */
  readonly issuer: number;
  /** The holder account that holds it (or last held it, once redeemed). */
  readonly holder: number;
  /** Its active rail. */
  readonly rail: VinoRail;
  /** ACTIVE or REDEEMED. */
  readonly state: VinoVoucherState;
}

/**
 * A play-lane sandbox (`zxv_vino_play`): play issuers post play backing
 * (play units come from nowhere and are worth nothing) and issue vouchers
 * of Fibonacci ladder faces to holders, who move them between each other
 * on their active rail and finally redeem them back to the issuer.
 *
 * COVERAGE: an issuer may only issue while `10 * backing >= 18 *
 * outstanding` (backing at least 1.8x what it has outstanding, in exact
 * integers). An issue or a withdrawal of backing that would break this is
 * refused with {@link CoverageError} and changes nothing.
 *
 * REDEEMING returns a voucher to its issuer and retires it: the issuer's
 * outstanding falls, and nothing is paid out to anyone. There is no call
 * that turns a voucher into backing, play units, live value or anything
 * else, and there is no interest, yield or return of any kind.
 *
 * HONEST LIMITS: play units are not money and cannot become live value.
 * The sandbox lives in process memory and is lost on close.
 *
 * Capabilities: every call presents the capability the sandbox was created
 * with, which needs {@link Cap.VINO} (else {@link CapabilityDeniedError}).
 * A poisoned context refuses every call with {@link ZxvFatalError}.
 * Internally serialized in C. Close it before its context (or `using`);
 * closing the context closes it first.
 */
export class VinoPlay extends Resource {
  /** The capability every call presents. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link VinoPlay.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * A new, empty play sandbox on `cap`'s context. It has no accounts.
   *
   * @param cap - needs {@link Cap.VINO}; kept for every later call
   * @throws {@link CapabilityDeniedError} without VINO; {@link ZxvFatalError} on a poisoned context
   */
  static create(cap: Capability): VinoPlay {
    const c = cap.context;
    return new VinoPlay(cap, newHandle(c._core, c._state, 'zxv_vino_play_create', cap._ptr()));
  }

  #call(fn: string, ...args: Arg[]): void {
    this.live();
    this.#ctx._core.call(this.#ctx._state, fn, this.cap._ptr(), this.#ptr, ...args);
  }

  /**
   * Open an account.
   *
   * @param owner - an id of your choosing (uint32; owners need not be distinct)
   * @param kind - {@link VinoAccountKind.ISSUER} or {@link VinoAccountKind.HOLDER}
   * @returns the new account number (they count up from 0)
   * @throws a ZXV_E_ARG error for any other kind; a ZXV_E_CAPACITY error when
   *   {@link VINO_MAX_ACCOUNTS} are open
   */
  openAccount(owner: number, kind: VinoAccountKind): number {
    const a = new Uint32Array(1);
    this.#call('zxv_vino_play_open', u32('owner', owner), i32('kind', kind), a);
    return a[0] ?? 0;
  }

  /** Open an issuer account for `owner`; see {@link VinoPlay.openAccount}. */
  openIssuer(owner: number): number {
    return this.openAccount(owner, VinoAccountKind.ISSUER);
  }

  /** Open a holder account for `owner`; see {@link VinoPlay.openAccount}. */
  openHolder(owner: number): number {
    return this.openAccount(owner, VinoAccountKind.HOLDER);
  }

  /**
   * Post `amount` play units of backing to an issuer (the play faucet).
   *
   * @throws a ZXV_E_ARG error (not an issuer), ZXV_E_NOT_FOUND (no such account),
   *   ZXV_E_RANGE (0, above {@link VINO_AMOUNT_MAX}, or backing would pass 2^62)
   */
  back(issuer: number, amount: bigint): void {
    this.#call('zxv_vino_play_back', u32('issuer', issuer), u64('amount', amount));
  }

  /**
   * Withdraw `amount` of an issuer's backing, only while it stays at 1.8x
   * coverage. A refusal changes nothing.
   *
   * @throws {@link CoverageError} (coverage would fall below 1.8x),
   *   {@link InsufficientFundsError} (more than its backing), plus {@link VinoPlay.back}'s errors
   */
  unback(issuer: number, amount: bigint): void {
    this.#call('zxv_vino_play_unback', u32('issuer', issuer), u64('amount', amount));
  }

  /**
   * Issue one voucher of face `face` from `issuer` to `holder` on `rail`.
   *
   * @param face - a Fibonacci ladder face (1, 2, 3, 5, 8, 13, ...), in play units
   * @returns the voucher id (> 0, never reused in this sandbox)
   * @throws {@link DenominationError} (not a ladder face), {@link CoverageError}
   *   (the issuer would fall below 1.8x), a ZXV_E_ARG error (issuer not an issuer,
   *   holder not a holder, bad rail), ZXV_E_NOT_FOUND, ZXV_E_CAPACITY
   *   ({@link VINO_MAX_VOUCHERS} active)
   */
  issue(issuer: number, holder: number, face: bigint, rail: VinoRail): bigint {
    const id = new BigUint64Array(1);
    this.#call('zxv_vino_issue', u32('issuer', issuer), u32('holder', holder), u64('face', face), i32('rail', rail), id);
    return id[0] ?? 0n;
  }

  /**
   * Spend an active voucher: move it from holder `from` to holder `to`.
   * `rail` must be its active rail; the rail does not change.
   *
   * @throws {@link WrongRailError} (not its active rail), a ZXV_E_STATE error
   *   (already redeemed), a ZXV_E_ARG error (`from` is not its holder, `to` not a
   *   holder, from == to, bad rail), ZXV_E_NOT_FOUND
   */
  transfer(voucher: bigint, from: number, to: number, rail: VinoRail): void {
    this.#call('zxv_vino_transfer', u64('voucher', voucher), u32('from', from), u32('to', to), i32('rail', rail));
  }

  /**
   * Switch an active voucher, by its holder, to the other rail (BEARER <->
   * GHOST). This moves nothing.
   *
   * @throws {@link WrongRailError} (it is already on `rail`), a ZXV_E_STATE error
   *   (redeemed), a ZXV_E_ARG error (not its holder, bad rail), ZXV_E_NOT_FOUND
   */
  setRail(voucher: bigint, holder: number, rail: VinoRail): void {
    this.#call('zxv_vino_set_rail', u64('voucher', voucher), u32('holder', holder), i32('rail', rail));
  }

  /**
   * Redeem an active voucher, by its holder, to its issuer: it is retired
   * and the issuer's outstanding falls by its face. Nothing is paid out.
   *
   * @throws a ZXV_E_STATE error (already redeemed), a ZXV_E_ARG error (not its holder), ZXV_E_NOT_FOUND
   */
  redeem(voucher: bigint, holder: number): void {
    this.#call('zxv_vino_redeem', u64('voucher', voucher), u32('holder', holder));
  }

  /**
   * A voucher's face, issuer, holder, active rail and state. Redeemed
   * vouchers stay listed by id.
   *
   * @throws a ZXV_E_NOT_FOUND error for an id never issued
   */
  voucher(id: bigint): VinoVoucherInfo {
    const face = new BigUint64Array(1);
    const who = new Uint32Array(2);
    const rs = new Int32Array(2);
    const v = u64('id', id);
    this.#call('zxv_vino_voucher_info', v, face, who.subarray(0, 1), who.subarray(1, 2), rs.subarray(0, 1), rs.subarray(1, 2));
    return Object.freeze({
      id: v, face: face[0] ?? 0n, issuer: who[0] ?? 0, holder: who[1] ?? 0,
      rail: (rs[0] ?? 0) as VinoRail, state: (rs[1] ?? 0) as VinoVoucherState,
    });
  }

  /**
   * An account's owner, kind and totals.
   *
   * @throws a ZXV_E_NOT_FOUND error for an account never opened
   */
  account(account: number): VinoAccountInfo {
    const owner = new Uint32Array(2);
    const kind = new Int32Array(1);
    const totals = new BigUint64Array(2);
    const a = u32('account', account);
    this.#call('zxv_vino_account_info', a, owner.subarray(0, 1), kind, totals.subarray(0, 1), totals.subarray(1, 2), owner.subarray(1, 2));
    return Object.freeze({
      account: a, owner: owner[0] ?? 0, isIssuer: kind[0] === 1, backing: totals[0] ?? 0n, faces: totals[1] ?? 0n,
      count: owner[1] ?? 0,
    });
  }

  /**
   * The ids of the ACTIVE vouchers an account issued (issuer) or holds
   * (holder), ascending. Redeemed vouchers are not listed.
   *
   * @throws a ZXV_E_NOT_FOUND error for an account never opened
   */
  vouchers(account: number): bigint[] {
    this.live();
    const a = u32('account', account);
    const core = this.#ctx._core;
    const n = core.b.sizeSlot();
    let s = core.callAllowing(this.#ctx._state, [Status.E_BUFFER], 'zxv_vino_vouchers', this.cap._ptr(), this.#ptr, a, null, 0, n);
    if (s === 0) return [];
    for (let attempt = 0; attempt < 3; attempt++) {
      const ids = new BigUint64Array(core.b.readSize(n));
      s = core.callAllowing(this.#ctx._state, [Status.E_BUFFER], 'zxv_vino_vouchers', this.cap._ptr(), this.#ptr, a, ids, ids.length, n);
      if (s === 0) return Array.from(ids.subarray(0, core.b.readSize(n)));
    }
    throw new ZxvSystemError(Status.E_INTERNAL, 'ZXV_E_INTERNAL', 'zxv_vino_vouchers: the list kept growing');
  }

  /**
   * Re-check every invariant of the sandbox: each active voucher has a
   * ladder face, one rail, an issuer and a holder; each issuer's
   * outstanding equals the sum of its active faces and keeps 1.8x
   * coverage; each holder's total equals what it holds.
   *
   * @throws {@link ZxvFatalError} (ZXV_E_INVARIANT; the context is then poisoned) when anything fails
   */
  check(): void {
    this.#call('zxv_vino_play_check');
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_vino_play_destroy', p));
  }
}

// ---- the daily meter -------------------------------------------------------

/** The outcome of {@link SpendMeter.tryAdmit}. */
export interface SpendMeterResult {
  /** True if the amount was counted; false if it would have passed the cap (nothing counted). */
  readonly admitted: boolean;
  /** What is left of today's cap after the call. */
  readonly left: bigint;
}

/**
 * One device's daily spend counter (`zxv_vino_meter`), for a deployment
 * that sets a per-device daily cap. It counts what the device approved per
 * day index and refuses an amount that would pass the cap.
 *
 * HONEST LIMITS: it is a counter, not a legal check, and makes no
 * exemption claim. Days are the caller's index (e.g. Unix time / 86400);
 * the meter never reads a clock.
 *
 * Every call presents the capability it was created with, which needs
 * {@link Cap.VINO}. Close it before its context (or `using`).
 */
export class SpendMeter extends Resource {
  /** The capability every call presents. */
  readonly cap: Capability;
  /** The daily cap it was created with. */
  readonly dailyCap: bigint;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link SpendMeter.create}. */
  constructor(cap: Capability, dailyCap: bigint, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.dailyCap = dailyCap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * A new meter with cap `dailyCap` per day.
   *
   * @param cap - needs {@link Cap.VINO}; kept for every later call
   * @param dailyCap - 1 .. {@link VINO_AMOUNT_MAX}
   * @throws a ZXV_E_RANGE error for a cap out of range; {@link CapabilityDeniedError} without VINO
   */
  static create(cap: Capability, dailyCap: bigint): SpendMeter {
    const c = cap.context;
    const d = u64('dailyCap', dailyCap);
    return new SpendMeter(cap, d, newHandle(c._core, c._state, 'zxv_vino_meter_create', cap._ptr(), d));
  }

  /**
   * Count `amount` against day `day`, or report that it would pass the cap.
   * A day above the last one starts a fresh count.
   *
   * @returns whether it was counted, and what is left today either way
   * @throws a ZXV_E_RANGE error (amount 0 or above {@link VINO_AMOUNT_MAX}, or a
   *   day below the last one: the clock went back)
   */
  tryAdmit(day: bigint, amount: bigint): SpendMeterResult {
    this.live();
    const left = new BigUint64Array(1);
    const s = this.#ctx._core.callAllowing(this.#ctx._state, [Status.E_LIMIT], 'zxv_vino_meter_admit', this.cap._ptr(), this.#ptr,
      u64('day', day), u64('amount', amount), left);
    return Object.freeze({ admitted: s === 0, left: left[0] ?? 0n });
  }

  /**
   * Count `amount` against day `day`.
   *
   * @returns what is left of today's cap
   * @throws {@link LimitExceededError} (the day's total would pass the cap; nothing
   *   is counted), plus {@link SpendMeter.tryAdmit}'s errors
   */
  admit(day: bigint, amount: bigint): bigint {
    this.live();
    const left = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_vino_meter_admit', this.cap._ptr(), this.#ptr, u64('day', day), u64('amount', amount), left);
    return left[0] ?? 0n;
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_vino_meter_destroy', p));
  }
}

// ---- payment requests ------------------------------------------------------

/** The fields of a {@link PaymentRequest}. */
export interface PaymentRequestFields {
  /** {@link VinoLane.LIVE} for real Vino, {@link VinoLane.PLAY} for play units. */
  lane: VinoLane;
  /** {@link VinoRequestFlag} bits (default 0). */
  flags?: number;
  /** The payee's id, {@link VINO_MERCHANT_BYTES} bytes (e.g. SHA-256 of its key). */
  merchant: Uint8Array;
  /** {@link VINO_NONCE_BYTES} bytes, unique per request. */
  nonce: Uint8Array;
  /** 1 .. {@link VINO_AMOUNT_MAX}: VFV minor units on LIVE, play units on PLAY. */
  amount: bigint;
  /** When the request was made (seconds of your clock; Unix time is the convention). */
  createdAt: bigint;
  /** When it stops being payable; must be after `createdAt`. */
  expiresAt: bigint;
  /** 0..64 printable ASCII characters (0x20..0x7e), shown to the user by their wallet (default ""). */
  memo?: string;
}

/** A request as encoded ({@link PaymentRequest.encode}). */
export interface EncodedPaymentRequest {
  /** The canonical {@link VINO_REQUEST_BYTES}-byte record: send these to the user's wallet. */
  readonly bytes: Uint8Array;
  /** Its id: SHA-256 of `bytes` ({@link VINO_REQUEST_ID_BYTES} bytes). */
  readonly id: Uint8Array;
}

/** A request as decoded ({@link PaymentRequest.decode}). */
export interface DecodedPaymentRequest {
  /** The request's fields. */
  readonly request: PaymentRequest;
  /** Its id: SHA-256 of the decoded bytes. */
  readonly id: Uint8Array;
}

/**
 * A payment request: what an app asks the user's wallet to approve. It is
 * a plain immutable value; {@link PaymentRequest.encode} turns it into the
 * canonical {@link VINO_REQUEST_BYTES}-byte record (layout in zxv_api.h)
 * and its SHA-256 id, and {@link PaymentRequest.decode} validates and reads
 * such a record back. Both are pure: no capability, no state.
 *
 * The app never moves value itself: the user approves the request in their
 * own wallet, which posts the payment on its own ledger and returns a
 * {@link Receipt} that the app checks offline. A LIVE request may not buy a
 * paid chance outcome ({@link VinoRequestFlag.CHANCE}): that is refused.
 */
export class PaymentRequest {
  /** The lane. */
  readonly lane: VinoLane;
  /** {@link VinoRequestFlag} bits. */
  readonly flags: number;
  readonly #merchant: Uint8Array;
  readonly #nonce: Uint8Array;
  /** The amount: VFV minor units on LIVE, play units on PLAY. */
  readonly amount: bigint;
  /** When the request was made. */
  readonly createdAt: bigint;
  /** When it stops being payable. */
  readonly expiresAt: bigint;
  /** The memo ("" if none). */
  readonly memo: string;

  /**
   * A request from its fields. Sizes, integer ranges and the memo's
   * characters are checked here only as far as JS types go; the full rules
   * are applied by {@link PaymentRequest.encode}.
   *
   * @throws a ZXV_E_ARG error for a wrong-sized merchant or nonce; ZXV_E_FORMAT for a memo with a NUL
   */
  constructor(fields: PaymentRequestFields) {
    this.lane = i32('lane', fields.lane) as VinoLane;
    this.flags = u32('flags', fields.flags ?? 0);
    this.#merchant = bytes('merchant', fields.merchant, VINO_MERCHANT_BYTES).slice();
    this.#nonce = bytes('nonce', fields.nonce, VINO_NONCE_BYTES).slice();
    this.amount = u64('amount', fields.amount);
    this.createdAt = u64('createdAt', fields.createdAt);
    this.expiresAt = u64('expiresAt', fields.expiresAt);
    const memo = fields.memo ?? '';
    if (typeof memo !== 'string') throw usage('memo must be a string');
    if (memo.includes('\0')) throw usage('memo must not contain NUL', Status.E_FORMAT);
    this.memo = memo;
    Object.freeze(this);
  }

  /** A copy of the payee's id. */
  get merchant(): Uint8Array {
    return this.#merchant.slice();
  }

  /** A copy of the nonce. */
  get nonce(): Uint8Array {
    return this.#nonce.slice();
  }

  /**
   * Encode into the canonical record and its id.
   *
   * @param owner - the loaded library (a {@link Zxv} or a {@link ZxvContext})
   * @throws {@link WrongLaneError} (lane not PLAY or LIVE), {@link PaidChanceRefusedError}
   *   (a LIVE request with CHANCE), a ZXV_E_FORMAT error (unknown flag, memo not
   *   printable ASCII), ZXV_E_RANGE (amount 0 or above {@link VINO_AMOUNT_MAX},
   *   expiresAt not after createdAt), ZXV_E_TOO_LONG (memo above 64 bytes)
   */
  encode(owner: LibraryOwner): EncodedPaymentRequest {
    const core = owner._core;
    const out = new Uint8Array(VINO_REQUEST_BYTES);
    const id = new Uint8Array(VINO_REQUEST_ID_BYTES);
    const memo = new TextEncoder().encode(this.memo);
    const cmemo = new Uint8Array(memo.length + 1);
    cmemo.set(memo);
    core.call(core, 'zxv_vino_request_encode', this.lane, this.flags, this.#merchant, this.#nonce, this.amount, this.createdAt,
      this.expiresAt, cmemo, out, id);
    return Object.freeze({ bytes: out, id });
  }

  /**
   * Validate and decode a {@link VINO_REQUEST_BYTES}-byte record (the
   * encode rules, plus the magic, version and zero padding).
   *
   * @param owner - the loaded library (a {@link Zxv} or a {@link ZxvContext})
   * @returns the request and its id
   * @throws a ZXV_E_FORMAT error (bad magic, version, padding, flag or memo),
   *   {@link WrongLaneError}, {@link PaidChanceRefusedError}, ZXV_E_RANGE, ZXV_E_TOO_LONG
   */
  static decode(owner: LibraryOwner, record: Uint8Array): DecodedPaymentRequest {
    const core = owner._core;
    const lane = new Int32Array(1);
    const flags = new Uint32Array(1);
    const merchant = new Uint8Array(VINO_MERCHANT_BYTES);
    const nonce = new Uint8Array(VINO_NONCE_BYTES);
    const times = new BigUint64Array(3);
    const memo = new Uint8Array(VINO_MEMO_MAX + 1);
    const id = new Uint8Array(VINO_REQUEST_ID_BYTES);
    core.call(core, 'zxv_vino_request_decode', bytes('record', record, VINO_REQUEST_BYTES), lane, flags, merchant, nonce,
      times.subarray(0, 1), times.subarray(1, 2), times.subarray(2, 3), memo, id);
    const end = memo.indexOf(0);
    const request = new PaymentRequest({
      lane: (lane[0] ?? 0) as VinoLane, flags: flags[0] ?? 0, merchant, nonce, amount: times[0] ?? 0n,
      createdAt: times[1] ?? 0n, expiresAt: times[2] ?? 0n,
      memo: new TextDecoder().decode(memo.subarray(0, end < 0 ? VINO_MEMO_MAX : end)),
    });
    return Object.freeze({ request, id });
  }
}

// ---- receipts --------------------------------------------------------------

/** The parts of a {@link Receipt}. */
export interface ReceiptFields {
  /** The approved request, as encoded ({@link VINO_REQUEST_BYTES} bytes). */
  request: Uint8Array;
  /** The wallet's ML-DSA-87 public key ({@link VINO_WALLET_PK_BYTES} bytes). */
  walletPublicKey: Uint8Array;
  /** When the wallet approved it (seconds of the same clock as the request). */
  approvedAt: bigint;
  /** The wallet's signature ({@link VINO_SIG_BYTES} bytes). */
  signature: Uint8Array;
}

/**
 * A wallet's receipt for one payment request: its ML-DSA-87 signature
 * (empty FIPS 204 context) over the {@link VINO_RECEIPT_MSG_BYTES}-byte
 * message "zxv/vino-rcpt/v1" || request id || SHA-256(wallet public key) ||
 * approvedAt (8 bytes little-endian).
 *
 * The app receives a receipt from the user's wallet and checks it offline
 * with {@link Receipt.verify}; it never holds the wallet's key or balance.
 * A receipt is an immutable value: it holds copies of the bytes given.
 *
 * HONEST LIMITS: ML-DSA-87 here is the reference implementation, not FIPS
 * 140-3 validated. Approving a request happens in the user's wallet, which
 * posts the payment on its own ledger; a verified receipt proves that the
 * wallet signed the approval, nothing more.
 */
export class Receipt {
  readonly #request: Uint8Array;
  readonly #pk: Uint8Array;
  readonly #sig: Uint8Array;
  /** When the wallet approved the request. */
  readonly approvedAt: bigint;

  /**
   * A receipt from its parts (e.g. as received from a wallet). Sizes are
   * checked here; the signature is checked only by {@link Receipt.verify}.
   */
  constructor(fields: ReceiptFields) {
    this.#request = bytes('request', fields.request, VINO_REQUEST_BYTES).slice();
    this.#pk = bytes('walletPublicKey', fields.walletPublicKey, VINO_WALLET_PK_BYTES).slice();
    this.#sig = bytes('signature', fields.signature, VINO_SIG_BYTES).slice();
    this.approvedAt = u64('approvedAt', fields.approvedAt);
    Object.freeze(this);
  }

  /**
   * WALLET SIDE: approve an encoded request at `approvedAt` and sign it
   * with the wallet's ML-DSA-87 key (hedged signing: two signatures of the
   * same approval differ, and both verify). Apps do not call this: it is
   * the wallet's call. The secret key stays where it is (off the JS heap);
   * it is neither copied nor kept.
   *
   * @param cap - needs {@link Cap.CRYPTO_SIGN}
   * @param request - the encoded request ({@link VINO_REQUEST_BYTES} bytes)
   * @param walletSecretKey - the wallet's ML-DSA-87 secret key ({@link VINO_WALLET_SK_BYTES} bytes)
   * @param walletPublicKey - its public key ({@link VINO_WALLET_PK_BYTES} bytes)
   * @param approvedAt - within the request's `[createdAt, expiresAt]`
   * @throws {@link RequestExpiredError} (approvedAt outside the window), the request decode
   *   errors (ZXV_E_FORMAT, {@link WrongLaneError}, ...), {@link NotImplementedError}
   *   (built without post-quantum crypto), {@link CapabilityDeniedError}
   */
  static sign(cap: Capability, request: Uint8Array, walletSecretKey: SecretKey, walletPublicKey: Uint8Array, approvedAt: bigint): Receipt {
    const c = cap.context;
    const sk = needSecret('walletSecretKey', walletSecretKey, VINO_WALLET_SK_BYTES, c._core);
    const req = bytes('request', request, VINO_REQUEST_BYTES);
    const pk = bytes('walletPublicKey', walletPublicKey, VINO_WALLET_PK_BYTES);
    const at = u64('approvedAt', approvedAt);
    const sig = new Uint8Array(VINO_SIG_BYTES);
    c._core.call(c._state, 'zxv_vino_receipt_sign', cap._ptr(), req, sk._ptr(), pk, at, sig);
    return new Receipt({ request: req, walletPublicKey: pk, approvedAt: at, signature: sig });
  }

  /** A copy of the encoded request this receipt approves. */
  get request(): Uint8Array {
    return this.#request.slice();
  }

  /** A copy of the wallet's public key. */
  get walletPublicKey(): Uint8Array {
    return this.#pk.slice();
  }

  /** A copy of the signature. */
  get signature(): Uint8Array {
    return this.#sig.slice();
  }

  /**
   * APP SIDE, offline: check that this is the wallet's approval of the
   * request at `approvedAt`, that `approvedAt` is within the request's
   * window, and that `now` is not past the request's expiry. Pure: no
   * capability, no network.
   *
   * @param owner - the loaded library (a {@link Zxv} or a {@link ZxvContext})
   * @param now - the app's current time, on the request's clock
   * @throws {@link VerificationFailedError} (not that wallet's signature over this
   *   request and time), {@link RequestExpiredError}, the request decode errors,
   *   {@link NotImplementedError} (built without post-quantum crypto)
   */
  verify(owner: LibraryOwner, now: bigint): void {
    Vino.verifyReceipt(owner, this.#request, this.#pk, this.approvedAt, this.#sig, now);
  }

  /** {@link Receipt.verify} as a boolean: true if it verifies, false if refused for any REFUSED or USAGE reason. */
  isValid(owner: LibraryOwner, now: bigint): boolean {
    try {
      this.verify(owner, now);
      return true;
    } catch (e) {
      if (e instanceof Error && 'status' in e && !('fatal' in e && e.fatal === true)) return false;
      throw e;
    }
  }
}

// ---- the pure side ---------------------------------------------------------

/**
 * The pure functions of Vino Embed, reached as `zxv.vino`: building the
 * receipt message and verifying a receipt. They need no capability. The
 * stateful parts are {@link VinoPlay} and {@link SpendMeter}; receipts are
 * {@link Receipt}.
 *
 * NOT IN THIS ABI, by design: minting live Vino, setting pegs, any fiat
 * on-ramp or off-ramp, V-Bill auctions or bids, V-Bonds, and every operator
 * or steward action. Whether a given deployment of Vino is store credit, a
 * prepaid access product or something else is a legal question pending
 * counsel; nothing in this SDK decides it.
 */
export class Vino {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * The {@link VINO_RECEIPT_MSG_BYTES} bytes a wallet signs to approve
   * request `requestId` with key `walletPublicKey` at `approvedAt`.
   *
   * @param requestId - SHA-256 of the encoded request ({@link VINO_REQUEST_ID_BYTES} bytes)
   */
  receiptMessage(requestId: Uint8Array, walletPublicKey: Uint8Array, approvedAt: bigint): Uint8Array {
    const out = new Uint8Array(VINO_RECEIPT_MSG_BYTES);
    this.#core.call(this.#core, 'zxv_vino_receipt_msg', bytes('requestId', requestId, VINO_REQUEST_ID_BYTES),
      bytes('walletPublicKey', walletPublicKey, VINO_WALLET_PK_BYTES), u64('approvedAt', approvedAt), out);
    return out;
  }

  /**
   * Verify a receipt from its raw parts; see {@link Receipt.verify}.
   *
   * @throws {@link VerificationFailedError}, {@link RequestExpiredError}, the request decode errors
   */
  verifyReceipt(request: Uint8Array, walletPublicKey: Uint8Array, approvedAt: bigint, signature: Uint8Array, now: bigint): void {
    Vino.verifyReceipt({ _core: this.#core }, request, walletPublicKey, approvedAt, signature, now);
  }

  /** @internal */
  static verifyReceipt(owner: LibraryOwner, request: Uint8Array, walletPublicKey: Uint8Array, approvedAt: bigint, signature: Uint8Array,
    now: bigint): void {
    const core = owner._core;
    core.call(core, 'zxv_vino_receipt_verify', bytes('request', request, VINO_REQUEST_BYTES),
      bytes('walletPublicKey', walletPublicKey, VINO_WALLET_PK_BYTES), u64('approvedAt', approvedAt),
      bytes('signature', signature, VINO_SIG_BYTES), u64('now', now));
  }
}
