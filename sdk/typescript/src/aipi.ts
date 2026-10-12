// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// AIPI (ABI 1.5, kernel/src/aipi): data and functions named by WHAT they
// are (a CID) instead of WHERE they live. Function, call and claim records
// (pure), and an offline, in-memory AIPI node (a handle in a context).
// Design and limits: docs/AIPI.md.

import { Cid, cidOut } from './cid.js';
import type { Capability, ZxvContext } from './context.js';
import { SecretKey } from './crypto.js';
import { Status, usage } from './errors.js';
import { type Arg, cstr, type Ptr } from './internal/backend.js';
import { bytes, str, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Longest export name: `[A-Za-z0-9_]{1,63}`. */
export const AIPI_NAME_MAX = 63;
/** Most bytes of a call input or a claim output. */
export const AIPI_IO_MAX = 1024;
/** Most bytes of a claim certificate. */
export const AIPI_CERT_MAX = 1024;
/** Largest QUORUM n. */
export const AIPI_QUORUM_MAX = 8;
/** Longest function record. */
export const AIPI_FN_MAX = 373;
/** Longest call record. */
export const AIPI_CALL_MAX = 1112;
/** Longest claim record. */
export const AIPI_CLAIM_MAX = 2170;
/** Largest blob one node stores (2 MiB). */
export const AIPI_BLOCK_MAX = 2_097_152;
/** Smallest node store, in bytes. */
export const AIPI_STORE_MIN = 4096;
/** Largest node store, in bytes (256 MiB). */
export const AIPI_STORE_MAX = 268_435_456;
/** Most blocks one node indexes. */
export const AIPI_BLOCKS_MAX = 65536;

/** What a function computes (`ZXV_AIPI_INTENT_*`); it picks the function CID's codec. */
export const AipiIntent = Object.freeze({
  /** A general function (`zxv-fn`). */
  FN: 0,
  /** Tensor inference. */
  TENSOR_INFER: 1,
  /** Ledger settlement. */
  SETTLE_LEDGER: 2,
  /** Swarm routing. */
  SWARM_ROUTE: 3,
} as const);

/** How a remote result is judged (`ZXV_AIPI_VERIFY_*`). */
export const AipiVerify = Object.freeze({
  /** A checker export judges (input, output). */
  CHECKER: 1,
  /** As CHECKER, with a certificate checked by a certifier export. */
  CERT: 2,
  /** k of n executors must agree byte for byte (needs DETERMINISTIC). */
  QUORUM: 3,
  /** Never accept a remote result. */
  LOCAL: 4,
} as const);

/** Whether a function may leave the node (`ZXV_AIPI_PRIV_*`). */
export const AipiPrivacy = Object.freeze({
  /** May be published and run anywhere. */
  PUBLIC: 0,
  /** Private or key-bearing: stored sealed and never leaves the node. */
  LOCAL: 1,
} as const);

/** Whether a function's output is a function of its input (`ZXV_AIPI_DET_*`). */
export const AipiDeterminism = Object.freeze({
  NONDETERMINISTIC: 0,
  DETERMINISTIC: 1,
} as const);

/**
 * The route a call would take (`ZXV_AIPI_ROUTE_*`). This offline library
 * only ever reports LOCAL or UNAVAILABLE; the others are a networked host's
 * routes, listed so the values are fixed.
 */
export const AipiRoute = Object.freeze({
  CACHE: 1,
  /** The code is held here. */
  LOCAL: 2,
  FETCH_LOCAL: 3,
  MESH: 4,
  /** The module (or contract) is not held here. */
  UNAVAILABLE: 5,
} as const);

/** The fields of a function record (see {@link AipiRecords.encodeFunction}). */
export interface AipiFunctionSpec {
  /** An {@link AipiIntent} value; it picks the function CID's codec. */
  intent: number;
  /** An {@link AipiVerify} value. */
  verify: number;
  /** An {@link AipiPrivacy} value (default PUBLIC). */
  privacy?: number;
  /** An {@link AipiDeterminism} value (default DETERMINISTIC). */
  determinism?: number;
  /** QUORUM only: 2 <= n <= 8 (0 otherwise, the default). */
  quorumN?: number;
  /** QUORUM only: n/2 < k <= n (0 otherwise, the default). */
  quorumK?: number;
  /** The most fuel one run may use (> 0). */
  fuelMax: bigint;
  /** CIDv1 raw sha2-256 of the Z-Wasm module bytes. */
  module: Cid;
  /** CIDv1 raw sha2-256 of the z-contract text, if any. */
  contract?: Cid;
  /** The export computing the result. */
  entry: string;
  /** CHECKER and CERT only: the checker export (differs from entry). */
  checker?: string;
  /** CERT only: the certifier export (differs from entry and checker). */
  certifier?: string;
}

/** A function record read back strictly ({@link AipiRecords.decodeFunction}). */
export interface AipiFunctionInfo {
  readonly intent: number;
  readonly verify: number;
  readonly privacy: number;
  readonly determinism: number;
  readonly quorumN: number;
  readonly quorumK: number;
  readonly fuelMax: bigint;
  readonly module: Cid;
  /** Undefined when the record names no contract. */
  readonly contract: Cid | undefined;
  readonly entry: string;
  /** "" when the verify mode has none. */
  readonly checker: string;
  /** "" when the verify mode has none. */
  readonly certifier: string;
}

/** A call record read back ({@link AipiRecords.decodeCall}). */
export interface AipiCall {
  /** The function being called. */
  readonly fn: Cid;
  /** The call input (a copy). */
  readonly input: Uint8Array;
}

/** A claim record read back ({@link AipiRecords.decodeClaim}). */
export interface AipiClaim {
  /** The call it answers. */
  readonly call: Cid;
  /** The executor's 32-byte node id. */
  readonly executor: Uint8Array;
  /** The claimed output. */
  readonly output: Uint8Array;
  /** The certificate (empty unless the function is CERT). */
  readonly cert: Uint8Array;
}

function optName(name: string, v: string | undefined): Uint8Array | null {
  return v === undefined ? null : cstr(str(name, v));
}

/**
 * AIPI record encoders and decoders of a loaded library, reached as
 * `zxv.aipi`. Pure: no context, no capability. Records are decoded
 * strictly (exact lengths, no trailing bytes), and a function record must
 * be the canonical encoding, so one function has exactly one CID.
 *
 * HONEST LIMITS: there is no network transport here (no mesh, IPFS peers,
 * gateway or libp2p) and no EXECUTE (running a function needs the Z-Wasm
 * sandbox, which is not part of libzxv). No money moves in this group.
 * Signing a claim is the caller's job.
 */
export class AipiRecords {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * The canonical function record. Every field is checked and none is masked.
   *
   * @throws a usage error (ZXV_E_ARG) for a field out of range or breaking a rule
   *   (QUORUM without DETERMINISTIC, checker equal to entry, fuel 0, a bad name), or
   *   {@link CidMismatchError} for a module/contract CID that is not CIDv1 raw sha2-256
   */
  encodeFunction(spec: AipiFunctionSpec): Uint8Array {
    const m = spec.module._bytes;
    const c = spec.contract ? spec.contract._bytes : null;
    const args: Arg[] = [
      u32('intent', spec.intent), u32('verify', spec.verify), u32('privacy', spec.privacy ?? AipiPrivacy.PUBLIC),
      u32('determinism', spec.determinism ?? AipiDeterminism.DETERMINISTIC), u32('quorumN', spec.quorumN ?? 0),
      u32('quorumK', spec.quorumK ?? 0), u64('fuelMax', spec.fuelMax), m, m.length, c, c ? c.length : 0,
      cstr(str('entry', spec.entry)), optName('checker', spec.checker), optName('certifier', spec.certifier),
    ];
    return this.#core.bytes(this.#core, 'zxv_aipi_fn_encode', (b, cap, l) => [...args, b, cap, l]);
  }

  /**
   * Check a function record strictly and read every field.
   *
   * @throws a ZXV_E_FORMAT error when the bytes are not a valid, canonical function record
   */
  decodeFunction(record: Uint8Array): AipiFunctionInfo {
    const r = bytes('record', record);
    const u = new Uint32Array(6);
    const fuel = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_aipi_fn_decode', r, r.length, u.subarray(0, 1), u.subarray(1, 2), u.subarray(2, 3),
      u.subarray(3, 4), u.subarray(4, 5), u.subarray(5, 6), fuel);
    const getCid = (field: number): Cid | undefined => {
      const out = new Uint8Array(80);
      const len = this.#core.b.sizeSlot();
      const s = this.#core.callAllowing(this.#core, [Status.E_NOT_FOUND], 'zxv_aipi_fn_get_cid', r, r.length, field, out, len);
      return s === 0 ? new Cid(this.#core, out.slice(0, this.#core.b.readSize(len))) : undefined;
    };
    const getName = (field: number): string =>
      this.#core.text(this.#core, 'zxv_aipi_fn_get_name', (b, c, l) => [r, r.length, field, b, c, l]);
    const module = getCid(0);
    if (!module) throw usage('function record has no module CID', Status.E_FORMAT);
    return Object.freeze({
      intent: u[0] ?? 0, verify: u[1] ?? 0, privacy: u[2] ?? 0, determinism: u[3] ?? 0, quorumN: u[4] ?? 0,
      quorumK: u[5] ?? 0, fuelMax: fuel[0] ?? 0n, module, contract: getCid(1), entry: getName(0), checker: getName(1),
      certifier: getName(2),
    });
  }

  /**
   * The function CID of a record: codec {@link CidCodec.AIPI_FN} + intent,
   * sha2-256 of the record bytes.
   *
   * @throws a ZXV_E_FORMAT error when the bytes are not a valid function record
   */
  functionCid(record: Uint8Array): Cid {
    const r = bytes('record', record);
    return new Cid(this.#core, cidOut(this.#core, 'zxv_aipi_fn_cid', [r, r.length]));
  }

  /**
   * The call record naming one invocation: function CID + input.
   *
   * @param input - at most {@link AIPI_IO_MAX} bytes
   * @throws {@link CidMismatchError} unless `fn` is a CIDv1 sha2-256 function CID; ZXV_E_TOO_LONG for a long input
   */
  encodeCall(fn: Cid, input: Uint8Array = new Uint8Array(0)): Uint8Array {
    const f = fn._bytes;
    const i = bytes('input', input);
    return this.#core.bytes(this.#core, 'zxv_aipi_call_encode', (b, c, l) => [f, f.length, i, i.length, b, c, l]);
  }

  /**
   * Check a call record strictly and read its parts.
   *
   * @throws a ZXV_E_FORMAT error for anything but a valid call record
   */
  decodeCall(record: Uint8Array): AipiCall {
    const r = bytes('record', record);
    const s = [0, 1, 2, 3].map(() => this.#core.b.sizeSlot());
    this.#core.call(this.#core, 'zxv_aipi_call_decode', r, r.length, s[0]!, s[1]!, s[2]!, s[3]!);
    const at = (k: number) => this.#core.b.readSize(s[k]!);
    return Object.freeze({
      fn: new Cid(this.#core, r.slice(at(0), at(0) + at(1))),
      input: r.slice(at(2), at(2) + at(3)),
    });
  }

  /**
   * The call CID (codec {@link CidCodec.AIPI_CALL}) of (function, input):
   * sha2-256 of the call record. Same refusals as {@link AipiRecords.encodeCall}.
   */
  callCid(fn: Cid, input: Uint8Array = new Uint8Array(0)): Cid {
    const f = fn._bytes;
    const i = bytes('input', input);
    return new Cid(this.#core, cidOut(this.#core, 'zxv_aipi_call_cid', [f, f.length, i, i.length]));
  }

  /**
   * The claim record an executor signs: its `output` (and, for CERT
   * functions, `cert`) for the call `call`, with its 32-byte node id.
   * Signing it is the caller's job.
   *
   * @throws {@link CidMismatchError} unless `call` is a call CID; ZXV_E_TOO_LONG for output or cert over 1024 bytes
   */
  encodeClaim(call: Cid, executor: Uint8Array, output: Uint8Array, cert: Uint8Array = new Uint8Array(0)): Uint8Array {
    const c = call._bytes;
    const e = bytes('executor', executor, 32);
    const o = bytes('output', output);
    const ce = bytes('cert', cert);
    return this.#core.bytes(this.#core, 'zxv_aipi_claim_encode', (b, cap, l) => [c, c.length, e, o, o.length, ce, ce.length, b, cap, l]);
  }

  /**
   * Check a claim record strictly and read its parts.
   *
   * @throws a ZXV_E_FORMAT error for anything but a valid claim record
   */
  decodeClaim(record: Uint8Array): AipiClaim {
    const r = bytes('record', record);
    const s = Array.from({ length: 7 }, () => this.#core.b.sizeSlot());
    this.#core.call(this.#core, 'zxv_aipi_claim_decode', r, r.length, ...s);
    const at = (k: number) => this.#core.b.readSize(s[k]!);
    return Object.freeze({
      call: new Cid(this.#core, r.slice(at(0), at(0) + at(1))),
      executor: r.slice(at(2), at(2) + 32),
      output: r.slice(at(3), at(3) + at(4)),
      cert: r.slice(at(5), at(5) + at(6)),
    });
  }
}

/** Options of {@link AipiNode.create}. */
export interface AipiNodeOptions {
  /** Memory for blocks, {@link AIPI_STORE_MIN}..{@link AIPI_STORE_MAX} bytes (each block costs its length plus about 100). Default 1 MiB. */
  storeBytes?: number;
  /** Most blocks, 1..{@link AIPI_BLOCKS_MAX}. Default 1024. */
  maxBlocks?: number;
  /**
   * A 32-byte key that seals private blocks with ChaCha20-Poly1305 (copied
   * into the node and wiped when it closes). Without one, private puts are
   * refused. A {@link SecretKey} keeps the key off the JS heap.
   */
  nodeKey?: SecretKey | Uint8Array;
}

/** The audit counters of an {@link AipiNode}. */
export interface AipiNodeInfo {
  /** Blocks indexed. */
  readonly blocks: number;
  /** Store bytes in use. */
  readonly storeUsed: bigint;
  /** GETs whose bytes matched their CID. */
  readonly getsVerified: number;
  /** GETs whose stored bytes failed their hash (dropped). */
  readonly forbidden: number;
}

/**
 * An offline, in-memory AIPI node (`zxv_aipi`): a block store with pins
 * and the AIPI context over it. GET finds only what was put into THIS
 * node; there is no network. It belongs to the context of the capability
 * that created it and fails closed with it (ZXV_E_POISONED). Storing needs
 * {@link Cap.AIPI}; reads need only the handle.
 *
 * Internally serialized in C; in JS use it from one agent (main thread or
 * one Worker). Close it before its context (or `using`); closing wipes the
 * node key.
 */
export class AipiNode extends Resource {
  /** The capability stores present. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link AipiNode.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * A new node in `cap`'s context.
   *
   * @param cap - must hold {@link Cap.AIPI} (else {@link CapabilityDeniedError})
   */
  static create(cap: Capability, options: AipiNodeOptions = {}): AipiNode {
    const c = cap.context;
    const k = options.nodeKey;
    let key: Arg = null;
    if (k instanceof SecretKey) {
      if (k.length !== 32) throw usage('nodeKey must be 32 bytes');
      key = k._ptr();
    } else if (k !== undefined) {
      key = bytes('nodeKey', k, 32);
    }
    const p = newHandle(c._core, c._state, 'zxv_aipi_create', cap._ptr(), u32('storeBytes', options.storeBytes ?? 1 << 20),
      u32('maxBlocks', options.maxBlocks ?? 1024), key);
    return new AipiNode(cap, p);
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  #cid(fn: string, gated: boolean, ...args: Arg[]): Cid {
    const pre: Arg[] = gated ? [this.cap._ptr(), this.#p()] : [this.#p()];
    return new Cid(this.#ctx._core, cidOut(this.#ctx._core, fn, [...pre, ...args], this.#ctx._state));
  }

  /**
   * Store and pin `data` as one raw block; returns its CID. Putting the
   * same bytes again is a no-op. Making something public that was stored
   * private (or the reverse) is refused: it must be a deliberate act.
   *
   * @param data - at most {@link AIPI_BLOCK_MAX} bytes
   * @param options - `private: true` stores the block sealed and never announceable (needs a node key)
   * @throws {@link CapabilityDeniedError} without {@link Cap.AIPI}; ZXV_E_STATE for the visibility conflict
   *   or a private put without a node key; ZXV_E_CAPACITY when the store is full
   */
  put(data: Uint8Array, options: { private?: boolean } = {}): Cid {
    const d = bytes('data', data);
    return this.#cid('zxv_aipi_put', true, d, d.length, options.private ? 1 : 0);
  }

  /**
   * The bytes `cid` names, re-hashed before they are returned.
   *
   * @throws a ZXV_E_NOT_FOUND error when not held here (there is no network);
   *   {@link CidMismatchError} for a bad or identity CID, or stored bytes that no longer match
   */
  get(cid: Cid): Uint8Array {
    const c = cid._bytes;
    const p = this.#p();
    return this.#ctx._core.bytes(this.#ctx._state, 'zxv_aipi_get', (b, cap, l) => [p, c, c.length, b, cap, l]);
  }

  /**
   * Store a function record (checked strictly; sealed when its privacy is
   * LOCAL) and return its function CID. The module and contract need not be
   * held yet.
   *
   * @throws a ZXV_E_FORMAT error for a bad record; {@link CapabilityDeniedError} without {@link Cap.AIPI}
   */
  publishFunction(record: Uint8Array): Cid {
    const r = bytes('record', record);
    return this.#cid('zxv_aipi_publish_fn', true, r, r.length);
  }

  /**
   * GET a function record by function CID, verified by hash, decoded
   * strictly and its codec checked against its intent.
   *
   * @throws {@link CidMismatchError} (not a function CID, or codec and intent disagree); ZXV_E_NOT_FOUND
   */
  loadFunction(fn: Cid): Uint8Array {
    const c = fn._bytes;
    const p = this.#p();
    return this.#ctx._core.bytes(this.#ctx._state, 'zxv_aipi_load_fn', (b, cap, l) => [p, c, c.length, b, cap, l]);
  }

  /**
   * The route a call of `fn` on `input` would take now. Runs and changes
   * nothing: here it is {@link AipiRoute.LOCAL} when the module (and
   * contract) are held, else {@link AipiRoute.UNAVAILABLE}.
   *
   * @param options - `inputPrivate: true` marks the input private
   */
  plan(fn: Cid, input: Uint8Array = new Uint8Array(0), options: { inputPrivate?: boolean } = {}): number {
    const c = fn._bytes;
    const i = bytes('input', input);
    const r = new Int32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_aipi_plan', this.#p(), c, c.length, i, i.length, options.inputPrivate ? 1 : 0, r);
    return r[0] ?? 0;
  }

  /** The audit counters. */
  info(): AipiNodeInfo {
    const u = new Uint32Array(3);
    const used = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_aipi_info', this.#p(), u.subarray(0, 1), used, u.subarray(1, 2), u.subarray(2, 3));
    return Object.freeze({ blocks: u[0] ?? 0, storeUsed: used[0] ?? 0n, getsVerified: u[1] ?? 0, forbidden: u[2] ?? 0 });
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_aipi_destroy', p));
  }
}
