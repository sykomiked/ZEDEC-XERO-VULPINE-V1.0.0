// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Content identifiers (ABI 1.5, kernel/src/ipfs_node): IPFS CIDv0 and
// CIDv1 in binary form, byte-for-byte the CIDs Kubo makes. Pure: no
// context, no capability.

import { CidMismatchError, Status, usage } from './errors.js';
import { type Arg, cstr } from './internal/backend.js';
import { bytes, str, u32 } from './internal/check.js';
import type { Core, Scope } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Longest binary CID accepted or produced (`ZXV_CID_MAX`). */
export const CID_MAX_BYTES = 80;
/** Longest CID text, excluding any terminator (`ZXV_CID_TEXT_MAX`). */
export const CID_TEXT_MAX = 139;
/** Longest multihash digest (an identity hash) (`ZXV_CID_DIGEST_MAX`). */
export const CID_DIGEST_MAX = 64;

/** Multicodec codes of the content a CID names (`ZXV_CID_CODEC_*` and the AIPI private-use codes). */
export const CidCodec = Object.freeze({
  /** Plain bytes. */
  RAW: 0x55,
  /** A UnixFS / dag-pb node (a file root above 256 KiB, or any CIDv0). */
  DAG_PB: 0x70,
  /** An AIPI function record (`zxv-fn`, intent 0); intents 1..3 add 1..3. */
  AIPI_FN: 0x3a1000,
  /** An AIPI tensor-inference function (intent 1). */
  AIPI_TENSOR_INFER: 0x3a1001,
  /** An AIPI ledger-settlement function (intent 2). */
  AIPI_SETTLE_LEDGER: 0x3a1002,
  /** An AIPI swarm-routing function (intent 3). */
  AIPI_SWARM_ROUTE: 0x3a1003,
  /** An AIPI call record (`zxv-aipi-call`). */
  AIPI_CALL: 0x3a10c0,
} as const);

/** Multihash codes (`ZXV_CID_HASH_*`). */
export const CidHash = Object.freeze({
  /** The digest is the content itself. */
  IDENTITY: 0x00,
  /** SHA-256. */
  SHA2_256: 0x12,
} as const);

/** Text forms of a CID (`ZXV_CID_BASE_*`). */
export const CidBase = Object.freeze({
  /** What Kubo prints: CIDv1 as "b..." base32, CIDv0 as "Qm..." base58. */
  DEFAULT: 0,
  /** "z" + base58btc, the AIPI form; CIDv1 only. */
  BASE58BTC: 1,
} as const);
/** A CidBase value. */
export type CidBase = (typeof CidBase)[keyof typeof CidBase];

/** @internal Call `fn(...args, out[80], outLen)` and return the CID bytes it wrote. */
export function cidOut(core: Core, fn: string, args: Arg[], scope: Scope = core): Uint8Array {
  const out = new Uint8Array(CID_MAX_BYTES);
  const len = core.b.sizeSlot();
  core.call(scope, fn, ...args, out, len);
  return out.slice(0, core.b.readSize(len));
}

/**
 * A content identifier: the binary form of one canonical CIDv0 or CIDv1
 * (sha2-256 or identity multihash). An immutable value; compare with
 * {@link Cid.equals}. `toString()` prints Kubo's text form.
 *
 * A CID proves WHICH bytes are meant. It does not make them available (a
 * CID resolves only while someone holds them), and for a function it does
 * not prove that a claimed output is right.
 */
export class Cid {
  readonly #core: Core;
  readonly #bytes: Uint8Array;
  /** 0 or 1. */
  readonly version: number;
  /** The multicodec of the content ({@link CidCodec}). */
  readonly codec: number;
  /** The multihash code ({@link CidHash}). */
  readonly hash: number;
  readonly #digest: Uint8Array;

  /** @internal Use {@link Cids.fromBytes}, {@link Cids.parse} or a computing function. */
  constructor(core: Core, cidBytes: Uint8Array) {
    const b = bytes('cid', cidBytes).slice();
    const u = new Uint32Array(3);
    const d = new Uint8Array(CID_DIGEST_MAX);
    const dl = core.b.sizeSlot();
    core.call(core, 'zxv_cid_info', b, b.length, u.subarray(0, 1), u.subarray(1, 2), u.subarray(2, 3), d, dl);
    this.#core = core;
    this.#bytes = b;
    this.version = u[0] ?? 0;
    this.codec = u[1] ?? 0;
    this.hash = u[2] ?? 0;
    this.#digest = d.slice(0, core.b.readSize(dl));
    Object.freeze(this);
  }

  /** A copy of the binary CID (1 to {@link CID_MAX_BYTES} bytes). */
  toBytes(): Uint8Array {
    return this.#bytes.slice();
  }

  /** The number of bytes of the binary CID. */
  get byteLength(): number {
    return this.#bytes.length;
  }

  /** A copy of the multihash digest (32 bytes for sha2-256). */
  get digest(): Uint8Array {
    return this.#digest.slice();
  }

  /** @internal The binary form, not copied (never hand it out). */
  get _bytes(): Uint8Array {
    return this.#bytes;
  }

  /**
   * The CID as text.
   *
   * @param base - {@link CidBase.DEFAULT} (Kubo's form) or {@link CidBase.BASE58BTC} ("z...", CIDv1 only)
   * @throws {@link CidMismatchError} for a CIDv0 in base58btc (it has no multibase form)
   */
  toString(base: CidBase = CidBase.DEFAULT): string {
    const b = this.#bytes;
    const base_ = u32('base', base);
    return this.#core.text(this.#core, 'zxv_cid_to_text', (buf, c, l) => [b, b.length, base_, buf, c, l]);
  }

  /** JSON form: the default text. */
  toJSON(): string {
    return this.toString();
  }

  /** True if both are the same binary CID. */
  equals(other: Cid): boolean {
    if (!(other instanceof Cid)) return false;
    const a = this.#bytes;
    const b = other.#bytes;
    return a.length === b.length && a.every((x, i) => x === b[i]);
  }

  /** The CIDv1 form (a CIDv0 becomes dag-pb CIDv1 with the same hash; a CIDv1 is returned as is). */
  toV1(): Cid {
    if (this.version === 1) return this;
    const b = this.#bytes;
    return new Cid(this.#core, cidOut(this.#core, 'zxv_cid_to_v1', [b, b.length]));
  }

  /** True if `data` is exactly the block this CID names (re-hashed; identity CIDs compare the bytes). */
  matches(data: Uint8Array): boolean {
    const d = bytes('data', data);
    const b = this.#bytes;
    return this.#core.callAllowing(this.#core, [Status.E_CID], 'zxv_cid_verify', b, b.length, d, d.length) === 0;
  }

  /**
   * Throw unless `data` is exactly the block this CID names. A UnixFS root
   * (dag-pb) names its root BLOCK, not the file: check a file with
   * {@link Cids.unixfs} and {@link Cid.equals}.
   *
   * @throws {@link CidMismatchError} when the bytes do not match
   */
  verify(data: Uint8Array): void {
    if (!this.matches(data)) throw new CidMismatchError(Status.E_CID, 'ZXV_E_CID', 'content does not match its CID');
  }

  /** The Kademlia key under which providers of this CID are announced: SHA-256("zxv/aipi/v1/provider" || CID). */
  providerKey(): Uint8Array {
    const k = new Uint8Array(32);
    const b = this.#bytes;
    this.#core.call(this.#core, 'zxv_aipi_provider_key', b, b.length, k);
    return k;
  }

  /** No implicit primitive value: use {@link Cid.toString} or {@link Cid.toBytes}. */
  valueOf(): never {
    throw new TypeError('Cid has no implicit primitive value: use .toString() or .toBytes()');
  }
}

/**
 * The CID group of a loaded library, reached as `zxv.cid`. Every function
 * is pure. The CIDs are byte for byte the ones Kubo 0.32.1 makes:
 * {@link Cids.compute} is `ipfs block put`, {@link Cids.unixfs} is
 * `ipfs add --cid-version=1`.
 */
export class Cids {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * The CIDv1 of `data` as one block: sha2-256 under `codec` (Kubo
   * `ipfs block put`).
   *
   * @param data - the block bytes
   * @param codec - the multicodec, {@link CidCodec.RAW} by default
   */
  compute(data: Uint8Array, codec: number = CidCodec.RAW): Cid {
    const d = bytes('data', data);
    return new Cid(this.#core, cidOut(this.#core, 'zxv_cid_compute', [u32('codec', codec), d, d.length]));
  }

  /**
   * The root CID Kubo gives `data` as a file (`ipfs add --cid-version=1`,
   * 256 KiB chunks, raw leaves, balanced DAG): a raw CID up to 262,144
   * bytes, a dag-pb root above. The blocks themselves are not kept.
   */
  unixfs(data: Uint8Array): Cid {
    const d = bytes('data', data);
    return new Cid(this.#core, cidOut(this.#core, 'zxv_cid_unixfs', [d, d.length]));
  }

  /**
   * Parse canonical CID text: "b..." (CIDv1 base32), "Qm..." (CIDv0) or
   * "z..." (CIDv1 base58btc). No upper case, padding or other multibases.
   *
   * @throws {@link CidMismatchError} (ZXV_E_CID) for anything else, or a usage error for "" or text over {@link CID_TEXT_MAX}
   */
  parse(text: string): Cid {
    return new Cid(this.#core, cidOut(this.#core, 'zxv_cid_from_text', [cstr(str('text', text))]));
  }

  /**
   * A CID from its binary form.
   *
   * @throws {@link CidMismatchError} unless the bytes are exactly one canonical CID
   */
  fromBytes(cid: Uint8Array): Cid {
    const b = bytes('cid', cid);
    if (b.length === 0 || b.length > CID_MAX_BYTES) throw usage(`a binary CID is 1..${CID_MAX_BYTES} bytes, got ${b.length}`);
    return new Cid(this.#core, b);
  }
}
