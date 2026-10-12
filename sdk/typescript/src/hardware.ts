// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Hardware emulation and hardware updates (ABI 1.4, kernel/src/hwemu):
// ZHP-1 profiles, the emulation matrix, dual-signed packages and
// reconfigurable regions. Concepts: sdk/docs/HW_EMULATION.md.

import type { Capability, ZxvContext } from './context.js';
import { SecretKey } from './crypto.js';
import { Status, usage, ZxvError } from './errors.js';
import type { Arg, Backend, HwHandler, Ptr } from './internal/backend.js';
import { cstr } from './internal/backend.js';
import { bytes, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';
import { Hexagram } from './hexagram.js';
import { Truth } from './truth.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  readonly _core: Core;
}

/** The resource every matrix starts with: this machine's CPU (`ZXV_HW_RES_HOST`). */
export const HW_RES_HOST = 0xfffffff0;
/** Parent id of a root profile node (`ZXV_HW_ROOT`). */
export const HW_ROOT = 0xffff;
/** Largest private extension per profile node, in bytes. */
export const HW_EXT_MAX = 256;
/** Component classes from here up are an implementer's own. */
export const HW_CLASS_PRIVATE = 0x80000000;
/** A release public key: the ML-DSA-87 key followed by the SLH-DSA one. */
export const HW_PK_BYTES = 2656;
export const HW_MLDSA_SK_BYTES = 4896;
export const HW_SLHDSA_SK_BYTES = 128;
/** Both signatures of a package (4627 + 29792). */
export const HW_SIG_BYTES = 34419;
export const HW_HEADER_BYTES = 96;

/** Implementation kinds (`ZXV_HW_KIND_*`), preferred in this order at equal cost. */
export const HwKind = Object.freeze({ HARD: 0, FABRIC: 1, MICRO: 2, PEER: 3, SOFT: 4, REMOTE: 5 } as const);
export type HwKind = (typeof HwKind)[keyof typeof HwKind];

/** ZHP-1 component classes (`ZXV_HW_CLASS_*`); see also {@link HW_CLASS_PRIVATE}. */
export const HwClass = Object.freeze({
  MACHINE: 1, CPU_CLUSTER: 2, CPU_CORE: 3, CACHE: 4, MEMORY: 5, INTERCONNECT: 6, MATRIX_UNIT: 7, VECTOR_UNIT: 8,
  TERNARY_UNIT: 9, HASH_UNIT: 10, CRYPTO_UNIT: 11, CODEC_UNIT: 12, RECONF_FABRIC: 13, STORAGE: 14, NETWORK: 15,
  RADIO: 16, SENSOR: 17, ACTUATOR: 18, CLOCK: 19, POWER: 20, SECURE_ELEMENT: 21, DISPLAY: 22, AUDIO: 23, WIRE: 24,
  GPU: 25, PACKAGE: 26, LANE: 27, LOGIC_UNIT: 28, NTT_UNIT: 29, FRAME_UNIT: 30, GCD_UNIT: 31, MEM_CHANNEL: 32,
  SECURE_ISLAND: 33, IO: 34,
} as const);

/** Capability keys (`ZXV_HW_CAP_*`): bit 15 critical, bits 14-13 comparison, bits 12-0 id. */
export const HwCapKey = Object.freeze({
  WORD_BITS: 0x0001, COUNT: 0x0002, FREQ_KHZ: 0x6003, BYTES: 0x0004, LINE_BYTES: 0x6005, ISA: 0x2006,
  INT_WIDTHS: 0x4007, RADIX: 0x2008, MACS_PER_CYCLE: 0x0009, SIMD: 0x400a, TICK_NS: 0x600b, REGIONS: 0x000c,
  BANDWIDTH_MBPS: 0x000d, LATENCY_NS: 0x600e, PQ_ALGS: 0x400f, HASH_ALGS: 0x4010, WIRE_BITS: 0x2011,
  PEAK_OPS_MHZ: 0x6012, CONTRACT_ID: 0x2013, CONTRACT_VER: 0x0014, KIND: 0x6015, CAPACITY_CLASS: 0x0016,
  PERF_BUCKET: 0x0017, POWER_BUCKET: 0x6018,
} as const);

/** `ZXV_HW_CAP_ISA` values. */
export const HwIsa = Object.freeze({ X86_64: 1, ARM64: 2, RISCV64: 3, WASM32: 4 } as const);

/** How a target component is served on a host ({@link HwProfile.pair}). */
export const HwPairing = Object.freeze({ NATIVE: 0, EMULATE: 1, GROUP: 2 } as const);
export type HwPairing = (typeof HwPairing)[keyof typeof HwPairing];

/** `ZXV_HW_RES_*`. */
export const HwResourceState = Object.freeze({ OK: 0, FAULTED: 1, RECONFIGURING: 2, OFFLINE: 3 } as const);
export type HwResourceState = (typeof HwResourceState)[keyof typeof HwResourceState];

/** The standard engines every matrix starts with (`ZXV_HW_FN_*`). */
export const HwFn = Object.freeze({
  /** op 0: SHA-256. */
  SHA256: 0x5a450001,
  /** op 0/1: SHA3-256/512, op 2/3: SHAKE. */
  KECCAK: 0x5a450002,
  /** op 0: HMAC-SHA256 (u16 LE key length, key, message). Key-bearing. */
  MAC: 0x5a450003,
  /** op 0: n pairs of u64 -> n gcds. */
  GCD: 0x5a450004,
  /** 64-lane four-valued AND / OR / NOT. */
  LOGIC64: 0x5a450005,
  /** op 0: one Q8_0 row dot (absent in builds without the tensor engine). */
  TENSOR: 0x5a450006,
} as const);

/** Placement policy flags (`ZXV_HW_POLICY_*`). */
export const HwPolicy = Object.freeze({
  NONE: 0,
  /** Always local, never memoized; sticky (clearing it is ZXV_E_POLICY). */
  KEY_BEARING: 0x1,
  /** No REMOTE implementation. */
  LOCAL_ONLY: 0x2,
} as const);

/** Implementation status flags (`ZXV_HW_PF_*`). */
export const HwProviderFlag = Object.freeze({
  FAULT: 0x01, DISPUTED: 0x02, FAILED: 0x04, UNPROVEN: 0x08, DISABLED: 0x10, POLICY: 0x20, SERVING: 0x40,
} as const);

/** Coarse power classes (`ZXV_HW_POWER_*`). */
export const HwPower = Object.freeze({ UNKNOWN: 0, MW10: 1, MW100: 2, W1: 3, W10: 4, W100: 5, MORE: 6 } as const);

/** Package kinds (`ZXV_HW_IMG_*`). */
export const HwImageKind = Object.freeze({ BITSTREAM: 1, PARTIAL: 2, MICROCODE: 3, FIRMWARE: 4, VECTORS: 5 } as const);
export type HwImageKind = (typeof HwImageKind)[keyof typeof HwImageKind];

/** `ZXV_HW_REGION_*`. */
export const HwRegionState = Object.freeze({ EMPTY: 0, ACTIVE: 1, STAGED: 2, PROBATION: 3, HALTED: 4 } as const);

/** Region events and audit records (`ZXV_HW_EV_*`). */
export const HwEvent = Object.freeze({
  STAGED: 1, TESTED: 2, ACTIVATED: 3, ROLLED_BACK: 4, REJECTED: 5, QUARANTINED: 6, CONTRACT: 7,
} as const);

// ------------------------------------------------------------------ callbacks

/** An implementation: `(op, input) => output bytes` (or a negative status). */
export type Implementation = (op: number, input: Uint8Array) => Uint8Array | number;

/**
 * The device side of a region. Throw a {@link ZxvError} (or return a
 * negative status) to fail a step with that status.
 */
export interface HwDevice {
  /** Load `payload` into `slot` (0 or 1). */
  load(slot: number, payload: Uint8Array): void | number;
  /** The SHA-256 of what `slot` really holds (32 bytes). */
  readback(slot: number): Uint8Array | number;
  /** Switch the region to `slot`. */
  activate(slot: number): void | number;
}

type Entry = { impl: Implementation } | { region: HwRegion };

const registry = new Map<number, Entry>();
let nextId = 1;
const installed = new WeakSet<Backend>();
/** JS exceptions raised inside callbacks, re-thrown by the outermost SDK call. */
let pending: unknown[] = [];
let depth = 0;

function register(e: Entry): number {
  while (registry.has(nextId) || nextId === 0) nextId = (nextId + 1) >>> 0;
  const id = nextId;
  nextId = (nextId + 1) >>> 0;
  registry.set(id, e);
  return id;
}

function statusOf(e: unknown): number {
  if (e instanceof ZxvError) return e.status;
  pending.push(e);
  return Status.E_IO;
}

function asStatus(r: unknown): number {
  if (r === undefined) return 0;
  if (typeof r === 'number' && Number.isInteger(r) && r <= 0) return r;
  pending.push(new TypeError('a device returned something other than nothing or a negative status'));
  return Status.E_IO;
}

const handler: HwHandler = {
  call(id, op, input) {
    const e = registry.get(id);
    if (!e || !('impl' in e)) return Status.E_STATE;
    try {
      const r = e.impl(op, input);
      if (typeof r === 'number') return Number.isInteger(r) && r < 0 ? r : Status.E_IO;
      if (!(r instanceof Uint8Array)) throw new TypeError('an implementation must return a Uint8Array');
      return r;
    } catch (err) {
      return statusOf(err);
    }
  },
  load(id, slot, payload) {
    const e = registry.get(id);
    if (!e || !('region' in e)) return Status.E_STATE;
    try {
      return asStatus(e.region._device.load(slot, payload));
    } catch (err) {
      return statusOf(err);
    }
  },
  readback(id, slot) {
    const e = registry.get(id);
    if (!e || !('region' in e)) return Status.E_STATE;
    try {
      const r = e.region._device.readback(slot);
      if (typeof r === 'number') return Number.isInteger(r) && r < 0 ? r : Status.E_IO;
      return bytes('readback digest', r, 32);
    } catch (err) {
      return statusOf(err);
    }
  },
  activate(id, slot) {
    const e = registry.get(id);
    if (!e || !('region' in e)) return Status.E_STATE;
    try {
      return asStatus(e.region._device.activate(slot));
    } catch (err) {
      return statusOf(err);
    }
  },
  event(id, regionId, event, reason, version) {
    const e = registry.get(id);
    if (!e || !('region' in e)) return;
    try {
      e.region._dispatch(Object.freeze({ regionId, event, reason, version }));
    } catch (err) {
      pending.push(err);
    }
  },
};

function install(core: Core): void {
  if (installed.has(core.b)) return;
  core.b.setHwHandler(handler);
  installed.add(core.b);
}

/** Run one call; throw its status, or a JS exception a callback raised during it. */
function guarded(f: () => void): void {
  if (depth === 0) pending = [];
  depth++;
  let failure: unknown;
  let failed = false;
  try {
    f();
  } catch (e) {
    failed = true;
    failure = e;
  } finally {
    depth--;
  }
  if (depth === 0 && pending.length > 0) {
    const first = pending[0];
    pending = [];
    if (failed && first instanceof Error && first.cause === undefined) {
      Object.defineProperty(first, 'cause', { value: failure, configurable: true, writable: true });
    }
    throw first;
  }
  if (failed) throw failure;
}

/**
 * Encode a golden-vector list, the payload of a {@link HwImageKind.VECTORS}
 * package: per vector `u32 op, u32 in_len, in, u32 out_len, out` (little-endian).
 */
export function encodeVectors(vectors: readonly (readonly [op: number, input: Uint8Array, expect: Uint8Array])[]): Uint8Array {
  let n = 0;
  for (const [, i, e] of vectors) n += 12 + bytes('input', i).length + bytes('expect', e).length;
  const out = new Uint8Array(n);
  const dv = new DataView(out.buffer);
  let o = 0;
  for (const [op, i, e] of vectors) {
    dv.setUint32(o, u32('op', op), true);
    dv.setUint32(o + 4, i.length, true);
    out.set(i, o + 8);
    o += 8 + i.length;
    dv.setUint32(o, e.length, true);
    out.set(e, o + 4);
    o += 4 + e.length;
  }
  return out;
}

// ------------------------------------------------------------------ profiles

/** One component of a {@link HwProfile}. */
export interface ProfileNode {
  readonly id: number;
  /** The parent's id, or {@link HW_ROOT}. */
  readonly parent: number;
  /** An {@link HwClass} value (or a private class >= {@link HW_CLASS_PRIVATE}). */
  readonly cls: number;
  readonly ncaps: number;
}

/** How one target node is served on a host. */
export interface ProfilePairing {
  readonly needId: number;
  readonly kind: HwPairing;
  /** The host node (NATIVE). */
  readonly haveId: number;
  /** The first unmet capability key (EMULATE; 0 if the host lacks the class). */
  readonly unmetKey: number;
}

/**
 * A ZHP-1 machine profile (`zxv_hw_profile`), the "hardware ROM": a tree of
 * abstract components with integer capability descriptors. Plain data: no
 * capability needed.
 */
export class HwProfile extends Resource {
  readonly #core: Core;
  #ptr: Ptr;

  /** @internal Use the static factories. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  static #make(owner: LibraryOwner, fn: string, ...args: Arg[]): HwProfile {
    const c = owner._core;
    return new HwProfile(c, newHandle(c, c, fn, ...args));
  }

  /** A profile with no nodes. */
  static empty(zxv: LibraryOwner): HwProfile {
    return HwProfile.#make(zxv, 'zxv_hw_profile_create');
  }

  /** This machine: CPU cluster (ISA, cores, SIMD), memory, cache line. Nothing is guessed. */
  static probeHost(zxv: LibraryOwner): HwProfile {
    return HwProfile.#make(zxv, 'zxv_hw_profile_probe_host');
  }

  /** A copy of the public VULPINE-class reference machine R1 (16 nodes). */
  static reference(zxv: LibraryOwner): HwProfile {
    return HwProfile.#make(zxv, 'zxv_hw_profile_reference');
  }

  /** Parse a canonical ZHP-1 encoding (anything non-canonical: ZXV_E_FORMAT). */
  static decode(zxv: LibraryOwner, data: Uint8Array): HwProfile {
    const b = bytes('data', data);
    return HwProfile.#make(zxv, 'zxv_hw_profile_decode', b, b.length);
  }

  /** `target` as served on `host`: running nodes carry {@link HwCapKey.KIND} (HARD or SOFT). */
  static present(host: HwProfile, target: HwProfile): HwProfile {
    return HwProfile.#make({ _core: host.#core }, 'zxv_hw_profile_present', host._ptr(), target._ptr());
  }

  /** `[native, emulated]`: how many target components run natively on `host`. */
  static match(host: HwProfile, target: HwProfile): [native: number, emulated: number] {
    const n = new Uint32Array(2);
    host.#core.call(host.#core, 'zxv_hw_profile_match', host._ptr(), target._ptr(), n.subarray(0, 1), n.subarray(1, 2));
    return [n[0] ?? 0, n[1] ?? 0];
  }

  /** How target node `index` (in target order) is served on `host`. */
  static pair(host: HwProfile, target: HwProfile, index: number): ProfilePairing {
    const u = new Uint32Array(3);
    const k = new Int32Array(1);
    host.#core.call(host.#core, 'zxv_hw_profile_pair', host._ptr(), target._ptr(), u32('index', index), u.subarray(0, 1), k,
      u.subarray(1, 2), u.subarray(2, 3));
    return Object.freeze({ needId: u[0] ?? 0, kind: (k[0] ?? 0) as HwPairing, haveId: u[1] ?? 0, unmetKey: u[2] ?? 0 });
  }

  /** @internal */
  _ptr(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** The canonical encoding. */
  encode(): Uint8Array {
    const p = this._ptr();
    return this.#core.bytes(this.#core, 'zxv_hw_profile_encode', (b, c, l) => [p, b, c, l]);
  }

  /** SHA-256 of the canonical encoding: the profile's identity. */
  digest(): Uint8Array {
    const d = new Uint8Array(32);
    this.#core.call(this.#core, 'zxv_hw_profile_digest', this._ptr(), d);
    return d;
  }

  /** Add a node: ids strictly increase, parent first (or {@link HW_ROOT}). */
  addNode(id: number, parent: number, cls: number): void {
    this.#core.call(this.#core, 'zxv_hw_profile_node_add', this._ptr(), u32('id', id), u32('parent', parent), u32('cls', cls));
  }

  /** Set capability `key` ({@link HwCapKey}) of a node. */
  setCap(nodeId: number, key: number, value: bigint | number): void {
    this.#core.call(this.#core, 'zxv_hw_profile_cap_set', this._ptr(), u32('nodeId', nodeId), u32('key', key), u64('value', value));
  }

  /** The node's capability `key`, or undefined if it lacks it. */
  getCap(nodeId: number, key: number): bigint | undefined {
    const v = new BigUint64Array(1);
    const s = this.#core.callAllowing(this.#core, [Status.E_NOT_FOUND], 'zxv_hw_profile_cap_get', this._ptr(),
      u32('nodeId', nodeId), u32('key', key), v);
    return s === 0 ? (v[0] ?? 0n) : undefined;
  }

  /** The node's opaque private extension (at most {@link HW_EXT_MAX} bytes). */
  setExt(nodeId: number, ext: Uint8Array): void {
    const b = bytes('ext', ext);
    this.#core.call(this.#core, 'zxv_hw_profile_ext_set', this._ptr(), u32('nodeId', nodeId), b, b.length);
  }

  /** The node's private extension (empty if none). */
  getExt(nodeId: number): Uint8Array {
    const p = this._ptr();
    const id = u32('nodeId', nodeId);
    return this.#core.bytes(this.#core, 'zxv_hw_profile_ext_get', (b, c, l) => [p, id, b, c, l]);
  }

  /** The number of nodes. */
  get count(): number {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_hw_profile_count', this._ptr(), n);
    return n[0] ?? 0;
  }

  /** Node `index` (in id order). */
  node(index: number): ProfileNode {
    const u = new Uint32Array(4);
    this.#core.call(this.#core, 'zxv_hw_profile_node', this._ptr(), u32('index', index), u.subarray(0, 1), u.subarray(1, 2),
      u.subarray(2, 3), u.subarray(3, 4));
    return Object.freeze({ id: u[0] ?? 0, parent: u[1] ?? 0, cls: u[2] ?? 0, ncaps: u[3] ?? 0 });
  }

  /** Every node, in id order. */
  nodes(): ProfileNode[] {
    return Array.from({ length: this.count }, (_, k) => this.node(k));
  }

  protected destroy(): void {
    this.#core.b.call('zxv_hw_profile_destroy', this.#ptr);
  }
}

// ------------------------------------------------------------------ matrix

/** What `zxv_hw_function_info` reports. */
export interface FunctionInfo {
  /** The current contract (golden-vector) version. */
  readonly contract: number;
  /** {@link HwPolicy} flags. */
  readonly policy: number;
  readonly vectors: number;
}

/** One implementation, as discovery reports it. */
export interface ProviderInfo {
  readonly index: number;
  readonly fnId: number;
  readonly kind: HwKind;
  /** TRUE serves; FALSE failed a vector; NEITHER unproven or no vectors; BOTH disputed. */
  readonly proof: Truth;
  /** The contract version it last proved. */
  readonly contract: number;
  /** {@link HwProviderFlag} bits. */
  readonly flags: number;
  readonly throughputClass: number;
  /** An {@link HwPower} value. */
  readonly powerClass: number;
  readonly calls: bigint;
  readonly costSpent: bigint;
}

/** One audit record (a region's or a matrix's). */
export interface HwLogEntry {
  /** An {@link HwEvent} value. */
  readonly event: number;
  /** The failure code (0 for none). */
  readonly reason: number;
  /** The region id (the function id for {@link HwEvent.CONTRACT}). */
  readonly id: number;
  readonly version: number;
  readonly tag: Hexagram;
  /** SHA-256 of the package header. */
  readonly subject: Uint8Array;
  /** The record's chain hash. */
  readonly hash: Uint8Array;
}

function logEntry(call: (index: number, ev: Int32Array, u: Uint32Array[], subj: Uint8Array, h: Uint8Array) => void, index: number): HwLogEntry {
  const ev = new Int32Array(1);
  const u = [new Uint32Array(1), new Uint32Array(1), new Uint32Array(1), new Uint32Array(1)];
  const subject = new Uint8Array(32);
  const hash = new Uint8Array(32);
  call(u32('index', index), ev, u, subject, hash);
  return Object.freeze({
    event: ev[0] ?? 0, reason: u[0]![0] ?? 0, id: u[1]![0] ?? 0, version: u[2]![0] ?? 0,
    tag: Hexagram.fromTag(u[3]![0] ?? 0), subject, hash,
  });
}

/** Options of {@link HwMatrix.setPolicy}. */
export interface PolicyOptions {
  /** {@link HwPolicy} flags. */
  flags?: number;
  /** Latency ceiling (0: none). */
  maxLatencyNs?: number;
  /** Power budget (0: none). */
  powerBudgetMw?: number;
}

/**
 * The emulation matrix (`zxv_hw_matrix`): functions x implementations.
 * Every call needs {@link Cap.HW_MATRIX}; {@link HwMatrix.trustKey} and
 * {@link HwMatrix.applyContract} need {@link Cap.HW_RECONF}. A new matrix
 * holds {@link HW_RES_HOST} and the standard engines ({@link HwFn}), each
 * served by its proven SOFT implementation.
 */
export class HwMatrix extends Resource {
  /** The capability every call presents. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  #ptr: Ptr;
  readonly #ids: number[] = [];

  /** @internal Use {@link HwMatrix.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A new matrix in `cap`'s context. */
  static create(cap: Capability): HwMatrix {
    const c = cap.context;
    install(c._core);
    return new HwMatrix(cap, newHandle(c._core, c._state, 'zxv_hw_matrix_create_cap', cap._ptr()));
  }

  /** @internal */
  _ptr(): Ptr {
    this.live();
    return this.#ptr;
  }

  #call(fn: string, ...args: Arg[]): void {
    const cp = this.cap._ptr();
    const p = this._ptr();
    guarded(() => this.#ctx._core.call(this.#ctx._state, fn, cp, p, ...args));
  }

  /** Declare a function (`name`: UTF-8, at most 23 bytes). */
  addFunction(fnId: number, name?: string): void {
    this.#call('zxv_hw_function_add', u32('fnId', fnId), name === undefined ? null : cstr(name));
  }

  /** Add a golden vector to the current contract; every implementation of the function becomes unproven. */
  addVector(fnId: number, op: number, input: Uint8Array, expect: Uint8Array): void {
    const i = bytes('input', input);
    const e = bytes('expect', expect);
    this.#call('zxv_hw_vector_add', u32('fnId', fnId), u32('op', op), i, i.length, e, e.length);
  }

  /** Start contract `version` (strictly newer, else ZXV_E_ROLLBACK); clears the vectors. */
  setContract(fnId: number, version: number): void {
    this.#call('zxv_hw_function_contract', u32('fnId', fnId), u32('version', version));
  }

  /** Placement constraints. Clearing KEY_BEARING once set: ZXV_E_POLICY. */
  setPolicy(fnId: number, options: PolicyOptions = {}): void {
    this.#call('zxv_hw_policy_set', u32('fnId', fnId), u32('flags', options.flags ?? 0),
      u32('maxLatencyNs', options.maxLatencyNs ?? 0), u32('powerBudgetMw', options.powerBudgetMw ?? 0));
  }

  /** Contract version, policy and vector count of a function. */
  functionInfo(fnId: number): FunctionInfo {
    const u = new Uint32Array(3);
    this.#call('zxv_hw_function_info', u32('fnId', fnId), u.subarray(0, 1), u.subarray(1, 2), u.subarray(2, 3));
    return Object.freeze({ contract: u[0] ?? 0, policy: u[1] ?? 0, vectors: u[2] ?? 0 });
  }

  addResource(resId: number): void {
    this.#call('zxv_hw_resource_add', u32('resId', resId));
  }

  setResource(resId: number, state: HwResourceState): void {
    this.#call('zxv_hw_resource_set', u32('resId', resId), state);
  }

  /**
   * Add a JS implementation; returns its index. It serves only after
   * {@link HwMatrix.attest}. It may call {@link HwMatrix.call} on this
   * matrix for functions declared with {@link HwMatrix.addDependency}.
   */
  addProvider(fnId: number, resId: number, kind: HwKind, cost: number, impl: Implementation): number {
    if (typeof impl !== 'function') throw usage('impl must be a function');
    const id = register({ impl });
    const out = new Uint32Array(1);
    try {
      const cp = this.cap._ptr();
      const p = this._ptr();
      guarded(() => this.#ctx._core.call(this.#ctx._state, 'zxvjs_hw_provider_add', cp, p, u32('fnId', fnId), u32('resId', resId),
        kind, u32('cost', cost), id, out));
    } catch (e) {
      registry.delete(id);
      throw e;
    }
    this.#ids.push(id);
    return out[0] ?? 0;
  }

  /** Declared traits used by placement and discovery (throughput class 0..15). */
  setTraits(provider: number, latencyNs: number, powerMw: number, throughputClass: number): void {
    this.#call('zxv_hw_provider_traits', u32('provider', provider), u32('latencyNs', latencyNs), u32('powerMw', powerMw),
      u32('throughputClass', throughputClass));
  }

  /** Let `provider` call function `depFnId` of this matrix. */
  addDependency(provider: number, depFnId: number): void {
    this.#call('zxv_hw_provider_dep', u32('provider', provider), u32('depFnId', depFnId));
  }

  enable(provider: number, on: boolean): void {
    this.#call('zxv_hw_provider_enable', u32('provider', provider), on ? 1 : 0);
  }

  /** Run the golden vectors: TRUE (serves), FALSE (a vector failed) or NEITHER (no vectors). */
  attest(provider: number): Truth {
    const lp = new Int32Array(1);
    this.#call('zxv_hw_attest', u32('provider', provider), lp);
    return Truth.of(lp[0] ?? 0);
  }

  /** The number of implementations (discovery indexes 0 .. count-1). */
  get providerCount(): number {
    const n = new Uint32Array(1);
    this.#call('zxv_hw_provider_count', n);
    return n[0] ?? 0;
  }

  /** Discovery: one implementation. */
  providerInfo(provider: number): ProviderInfo {
    const i = u32('provider', provider);
    const u = new Uint32Array(5);
    const s = new Int32Array(2);
    const big = new BigUint64Array(2);
    this.#call('zxv_hw_provider_info', i, u.subarray(0, 1), s.subarray(0, 1), s.subarray(1, 2), u.subarray(1, 2), u.subarray(2, 3),
      u.subarray(3, 4), u.subarray(4, 5), big.subarray(0, 1), big.subarray(1, 2));
    return Object.freeze({
      index: i, fnId: u[0] ?? 0, kind: (s[0] ?? 0) as HwKind, proof: Truth.of(s[1] ?? 0), contract: u[1] ?? 0, flags: u[2] ?? 0,
      throughputClass: u[3] ?? 0, powerClass: u[4] ?? 0, calls: big[0] ?? 0n, costSpent: big[1] ?? 0n,
    });
  }

  /** Discovery: every implementation, in index order. */
  providers(): ProviderInfo[] {
    return Array.from({ length: this.providerCount }, (_, k) => this.providerInfo(k));
  }

  /** The implementation serving `fnId` now (ZXV_E_NOPROVIDER if none). */
  bound(fnId: number): number {
    const n = new Uint32Array(1);
    this.#call('zxv_hw_bound', u32('fnId', fnId), n);
    return n[0] ?? 0;
  }

  #out(fn: string, fnId: number, input: Uint8Array, op: number, maxOut: number, extra: Int32Array[]): Uint8Array {
    const i = bytes('input', input);
    const buf = new Uint8Array(u32('maxOut', maxOut) || 1);
    const len = this.#ctx._core.b.sizeSlot();
    try {
      this.#call(fn, u32('fnId', fnId), u32('op', op), i, i.length, buf, buf.length, len, ...extra);
      return buf.slice(0, this.#ctx._core.b.readSize(len));
    } finally {
      buf.fill(0); // may hold key-bearing output
    }
  }

  /** Run `fnId` on its best proven, policy-allowed implementation. */
  call(fnId: number, input: Uint8Array, options: { op?: number; maxOut?: number } = {}): Uint8Array {
    return this.#out('zxv_hw_call', fnId, input, options.op ?? 0, options.maxOut ?? 4096, []);
  }

  /** {@link HwMatrix.call} that also runs the next-best implementation and compares (ZXV_E_DISPUTE on a mismatch). */
  shadow(fnId: number, input: Uint8Array, options: { op?: number; maxOut?: number } = {}): { output: Uint8Array; checked: boolean } {
    const checked = new Int32Array(1);
    const output = this.#out('zxv_hw_shadow', fnId, input, options.op ?? 0, options.maxOut ?? 4096, [checked]);
    return { output, checked: (checked[0] ?? 0) !== 0 };
  }

  /**
   * Declare the function pure with a memo of `entries` (power of two <=
   * 65536; 0 removes; the current size only queries). Returns hits and misses so far.
   */
  memo(fnId: number, entries: number): { hits: bigint; misses: bigint } {
    const b = new BigUint64Array(2);
    this.#call('zxv_hw_memo', u32('fnId', fnId), u32('entries', entries), b.subarray(0, 1), b.subarray(1, 2));
    return { hits: b[0] ?? 0n, misses: b[1] ?? 0n };
  }

  /** Trust a release key for contract packages (at most 4). Needs {@link Cap.HW_RECONF}. */
  trustKey(publicKey: Uint8Array): void {
    this.#call('zxv_hw_matrix_trust_key', bytes('publicKey', publicKey, HW_PK_BYTES));
  }

  /** Apply a signed VECTORS package; returns how many implementations prove the new contract. */
  applyContract(pkg: Uint8Array): number {
    const b = bytes('pkg', pkg);
    const n = new Uint32Array(1);
    this.#call('zxv_hw_contract_apply', b, b.length, n);
    return n[0] ?? 0;
  }

  /** Head and length of the matrix audit log (contract changes). */
  log(): { head: Uint8Array; count: number } {
    const head = new Uint8Array(32);
    const n = new Uint32Array(1);
    this.#call('zxv_hw_matrix_log', head, n);
    return { head, count: n[0] ?? 0 };
  }

  /** Record `index` (0 = oldest) of the matrix audit log. */
  logEntry(index: number): HwLogEntry {
    return logEntry((i, ev, u, s, h) => this.#call('zxv_hw_matrix_log_entry', i, ev, u[0]!, u[1]!, u[2]!, u[3]!, s, h), index);
  }

  protected destroy(): void {
    const p = this.#ptr;
    const ids = this.#ids.splice(0);
    this.#ctx._state.release(() => {
      this.#ctx._core.b.call('zxv_hw_matrix_destroy', p);
      for (const id of ids) registry.delete(id);
    });
  }
}

// ------------------------------------------------------------------ packages

/**
 * A release key pair: {@link HwReleaseKey.publicKey} and two off-heap
 * secrets wiped (zxv_secure_zero) on close. Every package is signed with
 * ML-DSA-87 AND SLH-DSA-SHAKE-256s; both must verify.
 */
export class HwReleaseKey implements Disposable {
  readonly publicKey: Uint8Array;
  readonly mldsaSecret: SecretKey;
  readonly slhdsaSecret: SecretKey;

  private constructor(publicKey: Uint8Array, mldsa: SecretKey, slhdsa: SecretKey) {
    this.publicKey = publicKey;
    this.mldsaSecret = mldsa;
    this.slhdsaSecret = slhdsa;
  }

  /** A fresh release key pair (needs {@link Cap.CRYPTO_SIGN}). */
  static generate(cap: Capability): HwReleaseKey {
    const c = cap.context;
    const pk = new Uint8Array(HW_PK_BYTES);
    const sk = new SecretKey(c._core, HW_MLDSA_SK_BYTES);
    const ssk = new SecretKey(c._core, HW_SLHDSA_SK_BYTES);
    try {
      c._core.call(c._state, 'zxv_hw_keygen', cap._ptr(), pk, sk._ptr(), ssk._ptr());
    } catch (e) {
      sk.close();
      ssk.close();
      throw e;
    }
    return new HwReleaseKey(pk, sk, ssk);
  }

  /** Wrap existing key material (both secrets stay owned by the caller until closed here). */
  static fromParts(publicKey: Uint8Array, mldsa: SecretKey, slhdsa: SecretKey): HwReleaseKey {
    if (mldsa.length !== HW_MLDSA_SK_BYTES || slhdsa.length !== HW_SLHDSA_SK_BYTES) {
      throw usage('secret key lengths are 4896 (ML-DSA) and 128 (SLH-DSA)');
    }
    return new HwReleaseKey(bytes('publicKey', publicKey, HW_PK_BYTES).slice(), mldsa, slhdsa);
  }

  /**
   * Build and sign a package: header || payload || both signatures (needs
   * {@link Cap.CRYPTO_SIGN}). For an image, `fnId` is the function the
   * region will serve; for VECTORS, the function, `version` the contract
   * version and `payload` a vector list ({@link encodeVectors}). `svn` is
   * the anti-rollback floor. SLH-DSA signing takes seconds.
   */
  build(cap: Capability, kind: HwImageKind, options: {
    fnId: number; version: number; svn: number; payload: Uint8Array; targetClass?: number; targetDigest?: Uint8Array;
  }): Uint8Array {
    const c = cap.context;
    const p = bytes('payload', options.payload);
    const td = options.targetDigest === undefined ? null : bytes('targetDigest', options.targetDigest, 32);
    let size = HW_HEADER_BYTES + p.length + HW_SIG_BYTES;
    const len = c._core.b.sizeSlot();
    for (let attempt = 0; attempt < 2; attempt++) {
      const buf = new Uint8Array(size);
      const s = c._core.callAllowing(c._state, [Status.E_BUFFER], 'zxv_hw_image_build', cap._ptr(), kind,
        u32('targetClass', options.targetClass ?? 0), td, u32('fnId', options.fnId), u32('version', options.version),
        u32('svn', options.svn), p, p.length, this.mldsaSecret._ptr(), this.slhdsaSecret._ptr(), buf, buf.length, len);
      if (s === 0) return buf.subarray(0, c._core.b.readSize(len));
      size = c._core.b.readSize(len);
    }
    throw usage('zxv_hw_image_build: package kept growing', Status.E_BUFFER);
  }

  /** True once both secrets are wiped. */
  get closed(): boolean {
    return this.mldsaSecret.closed && this.slhdsaSecret.closed;
  }

  /** Wipe both secrets. Idempotent. */
  close(): void {
    this.mldsaSecret.close();
    this.slhdsaSecret.close();
  }

  [Symbol.dispose](): void {
    this.close();
  }
}

// ------------------------------------------------------------------ regions

/** One step of a region, delivered to {@link HwRegion.onEvent} listeners. */
export interface RegionEvent {
  readonly regionId: number;
  /** An {@link HwEvent} value. */
  readonly event: number;
  readonly reason: number;
  readonly version: number;
}

/** What {@link HwRegion.info} reports. */
export interface RegionInfo {
  /** An {@link HwRegionState} value. */
  readonly state: number;
  readonly version: number;
  /** The anti-rollback floor. */
  readonly floor: number;
  readonly quarantined: number;
}

/** Options of {@link HwRegion.create}. */
export interface RegionOptions {
  regionId: number;
  /** The hardware class images must name. */
  targetClass: number;
  /** The matrix holding the region's FABRIC or MICRO implementation, whose vectors test each image. */
  matrix?: HwMatrix;
  provider?: number;
  /** 32 bytes images must name (default zeros). */
  targetDigest?: Uint8Array;
  /** Audit log size (default 64); when it fills, the region halts and the context is poisoned. */
  logCapacity?: number;
}

/**
 * A reconfigurable region (`zxv_hw_region`) updated by dual-signed images:
 * stage, trial, commit (or rollback). Every step is an event and a record
 * in a hash-chained audit log. Needs {@link Cap.HW_RECONF}.
 */
export class HwRegion extends Resource {
  readonly cap: Capability;
  /** @internal */
  readonly _device: HwDevice;
  readonly #ctx: ZxvContext;
  readonly #id: number;
  readonly #listeners: ((e: RegionEvent) => void)[] = [];
  #ptr: Ptr | null = null;

  private constructor(cap: Capability, device: HwDevice, parent: Resource) {
    super(parent);
    this.cap = cap;
    this._device = device;
    this.#ctx = cap.context;
    this.#id = register({ impl: () => Status.E_STATE });
  }

  /** A region driven by `device`. */
  static create(cap: Capability, device: HwDevice, options: RegionOptions): HwRegion {
    for (const m of ['load', 'readback', 'activate'] as const) {
      if (typeof device?.[m] !== 'function') throw usage(`device must have a ${m}() method`);
    }
    const c = cap.context;
    install(c._core);
    const td = options.targetDigest === undefined ? null : bytes('targetDigest', options.targetDigest, 32);
    const mx = options.matrix ? options.matrix._ptr() : null;
    const provider = u32('provider', options.provider ?? 0);
    const regionId = u32('regionId', options.regionId);
    const targetClass = u32('targetClass', options.targetClass);
    const logCapacity = u32('logCapacity', options.logCapacity ?? 64);
    const cp = cap._ptr();
    const rg = new HwRegion(cap, device, options.matrix ?? cap);
    registry.set(rg.#id, { region: rg });
    const slot = c._core.b.handleSlot();
    try {
      guarded(() => c._core.call(c._state, 'zxvjs_hw_region_create', cp, mx, provider, regionId, targetClass, td, rg.#id,
        logCapacity, slot));
    } catch (e) {
      rg.close(); // zxv_hw_region_destroy(NULL) is a no-op
      throw e;
    }
    rg.#ptr = c._core.b.readHandle(slot);
    rg.#call('zxvjs_hw_region_events', rg.#id);
    return rg;
  }

  /** @internal */
  _dispatch(e: RegionEvent): void {
    for (const f of [...this.#listeners]) f(e);
  }

  #call(fn: string, ...args: Arg[]): void {
    this.live();
    const cp = this.cap._ptr();
    const p = this.#ptr ?? this.#ctx._core.b.NULL;
    guarded(() => this.#ctx._core.call(this.#ctx._state, fn, cp, p, ...args));
  }

  /** Call `listener` after each recorded step. */
  onEvent(listener: (e: RegionEvent) => void): void {
    if (typeof listener !== 'function') throw usage('listener must be a function');
    this.#listeners.push(listener);
  }

  /** Trust a release key (at most 4 per region). */
  trustKey(publicKey: Uint8Array): void {
    this.#call('zxv_hw_region_trust_key', bytes('publicKey', publicKey, HW_PK_BYTES));
  }

  /** Verify and load into the inactive slot. Rejections are recorded. */
  stage(image: Uint8Array): void {
    const b = bytes('image', image);
    this.#call('zxv_hw_region_stage', b, b.length);
  }

  /** Switch to the staged image and prove it; failing rolls back (ZXV_E_QUARANTINED). */
  trial(ticks: number): void {
    this.#call('zxv_hw_region_trial', u32('ticks', ticks));
  }

  /** Make the tested image permanent and raise the floor. */
  commit(): void {
    this.#call('zxv_hw_region_commit');
  }

  rollback(): void {
    this.#call('zxv_hw_region_rollback');
  }

  /** One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED). */
  tick(): void {
    this.#call('zxv_hw_region_tick');
  }

  info(): RegionInfo {
    const s = new Int32Array(1);
    const u = new Uint32Array(3);
    this.#call('zxv_hw_region_info', s, u.subarray(0, 1), u.subarray(1, 2), u.subarray(2, 3));
    return Object.freeze({ state: s[0] ?? 0, version: u[0] ?? 0, floor: u[1] ?? 0, quarantined: u[2] ?? 0 });
  }

  /** Head and length of the audit log (verified; a broken chain is fatal). */
  log(): { head: Uint8Array; count: number } {
    const head = new Uint8Array(32);
    const n = new Uint32Array(1);
    this.#call('zxv_hw_region_log', head, n);
    return { head, count: n[0] ?? 0 };
  }

  /** Record `index` (0 = oldest). */
  logEntry(index: number): HwLogEntry {
    return logEntry((i, ev, u, s, h) => this.#call('zxv_hw_region_log_entry', i, ev, u[0]!, u[1]!, u[2]!, u[3]!, s, h), index);
  }

  protected destroy(): void {
    const p = this.#ptr;
    const id = this.#id;
    this.#listeners.length = 0;
    this.#ctx._state.release(() => {
      if (p !== null) this.#ctx._core.b.call('zxv_hw_region_destroy', p);
      registry.delete(id);
    });
  }
}
