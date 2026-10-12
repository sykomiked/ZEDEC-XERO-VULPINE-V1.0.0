// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The V-Bill hub (ABI 1.5, kernel/src/vhub): a DEMONSTRATION of
// cross-currency clearing through V-Bills, with test currencies, exact
// signed pegs, quotes, all-or-nothing swaps and limit rules, on the hub's
// own in-process ledger.

import type { Capability, ZxvContext } from './context.js';
import { type Arg, cstr, type Ptr } from './internal/backend.js';
import { bytes, str, u32, u64, u8 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Currency slots 0..7. */
export const VHUB_MAX_CURRENCIES = 8;
/** Limit rules per currency. */
export const VHUB_MAX_RULES = 4;
/** Distinct senders one limit tracks per epoch (a 17th is refused: fail closed). */
export const VHUB_LIMIT_SENDERS = 16;
/** Bytes of a peg message ({@link VHub.pegMessage}). */
export const VHUB_PEG_MSG_BYTES = 40;
/** Longest test currency code. */
export const VHUB_CODE_MAX = 12;
/** Largest single amount (2^59); balances stay below 2^62. */
export const VHUB_AMOUNT_MAX = 1n << 59n;
/** The V-Bill issuer's account: always account 0 of a hub. */
export const VHUB_VBILL_ISSUER = 0;
/** An issuer's ML-DSA-87 public key, in bytes. */
export const VHUB_ISSUER_KEY_BYTES = 2592;
/** An ML-DSA-87 peg signature, in bytes. */
export const VHUB_PEG_SIG_BYTES = 4627;

/** A currency's peg: `num` minor units of the currency per `den` V-Bills, set at `epoch`. */
export interface VHubPeg {
  readonly num: bigint;
  readonly den: bigint;
  readonly epoch: bigint;
}

/** A registered test currency ({@link VHub.currency}). */
export interface VHubCurrency {
  /** The currency issuer's account. */
  readonly issuerAccount: number;
  /** The hub's pool of the currency (what a swap pays out of). */
  readonly poolAccount: number;
  /** The currency's V-Bill float (what backs swaps out of it). */
  readonly vbillFloatAccount: number;
  /** The current peg, or undefined while unset (no quote or swap until it is set). */
  readonly peg: VHubPeg | undefined;
}

/** Options of {@link VHub.addCurrency}. */
export interface VHubCurrencyOptions {
  /** 3..12 characters of A-Z and 0-9; not an ISO 4217 code, not "VBL" or "VFV". */
  code: string;
  /** 0..4. */
  minorUnits: number;
  /** The issuer's ML-DSA-87 public key ({@link VHUB_ISSUER_KEY_BYTES} bytes); it alone may set the peg. */
  issuerPublicKey: Uint8Array;
  /** Issued into the hub's pool of the currency at once (default 0). */
  poolFunding?: bigint;
  /** V-Bills issued into the currency's float at once (default 0). */
  vbillBacking?: bigint;
}

/** One account's rails ({@link VHub.balance}). */
export interface VHubBalance {
  /** What the account holds. */
  readonly debit: bigint;
  /** What it owes (issuers only). */
  readonly credit: bigint;
  /** debit - credit. */
  readonly equity: bigint;
}

/** A quote ({@link VHub.quote}). */
export interface VHubQuote {
  /** The largest exact amount of the sending currency not above the one asked for (0 if none). */
  readonly amount: bigint;
  /** The V-Bills it would carry. */
  readonly vbills: bigint;
  /** What the receiver would get, in the receiving currency. */
  readonly received: bigint;
}

/** One swap ({@link VHub.swap}). */
export interface VHubSwapRequest {
  /** Sending currency slot. */
  from: number;
  /** Receiving currency slot (must differ). */
  to: number;
  /** Holder account in `from`. */
  sender: number;
  /** Holder account in `to`. */
  receiver: number;
  /** Minor units of `from` to send; must be exact under both pegs. */
  amount: bigint;
  /** A caller-supplied time (never the wall clock). */
  tick: bigint;
}

/** What a swap moved. */
export interface VHubSwapResult {
  /** V-Bills moved between the two floats. */
  readonly vbills: bigint;
  /** Units the receiver got. */
  readonly received: bigint;
}

/**
 * A V-Bill hub (`zxv_vhub`): each test currency has ONE exact peg against
 * the V-Bill, set only by its issuer with an ML-DSA-87 signature, and a
 * swap from A to B goes A -> V-Bill -> B as ONE balanced, all-or-nothing
 * six-line posting on the hub's own in-process ledger. Both divisions must
 * be exact or the swap is refused: nothing is rounded and no rate is a
 * float. A currency with no limit rule refuses every swap (fail closed).
 *
 * Capabilities: creating, adding currencies and rules, setting pegs,
 * opening and funding accounts and swapping need {@link Cap.LEDGER_POST};
 * quotes, balances, currency info and the check need
 * {@link Cap.LEDGER_READ}. Every call presents the capability the hub was
 * created with. A poisoned context refuses every call.
 *
 * Internally serialized in C; in JS use it from one agent. Close it before
 * its context (or `using`); its ledger is lost on close.
 *
 * HONEST LIMITS: a demo hub with test currencies only (ISO 4217 codes are
 * refused) and no real money. No network: it is designed to support
 * settlement rails such as PAPSS but is connected to none, nor to any
 * central bank or issuer. ML-DSA-87 here is not FIPS 140-3 validated. An
 * exact peg removes rounding, not exchange-rate risk. V-Bills are likely
 * securities in many jurisdictions; real use needs legal review and a
 * participating issuer. A limit rule reports what it decided; it is not a
 * legal check.
 */
export class VHub extends Resource {
  /** The capability every call presents. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link VHub.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * A new hub on `cap`'s context. Its ledger starts with the V-Bill asset
   * ("VBL") and the V-Bill issuer's account ({@link VHUB_VBILL_ISSUER}).
   *
   * @param cap - needs {@link Cap.LEDGER_POST}; kept for every later call
   * @param options - `seed`: 32 bytes from which every posting's idempotency
   *   key and UETR are derived (omit for a fresh random seed)
   */
  static create(cap: Capability, options: { seed?: Uint8Array } = {}): VHub {
    const c = cap.context;
    const seed = options.seed === undefined ? null : bytes('seed', options.seed, 32);
    return new VHub(cap, newHandle(c._core, c._state, 'zxv_vhub_create', cap._ptr(), seed));
  }

  /**
   * The exact 40 bytes an issuer signs to set a peg: "zxv/vhub-peg/v1" ||
   * slot || num || den || epoch (little-endian u64s). Sign them with
   * `MlDsa87.sign(cap, sk, message)` and an empty context. Pure.
   */
  static pegMessage(owner: LibraryOwner, slot: number, num: bigint, den: bigint, epoch: bigint): Uint8Array {
    const m = new Uint8Array(VHUB_PEG_MSG_BYTES);
    owner._core.call(owner._core, 'zxv_vhub_peg_msg', u8('slot', slot), u64('num', num), u64('den', den), u64('epoch', epoch), m);
    return m;
  }

  #call(fn: string, ...args: Arg[]): void {
    this.live();
    this.#ctx._core.call(this.#ctx._state, fn, this.cap._ptr(), this.#ptr, ...args);
  }

  /**
   * Register test currency `slot` (0..7) and open its three accounts (the
   * issuer's, the hub's pool, the V-Bill float), funding the pool and the
   * float as asked. The peg starts unset. A failure registers nothing.
   *
   * @throws a ZXV_E_CURRENCY error for an ISO 4217 code; ZXV_E_DUPLICATE for a slot or code taken;
   *   ZXV_E_FORMAT / ZXV_E_TOO_LONG / ZXV_E_RANGE for a bad code, slot, minor units or amount
   */
  addCurrency(slot: number, options: VHubCurrencyOptions): void {
    this.#call('zxv_vhub_add_ccy', u8('slot', slot), cstr(str('code', options.code)), u8('minorUnits', options.minorUnits),
      bytes('issuerPublicKey', options.issuerPublicKey, VHUB_ISSUER_KEY_BYTES), u64('poolFunding', options.poolFunding ?? 0n),
      u64('vbillBacking', options.vbillBacking ?? 0n));
  }

  /**
   * A currency's accounts and current peg (needs {@link Cap.LEDGER_READ}).
   *
   * @throws a ZXV_E_NOT_FOUND error when the slot is not registered
   */
  currency(slot: number): VHubCurrency {
    const a = new Uint32Array(3);
    const p = new BigUint64Array(3);
    this.#call('zxv_vhub_ccy_info', u8('slot', slot), a.subarray(0, 1), a.subarray(1, 2), a.subarray(2, 3), p.subarray(0, 1),
      p.subarray(1, 2), p.subarray(2, 3));
    const num = p[0] ?? 0n;
    const den = p[1] ?? 0n;
    const epoch = p[2] ?? 0n;
    return Object.freeze({
      issuerAccount: a[0] ?? 0, poolAccount: a[1] ?? 0, vbillFloatAccount: a[2] ?? 0,
      peg: den === 0n ? undefined : Object.freeze({ num, den, epoch }),
    });
  }

  /** The peg message for this hub's `slot` (see the static {@link VHub.pegMessage}). */
  pegMessage(slot: number, num: bigint, den: bigint, epoch: bigint): Uint8Array {
    return VHub.pegMessage(this.#ctx, slot, num, den, epoch);
  }

  /**
   * Set a currency's peg to num/den at `epoch` (above the current one),
   * given the issuer's ML-DSA-87 signature (empty context) over
   * {@link VHub.pegMessage}. It applies to the next quote and swap; a
   * refused peg changes nothing.
   *
   * @throws {@link PegError} (epoch not above the current one: stale or replayed),
   *   {@link VerificationFailedError} (not the issuer's signature over these numbers),
   *   a ZXV_E_RANGE error (num or den 0)
   */
  setPeg(slot: number, num: bigint, den: bigint, epoch: bigint, signature: Uint8Array): void {
    this.#call('zxv_vhub_set_peg', u8('slot', slot), u64('num', num), u64('den', den), u64('epoch', epoch),
      bytes('signature', signature, VHUB_PEG_SIG_BYTES));
  }

  /**
   * Open a holder account in currency `slot` for `owner` (an id of your choosing).
   *
   * @returns the account number
   * @throws a ZXV_E_CAPACITY error when the hub ledger holds 512 accounts
   */
  openAccount(slot: number, owner: number): number {
    const a = new Uint32Array(1);
    this.#call('zxv_vhub_open_account', u8('slot', slot), u32('owner', owner), a);
    return a[0] ?? 0;
  }

  /**
   * Test faucet: issue `amount` to a holder account or a currency's pool (by
   * that currency's issuer), or V-Bills to a currency's float (by the V-Bill
   * issuer). The issuer's CREDIT rises by the same amount.
   *
   * @throws a ZXV_E_RANGE error (0, above {@link VHUB_AMOUNT_MAX}, or a balance would reach 2^62)
   */
  issue(account: number, amount: bigint): void {
    this.#call('zxv_vhub_issue', u32('account', account), u64('amount', amount));
  }

  /** One account's DEBIT, CREDIT and EQUITY (needs {@link Cap.LEDGER_READ}). */
  balance(account: number): VHubBalance {
    const v = new BigUint64Array(2);
    const e = new BigInt64Array(1);
    this.#call('zxv_vhub_balance', u32('account', account), v.subarray(0, 1), v.subarray(1, 2), e);
    return Object.freeze({ debit: v[0] ?? 0n, credit: v[1] ?? 0n, equity: e[0] ?? 0n });
  }

  /**
   * Add the built-in limit rule to currency `slot`: at most `limit` minor
   * units out per sender per rule epoch (inflows are not counted). Several
   * limits on one currency all apply.
   *
   * @throws a ZXV_E_CAPACITY error when the currency has 4 rules already
   */
  addLimit(slot: number, limit: bigint): void {
    this.#call('zxv_vhub_add_limit', u8('slot', slot), u64('limit', limit));
  }

  /** Start the next rule epoch (every limit starts every sender at zero); returns the new epoch. */
  nextEpoch(): bigint {
    const e = new BigUint64Array(1);
    this.#call('zxv_vhub_next_epoch', e);
    return e[0] ?? 0n;
  }

  /**
   * The largest exact amount of `from` not above `want`, and what it would
   * carry (needs {@link Cap.LEDGER_READ}). Rules and balances are not
   * checked, so a quoted swap may still be refused. The rate is implied by
   * the two pegs, so the hub never quotes two prices for one swap.
   *
   * @throws {@link PegError} when either peg is unset
   */
  quote(from: number, to: number, want: bigint): VHubQuote {
    const o = new BigUint64Array(3);
    this.#call('zxv_vhub_quote', u8('from', from), u8('to', to), u64('want', want), o.subarray(0, 1), o.subarray(1, 2), o.subarray(2, 3));
    return Object.freeze({ amount: o[0] ?? 0n, vbills: o[1] ?? 0n, received: o[2] ?? 0n });
  }

  /**
   * Swap, as one six-line posting: sender -a, A pool +a; A float -v, B
   * float +v; B pool -b, receiver +b. A refused swap changes nothing.
   *
   * @throws a ZXV_E_STATE error (a currency has no rule: fail closed), {@link LimitExceededError}
   *   (a limit rule refused), {@link InsufficientFundsError} (sender, A's float or B's pool),
   *   {@link PegError} (a peg unset), a ZXV_E_RANGE error (not exact under the pegs), or
   *   a ZXV_E_CURRENCY error (an account of another currency)
   */
  swap(request: VHubSwapRequest): VHubSwapResult {
    const o = new BigUint64Array(2);
    this.#call('zxv_vhub_swap', u8('from', request.from), u8('to', request.to), u32('sender', request.sender),
      u32('receiver', request.receiver), u64('amount', request.amount), u64('tick', request.tick), o.subarray(0, 1), o.subarray(1, 2));
    return Object.freeze({ vbills: o[0] ?? 0n, received: o[1] ?? 0n });
  }

  /**
   * Verify the whole hub ledger (needs {@link Cap.LEDGER_READ}): A = L + E
   * per account, no negative rail, no CREDIT outside an issuer, DEBIT ==
   * CREDIT per asset, and the posting hash chain.
   *
   * @returns the postings made so far (issues and swaps)
   * @throws {@link ZxvFatalError} (ZXV_E_INVARIANT; the context is then poisoned) when anything fails
   */
  check(): bigint {
    const n = new BigUint64Array(1);
    this.#call('zxv_vhub_check', n);
    return n[0] ?? 0n;
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_vhub_destroy', p));
  }
}
