// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { CapabilityDeniedError, type FatalPolicy, Status, usage } from './errors.js';
import { type Ptr, cstr } from './internal/backend.js';
import { bytes, str, u32, u64, u8 } from './internal/check.js';
import { ContextState, type Core, newHandle, Resource } from './internal/core.js';
import { Truth } from './truth.js';

/**
 * Capability bits (`ZXV_CAP_*`), as bigint flags: combine with `|`.
 * A capability can only be narrowed, never widened.
 */
export const Cap = Object.freeze({
  /** Read balances and journals. */
  LEDGER_READ: 1n << 0n,
  /** Post transfers (SB_CAP_CAPITAL_TRANSFER). */
  LEDGER_POST: 1n << 1n,
  /** Sign (SB_CAP_CRYPTO_SIGN). */
  CRYPTO_SIGN: 1n << 2n,
  /** KEM operations (SB_CAP_CRYPTO_KEM). */
  CRYPTO_KEM: 1n << 3n,
  /** Model loading and generation (SB_CAP_AI_INFERENCE). */
  AI_INFERENCE: 1n << 4n,
  /** Budgets (SB_CAP_COMPUTE_SCHEDULE). */
  BUDGET: 1n << 5n,
  /** Admit / defer / advance phase ticks. */
  PHASE: 1n << 6n,
  /** Attestations (SB_CAP_IDENTITY_CREDENTIAL). */
  ATTEST: 1n << 7n,
  /** Swarm economy. */
  SWARM: 1n << 8n,
  /** EVM state-channel encoding. */
  EVM: 1n << 9n,
  /** Every capability of ABI 1.1 (ZXV_CAP_ALL; this value never changes). */
  ALL: (1n << 10n) - 1n,
  /** Build and call the hardware emulation matrix (ABI 1.4). */
  HW_MATRIX: 1n << 10n,
  /** Load hardware images and contract packages (ABI 1.4). */
  HW_RECONF: 1n << 11n,
  /** Every capability as of ABI 1.4 (ZXV_CAP_ALL_1_4). */
  ALL_1_4: (1n << 12n) - 1n,
} as const);

/** Octets in a capability identity (168 bits). */
export const IDENTITY_BYTES = 21;

/** What {@link Capability.info} reports. */
export interface CapabilityInfo {
  /** The bits this capability holds. */
  readonly caps: bigint;
  /** The process id it was created with. */
  readonly processId: number;
  /** Its attestation; anything but TRUE blocks gated calls. */
  readonly attestation: Truth;
  /** True once it (or an ancestor) was revoked. */
  readonly revoked: boolean;
}

/**
 * A capability handle (`zxv_cap`): a process id, a 168-bit identity, a set
 * of {@link Cap} bits and an attestation state. Every gated call verifies
 * it, every time: live (not revoked, nor any ancestor), the needed bit
 * set, and attestation TRUE.
 *
 * Hand narrower capabilities ({@link Capability.attenuate}) to the parts of
 * a program that need less. HONEST LIMIT: inside one process this is
 * least-privilege discipline, not a sandbox; any code in the process can
 * create its own root capability. The process boundary is where it becomes
 * enforcement.
 */
export class Capability extends Resource {
  /** The context this capability belongs to. */
  readonly context: ZxvContext;
  #ptr: Ptr;

  /** @internal */
  constructor(context: ZxvContext, ptr: Ptr, parent: Resource) {
    super(parent);
    this.context = context;
    this.#ptr = ptr;
  }

  /** @internal The live handle (throws if closed). */
  _ptr(): Ptr {
    this.live();
    this.context._state.ensureAlive();
    return this.#ptr;
  }

  /** A child holding `this.caps & keep`. It never widens; it dies with its parent. */
  attenuate(keep: bigint): Capability {
    const c = this.context;
    const p = newHandle(c._core, c._state, 'zxv_cap_attenuate', this._ptr(), u64('keep', keep));
    return new Capability(c, p, this);
  }

  /** Permanently revoke this capability and, through it, every descendant. */
  revoke(): void {
    this.context._core.call(this.context._state, 'zxv_cap_revoke', this._ptr());
  }

  /** Set the attestation. Anything but TRUE blocks gated calls; BOTH reports a contradiction. */
  setAttestation(state: Truth): void {
    if (!(state instanceof Truth)) throw usage('attestation must be a Truth value');
    this.context._core.call(this.context._state, 'zxv_cap_set_attestation', this._ptr(), state.value);
  }

  /** Bits, process id, attestation and revocation. */
  info(): CapabilityInfo {
    const caps = new BigUint64Array(1);
    const pid = new Uint32Array(1);
    const att = new Int32Array(1);
    const rev = new Int32Array(1);
    this.context._core.call(this.context._state, 'zxv_cap_info', this._ptr(), caps, pid, att, rev);
    return Object.freeze({
      caps: caps[0] ?? 0n,
      processId: pid[0] ?? 0,
      attestation: Truth.of(att[0] ?? 0),
      revoked: (rev[0] ?? 0) !== 0,
    });
  }

  /** True if a call needing `needed` would pass the capability check now. */
  check(needed: bigint): boolean {
    const s = this.context._core.callAllowing(this.context._state, [-17], 'zxv_cap_check', this._ptr(), u64('needed', needed));
    return s === 0;
  }

  /** Throw {@link CapabilityDeniedError} unless a call needing `needed` would pass. */
  require(needed: bigint): void {
    this.context._core.call(this.context._state, 'zxv_cap_check', this._ptr(), u64('needed', needed));
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.context._state.release(() => this.context._core.b.call('zxv_cap_destroy', p));
  }
}

/** Options of {@link Zxv.createContext}. */
export interface ContextOptions {
  /** What a FATAL status does; default `'throw'`. */
  fatalPolicy?: FatalPolicy;
}

/** Options of {@link ZxvContext.rootCapability}. */
export interface RootCapabilityOptions {
  /** Bits to hold (default {@link Cap.ALL}). */
  caps?: bigint;
  /** Process id recorded in the capability (default 0). */
  processId?: number;
  /** 21-octet identity (default zeros). */
  identity?: Uint8Array;
}

/** Facts about a currency enabled in a context. */
export interface CurrencyInfo {
  /** Minor units (2 for NGN). */
  readonly minorUnits: number;
  /** ZXV_CCYF_* flags (see {@link CurrencyFlag}). */
  readonly flags: number;
}

/** Currency flag bits (`ZXV_CCYF_*`). */
export const CurrencyFlag = Object.freeze({
  FUND: 0x01,
  MINOR_NA: 0x02,
  NOCTRY: 0x04,
  PAYABLE: 0x08,
  /** A platform-private unit: never valid as an ISO 20022 currency. */
  PRIVATE: 0x10,
} as const);

/**
 * One platform instance (`zxv_ctx`): the ledger, enabled currencies and
 * every handle made from it. Thread-safe in C; in JS, use it from one
 * agent (main thread or one Worker).
 *
 * Close it (or `using ctx = zxv.createContext()`) to free everything it
 * owns: dependents are closed first, in reverse order of creation.
 *
 * Fail-closed: when any call returns a FATAL status (a broken invariant,
 * or {@link ZxvContext.halt}), the context is poisoned in C and in JS;
 * every later call throws {@link ZxvFatalError} without reaching C.
 */
export class ZxvContext extends Resource {
  /** @internal */
  readonly _core: Core;
  /** @internal */
  readonly _state: ContextState;
  #ptr: Ptr;

  /** @internal Use {@link Zxv.createContext}. */
  constructor(core: Core, options: ContextOptions = {}) {
    super(null);
    const policy = options.fatalPolicy ?? 'throw';
    if (policy !== 'throw' && policy !== 'abort') throw usage("fatalPolicy must be 'throw' or 'abort'");
    this._core = core;
    this._state = new ContextState(core, policy);
    this.#ptr = newHandle(core, core, 'zxv_ctx_create');
  }

  /** The policy for FATAL statuses. */
  get fatalPolicy(): FatalPolicy {
    return this._state.policy;
  }

  /** @internal */
  _ptr(): Ptr {
    this.live();
    this._state.ensureAlive();
    return this.#ptr;
  }

  /** A root capability for this context (attestation starts TRUE). */
  rootCapability(options: RootCapabilityOptions = {}): Capability {
    const id = options.identity === undefined ? null : bytes('identity', options.identity, IDENTITY_BYTES);
    const p = newHandle(this._core, this._state, 'zxv_cap_create', this._ptr(), u32('processId', options.processId ?? 0), id,
      u64('caps', options.caps ?? Cap.ALL));
    return new Capability(this, p, this);
  }

  /**
   * Enable an ISO 4217 currency for ledger accounts (minor units from the
   * table, or asserted), through `cap` (needs {@link Cap.LEDGER_POST}). A
   * capability of another context throws {@link CapabilityDeniedError}.
   */
  enableCurrency(cap: Capability, alpha: string, minorUnits?: number): void;
  /**
   * Enable an ISO 4217 currency without a capability.
   * @deprecated Since ABI 1.2 (no capability): use `enableCurrency(cap, alpha, minorUnits?)`.
   */
  enableCurrency(alpha: string, minorUnits?: number): void;
  enableCurrency(a: Capability | string, b?: string | number, c?: number): void {
    if (a instanceof Capability) {
      const cp = this.#ownCap(a);
      const alpha = cstr(str('alpha', b));
      if (c === undefined) this._core.call(this._state, 'zxv_ccy_enable_cap', cp, alpha);
      else this._core.call(this._state, 'zxv_ccy_enable_with_minor_cap', cp, alpha, u8('minorUnits', c));
      return;
    }
    if (b !== undefined && typeof b !== 'number') throw usage('minorUnits must be a number');
    if (b === undefined) this._core.call(this._state, 'zxv_ccy_enable', this._ptr(), cstr(str('alpha', a)));
    else this._core.call(this._state, 'zxv_ccy_enable_with_minor', this._ptr(), cstr(str('alpha', a)), u8('minorUnits', b));
  }

  /**
   * Register a platform-private unit ("VFV"): ledger-only, refused in ISO
   * 20022 messages. Through `cap` (needs {@link Cap.LEDGER_POST}).
   */
  registerPrivateUnit(cap: Capability, code: string, minorUnits: number): void;
  /**
   * Register a platform-private unit without a capability.
   * @deprecated Since ABI 1.2 (no capability): use `registerPrivateUnit(cap, code, minorUnits)`.
   */
  registerPrivateUnit(code: string, minorUnits: number): void;
  registerPrivateUnit(a: Capability | string, b: string | number, c?: number): void {
    if (a instanceof Capability) {
      this._core.call(this._state, 'zxv_ccy_register_private_cap', this.#ownCap(a), cstr(str('code', b)), u8('minorUnits', c));
      return;
    }
    this._core.call(this._state, 'zxv_ccy_register_private', this._ptr(), cstr(str('code', a)), u8('minorUnits', b));
  }

  /** `cap`'s handle, after checking it was made from this context (else ZXV_E_CAP). */
  #ownCap(cap: Capability): Ptr {
    if (cap.context !== this) {
      throw new CapabilityDeniedError(Status.E_CAP, 'ZXV_E_CAP', 'the capability belongs to a different context');
    }
    this._ptr();
    return cap._ptr();
  }

  /** Minor units and flags of an enabled currency; ZXV_E_NOT_FOUND (usage) if not enabled. */
  currency(code: string): CurrencyInfo {
    const mu = new Uint8Array(1);
    const fl = new Uint32Array(1);
    this._core.call(this._state, 'zxv_ccy_get', this._ptr(), cstr(str('code', code)), mu, fl);
    return Object.freeze({ minorUnits: mu[0] ?? 0, flags: fl[0] ?? 0 });
  }

  /** Codes of every enabled currency, in enabling order. */
  enabledCurrencies(): string[] {
    const n = new Uint32Array(1);
    this._core.call(this._state, 'zxv_ccy_enabled_count', this._ptr(), n);
    const out: string[] = [];
    for (let i = 0; i < (n[0] ?? 0); i++) {
      out.push(this._core.text(this._state, 'zxv_ccy_enabled_at', (b, c, l) => [this._ptr(), i, b, c, l]));
    }
    return out;
  }

  /** True if the context halted (it then refuses every call). Never throws for a poisoned context. */
  get isPoisoned(): boolean {
    if (this._state.dead) return true;
    this.live();
    const p = new Int32Array(1);
    const s = this._core.b.call('zxv_ctx_is_poisoned', this.#ptr, p);
    return s !== 0 || (p[0] ?? 0) !== 0;
  }

  /**
   * Halt the context on purpose (an operator kill switch; also how tests
   * exercise the fatal path). Afterwards it behaves exactly like a context
   * that broke an invariant. `reason` is at most 150 bytes.
   */
  halt(reason = 'halted by the host'): void {
    this._core.call(this._state, 'zxv_ctx_halt', this._ptr(), cstr(str('reason', reason)));
  }

  protected destroy(): void {
    const p = this.#ptr;
    this._state.closed = true;
    this._state.release(() => this._core.b.call('zxv_ctx_destroy', p));
  }
}
