// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The Web 4 agent layer (ABI 1.5, kernel/src/web4): ML-DSA-65 agent keys
// held inside handles, signed agent cards, a directory of verified peers,
// signed tool manifests, human consent tokens and signed envelopes.

import type { Capability, ZxvContext } from './context.js';
import { SecretKey } from './crypto.js';
import { Status, usage, ZxvError } from './errors.js';
import { type Arg, cstr, fromUtf8, type Ptr } from './internal/backend.js';
import { bytes, i32, str, toHex, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** An ML-DSA-65 public key, in bytes. */
export const W4_PK_BYTES = 1952;
/** An ML-DSA-65 signature, in bytes. */
export const W4_SIG_BYTES = 3309;
/** An agent id, peer id or human id (SHA3-256 of a public key), in bytes. */
export const W4_ID_BYTES = 32;
/** Longest agent or tool name: 1..31 characters of [a-z0-9._-]. */
export const W4_NAME_MAX = 31;
/** Tools one manifest may list. */
export const W4_MAX_TOOLS = 16;
/** Largest envelope payload, in bytes. */
export const W4_MSG_MAX_PAYLOAD = 2048;
/** An encoded consent token, in bytes. */
export const W4_CONSENT_BYTES = 3453;
/** Largest agent card (a node-bound one; a self-bound card is 1952 bytes shorter). */
export const W4_CARD_MAX = 7259;
/** Largest encoded manifest. */
export const W4_MANIFEST_MAX = 4349;
/** Largest encoded envelope. */
export const W4_ENV_MAX = 8970;
/** Most entries a directory may hold. */
export const W4_DIR_MAX = 4096;
/** Most entries a consent log may hold. */
export const W4_CONSENT_LOG_MAX = 65536;

/** What a tool does (`ZXV_W4_TOOL_*`), as manifest tool flags. */
export const W4ToolFlag = Object.freeze({
  /** The tool moves money. */
  MONEY: 0x01,
  /** The tool reveals personal data. */
  PII: 0x02,
  /** The tool answers with STREAM chunks. */
  STREAM: 0x04,
} as const);

/** What a consent token allows (`ZXV_W4_CONSENT_*`). */
export const W4ConsentScope = Object.freeze({
  /** Moving money, up to the token's maximum. */
  MONEY: 0x01,
  /** Sharing personal data. */
  PII: 0x02,
} as const);

/** Envelope kinds (`ZXV_W4_MSG_*`). */
export const W4MessageKind = Object.freeze({
  REQUEST: 1,
  RESPONSE: 2,
  STREAM: 3,
  CANCEL: 4,
  ERROR: 5,
} as const);

/** Envelope flags (`ZXV_W4_MF_*`). */
export const W4MessageFlag = Object.freeze({
  /** The last STREAM chunk, or a final response. */
  FINAL: 0x01,
  /** A consent token is attached (set by sealing, never by the caller). */
  CONSENT: 0x02,
} as const);

/** What a verified agent card says ({@link Web4.verifyCard}). */
export interface W4CardInfo {
  /** SHA3-256 of the agent's public key. */
  readonly agentId: Uint8Array;
  /** SHA3-256 of the hosting node's public key (equal to agentId when self-bound). */
  readonly peerId: Uint8Array;
  /** The agent's name. */
  readonly name: string;
  /** When the binding was signed (Unix milliseconds). */
  readonly boundMs: bigint;
}

/** One tool of a manifest. */
export interface W4Tool {
  /** 1..31 characters of [a-z0-9._-]. */
  readonly name: string;
  /** Price per call in VFV minor units (2 digits). */
  readonly priceVfv: bigint;
  /** Calls per minute (>= 1). */
  readonly ratePerMin: number;
  /** Burst size (>= 1). */
  readonly burst: number;
  /** {@link W4ToolFlag} bits. */
  readonly flags: number;
  /** Largest input in bytes (<= {@link W4_MSG_MAX_PAYLOAD}). */
  readonly maxInput: number;
}

/** A manifest's header fields ({@link W4Manifest.info}). */
export interface W4ManifestInfo {
  /** The signer's agent id (zero until signed). */
  readonly agentId: Uint8Array;
  /** The signer's peer id (zero until signed). */
  readonly peerId: Uint8Array;
  readonly version: number;
  /** Valid from (Unix ms, inclusive). */
  readonly issuedMs: bigint;
  /** Valid until (Unix ms, exclusive). */
  readonly expiresMs: bigint;
  readonly toolCount: number;
  /** Whether a signature is held (it is not checked here). */
  readonly signed: boolean;
}

/** What a human consents to ({@link W4Agent.issueConsent}). */
export interface W4ConsentRequest {
  /** The agent being allowed to act. */
  readonly agentId: Uint8Array;
  /** The action digest ({@link Web4.actionDigest}) of the exact request. */
  readonly action: Uint8Array;
  /** {@link W4ConsentScope} bits (nonzero). */
  readonly scope: number;
  /** Most money the agent may move, in VFV minor units. */
  readonly maxVfv: bigint;
  /** Valid from (Unix ms, inclusive). */
  readonly issuedMs: bigint;
  /** Valid until (Unix ms, exclusive); must be above issuedMs. */
  readonly expiresMs: bigint;
  /** 16 bytes that make the token single-use; omit for a random nonce. */
  readonly nonce?: Uint8Array;
}

/** A consent token's fields, read without a signature check ({@link Web4.consentInfo}). */
export interface W4ConsentInfo {
  readonly humanId: Uint8Array;
  readonly agentId: Uint8Array;
  readonly action: Uint8Array;
  readonly scope: number;
  readonly maxVfv: bigint;
  readonly issuedMs: bigint;
  readonly expiresMs: bigint;
  readonly nonce: Uint8Array;
}

/** An envelope's fields, read without a signature check ({@link Web4.decodeEnvelope}). */
export interface W4Envelope {
  /** A {@link W4MessageKind} value. */
  readonly kind: number;
  /** {@link W4MessageFlag} bits. */
  readonly flags: number;
  readonly from: Uint8Array;
  readonly to: Uint8Array;
  /** The sender's sequence number (strictly increasing per sender). */
  readonly seq: bigint;
  /** The sender's timestamp (Unix ms). */
  readonly tsMs: bigint;
  readonly reqId: bigint;
  readonly chunk: number;
  readonly tool: string;
  readonly payloadLength: number;
}

/** Options for {@link W4Agent.seal}. */
export interface W4SealOptions {
  /** A {@link W4MessageKind} value. */
  readonly kind: number;
  /** The recipient's agent id. */
  readonly to: Uint8Array;
  readonly reqId: bigint;
  /** Chunk index (default 0). */
  readonly chunk?: number;
  /** A valid tool name. */
  readonly tool: string;
  /** 0 or {@link W4MessageFlag.FINAL} (default 0). */
  readonly flags?: number;
  /** Up to {@link W4_MSG_MAX_PAYLOAD} bytes (default empty). */
  readonly payload?: Uint8Array;
  /** A consent token to attach. */
  readonly consent?: Uint8Array;
  /** The sender's clock (Unix ms). */
  readonly nowMs: bigint;
}

/** Options for {@link W4Agent.reply}. */
export interface W4ReplyOptions {
  /** RESPONSE, STREAM or ERROR. */
  readonly kind: number;
  readonly chunk?: number;
  /** 0 or {@link W4MessageFlag.FINAL}. */
  readonly flags?: number;
  readonly payload?: Uint8Array;
  readonly nowMs: bigint;
}

/** Options for {@link W4Agent.create}. */
export interface W4AgentOptions {
  /** 1..31 characters of [a-z0-9._-]. */
  readonly name: string;
  /**
   * 32 bytes for a reproducible key (a {@link SecretKey} keeps it off the
   * JS heap), or omit for a fresh key from the OS.
   */
  readonly seed?: SecretKey | Uint8Array;
  /** The node hosting this agent (it signs the binding); omit for a self-bound agent. */
  readonly node?: W4Agent;
  /** The binding time (Unix ms). */
  readonly nowMs: bigint;
}

const NO_NUL = (b: Uint8Array): string => {
  const n = b.indexOf(0);
  return fromUtf8(b, n < 0 ? b.length : n);
};

const id32 = (name: string, v: unknown): Uint8Array => bytes(name, v, W4_ID_BYTES);

/**
 * An ML-DSA-65 key handle for an agent or a human (`zxv_w4_agent`). The
 * private key is generated and used inside the handle and wiped when it is
 * released; no call returns it, and this object never holds it. It belongs
 * to its capability's context and is released before it (R6). Not
 * thread-safe in C; JS is single-threaded per isolate.
 */
export class W4Agent extends Resource {
  /** The capability that created the handle; signing calls present it by default. */
  readonly cap: Capability;
  /** SHA3-256 of the public key. For a human key this is the human id. */
  readonly agentId: Uint8Array;
  /** SHA3-256 of the hosting node's key (equal to {@link agentId} when self-bound). */
  readonly peerId: Uint8Array;
  /** The ML-DSA-65 public key. */
  readonly publicKey: Uint8Array;
  readonly #ctx: ZxvContext;
  readonly #ptr: Ptr;

  /** @internal Use {@link W4Agent.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
    const id = new Uint8Array(32);
    const peer = new Uint8Array(32);
    const pk = new Uint8Array(W4_PK_BYTES);
    this.#ctx._core.call(this.#ctx._state, 'zxv_w4_agent_info', ptr, id, peer, pk);
    this.agentId = id;
    this.peerId = peer;
    this.publicKey = pk;
  }

  /**
   * A new key handle and its signed binding.
   *
   * @param cap - needs {@link Cap.CRYPTO_SIGN}; kept for later signing calls
   * @throws a ZXV_E_ARG / ZXV_E_TOO_LONG error for a bad name; ZXV_E_IO with no OS randomness
   */
  static create(cap: Capability, options: W4AgentOptions): W4Agent {
    const c = cap.context;
    let seed: Arg = null;
    const s = options.seed;
    if (s instanceof SecretKey) {
      if (s.length !== 32) throw usage('seed must be 32 bytes');
      seed = s._ptr();
    } else if (s !== undefined) {
      seed = bytes('seed', s, 32);
    }
    const node = options.node === undefined ? null : options.node._p();
    const p = newHandle(c._core, c._state, 'zxv_w4_agent_create', cap._ptr(), seed, cstr(str('name', options.name)), node,
      u64('nowMs', options.nowMs));
    return new W4Agent(cap, p);
  }

  /** @internal The handle (throws once released). */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  #bytes(fn: string, build: (b: Uint8Array | null, c: number, l: BigUint64Array | Uint32Array) => Arg[]): Uint8Array {
    this._p();
    return this.#ctx._core.bytes(this.#ctx._state, fn, build);
  }

  /** The agent's card: what a peer needs to verify it ({@link Web4.verifyCard}). */
  card(): Uint8Array {
    const p = this._p();
    return this.#bytes('zxv_w4_agent_card', (b, c, l) => [p, b, c, l]);
  }

  /**
   * Sign `manifest` with this agent's key; signing fixes its agent and
   * peer ids.
   *
   * @param cap - needs {@link Cap.CRYPTO_SIGN} in this handle's context (default: {@link cap})
   */
  signManifest(manifest: W4Manifest, cap: Capability = this.cap): void {
    const p = this._p();
    this.#ctx._core.call(this.#ctx._state, 'zxv_w4_manifest_sign', cap._ptr(), p, manifest._p());
  }

  /**
   * Issue a consent token on the human's side, after showing the human the
   * action. This handle is the human's key; the token is single-use.
   *
   * HONEST LIMITS: a token proves that a key holder signed. Whether that is
   * the right person and whether they understood what they signed depends
   * on the device and UI that showed the action, which this library cannot
   * see.
   *
   * @param cap - needs {@link Cap.ATTEST} in this handle's context (default: {@link cap})
   * @returns the encoded token ({@link W4_CONSENT_BYTES} bytes)
   */
  issueConsent(request: W4ConsentRequest, cap: Capability = this.cap): Uint8Array {
    const p = this._p();
    const out = new Uint8Array(W4_CONSENT_BYTES);
    const nonce = request.nonce === undefined ? null : bytes('nonce', request.nonce, 16);
    this.#ctx._core.call(this.#ctx._state, 'zxv_w4_consent_issue', cap._ptr(), p, id32('agentId', request.agentId),
      id32('action', request.action), u32('scope', request.scope), u64('maxVfv', request.maxVfv), u64('issuedMs', request.issuedMs),
      u64('expiresMs', request.expiresMs), nonce, out);
    return out;
  }

  /**
   * Build, sign and encode an envelope from this agent. Takes the handle's
   * next sequence number (none is used when the call is refused).
   * Envelopes are signed, not encrypted.
   *
   * @param cap - needs {@link Cap.CRYPTO_SIGN} in this handle's context (default: {@link cap})
   */
  seal(options: W4SealOptions, cap: Capability = this.cap): Uint8Array {
    const p = this._p();
    const to = id32('to', options.to);
    const pl = bytes('payload', options.payload ?? new Uint8Array(0));
    const consent = options.consent === undefined ? null : bytes('consent', options.consent, W4_CONSENT_BYTES);
    const tool = cstr(str('tool', options.tool));
    const kind = i32('kind', options.kind);
    return this.#bytes('zxv_w4_env_seal', (b, c, l) => [cap._ptr(), p, kind, to, u64('reqId', options.reqId),
      u32('chunk', options.chunk ?? 0), tool, u32('flags', options.flags ?? 0), pl, pl.length, consent,
      u64('nowMs', options.nowMs), b, c, l]);
  }

  /**
   * Seal a reply to a REQUEST envelope addressed to this agent (from and to
   * swapped, same request id and tool). The request's signature is not
   * checked here: call {@link Web4.verifyEnvelope} first.
   *
   * @param cap - needs {@link Cap.CRYPTO_SIGN} in this handle's context (default: {@link cap})
   */
  reply(request: Uint8Array, options: W4ReplyOptions, cap: Capability = this.cap): Uint8Array {
    const p = this._p();
    const rq = bytes('request', request);
    const pl = bytes('payload', options.payload ?? new Uint8Array(0));
    const kind = i32('kind', options.kind);
    return this.#bytes('zxv_w4_env_reply', (b, c, l) => [cap._ptr(), p, rq, rq.length, kind, u32('chunk', options.chunk ?? 0),
      u32('flags', options.flags ?? 0), pl, pl.length, u64('nowMs', options.nowMs), b, c, l]);
  }

  /** Never prints key material. */
  override toString(): string {
    return `W4Agent(${this.closed ? 'closed' : 'live'})`;
  }

  /** Never serializes key material. */
  toJSON(): unknown {
    return { agentId: toHex(this.agentId) };
  }

  /** Wipes the private key inside libzxv, then frees the handle. */
  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_w4_agent_destroy', p));
  }
}

/**
 * A directory of verified agent cards keyed by agent id (`zxv_w4_dir`).
 * Holds only its own memory: no context, no capability.
 */
export class W4Directory extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link Web4.directory}. */
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
   * Verify a card, then add it or replace the entry for the same agent.
   *
   * @throws a ZXV_E_FORMAT error (malformed), {@link VerificationFailedError}, ZXV_E_STATE (an older
   *   binding than the one held: no rollback), ZXV_E_CAPACITY (full)
   */
  add(card: Uint8Array): void {
    const c = bytes('card', card);
    this.#core.call(this.#core, 'zxv_w4_dir_add', this.#p(), c, c.length);
  }

  /** The card held for `agentId`, or undefined when none is. */
  find(agentId: Uint8Array): Uint8Array | undefined {
    const p = this.#p();
    const id = id32('agentId', agentId);
    try {
      return this.#core.bytes(this.#core, 'zxv_w4_dir_find', (b, c, l) => [p, id, b, c, l]);
    } catch (e) {
      if (e instanceof ZxvError && e.status === Status.E_NOT_FOUND) return undefined;
      throw e;
    }
  }

  /** Cards held. */
  get size(): number {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_w4_dir_count', this.#p(), n);
    return n[0] ?? 0;
  }

  protected destroy(): void {
    this.#core.b.call('zxv_w4_dir_destroy', this.#ptr);
  }
}

/**
 * A tool manifest (`zxv_w4_manifest`): up to 16 tools with prices in VFV
 * minor units, rate limits and flags, signed by an agent. Adding a tool
 * after signing drops the signature. Holds only its own memory.
 */
export class W4Manifest extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link Web4.manifest} or {@link Web4.decodeManifest}. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  /** @internal The handle (throws once released). */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /**
   * Add a tool.
   *
   * @throws a ZXV_E_ARG / ZXV_E_TOO_LONG error (bad name or limits), ZXV_E_DUPLICATE (name listed),
   *   ZXV_E_CAPACITY (16 tools)
   */
  addTool(tool: W4Tool): this {
    this.#core.call(this.#core, 'zxv_w4_manifest_add_tool', this._p(), cstr(str('name', tool.name)), u64('priceVfv', tool.priceVfv),
      u32('ratePerMin', tool.ratePerMin), u32('burst', tool.burst), u32('flags', tool.flags), u32('maxInput', tool.maxInput));
    return this;
  }

  /** Sign with `agent`'s key (see {@link W4Agent.signManifest}). */
  sign(agent: W4Agent, cap: Capability = agent.cap): this {
    agent.signManifest(this, cap);
    return this;
  }

  /**
   * Check that `publicKey` signed the manifest, that key is the manifest's
   * agent, and issuedMs <= nowMs < expiresMs.
   *
   * @throws {@link VerificationFailedError} (wrong key, bad or no signature); a ZXV_E_STATE error
   *   (a valid signature, but not valid at nowMs)
   */
  verify(publicKey: Uint8Array, nowMs: bigint): void {
    this.#core.call(this.#core, 'zxv_w4_manifest_verify', this._p(), bytes('publicKey', publicKey, W4_PK_BYTES), u64('nowMs', nowMs));
  }

  /**
   * The signed wire form.
   *
   * @throws a ZXV_E_STATE error if not signed
   */
  encode(): Uint8Array {
    const p = this._p();
    return this.#core.bytes(this.#core, 'zxv_w4_manifest_encode', (b, c, l) => [p, b, c, l]);
  }

  /** The Web 2 discovery face (for /.well-known/web4-agent.json); prices stay integer minor units. */
  toJson(): string {
    const p = this._p();
    return this.#core.text(this.#core, 'zxv_w4_manifest_to_json', (b, c, l) => [p, b, c, l]);
  }

  /** Header fields; ids are zero until signed. */
  info(): W4ManifestInfo {
    const a = new Uint8Array(32);
    const pe = new Uint8Array(32);
    const u = new Uint32Array(2);
    const t = new BigUint64Array(2);
    const sg = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_w4_manifest_info', this._p(), a, pe, u.subarray(0, 1), t.subarray(0, 1), t.subarray(1, 2),
      u.subarray(1, 2), sg);
    return Object.freeze({
      agentId: a, peerId: pe, version: u[0] ?? 0, issuedMs: t[0] ?? 0n, expiresMs: t[1] ?? 0n, toolCount: u[1] ?? 0,
      signed: (sg[0] ?? 0) !== 0,
    });
  }

  /**
   * Tool `index` (0-based).
   *
   * @throws a ZXV_E_NOT_FOUND error past the last tool
   */
  tool(index: number): W4Tool {
    const name = new Uint8Array(32);
    const price = new BigUint64Array(1);
    const u = new Uint32Array(4);
    this.#core.call(this.#core, 'zxv_w4_manifest_tool', this._p(), u32('index', index), name, price, u.subarray(0, 1),
      u.subarray(1, 2), u.subarray(2, 3), u.subarray(3, 4));
    return Object.freeze({
      name: NO_NUL(name), priceVfv: price[0] ?? 0n, ratePerMin: u[0] ?? 0, burst: u[1] ?? 0, flags: u[2] ?? 0, maxInput: u[3] ?? 0,
    });
  }

  /** Every tool, in order. */
  tools(): readonly W4Tool[] {
    const n = this.info().toolCount;
    return Object.freeze(Array.from({ length: n }, (_, i) => this.tool(i)));
  }

  /** The index of the tool named `name`, or undefined when it is not listed. */
  find(name: string): number | undefined {
    const i = new Uint32Array(1);
    const s = this.#core.callAllowing(this.#core, [Status.E_NOT_FOUND], 'zxv_w4_manifest_find', this._p(), cstr(str('name', name)), i);
    return s === 0 ? i[0] : undefined;
  }

  protected destroy(): void {
    this.#core.b.call('zxv_w4_manifest_destroy', this.#ptr);
  }
}

/**
 * A single-use log of consumed and revoked consent tokens
 * (`zxv_w4_consent_log`). When it is full even after pruning expired
 * entries, every new consent is refused (fail closed). Holds only its own
 * memory.
 */
export class W4ConsentLog extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link Web4.consentLog}. */
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
   * Check that `token` lets `agentId` do `action` with scope `need` for
   * `amount` VFV minor units (checked against the maximum when need has
   * MONEY), signed by the human whose key is `humanPublicKey`, valid at
   * nowMs; then record it as spent. Returning normally means consent was
   * given and is now used.
   *
   * @throws {@link ConsentDeniedError} (a mismatch, expired or not yet valid, a bad signature, or a
   *   token already used or revoked; the reason is in the error's detail); ZXV_E_FORMAT (malformed);
   *   ZXV_E_CAPACITY (the log is full: nothing is allowed)
   */
  check(token: Uint8Array, humanPublicKey: Uint8Array, agentId: Uint8Array, action: Uint8Array, need: number, amount: bigint,
    nowMs: bigint): void {
    this.#core.call(this.#core, 'zxv_w4_consent_check', this.#p(), bytes('token', token, W4_CONSENT_BYTES),
      bytes('humanPublicKey', humanPublicKey, W4_PK_BYTES), id32('agentId', agentId), id32('action', action), u32('need', need),
      u64('amount', amount), u64('nowMs', nowMs));
  }

  /** Revoke a token before use; later checks of it are refused. Revoking twice is fine. */
  revoke(token: Uint8Array, nowMs: bigint): void {
    this.#core.call(this.#core, 'zxv_w4_consent_revoke', this.#p(), bytes('token', token, W4_CONSENT_BYTES), u64('nowMs', nowMs));
  }

  /** Drop entries whose token expired at or before nowMs; returns how many remain. */
  prune(nowMs: bigint): number {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_w4_consent_log_prune', this.#p(), u64('nowMs', nowMs), n);
    return n[0] ?? 0;
  }

  protected destroy(): void {
    this.#core.b.call('zxv_w4_consent_log_destroy', this.#ptr);
  }
}

/**
 * The pure side of the Web 4 layer, reached as `zxv.web4`: card and
 * envelope checks, action digests, and the directory, manifest and
 * consent-log constructors. Key handles are {@link W4Agent}.
 *
 * HONEST LIMITS: envelopes are signed, not encrypted. Replay windows, rate
 * limits, carriage over freight and the Web 2 / Web 3 bridges are not part
 * of this layer. It interoperates between ZXV instances only; no other
 * agent protocol is implemented or claimed. ML-DSA-65 here is not FIPS
 * 140-3 validated.
 */
export class Web4 {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /**
   * Verify an agent card: well formed, with a binding signature valid
   * under the node key (or the agent's own key when self-bound).
   *
   * @throws a ZXV_E_FORMAT error (malformed); {@link VerificationFailedError} (bad signature)
   */
  verifyCard(card: Uint8Array): W4CardInfo {
    const c = bytes('card', card);
    const a = new Uint8Array(32);
    const p = new Uint8Array(32);
    const n = new Uint8Array(32);
    const t = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_w4_card_verify', c, c.length, a, p, n, t);
    return Object.freeze({ agentId: a, peerId: p, name: NO_NUL(n), boundMs: t[0] ?? 0n });
  }

  /**
   * The digest a consent token for one request must carry: SHA3-256 over
   * the tool, both parties, the request id and the payload.
   *
   * @throws a ZXV_E_ARG error (bad tool name); ZXV_E_TOO_LONG (payload over 2048 bytes)
   */
  actionDigest(tool: string, from: Uint8Array, to: Uint8Array, reqId: bigint, payload: Uint8Array = new Uint8Array(0)): Uint8Array {
    const pl = bytes('payload', payload);
    const d = new Uint8Array(32);
    this.#core.call(this.#core, 'zxv_w4_action_digest', cstr(str('tool', tool)), id32('from', from), id32('to', to),
      u64('reqId', reqId), pl, pl.length, d);
    return d;
  }

  /**
   * A consent token's fields, without checking its signature.
   *
   * @throws a ZXV_E_FORMAT error (malformed)
   */
  consentInfo(token: Uint8Array): W4ConsentInfo {
    const h = new Uint8Array(32);
    const a = new Uint8Array(32);
    const ac = new Uint8Array(32);
    const sc = new Uint32Array(1);
    const t = new BigUint64Array(3);
    const nonce = new Uint8Array(16);
    this.#core.call(this.#core, 'zxv_w4_consent_info', bytes('token', token, W4_CONSENT_BYTES), h, a, ac, sc, t.subarray(0, 1),
      t.subarray(1, 2), t.subarray(2, 3), nonce);
    return Object.freeze({
      humanId: h, agentId: a, action: ac, scope: sc[0] ?? 0, maxVfv: t[0] ?? 0n, issuedMs: t[1] ?? 0n, expiresMs: t[2] ?? 0n, nonce,
    });
  }

  /**
   * An envelope's fields, without checking its signature.
   *
   * @throws a ZXV_E_FORMAT error (malformed)
   */
  decodeEnvelope(envelope: Uint8Array): W4Envelope {
    const e = bytes('envelope', envelope);
    const k = new Int32Array(1);
    const u = new Uint32Array(3);
    const f = new Uint8Array(32);
    const to = new Uint8Array(32);
    const t = new BigUint64Array(3);
    const tool = new Uint8Array(32);
    this.#core.call(this.#core, 'zxv_w4_env_decode', e, e.length, k, u.subarray(0, 1), f, to, t.subarray(0, 1), t.subarray(1, 2),
      t.subarray(2, 3), u.subarray(1, 2), tool, u.subarray(2, 3));
    return Object.freeze({
      kind: k[0] ?? 0, flags: u[0] ?? 0, from: f, to, seq: t[0] ?? 0n, tsMs: t[1] ?? 0n, reqId: t[2] ?? 0n, chunk: u[1] ?? 0,
      tool: NO_NUL(tool), payloadLength: u[2] ?? 0,
    });
  }

  /** An envelope's payload (no signature check). */
  envelopePayload(envelope: Uint8Array): Uint8Array {
    const e = bytes('envelope', envelope);
    return this.#core.bytes(this.#core, 'zxv_w4_env_payload', (b, c, l) => [e, e.length, b, c, l]);
  }

  /** The consent token attached to an envelope, or undefined when none is. */
  envelopeConsent(envelope: Uint8Array): Uint8Array | undefined {
    const e = bytes('envelope', envelope);
    const t = new Uint8Array(W4_CONSENT_BYTES);
    const s = this.#core.callAllowing(this.#core, [Status.E_NOT_FOUND], 'zxv_w4_env_consent', e, e.length, t);
    return s === 0 ? t : undefined;
  }

  /**
   * Check that `senderPublicKey` is the envelope's sender and its
   * signature verifies. Replay, timestamp and rate checks are the
   * receiver's job.
   *
   * @throws {@link VerificationFailedError} otherwise
   */
  verifyEnvelope(envelope: Uint8Array, senderPublicKey: Uint8Array): void {
    const e = bytes('envelope', envelope);
    this.#core.call(this.#core, 'zxv_w4_env_verify', e, e.length, bytes('senderPublicKey', senderPublicKey, W4_PK_BYTES));
  }

  /**
   * An empty directory of verified peers.
   *
   * @param capacity - 1..{@link W4_DIR_MAX} (else a ZXV_E_RANGE error)
   */
  directory(capacity: number): W4Directory {
    return new W4Directory(this.#core, newHandle(this.#core, this.#core, 'zxv_w4_dir_create', u32('capacity', capacity)));
  }

  /**
   * An empty, unsigned manifest valid in [issuedMs, expiresMs).
   *
   * @throws a ZXV_E_ARG error when expiresMs <= issuedMs
   */
  manifest(version: number, issuedMs: bigint, expiresMs: bigint): W4Manifest {
    return new W4Manifest(this.#core, newHandle(this.#core, this.#core, 'zxv_w4_manifest_create', u32('version', version),
      u64('issuedMs', issuedMs), u64('expiresMs', expiresMs)));
  }

  /**
   * Parse a manifest's wire form. The signature is kept but NOT checked:
   * call {@link W4Manifest.verify}.
   *
   * @throws a ZXV_E_FORMAT error (malformed, trailing bytes or an invalid tool)
   */
  decodeManifest(encoded: Uint8Array): W4Manifest {
    const b = bytes('encoded', encoded);
    return new W4Manifest(this.#core, newHandle(this.#core, this.#core, 'zxv_w4_manifest_decode', b, b.length));
  }

  /**
   * An empty single-use consent log.
   *
   * @param capacity - 1..{@link W4_CONSENT_LOG_MAX} (else a ZXV_E_RANGE error)
   */
  consentLog(capacity: number): W4ConsentLog {
    return new W4ConsentLog(this.#core, newHandle(this.#core, this.#core, 'zxv_w4_consent_log_create', u32('capacity', capacity)));
  }
}
