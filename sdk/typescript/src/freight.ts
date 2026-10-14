// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Freight packets (ABI 1.5, kernel/src/freight): up to 3360 payload bytes
// as 168 erasure-coded 21-byte rows (one UBH-168 frame each), sets of
// freights for larger payloads, and op streams (LITERAL, COPY, CIDREF,
// SEED) that a receiver expands under its own size limit. Pure: no
// context, no capability; the join and writer handles hold only their own
// memory.

import { Cid } from './cid.js';
import { Status, usage } from './errors.js';
import type { Arg, Backend, Ptr } from './internal/backend.js';
import { bytes, u16, u32, u64, u8 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Bytes per smart packet (one UBH-168 frame): the row index, then 20 data bytes. */
export const FREIGHT_PACKET_BYTES = 21;
/** Data bytes per row. */
export const FREIGHT_ROW_PAYLOAD = 20;
/** Rows (packets) per freight. */
export const FREIGHT_ROWS = 168;
/** Bytes of one encoded freight: 168 * 21. */
export const FREIGHT_BYTES = 3528;
/** Largest payload of one freight (k = 168, no loss tolerated). */
export const FREIGHT_MAX_PAYLOAD = 3360;
/** Bytes of a serialized freight header. */
export const FREIGHT_HEADER_BYTES = 64;
/** The header version this library writes. */
export const FREIGHT_VERSION = 1;
/** Most freights in one set. */
export const FREIGHT_MAX_SET = 16384;
/** Largest set payload: 16384 * 3360 bytes. */
export const FREIGHT_SET_MAX_BYTES = 55_050_240n;
/** Longest content id a CIDREF op carries. */
export const FREIGHT_CID_MAX = 128;
/** Longest SEED generator parameter list. */
export const FREIGHT_GEN_PARAM_MAX = 32;
/** Largest op stream or expansion handled here (64 MiB). */
export const FREIGHT_OPS_MAX = 67_108_864n;

/** Header flags (`ZXV_FREIGHT_F_*`). */
export const FreightFlag = Object.freeze({
  /** The (set's) payload is an op stream to expand. */
  OPSTREAM: 0x01,
} as const);

/** SEED generators (`ZXV_FREIGHT_GEN_*`) and the parameters each takes. */
export const FreightGen = Object.freeze({
  /** 8 bytes: a nonzero u64 LE seed; xorshift64* output bytes, little-endian. */
  XORSHIFT: 1,
  /** 1..32 bytes: the pattern, repeated. */
  REPEAT: 2,
  /** 2 distinct bytes a, b: the Fibonacci word abaababaab... */
  FIBWORD: 3,
  /** 3 bytes: log2 width w (1..15), on, off: byte i is `on` iff (x & y) == 0, x = i mod 2^w, y = (i >> w) mod 2^w. */
  SIERPINSKI: 4,
} as const);

/**
 * A 64-byte freight header, parsed. It travels separately from the
 * packets (in the outer message) and is neither protected by the erasure
 * code nor authenticated: sign it, or the outer message, if the sender
 * matters.
 */
export class FreightHeader {
  readonly #bytes: Uint8Array;
  /** Header version (1). */
  readonly version: number;
  /** Data rows, 1..168: any k distinct rows rebuild the payload; 168 - k rows may be lost. */
  readonly k: number;
  /** {@link FreightFlag} bits. */
  readonly flags: number;
  /** Payload bytes carried by this freight (<= k * 20). */
  readonly payloadLength: number;
  /** This freight's position in its set. */
  readonly seq: number;
  /** Freights in the set (1..16384). */
  readonly seqCount: number;
  /** The id every member of a set shares. */
  readonly freightId: bigint;
  /** For an op stream, the largest expansion the sender declares (0 otherwise). */
  readonly expandLimit: bigint;
  readonly #sha: Uint8Array;

  /** @internal Use {@link FreightCodec.parseHeader} or a building function. */
  constructor(core: Core, header: Uint8Array) {
    const h = bytes('header', header, FREIGHT_HEADER_BYTES).slice();
    const b8 = new Uint8Array(3);
    const u = new Uint16Array(3);
    const big = new BigUint64Array(2);
    const sha = new Uint8Array(32);
    core.call(core, 'zxv_freight_header_parse', h, b8.subarray(0, 1), b8.subarray(1, 2), b8.subarray(2, 3), u.subarray(0, 1),
      u.subarray(1, 2), u.subarray(2, 3), big.subarray(0, 1), big.subarray(1, 2), sha);
    this.#bytes = h;
    this.version = b8[0] ?? 0;
    this.k = b8[1] ?? 0;
    this.flags = b8[2] ?? 0;
    this.payloadLength = u[0] ?? 0;
    this.seq = u[1] ?? 0;
    this.seqCount = u[2] ?? 0;
    this.freightId = big[0] ?? 0n;
    this.expandLimit = big[1] ?? 0n;
    this.#sha = sha;
    Object.freeze(this);
  }

  /** SHA-256 of the payload (a copy). */
  get payloadSha256(): Uint8Array {
    return this.#sha.slice();
  }

  /** The serialized 64 bytes (a copy). */
  toBytes(): Uint8Array {
    return this.#bytes.slice();
  }

  /** @internal Not copied. */
  get _bytes(): Uint8Array {
    return this.#bytes;
  }
}

/** One member of a freight set ({@link FreightCodec.split}). */
export interface FreightMember {
  /** Its header. */
  readonly header: FreightHeader;
  /** Its 168 packets, rows in order ({@link FREIGHT_BYTES} bytes). */
  readonly packets: Uint8Array;
}

/** Options for building a freight or a set. */
export interface FreightOptions {
  /** The id every member of a set shares. */
  freightId: bigint;
  /** Data rows 1..168: payload per freight is k * 20 bytes, loss tolerance 168 - k rows. */
  k: number;
  /** {@link FreightFlag} bits (default 0). */
  flags?: number;
  /** With {@link FreightFlag.OPSTREAM} only: the largest expansion declared (default 0). */
  expandLimit?: bigint;
}

/** The fields of a header to serialize ({@link FreightCodec.serializeHeader}). */
export interface FreightHeaderFields {
  k: number;
  flags?: number;
  payloadLength: number;
  seq: number;
  seqCount: number;
  freightId: bigint;
  expandLimit?: bigint;
  /** SHA-256 of the payload (32 bytes). */
  payloadSha256: Uint8Array;
}

/** What {@link FreightJoin.status} reports. */
export interface FreightJoinStatus {
  /** Members accepted so far. */
  readonly received: number;
  /** Members in the set (0 until the first is accepted). */
  readonly count: number;
  /** True once every member arrived. */
  readonly complete: boolean;
  /** The joined length once complete, else 0. */
  readonly total: bigint;
}

/**
 * Supplies the bytes a CIDREF op names: exactly `length` bytes for `cid`,
 * or `undefined` when it cannot (the expansion then fails with
 * ZXV_E_NOT_FOUND). Called synchronously during {@link FreightCodec.expand};
 * it must not start another expansion.
 */
export type FreightResolve = (cid: Uint8Array, length: number) => Uint8Array | undefined;

/** Options of {@link FreightCodec.expand}. */
export interface FreightExpandOptions {
  /** The most bytes you accept (<= {@link FREIGHT_OPS_MAX}); a declared total above it is refused before anything is written. */
  limit: bigint;
  /** Resolves CIDREF ops; without one, a CIDREF fails (ZXV_E_NOT_FOUND). */
  resolve?: FreightResolve;
}

// ---- the CIDREF resolver bridge (one dispatcher per backend) ----

const resolvers = new Map<number, FreightResolve>();
let nextResolver = 1;
const installed = new WeakSet<Backend>();
/** Exceptions thrown by resolvers during the current expansion, re-thrown after it. */
let thrown: unknown[] = [];

function dispatch(id: number, cid: Uint8Array, len: number): Uint8Array | number {
  const r = resolvers.get(id);
  if (!r) return Status.E_NOT_FOUND;
  try {
    const out = r(cid, len);
    if (out === undefined) return Status.E_NOT_FOUND;
    if (!(out instanceof Uint8Array)) throw new TypeError('a freight resolver must return a Uint8Array or undefined');
    if (out.length !== len) throw new RangeError(`a freight resolver returned ${out.length} bytes; the CIDREF asks for ${len}`);
    return out;
  } catch (e) {
    thrown.push(e);
    return Status.E_NOT_FOUND;
  }
}

/**
 * The freight group of a loaded library, reached as `zxv.freight`. Every
 * function is pure.
 *
 * HONEST LIMITS: erasure coding only. A lost row is recovered; a CORRUPTED
 * row is detected (the payload SHA-256 fails, {@link VerificationFailedError})
 * but not located or repaired: drop rows you distrust and decode again.
 * Parity and row indices make a freight larger than its payload (3528 wire
 * bytes for at most 3360). The op stream promises no size: random data
 * encodes slightly LARGER than itself, COPY helps only data that repeats,
 * CIDREF only names bytes the receiver must already hold or fetch, and SEED
 * only reproduces the built-in generators (nothing searches for seeds). No
 * information is created.
 */
export class FreightCodec {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * The header of a single freight (seq 0 of 1) carrying `payload`; it hashes the payload.
   *
   * @throws a ZXV_E_TOO_LONG error when the payload is over k * 20 bytes; ZXV_E_ARG for a bad k, flags or expandLimit
   */
  header(payload: Uint8Array, options: FreightOptions): FreightHeader {
    const p = bytes('payload', payload);
    const h = new Uint8Array(FREIGHT_HEADER_BYTES);
    this.#core.call(this.#core, 'zxv_freight_header_init', u64('freightId', options.freightId), u8('k', options.k),
      u8('flags', options.flags ?? 0), u64('expandLimit', options.expandLimit ?? 0n), p, p.length, h);
    return new FreightHeader(this.#core, h);
  }

  /**
   * Serialize a header from its fields (for a set member, or a header
   * received field by field). The version is always {@link FREIGHT_VERSION}.
   *
   * @throws a ZXV_E_ARG error for any field that fails the header check
   */
  serializeHeader(fields: FreightHeaderFields): FreightHeader {
    const h = new Uint8Array(FREIGHT_HEADER_BYTES);
    this.#core.call(this.#core, 'zxv_freight_header_serialize', u8('k', fields.k), u8('flags', fields.flags ?? 0),
      u16('payloadLength', fields.payloadLength), u16('seq', fields.seq), u16('seqCount', fields.seqCount),
      u64('freightId', fields.freightId), u64('expandLimit', fields.expandLimit ?? 0n), bytes('payloadSha256', fields.payloadSha256, 32), h);
    return new FreightHeader(this.#core, h);
  }

  /**
   * Parse and check a serialized header.
   *
   * @throws a ZXV_E_FORMAT error for bad magic, nonzero reserved bytes or a field that fails the check
   */
  parseHeader(header: Uint8Array): FreightHeader {
    return new FreightHeader(this.#core, header);
  }

  /** True if the 64 bytes are a well-formed header (magic, version 1, k 1..168, known flags, consistent fields). */
  isValidHeader(header: Uint8Array): boolean {
    const h = bytes('header', header, FREIGHT_HEADER_BYTES);
    return this.#core.callAllowing(this.#core, [Status.E_FORMAT], 'zxv_freight_header_check', h) === 0;
  }

  /**
   * The 168 smart packets ({@link FREIGHT_BYTES} bytes, rows in order) of the freight `header` describes.
   *
   * @param payload - exactly the bytes the header was built for
   * @throws {@link VerificationFailedError} when the payload's hash differs; ZXV_E_ARG for a length mismatch
   */
  encode(header: FreightHeader, payload: Uint8Array): Uint8Array {
    const p = bytes('payload', payload);
    const out = new Uint8Array(FREIGHT_BYTES);
    this.#core.call(this.#core, 'zxv_freight_encode', header._bytes, p, p.length, out);
    return out;
  }

  /**
   * Rebuild the payload from whatever packets arrived: 21-byte packets in
   * any order; duplicates and row indices above 167 are ignored, and the
   * first k distinct rows are used.
   *
   * @param packets - a multiple of 21 bytes
   * @throws {@link ErasureError} (fewer than k distinct rows) or {@link VerificationFailedError}
   *   (a corrupted row, or a header from another freight)
   */
  decode(header: FreightHeader, packets: Uint8Array): Uint8Array {
    const k = bytes('packets', packets);
    const h = header._bytes;
    return this.#core.bytes(this.#core, 'zxv_freight_decode', (b, c, l) => [h, k, k.length, b, c, l]);
  }

  /**
   * Freights needed to carry `length` bytes at `k` (0 bytes still needs one).
   *
   * @throws a ZXV_E_TOO_LONG error when more than {@link FREIGHT_MAX_SET} would be needed
   */
  setCount(length: bigint, k: number): number {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_freight_set_count', u64('length', length), u8('k', k), n);
    return n[0] ?? 0;
  }

  /**
   * Member `seq` of the set that carries `data`: its header and packets.
   * Member `seq` carries bytes [seq * k * 20, ...).
   *
   * @throws a ZXV_E_RANGE error when seq is not below the set count
   */
  split(data: Uint8Array, options: FreightOptions, seq: number): FreightMember {
    const d = bytes('data', data);
    const h = new Uint8Array(FREIGHT_HEADER_BYTES);
    const p = new Uint8Array(FREIGHT_BYTES);
    this.#core.call(this.#core, 'zxv_freight_split', u64('freightId', options.freightId), u8('k', options.k), u8('flags', options.flags ?? 0),
      u64('expandLimit', options.expandLimit ?? 0n), d, d.length, u32('seq', seq), h, p);
    return Object.freeze({ header: new FreightHeader(this.#core, h), packets: p });
  }

  /** Every member of the set that carries `data`, in order (lazily, one freight at a time). */
  *members(data: Uint8Array, options: FreightOptions): Generator<FreightMember, void, undefined> {
    const n = this.setCount(BigInt(bytes('data', data).length), options.k);
    for (let s = 0; s < n; s++) yield this.split(data, options, s);
  }

  /**
   * A join that reassembles one set into a buffer of `capacity` bytes it allocates.
   *
   * @param capacity - 0..{@link FREIGHT_SET_MAX_BYTES}
   */
  join(capacity: bigint): FreightJoin {
    return new FreightJoin(this.#core, newHandle(this.#core, this.#core, 'zxv_freight_join_create', u64('capacity', capacity)));
  }

  /**
   * Read an op stream's declared total and whether it carries an output SHA-256, without expanding.
   *
   * @throws a ZXV_E_FORMAT error for a bad stream header
   */
  peek(ops: Uint8Array): { total: bigint; hasSha256: boolean } {
    const o = bytes('ops', ops);
    const t = new BigUint64Array(1);
    const h = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_freight_ops_peek', o, o.length, t, h);
    return { total: t[0] ?? 0n, hasSha256: (h[0] ?? 0) !== 0 };
  }

  /**
   * Expand an op stream. The declared total is checked against
   * `options.limit` before a byte is written, so a stream cannot expand past
   * what you allow.
   *
   * @throws a ZXV_E_TOO_LONG error (declared total above the limit), ZXV_E_FORMAT (malformed stream:
   *   bad varint, distance, generator or params, an op past the total, trailing bytes, too few bytes),
   *   ZXV_E_NOT_FOUND (a CIDREF was not resolved), {@link VerificationFailedError} (the output fails
   *   the stream's SHA-256). An exception thrown by `resolve` is re-thrown here.
   */
  expand(ops: Uint8Array, options: FreightExpandOptions): Uint8Array {
    const o = bytes('ops', ops);
    const limit = u64('limit', options.limit);
    const resolve = options.resolve;
    if (resolve === undefined) {
      return this.#core.bytes(this.#core, 'zxv_freight_ops_expand', (b, c, l) => [o, o.length, limit, null, null, b, c, l]);
    }
    if (typeof resolve !== 'function') throw usage('resolve must be a function');
    if (!installed.has(this.#core.b)) {
      this.#core.b.setFreightResolver(dispatch);
      installed.add(this.#core.b);
    }
    const id = nextResolver;
    nextResolver = (nextResolver % 0xfffffffe) + 1;
    resolvers.set(id, resolve);
    const outer = thrown;
    thrown = [];
    try {
      const out = this.#core.bytes(this.#core, 'zxvjs_freight_ops_expand', (b, c, l) => [o, o.length, limit, id, b, c, l]);
      if (thrown.length > 0) throw thrown[0];
      return out;
    } catch (e) {
      const first = thrown[0];
      if (first !== undefined && first !== e) {
        if (first instanceof Error && first.cause === undefined) {
          Object.defineProperty(first, 'cause', { value: e, configurable: true, writable: true });
        }
        throw first;
      }
      throw e;
    } finally {
      resolvers.delete(id);
      thrown = outer;
    }
  }

  /**
   * Encode `data` as a LITERAL/COPY op stream (greedy LZ77, window 65536,
   * deterministic). The result may be LARGER than the input.
   *
   * @param options - `withSha256: true` adds the output SHA-256, which expansion verifies
   */
  encodeOps(data: Uint8Array, options: { withSha256?: boolean } = {}): Uint8Array {
    const d = bytes('data', data);
    const ws = options.withSha256 ? 1 : 0;
    return this.#core.bytes(this.#core, 'zxv_freight_ops_encode', (b, c, l) => [d, d.length, ws, b, c, l]);
  }

  /** Worst-case encoded size of `length` input bytes (all literals). */
  opsBound(length: bigint, options: { withSha256?: boolean } = {}): bigint {
    const n = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_freight_ops_bound', u64('length', length), options.withSha256 ? 1 : 0, n);
    return n[0] ?? 0n;
  }

  /**
   * Run one SEED generator directly: its first `length` bytes.
   *
   * @throws a ZXV_E_ARG error for an unknown generator or parameters it refuses
   */
  generate(gen: number, params: Uint8Array, length: number): Uint8Array {
    const p = bytes('params', params);
    const out = new Uint8Array(u32('length', length));
    this.#core.call(this.#core, 'zxv_freight_gen_run', u8('gen', gen), p, p.length, out, out.length);
    return out;
  }

  /**
   * A writer that builds an op stream op by op.
   *
   * @param declaredTotal - what the ops must produce in all
   * @param options - `sha256` of that output (expansion then verifies it); `maxBytes` of the
   *   stream buffer (14..{@link FREIGHT_OPS_MAX}, default 65536)
   */
  opsWriter(declaredTotal: bigint, options: { sha256?: Uint8Array; maxBytes?: bigint } = {}): FreightOpsWriter {
    const sha = options.sha256 === undefined ? null : bytes('sha256', options.sha256, 32);
    return new FreightOpsWriter(this.#core, newHandle(this.#core, this.#core, 'zxv_freight_ops_writer_create',
      u64('declaredTotal', declaredTotal), sha, u64('maxBytes', options.maxBytes ?? 65536n)));
  }
}

/**
 * Reassembles one freight set (`zxv_freight_join`), member by member in
 * any order. The first member fixes the set (freight id, k, flags, count,
 * expand limit). Not thread-safe; release with {@link FreightJoin.close} or `using`.
 */
export class FreightJoin extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link FreightCodec.join}. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /**
   * Decode one member into its place. A member already received changes
   * nothing. A failed member is not recorded: add it again when more rows
   * arrive.
   *
   * @throws a ZXV_E_STATE error (another set, or a non-last member not full), ZXV_E_CAPACITY (the set
   *   cannot fit), {@link ErasureError}, {@link VerificationFailedError}
   */
  add(header: FreightHeader, packets: Uint8Array): void {
    const k = bytes('packets', packets);
    this.#core.call(this.#core, 'zxv_freight_join_add', this.#p(), header._bytes, k, k.length);
  }

  /** Progress so far. */
  status(): FreightJoinStatus {
    const u = new Uint32Array(2);
    const c = new Int32Array(1);
    const t = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_freight_join_status', this.#p(), u.subarray(0, 1), u.subarray(1, 2), c, t);
    return Object.freeze({ received: u[0] ?? 0, count: u[1] ?? 0, complete: (c[0] ?? 0) !== 0, total: t[0] ?? 0n });
  }

  /**
   * The joined bytes.
   *
   * @throws a ZXV_E_STATE error until every member arrived
   */
  result(): Uint8Array {
    const p = this.#p();
    return this.#core.bytes(this.#core, 'zxv_freight_join_result', (b, c, l) => [p, b, c, l]);
  }

  protected destroy(): void {
    this.#core.b.call('zxv_freight_join_destroy', this.#ptr);
  }
}

/**
 * Builds an op stream op by op (`zxv_freight_ops_writer`). Every op
 * produces at least one byte. The first error is sticky: every later op
 * and {@link FreightOpsWriter.finish} throw it again. Not thread-safe;
 * release with {@link FreightOpsWriter.close} or `using`.
 */
export class FreightOpsWriter extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link FreightCodec.opsWriter}. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  #put(fn: string, ...args: Arg[]): this {
    this.live();
    this.#core.call(this.#core, fn, this.#ptr, ...args);
    return this;
  }

  /**
   * LITERAL: these bytes as they are.
   *
   * @throws a ZXV_E_ARG error (empty, or more than is left of the declared total); ZXV_E_CAPACITY (buffer full)
   */
  literal(data: Uint8Array): this {
    const d = bytes('data', data);
    return this.#put('zxv_freight_ops_put_literal', d, d.length);
  }

  /**
   * COPY: `length` bytes from `distance` bytes back in the output (an
   * LZ77-style back reference; the ranges may overlap).
   *
   * @throws a ZXV_E_ARG error for a distance of 0 or beyond what was produced
   */
  copy(distance: bigint, length: bigint): this {
    return this.#put('zxv_freight_ops_put_copy', u64('distance', distance), u64('length', length));
  }

  /**
   * CIDREF: `length` bytes the receiver resolves by content id (see {@link FreightExpandOptions.resolve}).
   *
   * @param cid - a {@link Cid} or any 1..128-byte content id
   */
  cidref(cid: Cid | Uint8Array, length: bigint): this {
    const c = cid instanceof Cid ? cid._bytes : bytes('cid', cid);
    return this.#put('zxv_freight_ops_put_cidref', c, c.length, u64('length', length));
  }

  /**
   * SEED: `length` bytes of a built-in generator ({@link FreightGen}).
   *
   * @throws a ZXV_E_ARG error for parameters the generator refuses
   */
  seed(gen: number, params: Uint8Array, length: bigint): this {
    const p = bytes('params', params);
    return this.#put('zxv_freight_ops_put_seed', u8('gen', gen), p, p.length, u64('length', length));
  }

  /**
   * The finished stream. The writer stays usable (finish again after more ops).
   *
   * @throws a ZXV_E_STATE error when the ops produce fewer bytes than declared, or the sticky error of an earlier op
   */
  finish(): Uint8Array {
    this.live();
    const p = this.#ptr;
    return this.#core.bytes(this.#core, 'zxv_freight_ops_writer_finish', (b, c, l) => [p, b, c, l]);
  }

  protected destroy(): void {
    this.#core.b.call('zxv_freight_ops_writer_destroy', this.#ptr);
  }
}
