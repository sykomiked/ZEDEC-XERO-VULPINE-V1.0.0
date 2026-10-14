// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Fibonacci coins and tokens (ABI 1.5): the VFV coin ladder and its
// Zeckendorf splits (kernel/src/vino_stores), the swarm's 64 AI-token
// tiers with octane and the Fibonacci code (kernel/src/swarm/swarm_denom),
// and a token wallet kept canonical at its octane. Integers only.

import type { Capability, ZxvContext } from './context.js';
import { Status, usage } from './errors.js';
import type { Arg, Ptr } from './internal/backend.js';
import { bytes, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Rungs 1..45 of the coin ladder carry value; rung 0 is the ceremonial V0 note (face 0). */
export const COIN_RUNGS = 45;
/** The face of rung 45: F(46) = 1,836,311,903. */
export const COIN_TOP_FACE = 1_836_311_903n;
/** Number of token tiers: tier k has face F(k+2). */
export const TOKEN_TIERS = 64;
/** Tiers from here up (face 144 and above) are super tokens. */
export const TOKEN_SUPER_TIER = 10;
/** Longest Fibonacci code of one value, in bits (92 Zeckendorf bits + the closing 1). */
export const TOKEN_CODE_MAX_BITS = 93;
/** Largest buffer the code functions take, in bytes (bit positions are uint32). */
export const TOKEN_CODE_MAX_BYTES = 536_870_911;

/**
 * Named octanes (`ZXV_OCTANE_*`): the densest tier a holding may use. Any
 * octane 0..63 is allowed; these are the fuel-pump grades.
 */
export const Octane = Object.freeze({
  /** Face 1 only. */
  PLAIN: 0,
  /** Up to face 8. */
  REGULAR: 4,
  /** Up to face 89. */
  MIDGRADE: 9,
  /** Up to face 987. */
  PREMIUM: 14,
  /** Up to face 75,025. */
  RACING: 23,
  /** Up to face F(65). */
  MAX: 63,
} as const);

/**
 * A token holding: `counts[k]` tokens on tier k, for k = 0..63 (exactly 64
 * entries). Its value is the sum of `counts[k] * face(k)`.
 */
export type TokenCounts = readonly bigint[];

/** Face of a token tier. */
export interface TokenFace {
  /** F(tier + 2). */
  readonly face: bigint;
  /** True for tier {@link TOKEN_SUPER_TIER} (face 144) and up. */
  readonly isSuper: boolean;
}

/** Value and piece count of a holding. */
export interface TokenValue {
  /** Sum of counts[k] * face(k). */
  readonly value: bigint;
  /** Sum of counts[k]: the number of pieces. */
  readonly pieces: bigint;
}

/** Fibonacci codes packed into bytes ({@link Tokens.encodeCodes}). */
export interface TokenCodeStream {
  /** The packed bits (bit i is bit i % 8 of byte i / 8). */
  readonly bytes: Uint8Array;
  /** How many bits are used. */
  readonly bits: number;
}

function counts(name: string, v: unknown): BigUint64Array {
  if (!Array.isArray(v) && !(v instanceof BigUint64Array)) throw usage(`${name} must be an array of 64 bigints`);
  const a = v as ArrayLike<unknown>;
  if (a.length !== TOKEN_TIERS) throw usage(`${name} must have exactly 64 entries (one per tier), got ${a.length}`);
  const out = new BigUint64Array(TOKEN_TIERS);
  for (let k = 0; k < TOKEN_TIERS; k++) out[k] = u64(`${name}[${k}]`, a[k]);
  return out;
}

const freezeCounts = (c: BigUint64Array): TokenCounts => Object.freeze(Array.from(c));

/**
 * The VFV coin ladder, reached as `zxv.coins`. A coin's face is a rung of
 * the Fibonacci ladder 1, 2, 3, 5, 8, 13, ... (rung r has face F(r+1)).
 * Any other face (4, 6, 7, 100, ...) is not a coin: {@link DenominationError}.
 * Pure: no context, no capability.
 *
 * HONEST LIMITS: minting coins is not in this ABI (the kernel's mint path
 * is not yet integer-only and bound to a ledger); this group only names
 * faces and splits amounts.
 */
export class Coins {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * The face of ladder rung `rung` (0..45; rung 0 is the V0 note, face 0).
   *
   * @throws a ZXV_E_RANGE error for a rung above 45
   */
  face(rung: number): bigint {
    const f = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_coin_face', u32('rung', rung), f);
    return f[0] ?? 0n;
  }

  /**
   * The rung (1..45) whose face is exactly `face` (face 1 is rung 1: the ladder has one 1-coin).
   *
   * @throws {@link DenominationError} for 0 or any face not on the ladder, including Fibonacci numbers above the top face
   */
  rung(face: bigint): number {
    const r = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_coin_rung', u64('face', face), r);
    return r[0] ?? 0;
  }

  /**
   * How many coins {@link Coins.split} gives for `amount` (its exact count, without building the list).
   *
   * @throws a ZXV_E_RANGE error when the split would need more than 4,294,967,294 coins (amounts of about 7.9e18 and up)
   */
  splitCount(amount: bigint): bigint {
    const n = this.#core.b.sizeSlot();
    this.#core.callAllowing(this.#core, [Status.E_BUFFER], 'zxv_coin_split', u64('amount', amount), null, 0, n);
    return BigInt(this.#core.b.readSize(n));
  }

  /**
   * The coins for a whole `amount`: its Zeckendorf split into rungs, largest
   * first. At or above the top face it takes floor(amount / top) coins of
   * rung 45 first; the rest uses each lower rung at most once and never two
   * neighbouring rungs, so the split is unique and the fewest coins
   * possible. 0 is an empty split.
   *
   * @param amount - the amount, in the coin's minor units
   * @param maxCoins - refuse (usage error, ZXV_E_RANGE) splits longer than this instead of allocating them; default 1,000,000
   * @returns the rungs (1..45), largest first
   */
  split(amount: bigint, maxCoins = 1_000_000): number[] {
    const a = u64('amount', amount);
    const n = this.splitCount(a);
    if (n === 0n) return [];
    if (n > BigInt(maxCoins)) throw usage(`${a} splits into ${n} coins (more than maxCoins = ${maxCoins}); use splitCount`, Status.E_RANGE);
    const r = new Uint32Array(Number(n));
    const len = this.#core.b.sizeSlot();
    this.#core.call(this.#core, 'zxv_coin_split', a, r, r.length, len);
    return Array.from(r.subarray(0, this.#core.b.readSize(len)));
  }
}

/**
 * The swarm's AI-token denominations, reached as `zxv.tokens`: 64 tiers
 * with faces 1, 2, 3, 5, 8, ... F(65), octane, canonical forms, fuse and
 * split, and the Fibonacci code. Pure: no context, no capability.
 * Holdings are {@link TokenCounts} (64 bigints); functions that change a
 * holding return a new one and never modify their argument.
 *
 * NO PREMIUM: a dense token is worth exactly its face in plain tokens.
 * Density earns no interest, costs no fee and buys no allotment or
 * priority; changing octane never changes a value. It only means fewer
 * pieces (1,000,000 tokens are 1,000,000 pieces at PLAIN, 17 at RACING).
 */
export class Tokens {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * A holding from sparse `[tier, count]` pairs (tiers not named hold 0).
   *
   * @example `zxv.tokens.holding([[0, 3n], [4, 2n]])` is 3 plain tokens and two 8-tokens
   */
  holding(entries: Iterable<readonly [tier: number, count: bigint]> = []): TokenCounts {
    const c = new BigUint64Array(TOKEN_TIERS);
    for (const [t, n] of entries) {
      const k = u32('tier', t);
      if (k >= TOKEN_TIERS) throw usage(`tier ${k} is not 0..63`, Status.E_RANGE);
      c[k] = u64('count', n);
    }
    return freezeCounts(c);
  }

  /**
   * The face of `tier` and whether it is a super token.
   *
   * @throws a ZXV_E_RANGE error for tier >= 64
   */
  face(tier: number): TokenFace {
    const f = new BigUint64Array(1);
    const s = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_token_face', u32('tier', tier), f, s);
    return Object.freeze({ face: f[0] ?? 0n, isSuper: (s[0] ?? 0) !== 0 });
  }

  /**
   * The tier whose face is exactly `face`.
   *
   * @throws {@link DenominationError} unless `face` is one of the 64 faces
   */
  tier(face: bigint): number {
    const t = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_token_tier', u64('face', face), t);
    return t[0] ?? 0;
  }

  /**
   * Value and piece count of a holding.
   *
   * @throws a ZXV_E_RANGE error when the value would pass 64 bits
   */
  value(holding: TokenCounts): TokenValue {
    const c = counts('holding', holding);
    const o = new BigUint64Array(2);
    this.#core.call(this.#core, 'zxv_token_value', c, o.subarray(0, 1), o.subarray(1, 2));
    return Object.freeze({ value: o[0] ?? 0n, pieces: o[1] ?? 0n });
  }

  /**
   * The canonical form of `value` at `octane`: floor(value / face(octane))
   * tokens on that tier and the remainder in Zeckendorf form below it.
   * Every value has exactly one canonical form per octane.
   *
   * @throws a ZXV_E_RANGE error for octane >= 64
   */
  canonical(value: bigint, octane: number): TokenCounts {
    const c = new BigUint64Array(TOKEN_TIERS);
    this.#core.call(this.#core, 'zxv_token_canonical', u64('value', value), u32('octane', octane), c);
    return freezeCounts(c);
  }

  /**
   * True if `holding` is the canonical form of its value at `octane` (false
   * also when it uses a tier above the octane).
   *
   * @throws a ZXV_E_RANGE error for octane >= 64 or a value past 64 bits
   */
  isCanonical(holding: TokenCounts, octane: number): boolean {
    const r = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_token_is_canonical', counts('holding', holding), u32('octane', octane), r);
    return (r[0] ?? 0) === 1;
  }

  #inPlace(fn: string, holding: TokenCounts, ...args: Arg[]): TokenCounts {
    const c = counts('holding', holding);
    this.#core.call(this.#core, fn, c, ...args);
    return freezeCounts(c);
  }

  /**
   * The canonical form of the holding's value at `octane` (the value never changes).
   *
   * @throws a ZXV_E_RANGE error for octane >= 64 or a value past 64 bits
   */
  compress(holding: TokenCounts, octane: number): TokenCounts {
    return this.#inPlace('zxv_token_compress', holding, u32('octane', octane));
  }

  /**
   * Fuse one tier-k and one tier-(k+1) token into one tier-(k+2) token
   * (F(k+2) + F(k+3) = F(k+4)).
   *
   * @throws {@link InsufficientFundsError} (no tier-k or tier-(k+1) token), a ZXV_E_STATE error
   *   (tier k+2 is denser than `octane` allows) or a ZXV_E_RANGE error (tier > 61, octane >= 64)
   */
  fuse(holding: TokenCounts, octane: number, tier: number): TokenCounts {
    return this.#inPlace('zxv_token_fuse', holding, u32('octane', octane), u32('tier', tier));
  }

  /**
   * Split one tier-k token into one tier-(k-1) and one tier-(k-2) token;
   * tier 1 becomes two plain tokens. Tier 0 is atomic. Needs no octane: it
   * only makes a holding plainer.
   *
   * @throws {@link InsufficientFundsError} (no tier-k token) or a ZXV_E_RANGE error (tier 0, >= 64, or a full count)
   */
  split(holding: TokenCounts, tier: number): TokenCounts {
    return this.#inPlace('zxv_token_split', holding, u32('tier', tier));
  }

  /**
   * Bits in the Fibonacci code of `value` (2..93).
   *
   * @throws a ZXV_E_RANGE error for 2^64 - 1 (not encodable)
   */
  codeBits(value: bigint): number {
    const b = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_token_code_bits', u64('value', value), b);
    return b[0] ?? 0;
  }

  /**
   * Pack the Fibonacci codes of `values` one after another: the Zeckendorf
   * bits of value+1, lowest first, then a closing 1, so every code ends in
   * "11" and needs no length field.
   */
  encodeCodes(values: readonly bigint[]): TokenCodeStream {
    let total = 0;
    const vs = values.map((v, i) => {
      const x = u64(`values[${i}]`, v);
      total += this.codeBits(x);
      return x;
    });
    const buf = new Uint8Array(Math.ceil(total / 8));
    const bit = new Uint32Array(1);
    for (const v of vs) this.#core.call(this.#core, 'zxv_token_code_put', buf, buf.length, bit, v);
    return Object.freeze({ bytes: buf, bits: bit[0] ?? 0 });
  }

  /**
   * Read Fibonacci codes from the start of `data`.
   *
   * @param data - packed codes
   * @param count - how many values to read. Default: read codes until only
   *   zero bits are left (the padding of the last byte), or until `bits` is used up
   * @param bits - how many bits of `data` hold codes (default: all of them)
   * @throws a ZXV_E_FORMAT error when a code has no closing "11" before the end or passes 64 bits
   */
  decodeCodes(data: Uint8Array, count?: number, bits?: number): bigint[] {
    const d = bytes('data', data);
    const limit = bits === undefined ? d.length * 8 : u32('bits', bits);
    const bit = new Uint32Array(1);
    const v = new BigUint64Array(1);
    const out: bigint[] = [];
    const more = (): boolean => {
      const b = bit[0] ?? 0;
      if (count !== undefined) return out.length < u32('count', count);
      if (b >= limit) return false;
      if (bits !== undefined) return true;
      for (let i = b; i < limit; i++) if (((d[i >> 3] ?? 0) >> (i & 7)) & 1) return true;
      return false;
    };
    while (more()) {
      this.#core.call(this.#core, 'zxv_token_code_get', d, d.length, bit, v);
      out.push(v[0] ?? 0n);
    }
    return out;
  }

  /**
   * Read one Fibonacci code at bit `bit` of `data`.
   *
   * @returns the value and the bit position just past its code
   * @throws a ZXV_E_FORMAT error (no closing "11", or past 64 bits) or a ZXV_E_RANGE error (`bit` past the end)
   */
  readCode(data: Uint8Array, bit: number): { value: bigint; next: number } {
    const d = bytes('data', data);
    const b = Uint32Array.of(u32('bit', bit));
    const v = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_token_code_get', d, d.length, b, v);
    return { value: v[0] ?? 0n, next: b[0] ?? 0 };
  }
}

/** What {@link TokenWallet.info} reports. */
export interface TokenWalletInfo {
  /** The wallet's free label (e.g. the swarm model that holds it). */
  readonly modelId: number;
  /** Its octane. */
  readonly octane: number;
  /** Its value in plain tokens. */
  readonly value: bigint;
  /** Its piece count. */
  readonly pieces: bigint;
}

/**
 * A token wallet (`zxv_token_wallet`): a holding kept canonical at its own
 * octane. Every change needs {@link Cap.MINT} from a capability of the
 * wallet's context; reads need none. Tokens enter only through
 * {@link TokenWallet.mint} (the kernel's budget lockstep with a swarm is not
 * in this ABI). Fails closed (ZxvFatalError) once its context halts.
 *
 * Internally serialized in C; in JS use it from one agent. Close it before
 * its context (or `using`).
 */
export class TokenWallet extends Resource {
  /** The capability changes present. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link TokenWallet.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * An empty wallet in `cap`'s context.
   *
   * @param cap - must hold {@link Cap.MINT} (else {@link CapabilityDeniedError})
   * @param options - `octane` (default {@link Octane.PLAIN}) and a free `modelId` label (default 0)
   */
  static create(cap: Capability, options: { octane?: number; modelId?: number } = {}): TokenWallet {
    const c = cap.context;
    const p = newHandle(c._core, c._state, 'zxv_token_wallet_create', cap._ptr(), u32('modelId', options.modelId ?? 0),
      u32('octane', options.octane ?? Octane.PLAIN));
    return new TokenWallet(cap, p);
  }

  /** @internal */
  _ptr(): Ptr {
    this.live();
    return this.#ptr;
  }

  #gated(fn: string, ...args: Arg[]): void {
    this.#ctx._core.call(this.#ctx._state, fn, this.cap._ptr(), this._ptr(), ...args);
  }

  /**
   * Create `amount` new tokens in the wallet.
   *
   * @throws a ZXV_E_RANGE error when the value would pass 64 bits (nothing changes)
   */
  mint(amount: bigint): void {
    this.#gated('zxv_token_wallet_mint', u64('amount', amount));
  }

  /**
   * Spend (burn) `amount`; the change stays canonical.
   *
   * @throws {@link InsufficientFundsError} when the wallet holds less (nothing changes)
   */
  spend(amount: bigint): void {
    this.#gated('zxv_token_wallet_spend', u64('amount', amount));
  }

  /**
   * Move `amount` to `to`. This wallet keeps its change at its octane; `to`
   * receives the payment at ITS octane; the total is conserved exactly.
   * Both wallets must belong to the same context as this wallet's capability.
   *
   * @throws {@link InsufficientFundsError} (this wallet holds less) or {@link CapabilityDeniedError}
   *   (a wallet of another context); neither wallet changes on failure
   */
  transferTo(to: TokenWallet, amount: bigint): void {
    if (!(to instanceof TokenWallet)) throw usage('to must be a TokenWallet');
    this.#ctx._core.call(this.#ctx._state, 'zxv_token_wallet_transfer', this.cap._ptr(), this._ptr(), to._ptr(), u64('amount', amount));
  }

  /** Change the octane (up = denser, down = plainer) and re-express the same value canonically. */
  setOctane(octane: number): void {
    this.#gated('zxv_token_wallet_set_octane', u32('octane', octane));
  }

  /** Label, octane, value and piece count. */
  info(): TokenWalletInfo {
    const u = new Uint32Array(2);
    const v = new BigUint64Array(2);
    this.#ctx._core.call(this.#ctx._state, 'zxv_token_wallet_info', this._ptr(), u.subarray(0, 1), u.subarray(1, 2),
      v.subarray(0, 1), v.subarray(1, 2));
    return Object.freeze({ modelId: u[0] ?? 0, octane: u[1] ?? 0, value: v[0] ?? 0n, pieces: v[1] ?? 0n });
  }

  /** The tokens held per tier (always the canonical form at the octane). */
  counts(): TokenCounts {
    const c = new BigUint64Array(TOKEN_TIERS);
    this.#ctx._core.call(this.#ctx._state, 'zxv_token_wallet_counts', this._ptr(), c);
    return freezeCounts(c);
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_token_wallet_destroy', p));
  }
}
