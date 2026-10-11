// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Capability, type ZxvContext } from './context.js';
import { type Ptr, cstr } from './internal/backend.js';
import { i32, i64, type IntLike, str, u32, u64, u8 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';

/** Account kinds (`zxv_account_kind`). */
export const AccountKind = Object.freeze({
  /** May never go below zero (no debt). */
  HOLDER: 0,
  /** May carry a CREDIT (777) balance: issuance / nostro. */
  ISSUER: 1,
} as const);
/** An AccountKind value. */
export type AccountKind = (typeof AccountKind)[keyof typeof AccountKind];

/** Largest amount in one posting and largest |balance|: 2^53 - 1 minor units. */
export const AMOUNT_MAX = 9007199254740991n;

/** Vino rail balances of an account, in exact minor units. */
export interface Balance {
  /** DEBIT 555 (NCR): everything received. */
  readonly debit: bigint;
  /** CREDIT 777 (NRE): everything sent. */
  readonly credit: bigint;
  /** EQUITY 888 (PNS) = debit - credit: the spendable position. */
  readonly equity: bigint;
}

/** One journal line of an account. */
export interface JournalEntry {
  /** Platform entry id (shared by both sides of a posting). */
  readonly entryId: bigint;
  /** + received, - sent, in minor units. */
  readonly amount: bigint;
  /** The other account. */
  readonly counterparty: number;
  /** The posting's unique reference. */
  readonly reference: string;
}

/** Facts about an account. */
export interface AccountInfo {
  readonly id: number;
  readonly name: string;
  readonly currency: string;
  readonly kind: AccountKind;
}

/**
 * The triple ledger of a context, used through one capability: posting and
 * opening accounts need {@link Cap.LEDGER_POST}; balances, accounts and
 * journals need {@link Cap.LEDGER_READ} (ABI 1.2: every call is gated).
 * Amounts are exact integer minor units as bigint (cents, kobo, ...).
 */
export class Ledger {
  /** The capability every gated call presents. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;

  /** @internal Use {@link Ledger.of}. */
  constructor(cap: Capability) {
    this.cap = cap;
    this.#ctx = cap.context;
  }

  /** The ledger of `cap`'s context, used through `cap`. */
  static of(cap: Capability): Ledger {
    return new Ledger(cap);
  }

  /** Open an account in an enabled currency. `name` <= 63 bytes. Needs {@link Cap.LEDGER_POST}. */
  openAccount(name: string, currency: string, kind: AccountKind = AccountKind.HOLDER): number {
    const id = new Uint32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_ledger_open_account_cap', this.cap._ptr(), cstr(str('name', name)),
      cstr(str('currency', currency)), i32('kind', kind), id);
    return id[0] ?? 0;
  }

  /**
   * Post a balanced transfer of `amount` (> 0, <= {@link AMOUNT_MAX}) minor
   * units. All three axes are written atomically or not at all. `reference`
   * (<= 35 bytes, e.g. the EndToEndId) must be unique in the context.
   * Throws {@link InsufficientFundsError} if a holder would go negative.
   * Returns the entry id.
   */
  post(from: number, to: number, amount: IntLike, currency: string, reference: string): bigint {
    const e = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_ledger_post_cap', this.cap._ptr(), u32('from', from), u32('to', to),
      i64('amount', amount), cstr(str('currency', currency)), cstr(str('reference', reference)), e);
    return e[0] ?? 0n;
  }

  /** Rail balances of an account. */
  balance(account: number): Balance {
    const d = new BigInt64Array(1);
    const c = new BigInt64Array(1);
    const q = new BigInt64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_ledger_balance_cap', this.cap._ptr(), u32('account', account), d, c, q);
    return Object.freeze({ debit: d[0] ?? 0n, credit: c[0] ?? 0n, equity: q[0] ?? 0n });
  }

  /**
   * Verify every invariant (per currency debits == credits, holders >= 0,
   * exact shadow == ledger). On failure the context is poisoned and this
   * throws {@link ZxvFatalError}.
   */
  check(): void {
    this.#ctx._core.call(this.#ctx._state, 'zxv_ledger_check_cap', this.cap._ptr());
  }

  /** Number of accounts. Needs {@link Cap.LEDGER_READ}. */
  accountCount(): number {
    const n = new Uint32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_ledger_account_count_cap', this.cap._ptr(), n);
    return n[0] ?? 0;
  }

  /** Name, currency and kind of an account. Needs {@link Cap.LEDGER_READ}. */
  account(id: number): AccountInfo {
    const ctx = this.#ctx;
    const a = u32('account', id);
    const name = ctx._core.text(ctx._state, 'zxv_ledger_account_name_cap', (b, c, l) => [this.cap._ptr(), a, b, c, l]);
    const currency = ctx._core.text(ctx._state, 'zxv_ledger_account_ccy_cap', (b, c, l) => [this.cap._ptr(), a, b, c, l]);
    const k = new Int32Array(1);
    ctx._core.call(ctx._state, 'zxv_ledger_account_kind_cap', this.cap._ptr(), a, k);
    return Object.freeze({ id: a, name, currency, kind: (k[0] ?? 0) as AccountKind });
  }

  /** The account's journal, oldest first. Needs {@link Cap.LEDGER_READ}. */
  journal(account: number): JournalEntry[] {
    const ctx = this.#ctx;
    const a = u32('account', account);
    const n = new Uint32Array(1);
    ctx._core.call(ctx._state, 'zxv_ledger_entry_count_cap', this.cap._ptr(), a, n);
    const out: JournalEntry[] = [];
    for (let i = 0; i < (n[0] ?? 0); i++) {
      const id = new BigUint64Array(1);
      const amt = new BigInt64Array(1);
      const cp = new Uint32Array(1);
      const reference = ctx._core.text(ctx._state, 'zxv_ledger_entry_at_cap', (b, c, l) => [this.cap._ptr(), a, i, id, amt, cp, b, c, l]);
      out.push(Object.freeze({ entryId: id[0] ?? 0n, amount: amt[0] ?? 0n, counterparty: cp[0] ?? 0, reference }));
    }
    return out;
  }
}

/** ISO 20022 message kinds (`zxv_mx_kind`). */
export const MxKind = Object.freeze({
  PACS008: 1, CAMT053: 2, PACS002: 3, PACS004: 4, PACS009: 5, CAMT056: 6, CAMT029: 7,
} as const);
/** An MxKind value. */
export type MxKind = (typeof MxKind)[keyof typeof MxKind];

/** Text fields (`zxv_mx_field`); each kind accepts a subset. */
export const MxField = Object.freeze({
  MSG_ID: 1, CRE_DT_TM: 2, DEBTOR_NAME: 3, DEBTOR_ACCT: 4, CREDITOR_NAME: 5, CREDITOR_ACCT: 6,
  END_TO_END_ID: 7, CCY: 8, ACCT_ID: 9,
} as const);
/** An MxField value. */
export type MxField = (typeof MxField)[keyof typeof MxField];

/** Amount fields (`zxv_mx_amount`). */
export const MxAmount = Object.freeze({ SETTLEMENT: 1, OPENING: 2, CLOSING: 3 } as const);
/** An MxAmount value. */
export type MxAmount = (typeof MxAmount)[keyof typeof MxAmount];

/** Statement entry direction and status. */
export const CreditDebit = Object.freeze({ CRDT: 0, DBIT: 1 } as const);
/** A CreditDebit value. */
export type CreditDebit = (typeof CreditDebit)[keyof typeof CreditDebit];
/** Statement entry status. */
export const EntryStatus = Object.freeze({ BOOK: 0, PDNG: 1, INFO: 2 } as const);
/** An EntryStatus value. */
export type EntryStatus = (typeof EntryStatus)[keyof typeof EntryStatus];

/**
 * One ISO 20022 message under construction (pacs.008, camt.053; other
 * kinds report NotImplementedError until the kernel builder lands).
 * Not thread-safe; close it when done. A message made through a capability
 * (needs {@link Cap.LEDGER_READ}) re-checks it on every call, and closing
 * the capability closes the message first.
 */
export class Iso20022Message extends Resource {
  readonly #ctx: ZxvContext;
  #ptr: Ptr;

  /** @internal Use {@link Iso20022Message.create}. */
  constructor(ctx: ZxvContext, ptr: Ptr, parent: Capability | ZxvContext = ctx) {
    super(parent);
    this.#ctx = ctx;
    this.#ptr = ptr;
  }

  /** Start a message of `kind` through `cap` (needs {@link Cap.LEDGER_READ}). */
  static create(cap: Capability, kind: MxKind): Iso20022Message;
  /**
   * Start a message of `kind` in a context, without a capability.
   * @deprecated Since ABI 1.2 (no capability): use `Iso20022Message.create(cap, kind)`.
   */
  static create(ctx: ZxvContext, kind: MxKind): Iso20022Message;
  static create(owner: Capability | ZxvContext, kind: MxKind): Iso20022Message {
    if (owner instanceof Capability) {
      const ctx = owner.context;
      return new Iso20022Message(ctx, newHandle(ctx._core, ctx._state, 'zxv_msg_create_cap', owner._ptr(), i32('kind', kind)), owner);
    }
    return new Iso20022Message(owner, newHandle(owner._core, owner._state, 'zxv_msg_create', owner._ptr(), i32('kind', kind)));
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Set a text field (plain text; the serializer escapes XML). */
  setText(field: MxField, text: string): this {
    this.#ctx._core.call(this.#ctx._state, 'zxv_msg_set_text', this.#p(), i32('field', field), cstr(str('text', text)));
    return this;
  }

  /** Set an amount: exact `units` with `fracDigits` decimal places (must match the currency). */
  setAmount(field: MxAmount, units: IntLike, fracDigits: number): this {
    this.#ctx._core.call(this.#ctx._state, 'zxv_msg_set_amount', this.#p(), i32('field', field), i64('units', units), u8('fracDigits', fracDigits));
    return this;
  }

  /** pacs.008 GrpHdr/NbOfTxs (default 1). */
  setTxCount(n: number): this {
    this.#ctx._core.call(this.#ctx._state, 'zxv_msg_set_tx_count', this.#p(), u32('n', n));
    return this;
  }

  /** Append a camt.053 entry (non-negative magnitude + direction). */
  addEntry(units: IntLike, fracDigits: number, direction: CreditDebit, status: EntryStatus = EntryStatus.BOOK): this {
    this.#ctx._core.call(this.#ctx._state, 'zxv_msg_add_entry', this.#p(), i64('units', units), u8('fracDigits', fracDigits),
      i32('direction', direction), i32('status', status));
    return this;
  }

  /** Validate and serialize to XML. */
  render(): string {
    return this.#ctx._core.text(this.#ctx._state, 'zxv_msg_render', (b, c, l) => [this.#p(), b, c, l]);
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_msg_destroy', p));
  }
}

/** One net position after a netting cycle closed. */
export interface NetPosition {
  readonly participant: string;
  /** + receives, - pays; the positions sum to zero. */
  readonly net: bigint;
}

/**
 * A multilateral netting cycle in one settlement currency (kernel cbank;
 * reports NotImplementedError until that module is built in).
 */
export class NettingCycle extends Resource {
  readonly #ctx: ZxvContext;
  #ptr: Ptr;

  /** @internal Use {@link NettingCycle.open}. */
  constructor(ctx: ZxvContext, ptr: Ptr) {
    super(ctx);
    this.#ctx = ctx;
    this.#ptr = ptr;
  }

  /** Open a cycle. */
  static open(ctx: ZxvContext, cycleId: string, currency: string): NettingCycle {
    return new NettingCycle(ctx, newHandle(ctx._core, ctx._state, 'zxv_netting_open', ctx._ptr(), cstr(str('cycleId', cycleId)),
      cstr(str('currency', currency))));
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Add a participant (a BIC or participant code). */
  addParticipant(participant: string): void {
    this.#ctx._core.call(this.#ctx._state, 'zxv_netting_add_participant', this.#p(), cstr(str('participant', participant)));
  }

  /** Submit an obligation debtor -> creditor; idempotent on `paymentId`. */
  submit(paymentId: string, debtor: string, creditor: string, amount: IntLike): void {
    this.#ctx._core.call(this.#ctx._state, 'zxv_netting_submit', this.#p(), cstr(str('paymentId', paymentId)), cstr(str('debtor', debtor)),
      cstr(str('creditor', creditor)), i64('amount', amount));
  }

  /** Close the cycle and compute net positions. */
  settle(): NetPosition[] {
    const c = this.#ctx;
    c._core.call(c._state, 'zxv_netting_close', this.#p());
    const n = new Uint32Array(1);
    c._core.call(c._state, 'zxv_netting_position_count', this.#p(), n);
    const out: NetPosition[] = [];
    for (let i = 0; i < (n[0] ?? 0); i++) {
      const net = new BigInt64Array(1);
      const participant = c._core.text(c._state, 'zxv_netting_position_at', (b, cap, l) => [this.#p(), i, b, cap, l, net]);
      out.push(Object.freeze({ participant, net: net[0] ?? 0n }));
    }
    return out;
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_netting_destroy', p));
  }
}

/** Card networks and their check-digit schemes. */
export const CardNetwork = Object.freeze({
  /** Damm check digit. */
  PHOENIX: 1,
  /** Luhn check digit. */
  DRAGON: 2,
  /** Verhoeff check digit. */
  THUNDERBIRD: 3,
} as const);
/** A CardNetwork value. */
export type CardNetwork = (typeof CardNetwork)[keyof typeof CardNetwork];

/** A charge card issued by {@link Cards.issue}. */
export interface IssuedCard {
  readonly cardId: bigint;
  readonly maskedPan: string;
}

/** The outcome of {@link Cards.authorize}. */
export interface Authorization {
  /** True with an approval code; false with an ISO 8583 response code (51 = insufficient funds). */
  readonly approved: boolean;
  readonly approvalCode: string;
  readonly responseCode: number;
}

/** Card issue/reissue/authorize on a context's ledger (kernel cardnet; NotImplementedError until built). */
export class Cards {
  readonly #ctx: ZxvContext;

  /** @internal */
  constructor(ctx: ZxvContext) {
    this.#ctx = ctx;
  }

  /** Cards of a context. */
  static of(ctx: ZxvContext): Cards {
    return new Cards(ctx);
  }

  /** Issue a charge card bound to a ledger account. */
  issue(network: CardNetwork, account: number, limit: IntLike): IssuedCard {
    const c = this.#ctx;
    const id = new BigUint64Array(1);
    const maskedPan = c._core.text(c._state, 'zxv_card_issue', (b, cap, l) => [c._ptr(), i32('network', network), u32('account', account),
      i64('limit', limit), id, b, cap, l]);
    return Object.freeze({ cardId: id[0] ?? 0n, maskedPan });
  }

  /** Block a card and issue a replacement; returns the new id. */
  reissue(cardId: bigint): bigint {
    const id = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_card_reissue', this.#ctx._ptr(), u64('cardId', cardId), id);
    return id[0] ?? 0n;
  }

  /** Authorize an amount. A decline is a normal result, not an exception. */
  authorize(cardId: bigint, amount: IntLike, currency: string): Authorization {
    const c = this.#ctx;
    const resp = new Int32Array(1);
    let declined = false;
    const len = c._core.b.sizeSlot();
    const args = (b: Uint8Array | null, cap: number) => [c._ptr(), u64('cardId', cardId), i64('amount', amount), cstr(str('currency', currency)),
      resp, b, cap, len];
    let s = c._core.callAllowing(c._state, [-2, -15], 'zxv_card_authorize', ...args(null, 0));
    let code = '';
    if (s === -2) {
      const buf = new Uint8Array(c._core.b.readSize(len) + 1);
      s = c._core.callAllowing(c._state, [-15], 'zxv_card_authorize', ...args(buf, buf.length));
      code = new TextDecoder().decode(buf.subarray(0, c._core.b.readSize(len)));
    }
    declined = s === -15;
    return Object.freeze({ approved: !declined, approvalCode: declined ? '' : code, responseCode: resp[0] ?? 0 });
  }
}

/** VSS conformance checks. */
export const VssCheck = Object.freeze({ C1_BALANCE: 1, C2_RECEIPTS: 2, C3_ROUNDTRIP: 3, C4_BACKING: 4 } as const);
/** A VssCheck value. */
export type VssCheck = (typeof VssCheck)[keyof typeof VssCheck];
/** VSS levels. */
export const VssLevel = Object.freeze({ NONE: 0, BRONZE: 1, SILVER: 2, GOLD: 3 } as const);
/** A VssLevel value. */
export type VssLevel = (typeof VssLevel)[keyof typeof VssLevel];

/** Run one VSS check over a context (kernel cbank; NotImplementedError until built). */
export function runVss(ctx: ZxvContext, check: VssCheck): { passed: boolean; detail: string } {
  const ok = new Int32Array(1);
  const detail = ctx._core.text(ctx._state, 'zxv_vss_run', (b, c, l) => [ctx._ptr(), i32('check', check), ok, b, c, l]);
  return { passed: (ok[0] ?? 0) === 1, detail };
}

/** The context's overall VSS level. */
export function vssLevel(ctx: ZxvContext): VssLevel {
  const l = new Int32Array(1);
  ctx._core.call(ctx._state, 'zxv_vss_level_get', ctx._ptr(), l);
  return (l[0] ?? 0) as VssLevel;
}

