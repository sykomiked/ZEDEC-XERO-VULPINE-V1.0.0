// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The paradox engine (ABI 1.5, kernel/src/paradox): four kinds of paradox
// read from a hexagram tag, a fixed and logged operator per kind, and
// complex paradoxes as dependency graphs. Integers only.

import type { Capability, ZxvContext } from './context.js';
import { Status } from './errors.js';
import type { Arg, Ptr } from './internal/backend.js';
import { bytes, i32, i64, u32, u64, u8 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** The four kinds of paradox, plus NONE (`ZXV_PDX_KIND_*`). */
export const ParadoxKind = Object.freeze({
  /** Both, evidence margin growing: a bounded parameter update. */
  CONSTRUCTIVE: 0,
  /** Both, margin shrinking or stale: contain, quarantine, roll back. */
  DESTRUCTIVE: 1,
  /** Both and steady past the potential phase, or Neither: hold every branch. */
  NEUTRAL: 2,
  /** Both, steady, potential phase, no registry signature: the frontier. */
  SPECIAL: 3,
  /** True or False: not a paradox. */
  NONE: 4,
} as const);

/** What to do with a classified paradox (`ZXV_PDX_DO_*`, {@link Paradox.action}). */
export const ParadoxDo = Object.freeze({
  /** Not a paradox: use the hexagram action. */
  NONE: 0,
  /** Constructive: {@link ParadoxBook.constructive}. */
  SYNTHESIZE: 1,
  /** Destructive: {@link ParadoxBook.destructive}. */
  CONTAIN: 2,
  /** Neutral: {@link ParadoxBook.neutral}. */
  HOLD: 3,
  /** Special: {@link ParadoxBook.special} (sandbox). */
  RECORD: 4,
  /** Forbidden: refuse, roll back, quarantine. */
  ROLLBACK: 5,
} as const);

/** Operator outcomes (`ZXV_PDX_OUT_*`). REFUSED and PROPOSAL_FOR_OWNER are decisions, not errors. */
export const ParadoxOutcome = Object.freeze({
  APPLIED: 0,
  KEPT: 1,
  PROPOSAL_FOR_OWNER: 2,
  ROLLED_BACK: 3,
  CONTAINED: 4,
  DEFERRED: 5,
  SETTLED: 6,
  RECORDED: 7,
  ACCEPTED: 8,
  PENDING: 9,
  RESOLVED: 10,
  ESCALATE_TO_OWNER: 11,
  REFUSED: 12,
  /** Never reported: a full log is a ZXV_E_CAPACITY error instead. */
  LOG_FULL: 13,
} as const);

/** Graph node states (`ZXV_PDX_NS_*`). Final: CLEAR, CONTAINED, ROLLED_BACK, TAINTED, APPLIED. */
export const ParadoxNodeState = Object.freeze({
  OPEN: 0,
  CLEAR: 1,
  CONTAINED: 2,
  ROLLED_BACK: 3,
  TAINTED: 4,
  SANDBOXED: 5,
  WAITING: 6,
  HELD: 7,
  BLOCKED: 8,
  APPLIED: 9,
} as const);

/** Flags of a policy, a registry entry or a draft (`ZXV_PDX_FLAG_*`). */
export const ParadoxFlag = Object.freeze({
  /** Touches money: the owner decides. */
  MONEY: 1,
  /** Touches devices: the owner decides. */
  DEVICE: 2,
} as const);

/** Which checks fired (`ZXV_PDX_TRIP_*`): a RepairProof's trips and the quarantine reason. */
export const ParadoxTrip = Object.freeze({
  STEPS: 1,
  BUDGET: 2,
  CYCLE: 4,
  INVARIANT: 8,
  FORBIDDEN: 16,
} as const);

/** The book's three logs (`ZXV_PDX_LOG_*`). */
export const ParadoxLog = Object.freeze({
  /** The operator log. */
  OPS: 0,
  /** Special records, never edited. */
  FRONTIER: 1,
  /** Contained work. */
  QUARANTINE: 2,
} as const);

/** An operator or frontier log record, in bytes. */
export const PDX_REC_BYTES = 144;
/** A quarantine log record, in bytes. */
export const PDX_QREC_BYTES = 110;
/** A RepairProof, in bytes. */
export const PDX_REPAIR_BYTES = 160;
/** A draft classification, in bytes. */
export const PDX_DRAFT_BYTES = 71;
/** Records per log. */
export const PDX_LOG_MAX = 65536;
/** Registry entries. */
export const PDX_REGISTRY_MAX = 65536;
/** Parameters per policy. */
export const PDX_POLICY_MAX = 64;
/** Branches per hold. */
export const PDX_HOLD_MAX = 64;
/** Nodes per graph. */
export const PDX_GRAPH_MAX = 64;
/** Dependencies per node. */
export const PDX_MAX_DEPS = 4;
/** Queued node updates per pass. */
export const PDX_UPDATES_MAX = 256;
/** State fingerprints per detection. */
export const PDX_TRACE_MAX = 4096;
/** The source of a registry entry not learned from the frontier. */
export const PDX_SRC_SEED = 0xffff_ffff_ffff_ffffn;
/** Bytes of a policy proposal with `n` parameters. */
export const pdxProposalBytes = (n: number): number => 12 + 32 * n;

/** A classified pair: every operator takes the (kind, tag) {@link ParadoxBook.classify} returned. */
export interface ParadoxClass {
  /** A {@link ParadoxKind} value. */
  readonly kind: number;
  /** The hexagram tag (0..63) carrying the kind in its polarity bits. */
  readonly tag: number;
}

/** A classification, with the registry entry that decided it. */
export interface ParadoxClassification extends ParadoxClass {
  /** The registry index whose signature decided the kind, or -1. */
  readonly registryIndex: number;
}

/** Options for {@link ParadoxBook.classify}. */
export interface ParadoxClassifyOptions {
  /** Evidence counts before and after: [pos0, neg0, pos1, neg1]. Omit for no trend. */
  readonly evidence?: readonly [number, number, number, number];
  /** The newest evidence is past the caller's freshness limit (ignored without evidence). */
  readonly stale?: boolean;
  /** The anomaly's signature ({@link Paradox.signature}), looked up in the registry. */
  readonly signature?: Uint8Array;
}

/** A registry entry ({@link ParadoxBook.registryAt}). */
export interface ParadoxRegistryEntry {
  readonly signature: Uint8Array;
  readonly kind: number;
  readonly reason: number;
  readonly flags: number;
  /** The frontier record it was learned from, or {@link PDX_SRC_SEED}. */
  readonly source: bigint;
}

/** Inputs to {@link ParadoxBook.destructive}. */
export interface ParadoxDestructiveInput {
  readonly steps: number;
  /** 0: no limit. */
  readonly stepLimit: number;
  readonly spent: number;
  /** 0: no budget. */
  readonly budget: number;
  /** State fingerprints in visit order (at most {@link PDX_TRACE_MAX}). */
  readonly trace?: readonly bigint[];
  /** The caller's invariant check result. */
  readonly invariantOk: boolean;
  /** The live state; restored IN PLACE from the checkpoint when contained. */
  readonly state: Uint8Array;
  /** The checkpoint (same length as the state). */
  readonly checkpoint: Uint8Array;
  /** The canonical description of the trap (its signature goes in the proof). */
  readonly pattern?: Uint8Array;
  /** Who submitted the work. */
  readonly submitter: number;
}

/** What {@link ParadoxBook.destructive} decided. */
export interface ParadoxDestructiveResult {
  /** CONTAINED, or REFUSED (nothing changed). */
  readonly outcome: number;
  /** The RepairProof when contained. */
  readonly proof?: RepairProof;
}

/**
 * A RepairProof ({@link PDX_REPAIR_BYTES} bytes, little-endian): what
 * destructive containment found and did. Sign {@link RepairProof.bytes}
 * yourself to share it; {@link Paradox.verifyRepair} checks only its
 * binding hash.
 */
export class RepairProof {
  /** The 160 encoded bytes. */
  readonly bytes: Uint8Array;
  /** {@link ParadoxTrip} bits. */
  readonly trips: number;
  readonly steps: number;
  readonly spent: number;
  readonly traceLength: number;
  readonly submitter: number;
  readonly stateLength: number;
  /** SHA-256 of the state as found. */
  readonly stateFound: Uint8Array;
  /** SHA-256 of the state after rollback. */
  readonly stateAfter: Uint8Array;
  /** The signature to share with peers (of the pattern). */
  readonly signature: Uint8Array;
  /** The proof hash: SHA-256("zxv/pdx-repair/v1" || bytes 0..119). */
  readonly hash: Uint8Array;
  /** The quarantine record's index. */
  readonly quarantineIndex: bigint;

  /** Read a proof's fields (no check: see {@link Paradox.verifyRepair}). */
  constructor(proof: Uint8Array) {
    const b = Uint8Array.from(bytes('proof', proof, PDX_REPAIR_BYTES));
    const v = new DataView(b.buffer);
    this.bytes = b;
    this.trips = v.getUint32(0, true);
    this.steps = v.getUint32(4, true);
    this.spent = v.getUint32(8, true);
    this.traceLength = v.getUint32(12, true);
    this.submitter = v.getUint32(16, true);
    this.stateLength = v.getUint32(20, true);
    this.stateFound = b.slice(24, 56);
    this.stateAfter = b.slice(56, 88);
    this.signature = b.slice(88, 120);
    this.hash = b.slice(120, 152);
    this.quarantineIndex = v.getBigUint64(152, true);
    Object.freeze(this);
  }
}

/** A policy's live values ({@link ParadoxPolicy.values}). */
export interface ParadoxPolicyValues {
  readonly values: readonly bigint[];
  /** The values before the last applied update. */
  readonly previous: readonly bigint[];
  /** An applied update awaits evaluation. */
  readonly armed: boolean;
  /** The tension it was applied at. */
  readonly tensionAt: number;
}

/** A policy's published bounds ({@link ParadoxPolicy.bounds}). */
export interface ParadoxPolicyBounds {
  readonly lo: readonly bigint[];
  readonly hi: readonly bigint[];
  readonly flags: number;
}

/** A hold's state ({@link ParadoxHold.info}). */
export interface ParadoxHoldInfo {
  readonly count: number;
  /** The settled branch, or -1 while deferred. */
  readonly settled: number;
  /** The branch with decisive evidence now, or -1. */
  readonly decisive: number;
}

/** One branch of a hold. */
export interface ParadoxBranch {
  readonly id: Uint8Array;
  readonly positive: number;
  readonly negative: number;
}

/** A graph node read in id order ({@link ParadoxGraph.nodeAt}). */
export interface ParadoxNode {
  readonly id: Uint8Array;
  readonly kind: number;
  /** A {@link ParadoxNodeState} value. */
  readonly state: number;
}

/** A graph's summary ({@link ParadoxGraph.info}). */
export interface ParadoxGraphInfo {
  readonly nodes: number;
  readonly passes: number;
  /** The net tension of the open nodes after the last pass: magnitude... */
  readonly totalMagnitude: number;
  /** ...and direction (-1, 0 or +1). */
  readonly totalDirection: number;
  readonly escalated: boolean;
  /** The SHA-256 chain over every pass (zeros before the first). */
  readonly chain: Uint8Array;
}

const id32 = (name: string, v: unknown): Uint8Array => bytes(name, v, 32);
const tagOf = (v: unknown): number => u8('tag', v);

/**
 * The pure paradox functions, reached as `zxv.paradox`. No capability, no
 * context.
 *
 * HONEST LIMITS: this is contradiction-tolerant bookkeeping. It contains,
 * holds and routes contradictions; it never makes both sides true.
 * Automatic changes happen only inside published bounds, and anything that
 * touches money or devices goes to the owner. Detection is only by the
 * checks the caller runs; what they do not look at goes undetected.
 * "Subquanta" and similar words in the design notes are a metaphor, not
 * physics.
 */
export class Paradox {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /** The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data) of a canonical description. */
  signature(data: Uint8Array): Uint8Array {
    const d = bytes('data', data);
    const o = new Uint8Array(32);
    this.#core.call(this.#core, 'zxv_paradox_signature', d, d.length, o);
    return o;
  }

  /** The kind a tag reads as on its own; a forbidden tag reads as NONE. */
  kindOfTag(tag: number): number {
    const o = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_paradox_kind_of_tag', tagOf(tag), o);
    return o[0] ?? 0;
  }

  /**
   * What to do with a classified pair (a {@link ParadoxDo} value). A
   * forbidden tag gives ROLLBACK whatever the kind.
   *
   * @throws a ZXV_E_ARG error for an invalid pair
   */
  action(kind: number, tag: number): number {
    const o = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_paradox_action', i32('kind', kind), tagOf(tag), o);
    return o[0] ?? 0;
  }

  /** The hexagram action (`ZXV_HEX_ACT_*`) an outcome amounts to. */
  outcomeAction(outcome: number): number {
    const o = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_paradox_outcome_action', i32('outcome', outcome), o);
    return o[0] ?? 0;
  }

  /**
   * Recompute a RepairProof's binding hash. Integrity only: a signature
   * over the proof is the caller's to verify.
   *
   * @throws {@link VerificationFailedError} when it does not match
   */
  verifyRepair(proof: RepairProof | Uint8Array): void {
    const b = proof instanceof RepairProof ? proof.bytes : bytes('proof', proof, PDX_REPAIR_BYTES);
    this.#core.call(this.#core, 'zxv_paradox_repair_verify', b);
  }

  /** "constructive", ..., "none"; "INVALID" for an unknown value. */
  kindName(kind: number): string {
    return this.#core.b.callString('zxv_paradox_kind_name', i32('kind', kind));
  }

  /** "applied", "proposal-for-owner", ...; "INVALID" for an unknown value. */
  outcomeName(outcome: number): string {
    return this.#core.b.callString('zxv_paradox_outcome_name', i32('outcome', outcome));
  }

  /** "open", "sandboxed", ...; "INVALID" for an unknown value. */
  nodeStateName(state: number): string {
    return this.#core.b.callString('zxv_paradox_node_state_name', i32('state', state));
  }
}

/** Base for the context-bound paradox handles. */
abstract class ParadoxHandle extends Resource {
  /** The capability that created the handle. */
  readonly cap: Capability;
  /** @internal */
  readonly _ctx: ZxvContext;
  readonly #ptr: Ptr;
  readonly #destroy: string;

  protected constructor(cap: Capability, ptr: Ptr, destroy: string) {
    super(cap.context);
    this.cap = cap;
    this._ctx = cap.context;
    this.#ptr = ptr;
    this.#destroy = destroy;
  }

  /** @internal The handle (throws once released). */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** @internal */
  _call(fn: string, ...args: Arg[]): void {
    this._ctx._core.call(this._ctx._state, fn, ...args);
  }

  protected destroy(): void {
    const p = this.#ptr;
    const fn = this.#destroy;
    this._ctx._state.release(() => this._ctx._core.b.call(fn, p));
  }
}

/**
 * Constructive parameters (`zxv_paradox_policy`): n swarm parameters
 * (shares, weights, timings; never balances) with published bounds that
 * never change. A policy flagged MONEY or DEVICE is never changed
 * automatically.
 */
export class ParadoxPolicy extends ParadoxHandle {
  /** Parameter count. */
  readonly size: number;

  /** @internal Use {@link ParadoxPolicy.create}. */
  constructor(cap: Capability, ptr: Ptr, size: number) {
    super(cap, ptr, 'zxv_paradox_policy_destroy');
    this.size = size;
  }

  /**
   * A new policy with bounds lo[i] <= hi[i] and initial values inside them.
   *
   * @param cap - needs {@link Cap.PARADOX}
   * @throws a ZXV_E_RANGE error (lo > hi, or an initial value outside its bounds)
   */
  static create(cap: Capability, options: { lo: readonly bigint[]; hi: readonly bigint[]; initial: readonly bigint[]; flags?: number }):
  ParadoxPolicy {
    const n = options.lo.length;
    const arr = (name: string, v: readonly bigint[]): BigInt64Array => {
      if (v.length !== n) throw new RangeError(`${name} must have ${n} values`);
      return BigInt64Array.from(v.map((x, i) => i64(`${name}[${i}]`, x)));
    };
    const c = cap.context;
    const p = newHandle(c._core, c._state, 'zxv_paradox_policy_create', cap._ptr(), u32('n', n), arr('lo', options.lo),
      arr('hi', options.hi), arr('initial', options.initial), u32('flags', options.flags ?? 0));
    return new ParadoxPolicy(cap, p, n);
  }

  /** @internal */
  _synth(synth: readonly bigint[]): BigInt64Array {
    if (synth.length !== this.size) throw new RangeError(`synth must have ${this.size} values`);
    return BigInt64Array.from(synth.map((x, i) => i64(`synth[${i}]`, x)));
  }

  /** The live values, the values before the last applied update, and whether that update is armed. */
  values(): ParadoxPolicyValues {
    const v = new BigInt64Array(this.size);
    const pv = new BigInt64Array(this.size);
    const a = new Int32Array(1);
    const t = new Uint32Array(1);
    this._call('zxv_paradox_policy_values', this._p(), v, pv, this.size, a, t);
    return Object.freeze({ values: Object.freeze(Array.from(v)), previous: Object.freeze(Array.from(pv)), armed: (a[0] ?? 0) !== 0,
      tensionAt: t[0] ?? 0 });
  }

  /** The published bounds and flags. */
  bounds(): ParadoxPolicyBounds {
    const lo = new BigInt64Array(this.size);
    const hi = new BigInt64Array(this.size);
    const f = new Uint32Array(1);
    this._call('zxv_paradox_policy_bounds', this._p(), lo, hi, this.size, f);
    return Object.freeze({ lo: Object.freeze(Array.from(lo)), hi: Object.freeze(Array.from(hi)), flags: f[0] ?? 0 });
  }

  /**
   * A proposed update as bytes for the owner or for signing: n, how many
   * values are outside their bounds, flags, then (lo, hi, current,
   * proposed) per parameter. Reads only.
   */
  proposal(synth: readonly bigint[]): Uint8Array {
    const p = this._p();
    const s = this._synth(synth);
    return this._ctx._core.bytes(this._ctx._state, 'zxv_paradox_policy_proposal', (b, c, l) => [p, s, this.size, b, c, l]);
  }
}

/** Neutral branches (`zxv_paradox_hold`): every candidate is kept until one is decisive or the owner picks. */
export class ParadoxHold extends ParadoxHandle {
  /** @internal Use {@link ParadoxHold.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap, ptr, 'zxv_paradox_hold_destroy');
  }

  /**
   * A hold of up to `capacity` branches (1..{@link PDX_HOLD_MAX}). A branch
   * settles when its net evidence leads every other by at least `decisive`
   * (0 means 1), or alone reaches it, or the owner picks it.
   *
   * @param cap - needs {@link Cap.PARADOX}
   */
  static create(cap: Capability, options: { capacity: number; decisive?: number }): ParadoxHold {
    const c = cap.context;
    return new ParadoxHold(cap, newHandle(c._core, c._state, 'zxv_paradox_hold_create', cap._ptr(), u32('capacity', options.capacity),
      u32('decisive', options.decisive ?? 0)));
  }

  /** Add a candidate (a 32-byte digest); the same id again is the same candidate. */
  add(id: Uint8Array): this {
    this._call('zxv_paradox_hold_add', this._p(), id32('id', id));
    return this;
  }

  /** One piece of evidence for (`positive`) or against a candidate. */
  evidence(id: Uint8Array, positive: boolean): this {
    this._call('zxv_paradox_hold_evidence', this._p(), id32('id', id), positive ? 1 : 0);
    return this;
  }

  /** Branch count, settled branch and decisive branch. */
  info(): ParadoxHoldInfo {
    const n = new Uint32Array(1);
    const s = new Int32Array(2);
    this._call('zxv_paradox_hold_info', this._p(), n, s.subarray(0, 1), s.subarray(1, 2));
    return Object.freeze({ count: n[0] ?? 0, settled: s[0] ?? -1, decisive: s[1] ?? -1 });
  }

  /** Branch `index`. */
  branch(index: number): ParadoxBranch {
    const id = new Uint8Array(32);
    const c = new Uint32Array(2);
    this._call('zxv_paradox_hold_branch', this._p(), u32('index', index), id, c.subarray(0, 1), c.subarray(1, 2));
    return Object.freeze({ id, positive: c[0] ?? 0, negative: c[1] ?? 0 });
  }
}

/**
 * A complex paradox (`zxv_paradox_graph`): nodes with dependencies,
 * resolved pass by pass in a fixed order (by node id), so every peer with
 * the same signed inputs reaches the same states and chain.
 */
export class ParadoxGraph extends ParadoxHandle {
  /** @internal Use {@link ParadoxGraph.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap, ptr, 'zxv_paradox_graph_destroy');
  }

  /**
   * A graph that escalates to the owner after `stepLimit` passes (>= 1)
   * without resolving.
   *
   * @param cap - needs {@link Cap.PARADOX}
   */
  static create(cap: Capability, stepLimit: number): ParadoxGraph {
    const c = cap.context;
    return new ParadoxGraph(cap, newHandle(c._core, c._state, 'zxv_paradox_graph_create', cap._ptr(), u32('stepLimit', stepLimit)));
  }

  /**
   * Add a node before the first pass: id = the hash of its signed input,
   * a classified pair, and a tension magnitude and direction (-1, 0, +1).
   */
  addNode(id: Uint8Array, cls: ParadoxClass, magnitude: number, direction: number): this {
    this._call('zxv_paradox_graph_add_node', this._p(), id32('id', id), i32('kind', cls.kind), tagOf(cls.tag), u32('magnitude', magnitude),
      i32('direction', direction));
    return this;
  }

  /** Node `id` depends on node `dep` (which must exist by the first pass). */
  addDependency(id: Uint8Array, dep: Uint8Array): this {
    this._call('zxv_paradox_graph_add_dep', this._p(), id32('id', id), id32('dep', dep));
    return this;
  }

  /** Queue new evidence for a node, applied at the start of the next pass ("worst wins" within a batch). */
  update(id: Uint8Array, cls: ParadoxClass, magnitude: number, direction: number): this {
    this._call('zxv_paradox_graph_update', this._p(), id32('id', id), i32('kind', cls.kind), tagOf(cls.tag), u32('magnitude', magnitude),
      i32('direction', direction));
    return this;
  }

  /** A node's {@link ParadoxNodeState}. */
  state(id: Uint8Array): number {
    const s = new Int32Array(1);
    this._call('zxv_paradox_graph_state', this._p(), id32('id', id), s);
    return s[0] ?? 0;
  }

  /** Node `index` in id order (after the first pass). */
  nodeAt(index: number): ParadoxNode {
    const id = new Uint8Array(32);
    const s = new Int32Array(2);
    this._call('zxv_paradox_graph_node_at', this._p(), u32('index', index), id, s.subarray(0, 1), s.subarray(1, 2));
    return Object.freeze({ id, kind: s[0] ?? 0, state: s[1] ?? 0 });
  }

  /** Node count, passes, net tension, escalation and the pass chain. */
  info(): ParadoxGraphInfo {
    const u = new Uint32Array(3);
    const s = new Int32Array(2);
    const chain = new Uint8Array(32);
    this._call('zxv_paradox_graph_info', this._p(), u.subarray(0, 1), u.subarray(1, 2), u.subarray(2, 3), s.subarray(0, 1),
      s.subarray(1, 2), chain);
    return Object.freeze({ nodes: u[0] ?? 0, passes: u[1] ?? 0, totalMagnitude: u[2] ?? 0, totalDirection: s[0] ?? 0,
      escalated: (s[1] ?? 0) !== 0, chain });
  }
}

/**
 * The book (`zxv_paradox`): the operator log, the frontier log, the
 * quarantine log (all append-only and SHA-256 hash-chained) and the
 * signature registry. Every operator call that returns has logged one
 * record and reports its decision as a {@link ParadoxOutcome}; a thrown
 * error means it did not run. A full log is a ZXV_E_CAPACITY error: halt
 * the context, since work that cannot be recorded must not continue.
 *
 * Writes need {@link Cap.PARADOX} (the book's own capability by default);
 * nothing here signs. Release before the context (R6).
 */
export class ParadoxBook extends ParadoxHandle {
  /** @internal Use {@link ParadoxBook.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap, ptr, 'zxv_paradox_destroy');
  }

  /**
   * A new book.
   *
   * @param cap - needs {@link Cap.PARADOX}
   * @param options - log capacity (records per log, 1..{@link PDX_LOG_MAX}) and registry capacity (1..{@link PDX_REGISTRY_MAX})
   */
  static create(cap: Capability, options: { logCapacity: number; registryCapacity: number }): ParadoxBook {
    const c = cap.context;
    return new ParadoxBook(cap, newHandle(c._core, c._state, 'zxv_paradox_create', cap._ptr(), u32('logCapacity', options.logCapacity),
      u32('registryCapacity', options.registryCapacity)));
  }

  /**
   * Classify a tag: a registry signature overrides the trend for Both and
   * Neither; otherwise the evidence trend picks the polarity. Reads only.
   */
  classify(tag: number, options: ParadoxClassifyOptions = {}): ParadoxClassification {
    const ev = options.evidence === undefined ? null : Uint32Array.from(options.evidence.map((x, i) => u32(`evidence[${i}]`, x)));
    if (ev !== null && ev.length !== 4) throw new RangeError('evidence must have 4 counts');
    const sig = options.signature === undefined ? null : id32('signature', options.signature);
    const k = new Int32Array(2);
    const t = new Uint8Array(1);
    this._call('zxv_paradox_classify', this._p(), tagOf(tag), ev, options.stale ? 1 : 0, sig, k.subarray(0, 1), t, k.subarray(1, 2));
    return Object.freeze({ kind: k[0] ?? 0, tag: t[0] ?? 0, registryIndex: k[1] ?? -1 });
  }

  /**
   * Add a reviewed signature as CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL. An
   * entry flagged MONEY or DEVICE needs `ownerOk` (the caller's assertion:
   * this library cannot check who the owner is).
   *
   * @returns the entry's index
   * @throws {@link OwnerApprovalRequiredError}; a ZXV_E_DUPLICATE error (already registered); ZXV_E_CAPACITY
   */
  registryAdd(signature: Uint8Array, kind: number, options: { reason?: number; flags?: number; ownerOk?: boolean } = {},
    cap: Capability = this.cap): number {
    const o = new Uint32Array(1);
    this._call('zxv_paradox_registry_add', cap._ptr(), this._p(), id32('signature', signature), i32('kind', kind),
      u8('reason', options.reason ?? 0), u8('flags', options.flags ?? 0), options.ownerOk ? 1 : 0, o);
    return o[0] ?? 0;
  }

  /** The index of a signature, or undefined when absent. */
  registryFind(signature: Uint8Array): number | undefined {
    const o = new Uint32Array(1);
    const s = this._ctx._core.callAllowing(this._ctx._state, [Status.E_NOT_FOUND], 'zxv_paradox_registry_find', this._p(),
      id32('signature', signature), o);
    return s === 0 ? o[0] : undefined;
  }

  /** Registry entries. */
  registryCount(): number {
    const o = new Uint32Array(1);
    this._call('zxv_paradox_registry_count', this._p(), o);
    return o[0] ?? 0;
  }

  /** Registry entry `index`. */
  registryAt(index: number): ParadoxRegistryEntry {
    const sig = new Uint8Array(32);
    const k = new Int32Array(1);
    const rf = new Uint8Array(2);
    const src = new BigUint64Array(1);
    this._call('zxv_paradox_registry_at', this._p(), u32('index', index), sig, k, rf.subarray(0, 1), rf.subarray(1, 2), src);
    return Object.freeze({ signature: sig, kind: k[0] ?? 0, reason: rf[0] ?? 0, flags: rf[1] ?? 0, source: src[0] ?? 0n });
  }

  /** Record count of a {@link ParadoxLog}. */
  logCount(which: number): number {
    const o = new Uint32Array(1);
    this._call('zxv_paradox_log_count', this._p(), i32('which', which), o);
    return o[0] ?? 0;
  }

  /** A log's chain head (32 zeros when empty). */
  logHead(which: number): Uint8Array {
    const o = new Uint8Array(32);
    this._call('zxv_paradox_log_head', this._p(), i32('which', which), o);
    return o;
  }

  /** Record `index` of a log as bytes ({@link PDX_REC_BYTES} or {@link PDX_QREC_BYTES}). */
  logRecord(which: number, index: number): Uint8Array {
    const p = this._p();
    const w = i32('which', which);
    const i = u32('index', index);
    return this._ctx._core.bytes(this._ctx._state, 'zxv_paradox_log_record', (b, c, l) => [p, w, i, b, c, l]);
  }

  /**
   * Recompute every link of a log.
   *
   * @throws {@link ZxvFatalError} (ZXV_E_INVARIANT) if it no longer matches; that poisons the context
   */
  verifyLog(which: number): void {
    this._call('zxv_paradox_log_verify', this._p(), i32('which', which));
  }

  /**
   * One evaluation, then (if `synth` is given) one proposed update. The
   * synthesis is the caller's; this checks and applies it only inside the
   * published bounds, and never for a MONEY or DEVICE policy (those become
   * PROPOSAL_FOR_OWNER). `tension` is the caller's integer measure (higher
   * is worse); an armed update whose tension grew is rolled back.
   *
   * @returns a {@link ParadoxOutcome}
   */
  constructive(policy: ParadoxPolicy, cls: ParadoxClass, synth: readonly bigint[] | null, tension: number,
    cap: Capability = this.cap): number {
    const s = synth === null ? null : policy._synth(synth);
    const o = new Int32Array(1);
    this._call('zxv_paradox_constructive', cap._ptr(), this._p(), policy._p(), i32('kind', cls.kind), tagOf(cls.tag), s, policy.size,
      u32('tension', tension), o);
    return o[0] ?? 0;
  }

  /**
   * Detect and contain. When the kind is DESTRUCTIVE or any check fires,
   * the work is quarantined, `input.state` is restored in place from the
   * checkpoint and a RepairProof is returned (outcome CONTAINED);
   * otherwise the refusal is logged and nothing changes. It contains: it
   * has no way to reply to or act against the submitter.
   */
  destructive(cls: ParadoxClass, input: ParadoxDestructiveInput, cap: Capability = this.cap): ParadoxDestructiveResult {
    const st = bytes('state', input.state);
    const ck = bytes('checkpoint', input.checkpoint, st.length);
    const tr = input.trace === undefined || input.trace.length === 0 ? null
      : BigUint64Array.from(input.trace.map((x, i) => u64(`trace[${i}]`, x)));
    const pat = input.pattern === undefined || input.pattern.length === 0 ? null : bytes('pattern', input.pattern);
    const o = new Int32Array(1);
    const proof = new Uint8Array(PDX_REPAIR_BYTES);
    this._call('zxv_paradox_destructive', cap._ptr(), this._p(), i32('kind', cls.kind), tagOf(cls.tag), u32('steps', input.steps),
      u32('stepLimit', input.stepLimit), u32('spent', input.spent), u32('budget', input.budget), tr, tr === null ? 0 : tr.length,
      input.invariantOk ? 1 : 0, st, ck, st.length, pat, pat === null ? 0 : pat.length, u32('submitter', input.submitter), o, proof);
    const outcome = o[0] ?? 0;
    return Object.freeze(outcome === ParadoxOutcome.CONTAINED ? { outcome, proof: new RepairProof(proof) } : { outcome });
  }

  /**
   * Settle a hold on decisive evidence or on `ownerPick` (a branch index,
   * -1 for none); otherwise keep every branch.
   */
  neutral(hold: ParadoxHold, cls: ParadoxClass, ownerPick = -1, cap: Capability = this.cap): { readonly outcome: number; readonly settled: number } {
    const o = new Int32Array(2);
    this._call('zxv_paradox_neutral', cap._ptr(), this._p(), hold._p(), i32('kind', cls.kind), tagOf(cls.tag), i32('ownerPick', ownerPick),
      o.subarray(0, 1), o.subarray(1, 2));
    return Object.freeze({ outcome: o[0] ?? 0, settled: o[1] ?? -1 });
  }

  /**
   * Record an unclassified paradox as a permanent frontier record (never
   * edited or erased). The work stays sandboxed until a reviewed
   * classification exists.
   *
   * @returns the outcome and, when RECORDED, the frontier record's index
   */
  special(cls: ParadoxClass, signature: Uint8Array, subject: Uint8Array, cap: Capability = this.cap):
  { readonly outcome: number; readonly record?: number } {
    const o = new Int32Array(1);
    const r = new Uint32Array(1);
    this._call('zxv_paradox_special', cap._ptr(), this._p(), i32('kind', cls.kind), tagOf(cls.tag), id32('signature', signature),
      id32('subject', subject), o, r);
    const outcome = o[0] ?? 0;
    return Object.freeze(outcome === ParadoxOutcome.RECORDED ? { outcome, record: r[0] ?? 0 } : { outcome });
  }

  /** A draft classification of frontier record `record`, for review ({@link PDX_DRAFT_BYTES} bytes). */
  draft(record: number, kind: number, options: { reason?: number; flags?: number } = {}, cap: Capability = this.cap): Uint8Array {
    const o = new Uint8Array(PDX_DRAFT_BYTES);
    this._call('zxv_paradox_draft', cap._ptr(), this._p(), u32('record', record), i32('kind', kind), u8('reason', options.reason ?? 0),
      u8('flags', options.flags ?? 0), o);
    return o;
  }

  /**
   * Review a draft (ours or a peer's). A MONEY or DEVICE draft needs
   * `ownerOk`; others need `ownerOk` or `consensusOk` (the caller's quorum
   * result). Both are the caller's assertions.
   *
   * @returns ACCEPTED, PROPOSAL_FOR_OWNER or REFUSED
   */
  review(draft: Uint8Array, options: { ownerOk?: boolean; consensusOk?: boolean } = {}, cap: Capability = this.cap): number {
    const o = new Int32Array(1);
    this._call('zxv_paradox_review', cap._ptr(), this._p(), bytes('draft', draft, PDX_DRAFT_BYTES), options.ownerOk ? 1 : 0,
      options.consensusOk ? 1 : 0, o);
    return o[0] ?? 0;
  }

  /**
   * One pass over a graph, in the fixed order (contain, sandbox, hold,
   * apply). Logs one record.
   *
   * @returns RESOLVED or PENDING
   * @throws {@link ParadoxStepLimitError} when the pass count reaches the step limit unresolved (the
   *   pass is logged; the graph stays readable and goes to the owner)
   */
  graphPass(graph: ParadoxGraph, cap: Capability = this.cap): number {
    const o = new Int32Array(1);
    this._call('zxv_paradox_graph_pass', cap._ptr(), this._p(), graph._p(), o);
    return o[0] ?? 0;
  }
}
