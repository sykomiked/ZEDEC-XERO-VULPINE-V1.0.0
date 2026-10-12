// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { type ContextOptions, ZxvContext } from './context.js';
import { Hashes, SecretKey } from './crypto.js';
import { usage } from './errors.js';
import { type Backend, cstr } from './internal/backend.js';
import { bytes, i32, i64, str, u16, u32, u64 } from './internal/check.js';
import { Core } from './internal/core.js';
import { type LoadOptions, platform } from './internal/platform.js';
import { loadWasm } from './internal/wasm.js';
import { type CardNetwork } from './ledger.js';
import { Rational } from './rational.js';
import { SwarmInfo } from './swarm.js';
import { Truth } from './truth.js';
import { type SMinus, type SPlus, Ubh168Frame, type WireTruth } from './ubh168.js';

export type { LoadOptions };

/** Feature bits (`ZXV_FEAT_*`) reported by {@link Zxv.features}. */
export const Feature = Object.freeze({
  LEDGER: 0x00000001,
  MX_PACS008: 0x00000002,
  MX_CAMT053: 0x00000004,
  ISO4217: 0x00000008,
  MX_PACS002: 0x00000010,
  MX_PACS004: 0x00000020,
  MX_PACS009: 0x00000040,
  MX_CAMT056: 0x00000080,
  MX_CAMT029: 0x00000100,
  NETTING: 0x00000200,
  CARDS: 0x00000400,
  CARD_CHECK: 0x00000800,
  VSS: 0x00001000,
  RATIONAL: 0x00002000,
  LPRES: 0x00004000,
  CAPS: 0x00008000,
  RMAG: 0x00010000,
  PHASE: 0x00020000,
  WIRE: 0x00040000,
  SWARM: 0x00080000,
  HASH: 0x00100000,
  MLKEM1024: 0x00200000,
  MLDSA87: 0x00400000,
  ED25519: 0x00800000,
  TENSOR: 0x01000000,
  /** LoRA adapters: NOT built yet (never set). */
  LORA: 0x02000000,
  EVM: 0x04000000,
  /** The 64-state hexagram tag and quarantine log (ABI 1.3). */
  HEX: 0x08000000,
  /** Hardware emulation matrix, ZHP-1 profiles and hardware updates (ABI 1.4). */
  HW: 0x10000000,
} as const);
/** A Feature name. */
export type FeatureName = keyof typeof Feature;

/** The ABI version of the loaded library. */
export interface Version {
  readonly major: number;
  readonly minor: number;
  readonly patch: number;
}

/** An ISO 4217 currency. */
export interface Iso4217Currency {
  readonly alpha: string;
  readonly numeric: number;
  /** 255 when the minor unit is N.A. (metals, XDR, XTS, XXX). */
  readonly minorUnits: number;
  readonly flags: number;
}

/** The rationals, logic tables and float boundary as computed by the C kernel (for cross-checks). */
export class KernelExact {
  readonly #core: Core;

  /** @internal */
  constructor(core: Core) {
    this.#core = core;
  }

  #bin(fn: string, a: Rational, b: Rational): Rational {
    const n = new BigInt64Array(1);
    const d = new BigInt64Array(1);
    this.#core.call(this.#core, fn, a.num, a.den, b.num, b.den, n, d);
    return Rational.of(n[0] ?? 0n, d[0] ?? 1n);
  }

  /** zxv_rat_add */
  add(a: Rational, b: Rational): Rational {
    return this.#bin('zxv_rat_add', a, b);
  }

  /** zxv_rat_sub */
  sub(a: Rational, b: Rational): Rational {
    return this.#bin('zxv_rat_sub', a, b);
  }

  /** zxv_rat_mul */
  mul(a: Rational, b: Rational): Rational {
    return this.#bin('zxv_rat_mul', a, b);
  }

  /** zxv_rat_div */
  div(a: Rational, b: Rational): Rational {
    return this.#bin('zxv_rat_div', a, b);
  }

  /** zxv_rat_cmp */
  cmp(a: Rational, b: Rational): number {
    const c = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_rat_cmp', a.num, a.den, b.num, b.den, c);
    return c[0] ?? 0;
  }

  /** zxv_rat_normalize of raw int64 parts. */
  normalize(num: bigint, den: bigint): Rational {
    const n = new BigInt64Array(1);
    const d = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_rat_normalize', i64('num', num), i64('den', den), n, d);
    return Rational.of(n[0] ?? 0n, d[0] ?? 1n);
  }

  /** zxv_rat_parse */
  parse(text: string): Rational {
    const n = new BigInt64Array(1);
    const d = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_rat_parse', cstr(str('text', text)), n, d);
    return Rational.of(n[0] ?? 0n, d[0] ?? 1n);
  }

  /** zxv_rat_format */
  format(r: Rational): string {
    return this.#core.text(this.#core, 'zxv_rat_format', (b, c, l) => [r.num, r.den, b, c, l]);
  }

  /** zxv_rat_from_ieee754 of raw bits. */
  fromIeee754Bits(bits: bigint): Rational {
    const n = new BigInt64Array(1);
    const d = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_rat_from_ieee754', u64('bits', bits), n, d);
    return Rational.of(n[0] ?? 0n, d[0] ?? 1n);
  }

  /** zxv_rat_to_q16 */
  toQ16(r: Rational): number {
    const q = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_rat_to_q16', r.num, r.den, q);
    return q[0] ?? 0;
  }

  #lp(fn: string, ...v: number[]): Truth {
    const o = new Int32Array(1);
    this.#core.call(this.#core, fn, ...v, o);
    return Truth.of(o[0] ?? 0);
  }

  /** zxv_lp_not */
  not(a: Truth): Truth {
    return this.#lp('zxv_lp_not', a.value);
  }

  /** zxv_lp_and */
  and(a: Truth, b: Truth): Truth {
    return this.#lp('zxv_lp_and', a.value, b.value);
  }

  /** zxv_lp_or */
  or(a: Truth, b: Truth): Truth {
    return this.#lp('zxv_lp_or', a.value, b.value);
  }

  /** zxv_lp_from_evidence */
  fromEvidence(positive: number, negative: number): Truth {
    return this.#lp('zxv_lp_from_evidence', u32('positive', positive), u32('negative', negative));
  }

  /** zxv_lp_name */
  truthName(t: Truth): string {
    return this.#core.b.lpName(t.value);
  }
}

/** UBH-168 framing through the C library: the cross-check of {@link Ubh168Frame}, plus the W5 resolver. */
export class KernelWire {
  readonly #core: Core;

  /** @internal */
  constructor(core: Core) {
    this.#core = core;
  }

  /** zxv_wire_tag */
  tag(trunk: number, shell: number, sync: number): number {
    const t = new Uint8Array(1);
    this.#core.call(this.#core, 'zxv_wire_tag', u32('trunk', trunk), u32('shell', shell), u32('sync', sync), t);
    return t[0] ?? 0;
  }

  /** zxv_wire_pack */
  pack(tag: number, sPlus: SPlus, sMinus: SMinus): Ubh168Frame {
    const f = new Uint8Array(21);
    this.#core.call(this.#core, 'zxv_wire_pack', i32('tag', tag), Uint32Array.from(sPlus), Uint32Array.from(sMinus), f);
    return Ubh168Frame.unpack(f);
  }

  /** zxv_wire_unpack: tag and rails as the C library reads them. */
  unpack(frame: Ubh168Frame | Uint8Array): { tag: number; sPlus: SPlus; sMinus: SMinus } {
    const t = new Uint8Array(1);
    const p = new Uint32Array(3);
    const m = new Uint32Array(2);
    this.#core.call(this.#core, 'zxv_wire_unpack', octets(frame), t, p, m);
    return { tag: t[0] ?? 0, sPlus: [p[0] ?? 0, p[1] ?? 0, p[2] ?? 0], sMinus: [m[0] ?? 0, m[1] ?? 0] };
  }

  /** The W5 correlation resolver (words read as signed Q16.16). */
  resolve(frame: Ubh168Frame | Uint8Array): WireTruth {
    const t = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_wire_resolve', octets(frame), t);
    return (t[0] ?? 0) as WireTruth;
  }

  /** zxv_wire_idle */
  idle(trunk: number): Ubh168Frame {
    const f = new Uint8Array(21);
    this.#core.call(this.#core, 'zxv_wire_idle', u32('trunk', trunk), f);
    return Ubh168Frame.unpack(f);
  }
}

function octets(f: Ubh168Frame | Uint8Array): Uint8Array {
  return f instanceof Ubh168Frame ? f.toBytes() : bytes('frame', f, 21);
}

/** ISO 4217 reference data (process-wide, read-only). Reached as `zxv.iso4217`. */
export class Iso4217 {
  readonly #core: Core;

  /** @internal */
  constructor(core: Core) {
    this.#core = core;
  }

  /** Look up by alpha-3 ("NGN"); undefined if not in ISO 4217 (VFV, 555/777/888 rails are not). */
  byAlpha(alpha: string): Iso4217Currency | undefined {
    const num = new Uint16Array(1);
    const mu = new Uint8Array(1);
    const fl = new Uint32Array(1);
    const s = this.#core.callAllowing(this.#core, [-4], 'zxv_iso4217_by_alpha', cstr(str('alpha', alpha)), num, mu, fl);
    return s === 0 ? Object.freeze({ alpha, numeric: num[0] ?? 0, minorUnits: mu[0] ?? 0, flags: fl[0] ?? 0 }) : undefined;
  }

  /** Look up by numeric (566 -> NGN); undefined if unknown. */
  byNumeric(numeric: number): Iso4217Currency | undefined {
    const n = u16('numeric', numeric);
    const mu = new Uint8Array(1);
    const fl = new Uint32Array(1);
    let alpha = '';
    try {
      alpha = this.#core.text(this.#core, 'zxv_iso4217_by_numeric', (b, c, l) => [n, b, c, l, mu, fl]);
    } catch (e) {
      if ((e as { status?: number }).status === -4) return undefined;
      throw e;
    }
    return Object.freeze({ alpha, numeric: n, minorUnits: mu[0] ?? 0, flags: fl[0] ?? 0 });
  }

  /** English name ("Naira"). */
  name(alpha: string): string {
    return this.#core.text(this.#core, 'zxv_iso4217_name', (b, c, l) => [cstr(str('alpha', alpha)), b, c, l]);
  }

  /** Every alpha code in the table. */
  list(): string[] {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_iso4217_count', n);
    const out: string[] = [];
    for (let i = 0; i < (n[0] ?? 0); i++) out.push(this.#core.text(this.#core, 'zxv_iso4217_at', (b, c, l) => [i, b, c, l]));
    return out;
  }

  /** Publication date of the compiled-in list ("2026-01-01"). */
  published(): string {
    return this.#core.text(this.#core, 'zxv_iso4217_published', (b, c, l) => [b, c, l]);
  }

  /** Interop caveat for a numeric code (777 = Vino CREDIT rail), "" if none. */
  caveat(numeric: number): string {
    return this.#core.text(this.#core, 'zxv_ccy_caveat', (b, c, l) => [u16('numeric', numeric), b, c, l]);
  }

  /** True if an ISO 3166 alpha-2 country is an African Union member. */
  isAuMember(countryA2: string): boolean {
    const m = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_au_is_member', cstr(str('countryA2', countryA2)), m);
    return (m[0] ?? 0) === 1;
  }
}

/**
 * The loaded library: the entry point of the SDK.
 *
 * ```ts
 * const zxv = await Zxv.load();            // Node: native addon (falls back to wasm)
 * const zxv = await Zxv.load({ backend: 'wasm' });
 * using ctx = zxv.createContext();
 * ```
 *
 * Both backends expose the same API and return the same results.
 */
export class Zxv {
  /** @internal */
  readonly _core: Core;
  /** ABI version of the library (major 1, minor >= 4). */
  readonly version: Version;
  /** SHA3-256, SHAKE256, Keccak-256. */
  readonly hashes: Hashes;
  /** UBH-168 framing and the W5 resolver through C. */
  readonly wire: KernelWire;
  /** Rationals and four-valued logic as computed by the C kernel. */
  readonly kernel: KernelExact;
  /** ISO 4217 reference data. */
  readonly iso4217: Iso4217;
  /** Swarm level capacities and emotion profiles. */
  readonly swarm: SwarmInfo;
  readonly #features: number;

  private constructor(backend: Backend) {
    const core = new Core(backend);
    this._core = core;
    const maj = new Uint32Array(1);
    const min = new Uint32Array(1);
    const pat = new Uint32Array(1);
    core.call(core, 'zxv_version', maj, min, pat);
    this.version = Object.freeze({ major: maj[0] ?? 0, minor: min[0] ?? 0, patch: pat[0] ?? 0 });
    if (this.version.major !== 1 || this.version.minor < 4) {
      throw usage(`libzxv ABI ${this.version.major}.${this.version.minor} is not supported (need 1.4+): ${backend.origin}`);
    }
    const f = new Uint32Array(1);
    core.call(core, 'zxv_features', f);
    this.#features = f[0] ?? 0;
    this.hashes = new Hashes(core);
    this.wire = new KernelWire(core);
    this.kernel = new KernelExact(core);
    this.iso4217 = new Iso4217(core);
    this.swarm = new SwarmInfo(core);
    Object.freeze(this);
  }

  /**
   * Load a backend. In Node the default (`'auto'`) is the Node-API addon,
   * falling back to WebAssembly if the addon or libzxv cannot be loaded;
   * in a browser it is WebAssembly. The native library is found in the
   * order: `libraryPath`, `ZXV_LIBRARY_PATH`, the bundled copy, the system
   * loader. A library whose ABI major differs, or minor is below 4, is
   * refused.
   */
  static async load(options: LoadOptions = {}): Promise<Zxv> {
    const pf = platform();
    const want = options.backend ?? (pf.name === 'node' ? 'auto' : 'wasm');
    if (want !== 'native' && want !== 'auto' && want !== 'wasm') throw usage(`unknown backend ${String(want)}`);
    if (want === 'native' || want === 'auto') {
      if (!pf.loadNative) {
        if (want === 'native') throw usage('the native backend is only available in Node');
      } else {
        try {
          return new Zxv(pf.loadNative(options));
        } catch (e) {
          if (want === 'native') throw e;
        }
      }
    }
    const src = options.wasm instanceof WebAssembly.Module || ArrayBuffer.isView(options.wasm) || options.wasm instanceof ArrayBuffer
      ? options.wasm
      : await pf.wasmBytes(options.wasm);
    return new Zxv(await loadWasm(src));
  }

  /** 'native' (Node-API addon + libzxv) or 'wasm'. */
  get backend(): 'native' | 'wasm' {
    return this._core.b.kind;
  }

  /** Where the library was loaded from (a path, or "wasm"). */
  get origin(): string {
    return this._core.b.origin;
  }

  /** The raw ZXV_FEAT_* mask. */
  get featureMask(): number {
    return this.#features;
  }

  /** True if a feature group is live in this build. */
  has(feature: FeatureName): boolean {
    return (this.#features & Feature[feature]) !== 0;
  }

  /** Names of every live feature group. */
  features(): FeatureName[] {
    return (Object.keys(Feature) as FeatureName[]).filter((k) => this.has(k));
  }

  /** A new platform context (empty ledger, no currencies). */
  createContext(options: ContextOptions = {}): ZxvContext {
    return new ZxvContext(this._core, options);
  }

  /** "ZXV_E_FUNDS" for -6. */
  statusName(status: number): string {
    return this._core.b.statusName(i32('status', status));
  }

  /** The StatusClass of a status. */
  statusClass(status: number): number {
    return this._core.b.call('zxv_status_class', i32('status', status));
  }

  /** Bytes from the OS CSPRNG (crypto.getRandomValues under wasm). */
  random(length: number): Uint8Array {
    const b = new Uint8Array(u32('length', length));
    this._core.call(this._core, 'zxv_random', b, b.length);
    return b;
  }

  /** Copy known key bytes into off-heap memory (see {@link SecretKey.fromBytes}). */
  secretKey(key: Uint8Array): SecretKey {
    return SecretKey.fromBytes(this, key);
  }

  /** Validate a PAN's check digit for a card network. */
  cardCheckDigit(network: CardNetwork, pan: string): boolean {
    const v = new Int32Array(1);
    this._core.call(this._core, 'zxv_card_check_digit', i32('network', network), cstr(str('pan', pan)), v);
    return (v[0] ?? 0) === 1;
  }

  /** Internal rail numeric of a Vino rail (0 DEBIT 555, 1 CREDIT 777, 2 EQUITY 888). */
  railNumeric(rail: 0 | 1 | 2): number {
    const n = new Uint16Array(1);
    this._core.call(this._core, 'zxv_rail_numeric', i32('rail', rail), n);
    return n[0] ?? 0;
  }
}
