// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: the paradox engine. A paradox here is work whose four-valued
/// state is Both (evidence points both ways) or Neither (no evidence). The
/// engine does not decide which side is "really" true: it reads the work's
/// `Hexagram` tag, gives the paradox one of four kinds, and runs a fixed,
/// logged operator for that kind:
///
/// | Kind           | Operator             | What it does                                  |
/// |----------------|----------------------|-----------------------------------------------|
/// | constructive   | `Book.constructive`  | a bounded policy update, only inside bounds   |
/// | destructive    | `Book.destructive`   | contain: quarantine, roll back, `RepairProof` |
/// | neutral        | `Book.neutral`       | hold every candidate branch (`Hold`)          |
/// | special        | `Book.special`       | a permanent frontier record plus a draft      |
///
/// Every operator takes the pair (kind, tag) that `classify` returned. A
/// pair that breaks the class rules is `.usage` (`ZXV_E_ARG`). A forbidden
/// tag (a real check found a broken invariant) is accepted with any kind;
/// every operator refuses it and rolls back.
///
/// The `Book` holds three append-only, SHA-256 hash-chained logs (operator,
/// frontier, quarantine) and the signature registry. Operators that ran
/// return an `Outcome` (`.refused` and `.proposalForOwner` are decisions,
/// not errors). A thrown error means nothing ran and nothing was logged,
/// with two exceptions documented on `Book.destructive` and `Book.pass`. A
/// full log is `.refused` (`ZXV_E_CAPACITY`): halt the context.
///
/// Capabilities: creating a `Book`, `Policy`, `Hold` or `Graph` and every
/// operator need `.paradox` (`.capabilityDenied` otherwise). Reading,
/// classifying, adding branches, nodes and updates to a handle you hold,
/// and the free functions need none. Nothing here signs: sign the bytes
/// yourself (`MlDsa87` or `Ed25519` with `.cryptoSign`).
///
/// Threading: a `Book` is internally serialized. `Policy`, `Hold` and
/// `Graph` are not thread-safe: use each from one thread at a time.
///
/// Honest limits:
/// * Automatic changes happen only inside published bounds. An update with
///   a value outside [lo, hi] changes nothing and becomes a proposal for the
///   owner. The operator has no balance input: it cannot move money.
/// * MONEY AND DEVICE POLICIES NEED THE OWNER: policies, registry entries
///   and drafts flagged `.money` or `.device` are never changed
///   automatically. `ownerOk` is the caller's assertion; inside one process
///   this library cannot check who the owner is.
/// * Detection is only by the checks the caller runs (step limit, budget,
///   repeated state fingerprints, the caller's invariant result, registry
///   lookups), each when the caller calls. Nothing is instant.
/// * Destructive work is contained, never answered: there is no way to
///   reply to or act against a sender.
/// * This is contradiction-tolerant bookkeeping; it never makes both sides
///   true. Peers agree by applying the same fixed order to the same signed
///   inputs, not through any shared clock. Integers only.
public enum Paradox {
    /// A signature, id or hash.
    public static let hashBytes = Int(ZXV_PDX_HASH_BYTES)
    /// An operator or frontier record.
    public static let recordBytes = Int(ZXV_PDX_REC_BYTES)
    /// A quarantine record.
    public static let quarantineRecordBytes = Int(ZXV_PDX_QREC_BYTES)
    /// A `RepairProof`.
    public static let repairBytes = Int(ZXV_PDX_REPAIR_BYTES)
    /// A `Draft`.
    public static let draftBytes = Int(ZXV_PDX_DRAFT_BYTES)
    /// Records per log.
    public static let logMax = UInt32(ZXV_PDX_LOG_MAX)
    /// Registry entries.
    public static let registryMax = UInt32(ZXV_PDX_REGISTRY_MAX)
    /// Parameters per policy.
    public static let policyMax = UInt32(ZXV_PDX_POLICY_MAX)
    /// Branches per hold.
    public static let holdMax = UInt32(ZXV_PDX_HOLD_MAX)
    /// Nodes per graph.
    public static let graphMax = UInt32(ZXV_PDX_GRAPH_MAX)
    /// Dependencies per node.
    public static let maxDeps = UInt32(ZXV_PDX_MAX_DEPS)
    /// Queued updates per pass.
    public static let updatesMax = UInt32(ZXV_PDX_UPDATES_MAX)
    /// Fingerprints per detection.
    public static let traceMax = UInt32(ZXV_PDX_TRACE_MAX)
    /// `RegistryEntry.source` of an entry not learned from the frontier.
    public static let sourceSeed = UInt64.max

    /// The size of a policy proposal of `n` parameters.
    public static func proposalBytes(_ n: UInt32) -> Int { 12 + 32 * Int(n) }

    /// The four kinds, plus none (True or False: not a paradox).
    public enum Kind: Int32, Sendable, CaseIterable, CustomStringConvertible {
        /// Both, margin growing (catalyst).
        case constructive = 0
        /// Both, margin shrinking or stale (immune).
        case destructive = 1
        /// Both and steady, or Neither.
        case neutral = 2
        /// Unclassified: the frontier.
        case special = 3
        /// True or False: not a paradox.
        case none = 4
        /// "constructive", ..., "none".
        public var description: String { Paradox.kindName(rawValue) }
    }

    /// What to do with a classified pair (`action(kind:tag:)`).
    public enum Action: Int32, Sendable, CaseIterable {
        /// Not a paradox: use `Hexagram.action`.
        case none = 0
        /// Constructive: `Book.constructive`.
        case synthesize = 1
        /// Destructive: `Book.destructive`.
        case contain = 2
        /// Neutral: `Book.neutral`.
        case hold = 3
        /// Special: `Book.special` (sandbox).
        case record = 4
        /// Forbidden: refuse, roll back, quarantine.
        case rollback = 5
    }

    /// An operator's decision.
    public enum Outcome: Int32, Sendable, CaseIterable, CustomStringConvertible {
        /// A constructive update applied.
        case applied = 0
        /// An applied update survived evaluation.
        case kept = 1
        /// Out of bounds or unapproved: no change.
        case proposalForOwner = 2
        /// Tension grew, or forbidden: restored.
        case rolledBack = 3
        /// Destructive: quarantined, rolled back.
        case contained = 4
        /// Neutral: still holding every branch.
        case deferred = 5
        /// Neutral: one branch chosen.
        case settled = 6
        /// Special: frontier record written.
        case recorded = 7
        /// A reviewed draft joined the registry.
        case accepted = 8
        /// Graph: more passes needed.
        case pending = 9
        /// Graph: every node is final.
        case resolved = 10
        /// Graph: step limit reached.
        case escalateToOwner = 11
        /// Wrong kind: nothing changed.
        case refused = 12
        /// Never reported (`ZXV_E_CAPACITY` instead).
        case logFull = 13
        /// "applied", "proposal-for-owner", ...
        public var description: String { Paradox.outcomeName(rawValue) }
    }

    /// A graph node's state.
    public enum NodeState: Int32, Sendable, CaseIterable, CustomStringConvertible {
        /// Not resolved yet.
        case open = 0
        /// Kind none: not a paradox.
        case clear = 1
        /// Destructive: contained (sticky).
        case contained = 2
        /// Forbidden: refused and rolled back (sticky).
        case rolledBack = 3
        /// Depends on contained work (sticky).
        case tainted = 4
        /// Special: recorded, awaiting review.
        case sandboxed = 5
        /// Depends on a sandboxed node.
        case waiting = 6
        /// Neutral: branches kept.
        case held = 7
        /// Constructive reached by held or waiting work.
        case blocked = 8
        /// Constructive: applied.
        case applied = 9
        /// "open", "sandboxed", ...
        public var description: String { Paradox.nodeStateName(rawValue) }
    }

    /// Which log of a `Book`.
    public enum Log: Int32, Sendable, CaseIterable {
        /// The operator log.
        case ops = 0
        /// Special records, never edited.
        case frontier = 1
        /// Contained work (lpres_hex X6).
        case quarantine = 2
    }

    /// Flags on policies, registry entries and drafts.
    public struct Flags: OptionSet, Sendable, Hashable {
        public let rawValue: UInt8
        /// Wrap raw `ZXV_PDX_FLAG_*` bits.
        public init(rawValue: UInt8) { self.rawValue = rawValue }
        /// Touches money: the owner decides.
        public static let money = Flags(rawValue: UInt8(ZXV_PDX_FLAG_MONEY))
        /// Touches devices: the owner decides.
        public static let device = Flags(rawValue: UInt8(ZXV_PDX_FLAG_DEVICE))
    }

    /// The checks that fired in `Book.destructive` (`RepairProof.trips`).
    public struct Trips: OptionSet, Sendable, Hashable {
        public let rawValue: UInt32
        /// Wrap raw `ZXV_PDX_TRIP_*` bits.
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        /// steps >= step limit.
        public static let steps = Trips(rawValue: ZXV_PDX_TRIP_STEPS)
        /// spent >= budget.
        public static let budget = Trips(rawValue: ZXV_PDX_TRIP_BUDGET)
        /// A state fingerprint repeated.
        public static let cycle = Trips(rawValue: ZXV_PDX_TRIP_CYCLE)
        /// The caller's invariant failed.
        public static let invariant = Trips(rawValue: ZXV_PDX_TRIP_INVARIANT)
        /// The tag was already forbidden.
        public static let forbidden = Trips(rawValue: ZXV_PDX_TRIP_FORBIDDEN)
    }

    /// The name of a raw kind ("constructive", ..., "none"; "INVALID" otherwise).
    public static func kindName(_ raw: Int32) -> String { String(cString: zxv_paradox_kind_name(raw)) }
    /// The name of a raw outcome ("applied", ...; "INVALID" otherwise).
    public static func outcomeName(_ raw: Int32) -> String { String(cString: zxv_paradox_outcome_name(raw)) }
    /// The name of a raw node state ("open", ...; "INVALID" otherwise).
    public static func nodeStateName(_ raw: Int32) -> String { String(cString: zxv_paradox_node_state_name(raw)) }

    /// The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data), from a
    /// canonical description the caller builds.
    public static func signature(_ data: [UInt8]) throws(ZxvError) -> [UInt8] {
        var d = [UInt8](repeating: 0, count: 32)
        try check(zxv_paradox_signature(data, data.count, &d))
        return d
    }

    /// `signature` of the UTF-8 bytes of `text`.
    public static func signature(_ text: String) throws(ZxvError) -> [UInt8] { try signature(Array(text.utf8)) }

    /// The kind a tag reads as on its own (a forbidden tag reads as `.none`).
    public static func kindOfTag(_ tag: Hexagram) throws(ZxvError) -> Kind {
        var k: Int32 = 0
        try check(zxv_paradox_kind_of_tag(tag.tag, &k))
        return try cEnum(k, "paradox kind")
    }

    /// What to do with a classified pair. `.usage` (`ZXV_E_ARG`) for a pair
    /// that breaks the class rules; a forbidden tag gives `.rollback`.
    public static func action(kind: Kind, tag: Hexagram) throws(ZxvError) -> Action {
        var a: Int32 = 0
        try check(zxv_paradox_action(kind.rawValue, tag.tag, &a))
        return try cEnum(a, "paradox action")
    }

    /// The `HexAction` an outcome amounts to (applied: proceed, deferred:
    /// wait, proposalForOwner: escalate, contained: rollback, ...).
    public static func outcomeAction(_ outcome: Outcome) throws(ZxvError) -> HexAction {
        var a: Int32 = 0
        try check(zxv_paradox_outcome_action(outcome.rawValue, &a))
        return try cEnum(UInt8(truncatingIfNeeded: a), "hex action")
    }

    /// Evidence counts before and after: the trend `classify` reads.
    public struct Evidence: Sendable, Equatable {
        /// Evidence for, before.
        public var pos0: UInt32
        /// Evidence against, before.
        public var neg0: UInt32
        /// Evidence for, after.
        public var pos1: UInt32
        /// Evidence against, after.
        public var neg1: UInt32
        /// Build from the four counts.
        public init(pos0: UInt32, neg0: UInt32, pos1: UInt32, neg1: UInt32) {
            self.pos0 = pos0
            self.neg0 = neg0
            self.pos1 = pos1
            self.neg1 = neg1
        }
    }

    /// What `classify` decided.
    public struct Classification: Sendable, Equatable {
        /// The kind.
        public let kind: Kind
        /// The tag with the kind's polarity (forbidden stays forbidden).
        public let tag: Hexagram
        /// The registry entry that decided it, or -1.
        public let registryIndex: Int32
    }

    static func classify(_ book: OpaquePointer?, _ tag: Hexagram, _ evidence: Evidence?, _ stale: Bool,
                         _ sig: [UInt8]?, _ context: ZxvContext?) throws(ZxvError) -> Classification {
        if let sig { try needSize(sig.count, 32, "paradox signature") }
        let ev: [UInt32]? = evidence.map { [$0.pos0, $0.neg0, $0.pos1, $0.neg1] }
        var k: Int32 = 0, t: UInt8 = 0, idx: Int32 = -1
        try check(zxv_paradox_classify(book, tag.tag, ev, stale ? 1 : 0, sig, &k, &t, &idx), context)
        return Classification(kind: try cEnum(k, "paradox kind"), tag: Hexagram.all[Int(t & 63)], registryIndex: idx)
    }

    /// Classify a tag without a registry (no `Book`). `evidence` nil means no
    /// trend (polarity neutral); `stale` says the newest evidence is past
    /// your freshness limit (ignored without evidence). Pure.
    public static func classify(_ tag: Hexagram, evidence: Evidence? = nil, stale: Bool = false) throws(ZxvError) -> Classification {
        try classify(nil, tag, evidence, stale, nil, nil)
    }

    /// A RepairProof (`repairBytes`, little-endian), returned by
    /// `Book.destructive`: a value type. Share `signature` with peers: they
    /// add it to their registries as `.destructive` to recognise the trap.
    /// Signing the proof is the caller's job.
    public struct RepairProof: Sendable, Hashable {
        /// The proof's bytes.
        public let bytes: [UInt8]
        /// A proof from its bytes (size checked; content not verified).
        public init(bytes: [UInt8]) throws(ZxvError) {
            try needSize(bytes.count, Paradox.repairBytes, "RepairProof")
            self.bytes = bytes
        }
        /// The checks that fired.
        public var trips: Trips { Trips(rawValue: le32(bytes, 0)) }
        /// Steps taken when it was detected.
        public var steps: UInt32 { le32(bytes, 4) }
        /// Budget spent when it was detected.
        public var spent: UInt32 { le32(bytes, 8) }
        /// Number of state fingerprints checked.
        public var traceCount: UInt32 { le32(bytes, 12) }
        /// Who submitted the work.
        public var submitter: UInt32 { le32(bytes, 16) }
        /// The state length.
        public var stateLength: UInt32 { le32(bytes, 20) }
        /// SHA-256 of the state as found.
        public var stateFound: [UInt8] { digest32(bytes, 24) }
        /// SHA-256 of the state after rollback.
        public var stateRestored: [UInt8] { digest32(bytes, 56) }
        /// The signature to share with peers (`signature` of the pattern).
        public var signature: [UInt8] { digest32(bytes, 88) }
        /// The proof hash, SHA-256("zxv/pdx-repair/v1" || bytes[0..<120]).
        public var proofHash: [UInt8] { digest32(bytes, 120) }
        /// The quarantine record's index.
        public var quarantineIndex: UInt64 { le64(bytes, 152) }
        /// True when the proof hash matches its bytes (integrity only; a
        /// signature over the proof is the caller's to verify).
        public func verify() throws(ZxvError) -> Bool {
            let st = zxv_paradox_repair_verify(bytes)
            if st == ZXV_E_VERIFY { return false }
            try check(st)
            return true
        }
    }

    /// A draft classification of a frontier record (`draftBytes`), from
    /// `Book.draft` or sent by a peer, for `Book.review`: a value type.
    public struct Draft: Sendable, Hashable {
        /// The draft's bytes.
        public let bytes: [UInt8]
        /// A draft from its bytes (size checked; `Book.review` checks content).
        public init(bytes: [UInt8]) throws(ZxvError) {
            try needSize(bytes.count, Paradox.draftBytes, "paradox draft")
            self.bytes = bytes
        }
        /// The frontier record index.
        public var record: UInt32 { le32(bytes, 0) }
        /// The frontier record's hash.
        public var recordHash: [UInt8] { digest32(bytes, 4) }
        /// The record's signature.
        public var signature: [UInt8] { digest32(bytes, 36) }
        /// The proposed kind (raw; see `Kind`).
        public var kindRaw: Int32 { Int32(bytes[68]) }
        /// The proposed kind, or nil for a value this SDK does not know.
        public var kind: Kind? { Kind(rawValue: kindRaw) }
        /// The caller's reason code.
        public var reason: UInt8 { bytes[69] }
        /// Its flags.
        public var flags: Flags { Flags(rawValue: bytes[70]) }
    }

    /// One registry entry (append-only: never edited).
    public struct RegistryEntry: Sendable, Equatable {
        /// The anomaly's signature.
        public let signature: [UInt8]
        /// Constructive, destructive or neutral.
        public let kind: Kind
        /// The caller's reason code.
        public let reason: UInt8
        /// Its flags.
        public let flags: Flags
        /// The frontier record it was learned from, or `sourceSeed`.
        public let source: UInt64
    }

    /// An operator or frontier log record (`recordBytes`), parsed.
    public struct OpRecord: Sendable, Equatable {
        /// Its index.
        public let seq: UInt64
        /// 1 constructive, 2 destructive, 3 neutral, 4 special, 5 draft,
        /// 6 review/registry, 7 graph pass.
        public let op: UInt8
        /// The kind it ran for.
        public let kind: UInt8
        /// The tag it ran for.
        public let tag: UInt8
        /// The raw `Outcome`.
        public let outcome: UInt8
        /// Per-op detail (see the C header).
        public let aux: UInt32
        /// Per-op hash a.
        public let a: [UInt8]
        /// Per-op hash b.
        public let b: [UInt8]
        /// The previous record's hash.
        public let prev: [UInt8]
        /// SHA-256("zxv/pdx-log/v1" || bytes[0..<112]).
        public let hash: [UInt8]

        /// Parse record bytes (size checked).
        public init(bytes r: [UInt8]) throws(ZxvError) {
            try needSize(r.count, Paradox.recordBytes, "paradox log record")
            seq = le64(r, 0)
            op = r[8]
            kind = r[9]
            tag = r[10]
            outcome = r[11]
            aux = le32(r, 12)
            a = digest32(r, 16)
            b = digest32(r, 48)
            prev = digest32(r, 80)
            hash = digest32(r, 112)
        }
    }

    /// Parse a quarantine log record (`quarantineRecordBytes`) into the ABI
    /// 1.3 `QuarantineRecord` (`reason` holds the `Trips` bits).
    public static func parseQuarantineRecord(_ r: [UInt8]) throws(ZxvError) -> QuarantineRecord {
        try needSize(r.count, quarantineRecordBytes, "paradox quarantine record")
        return QuarantineRecord(seq: le64(r, 0), tag: Hexagram.all[Int(r[8] & 63)], reason: r[9],
                                submitter: le32(r, 10), subject: digest32(r, 14), prev: digest32(r, 46),
                                hash: digest32(r, 78))
    }

    /// The live state of a `Policy`.
    public struct PolicyState: Sendable, Equatable {
        /// The live values.
        public let values: [Int64]
        /// The values before the last applied update.
        public let previous: [Int64]
        /// An applied update awaits evaluation.
        public let armed: Bool
        /// The tension it was applied at.
        public let tensionAt: UInt32
    }

    /// A constructive policy: n parameters (1...`policyMax`) with published
    /// bounds that never change. The values are swarm parameters (budget
    /// shares, routing weights, escrow timing), never balances. A policy
    /// flagged `.money` or `.device` is never changed automatically: every
    /// update becomes a proposal for the owner. Needs `.paradox`. Not
    /// thread-safe.
    public final class Policy {
        let cap: Capability
        let cell: HandleCell
        /// The number of parameters.
        public let count: UInt32

        /// A policy with bounds lower[i] <= upper[i] and initial values
        /// inside them. `.usage` `ZXV_E_ARG` (sizes differ, n out of range,
        /// unknown flags), `ZXV_E_RANGE` (lo > hi, or an initial value
        /// outside its bounds).
        public init(_ cap: Capability, lower: [Int64], upper: [Int64], initial: [Int64],
                    flags: Flags = []) throws(ZxvError) {
            guard upper.count == lower.count, initial.count == lower.count, lower.count <= Int(UInt32.max) else {
                throw usage("paradox policy: lower, upper and initial differ in size")
            }
            self.cap = cap
            count = UInt32(lower.count)
            var p: OpaquePointer?
            let n = count
            try cap.call { zxv_paradox_policy_create($0, n, lower, upper, initial, UInt32(flags.rawValue), &p) }
            cell = HandleCell(p!, "Paradox.Policy") { zxv_paradox_policy_destroy($0) }
        }

        private func read(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
            let p = try cell.acquire()
            defer { cell.release() }
            try cap.read { body(p) }
        }

        /// The live values, previous values, armed flag and tension at apply.
        public func state() throws(ZxvError) -> PolicyState {
            var v = [Int64](repeating: 0, count: Int(count)), pv = v
            var armed: Int32 = 0, at: UInt32 = 0
            let n = count
            try read { zxv_paradox_policy_values($0, &v, &pv, n, &armed, &at) }
            return PolicyState(values: v, previous: pv, armed: armed != 0, tensionAt: at)
        }

        /// The live values.
        public func values() throws(ZxvError) -> [Int64] { try state().values }

        /// The lower bounds.
        public func lower() throws(ZxvError) -> [Int64] {
            var v = [Int64](repeating: 0, count: Int(count))
            let n = count
            try read { zxv_paradox_policy_bounds($0, &v, nil, n, nil) }
            return v
        }

        /// The upper bounds.
        public func upper() throws(ZxvError) -> [Int64] {
            var v = [Int64](repeating: 0, count: Int(count))
            let n = count
            try read { zxv_paradox_policy_bounds($0, nil, &v, n, nil) }
            return v
        }

        /// The flags.
        public func flags() throws(ZxvError) -> Flags {
            var f: UInt32 = 0
            let n = count
            try read { zxv_paradox_policy_bounds($0, nil, nil, n, &f) }
            return Flags(rawValue: UInt8(truncatingIfNeeded: f))
        }

        /// A proposed update as bytes for the owner or for signing
        /// (`proposalBytes(n)`, little-endian: n, how many values are out of
        /// bounds, flags, then lo, hi, current, proposed per entry). Reads only.
        public func proposal(_ synth: [Int64]) throws(ZxvError) -> [UInt8] {
            let p = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context, guess: Paradox.proposalBytes(count)) {
                zxv_paradox_policy_proposal(p, synth, UInt32(truncatingIfNeeded: synth.count), $0, $1, $2)
            }
        }

        /// Release the policy now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// One branch of a `Hold`.
    public struct Branch: Sendable, Equatable {
        /// The candidate's 32-byte digest.
        public let id: [UInt8]
        /// Evidence for.
        public let pos: UInt32
        /// Evidence against.
        public let neg: UInt32
    }

    /// A `Hold`'s summary.
    public struct HoldInfo: Sendable, Equatable {
        /// Branches.
        public let count: UInt32
        /// The settled branch, -1 while deferred.
        public let settled: Int32
        /// The branch with decisive evidence now, -1 if none.
        public let decisive: Int32
    }

    /// A neutral hold of up to `capacity` (1...`holdMax`) candidate branches.
    /// A branch settles (`Book.neutral`) when its net evidence leads every
    /// other by at least `decisive` (0 means 1), or alone reaches it, or the
    /// owner picks it. Creating needs `.paradox`; adding branches and
    /// evidence needs none. Not thread-safe.
    public final class Hold {
        let cap: Capability
        let cell: HandleCell

        /// A new, empty hold.
        public init(_ cap: Capability, capacity: UInt32, decisive: UInt32 = 0) throws(ZxvError) {
            self.cap = cap
            var h: OpaquePointer?
            try cap.call { zxv_paradox_hold_create($0, capacity, decisive, &h) }
            cell = HandleCell(h!, "Paradox.Hold") { zxv_paradox_hold_destroy($0) }
        }

        private func read(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
            let h = try cell.acquire()
            defer { cell.release() }
            try cap.read { body(h) }
        }

        /// Add a candidate (the same id again is the same candidate).
        /// `.refused` `ZXV_E_STATE` (settled), `ZXV_E_CAPACITY` (full).
        public func add(_ id: [UInt8]) throws(ZxvError) {
            try needSize(id.count, 32, "branch id")
            try read { zxv_paradox_hold_add($0, id) }
        }

        /// One piece of evidence for (`positive`) or against a candidate.
        /// `.usage` `ZXV_E_NOT_FOUND` (unknown id), `.refused` `ZXV_E_STATE`.
        public func evidence(_ id: [UInt8], positive: Bool) throws(ZxvError) {
            try needSize(id.count, 32, "branch id")
            try read { zxv_paradox_hold_evidence($0, id, positive ? 1 : 0) }
        }

        /// The branch count, settled branch and decisive branch.
        public func info() throws(ZxvError) -> HoldInfo {
            var c: UInt32 = 0, s: Int32 = 0, d: Int32 = 0
            try read { zxv_paradox_hold_info($0, &c, &s, &d) }
            return HoldInfo(count: c, settled: s, decisive: d)
        }

        /// Branch `index`. `.usage` `ZXV_E_NOT_FOUND`.
        public func branch(_ index: UInt32) throws(ZxvError) -> Branch {
            var id = [UInt8](repeating: 0, count: 32), p: UInt32 = 0, n: UInt32 = 0
            try read { zxv_paradox_hold_branch($0, index, &id, &p, &n) }
            return Branch(id: id, pos: p, neg: n)
        }

        /// Release the hold now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// One graph node, in id order.
    public struct GraphNode: Sendable, Equatable {
        /// The hash of its signed input.
        public let id: [UInt8]
        /// Its current kind.
        public let kind: Kind
        /// Its state.
        public let state: NodeState
    }

    /// A `Graph`'s summary.
    public struct GraphInfo: Sendable, Equatable {
        /// Node count.
        public let nodes: UInt32
        /// Passes run.
        public let passes: UInt32
        /// Net tension of the open nodes: magnitude.
        public let totalMagnitude: UInt32
        /// Net tension of the open nodes: direction -1/0/+1.
        public let totalDirection: Int32
        /// Reached the step limit: the owner decides.
        public let escalated: Bool
        /// SHA-256 chain over every pass (zeros before the first).
        public let chain: [UInt8]
    }

    /// A complex paradox: a dependency graph of up to `graphMax` nodes,
    /// resolved by `Book.pass` in a fixed order (destructive and forbidden
    /// contained, and their dependants tainted; special sandboxed, dependants
    /// wait; neutral held; constructive applied where nothing reaches it).
    /// Nodes are taken in id order, so every peer with the same signed inputs
    /// reaches the same states and chain. It escalates to the owner after
    /// `stepLimit` passes (>= 1) without resolving. Creating needs
    /// `.paradox`; adding nodes, edges and updates needs none. Not
    /// thread-safe.
    public final class Graph {
        let cap: Capability
        let cell: HandleCell

        /// A new, empty graph.
        public init(_ cap: Capability, stepLimit: UInt32) throws(ZxvError) {
            self.cap = cap
            var g: OpaquePointer?
            try cap.call { zxv_paradox_graph_create($0, stepLimit, &g) }
            cell = HandleCell(g!, "Paradox.Graph") { zxv_paradox_graph_destroy($0) }
        }

        private func read(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
            let g = try cell.acquire()
            defer { cell.release() }
            try cap.read { body(g) }
        }

        /// Add node `id` (a classified pair, tension magnitude and direction
        /// -1/0/+1) before the first pass. `.usage` `ZXV_E_DUPLICATE`,
        /// `.refused` `ZXV_E_CAPACITY` / `ZXV_E_STATE` (after the first pass).
        public func addNode(_ id: [UInt8], kind: Kind, tag: Hexagram, magnitude: UInt32,
                            direction: Int32) throws(ZxvError) {
            try needSize(id.count, 32, "node id")
            try read { zxv_paradox_graph_add_node($0, id, kind.rawValue, tag.tag, magnitude, direction) }
        }

        /// Node `id` depends on node `dependency` (which may be added later).
        public func addDependency(_ id: [UInt8], on dependency: [UInt8]) throws(ZxvError) {
            try needSize(id.count, 32, "node id")
            try needSize(dependency.count, 32, "dependency id")
            try read { zxv_paradox_graph_add_dep($0, id, dependency) }
        }

        /// Queue new evidence for a node, applied at the next pass ("worst
        /// wins" when several are queued for one node).
        public func update(_ id: [UInt8], kind: Kind, tag: Hexagram, magnitude: UInt32,
                           direction: Int32) throws(ZxvError) {
            try needSize(id.count, 32, "node id")
            try read { zxv_paradox_graph_update($0, id, kind.rawValue, tag.tag, magnitude, direction) }
        }

        /// A node's state (`.open` before the first pass).
        public func state(_ id: [UInt8]) throws(ZxvError) -> NodeState {
            try needSize(id.count, 32, "node id")
            var s: Int32 = 0
            try read { zxv_paradox_graph_state($0, id, &s) }
            return try cEnum(s, "paradox node state")
        }

        /// Node `index` in id order (after the first pass, else `.refused`
        /// `ZXV_E_STATE`).
        public func node(at index: UInt32) throws(ZxvError) -> GraphNode {
            var id = [UInt8](repeating: 0, count: 32), k: Int32 = 0, s: Int32 = 0
            try read { zxv_paradox_graph_node_at($0, index, &id, &k, &s) }
            return GraphNode(id: id, kind: try cEnum(k, "paradox kind"), state: try cEnum(s, "paradox node state"))
        }

        /// The graph's summary.
        public func info() throws(ZxvError) -> GraphInfo {
            var n: UInt32 = 0, p: UInt32 = 0, m: UInt32 = 0, d: Int32 = 0, e: Int32 = 0
            var chain = [UInt8](repeating: 0, count: 32)
            try read { zxv_paradox_graph_info($0, &n, &p, &m, &d, &e, &chain) }
            return GraphInfo(nodes: n, passes: p, totalMagnitude: m, totalDirection: d, escalated: e != 0, chain: chain)
        }

        /// Release the graph now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// The inputs of `Book.destructive`'s checks.
    public struct Containment: Sendable, Equatable {
        /// Steps taken.
        public var steps: UInt32
        /// Trips when steps >= it (0: no limit).
        public var stepLimit: UInt32
        /// Budget spent.
        public var spent: UInt32
        /// Trips when spent >= it (0: no budget).
        public var budget: UInt32
        /// State fingerprints in visit order (trips on a repeat).
        public var trace: [UInt64]
        /// The caller's invariant check result.
        public var invariantOk: Bool
        /// A canonical description of the trap (its signature is shared).
        public var pattern: [UInt8]
        /// Who submitted the work.
        public var submitter: UInt32
        /// Build the inputs.
        public init(steps: UInt32 = 0, stepLimit: UInt32 = 0, spent: UInt32 = 0, budget: UInt32 = 0,
                    trace: [UInt64] = [], invariantOk: Bool = true, pattern: [UInt8] = [], submitter: UInt32 = 0) {
            self.steps = steps
            self.stepLimit = stepLimit
            self.spent = spent
            self.budget = budget
            self.trace = trace
            self.invariantOk = invariantOk
            self.pattern = pattern
            self.submitter = submitter
        }
    }

    /// `Book.destructive`'s result.
    public struct Contained: Sendable, Equatable {
        /// `.contained`, or `.refused` (nothing changed).
        public let outcome: Outcome
        /// The proof when contained.
        public let proof: RepairProof?
    }

    /// `Book.neutral`'s result.
    public struct Settlement: Sendable, Equatable {
        /// `.settled`, `.deferred`, `.rolledBack` or `.refused`.
        public let outcome: Outcome
        /// The settled branch, or -1.
        public let settled: Int32
    }

    /// `Book.special`'s result.
    public struct FrontierEntry: Sendable, Equatable {
        /// `.recorded`, `.refused` or `.rolledBack`.
        public let outcome: Outcome
        /// The frontier index when recorded.
        public let record: UInt32?
    }

    /// The paradox book: the operator, frontier and quarantine logs and the
    /// signature registry, and the operators that write them. Internally
    /// serialized (thread-safe). It keeps its capability and context alive.
    /// `close()` releases it at once.
    public final class Book: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// A book with three empty logs of `logCapacity` records
        /// (1...`logMax`) and a registry of `registryCapacity` entries
        /// (1...`registryMax`). Needs `.paradox`.
        public init(_ cap: Capability, logCapacity: UInt32, registryCapacity: UInt32) throws(ZxvError) {
            self.cap = cap
            var b: OpaquePointer?
            try cap.call { zxv_paradox_create($0, logCapacity, registryCapacity, &b) }
            cell = HandleCell(b!, "Paradox.Book") { zxv_paradox_destroy($0) }
        }

        private func read(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
            let b = try cell.acquire()
            defer { cell.release() }
            try cap.read { body(b) }
        }

        private func gated(_ body: (OpaquePointer, OpaquePointer) -> Int32) throws(ZxvError) {
            let b = try cell.acquire()
            defer { cell.release() }
            try cap.call { body($0, b) }
        }

        /// Classify a tag with this book's registry: a known `signature`
        /// overrides the trend for Both and Neither. Reads only.
        public func classify(_ tag: Hexagram, evidence: Evidence? = nil, stale: Bool = false,
                             signature: [UInt8]? = nil) throws(ZxvError) -> Classification {
            let b = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try Paradox.classify(b, tag, evidence, stale, signature, cap.context)
        }

        /// Add a reviewed signature as kind constructive, destructive or
        /// neutral; returns its index. An entry flagged `.money` or `.device`
        /// needs `ownerOk`, else `.refused` (`.ownerApprovalRequired`).
        /// `.usage` `ZXV_E_DUPLICATE` (already registered), `.refused`
        /// `ZXV_E_CAPACITY`. Needs `.paradox`.
        @discardableResult
        public func registryAdd(_ signature: [UInt8], kind: Kind, reason: UInt8, flags: Flags = [],
                                ownerOk: Bool = false) throws(ZxvError) -> UInt32 {
            try needSize(signature.count, 32, "paradox signature")
            var i: UInt32 = 0
            try gated { zxv_paradox_registry_add($0, $1, signature, kind.rawValue, reason, flags.rawValue, ownerOk ? 1 : 0, &i) }
            return i
        }

        /// The index of a signature, or nil.
        public func registryFind(_ signature: [UInt8]) throws(ZxvError) -> UInt32? {
            try needSize(signature.count, 32, "paradox signature")
            let b = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            var i: UInt32 = 0
            let st = zxv_paradox_registry_find(b, signature, &i)
            if st == ZXV_E_NOT_FOUND { return nil }
            try check(st, cap.context)
            return i
        }

        /// The number of registry entries.
        public func registryCount() throws(ZxvError) -> UInt32 {
            var n: UInt32 = 0
            try read { zxv_paradox_registry_count($0, &n) }
            return n
        }

        /// Registry entry `index`. `.usage` `ZXV_E_NOT_FOUND`.
        public func registryEntry(at index: UInt32) throws(ZxvError) -> RegistryEntry {
            var sig = [UInt8](repeating: 0, count: 32), k: Int32 = 0, r: UInt8 = 0, f: UInt8 = 0, src: UInt64 = 0
            try read { zxv_paradox_registry_at($0, index, &sig, &k, &r, &f, &src) }
            return RegistryEntry(signature: sig, kind: try cEnum(k, "paradox kind"), reason: r,
                                 flags: Flags(rawValue: f), source: src)
        }

        /// The record count of a log.
        public func logCount(_ which: Log) throws(ZxvError) -> UInt32 {
            var n: UInt32 = 0
            try read { zxv_paradox_log_count($0, which.rawValue, &n) }
            return n
        }

        /// A log's chain head (32 zeros when empty).
        public func logHead(_ which: Log) throws(ZxvError) -> [UInt8] {
            var h = [UInt8](repeating: 0, count: 32)
            try read { zxv_paradox_log_head($0, which.rawValue, &h) }
            return h
        }

        /// Record `index` of a log as bytes (`recordBytes`, or
        /// `quarantineRecordBytes` for `.quarantine`). `.usage`
        /// `ZXV_E_NOT_FOUND` past the end.
        public func logRecord(_ which: Log, at index: UInt32) throws(ZxvError) -> [UInt8] {
            let b = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context, guess: Paradox.recordBytes) {
                zxv_paradox_log_record(b, which.rawValue, index, $0, $1, $2)
            }
        }

        /// Operator or frontier record `index`, parsed (`.usage` `ZXV_E_ARG`
        /// for `.quarantine`: use `quarantineRecord`).
        public func opRecord(_ which: Log, at index: UInt32) throws(ZxvError) -> OpRecord {
            if which == .quarantine { throw usage("opRecord: use quarantineRecord for the quarantine log") }
            return try OpRecord(bytes: try logRecord(which, at: index))
        }

        /// Quarantine record `index`, parsed.
        public func quarantineRecord(at index: UInt32) throws(ZxvError) -> QuarantineRecord {
            try Paradox.parseQuarantineRecord(try logRecord(.quarantine, at: index))
        }

        /// Recompute every link of a log. A broken chain is `.fatal`
        /// (`ZXV_E_INVARIANT`) and poisons the context (fail closed).
        public func logVerify(_ which: Log) throws(ZxvError) {
            try read { zxv_paradox_log_verify($0, which.rawValue) }
        }

        /// One constructive evaluation with an optional proposed update
        /// `synth` (one value per policy parameter; nil evaluates only).
        /// Outcomes: `.applied` (in bounds, unflagged), `.kept` (an armed
        /// update survives evaluation), `.proposalForOwner` (out of bounds or
        /// a money/device policy: nothing changes), `.rolledBack` (tension
        /// above the tension at apply, or a forbidden tag), `.refused` (kind
        /// not constructive). `.usage` `ZXV_E_ARG` (a bad pair, a size
        /// mismatch). Needs `.paradox`.
        public func constructive(_ policy: Policy, kind: Kind, tag: Hexagram, synth: [Int64]? = nil,
                                 tension: UInt32) throws(ZxvError) -> Outcome {
            if let synth, synth.count != Int(policy.count) {
                throw usage("paradox constructive: synth size mismatch")
            }
            let p = try policy.cell.acquire()
            defer { policy.cell.release() }
            var out: Int32 = 0
            let n = policy.count
            try gated { zxv_paradox_constructive($0, $1, p, kind.rawValue, tag.tag, synth, n, tension, &out) }
            return try cEnum(out, "paradox outcome")
        }

        /// Detect and contain. When the kind is destructive or any check in
        /// `containment` fires, the work is quarantined, `state` is restored
        /// in place from `checkpoint` (same size) and the result carries the
        /// `RepairProof`; otherwise the outcome is `.refused` and nothing
        /// changes. It contains: there is no way to reply to or act against
        /// the submitter. Rollback is always done, even when `.refused`
        /// `ZXV_E_CAPACITY` (a log full: halt) is thrown. `.usage`
        /// `ZXV_E_RANGE` (trace over `traceMax`). Needs `.paradox`.
        public func destructive(kind: Kind, tag: Hexagram, state: inout [UInt8], checkpoint: [UInt8],
                                containment c: Containment) throws(ZxvError) -> Contained {
            if checkpoint.count != state.count {
                throw usage("paradox destructive: state and checkpoint differ in size")
            }
            guard state.count <= Int(UInt32.max), c.trace.count <= Int(UInt32.max), c.pattern.count <= Int(UInt32.max) else {
                throw usage("paradox destructive: input too long", ZXV_E_RANGE)
            }
            var proof = [UInt8](repeating: 0, count: Paradox.repairBytes)
            var out: Int32 = 0
            let len = UInt32(state.count)
            try gated { capPtr, b in
                state.withUnsafeMutableBufferPointer { s in
                    zxv_paradox_destructive(capPtr, b, kind.rawValue, tag.tag, c.steps, c.stepLimit, c.spent, c.budget,
                                            c.trace, UInt32(c.trace.count), c.invariantOk ? 1 : 0,
                                            s.count == 0 ? nil : s.baseAddress, checkpoint, len, c.pattern,
                                            UInt32(c.pattern.count), c.submitter, &out, &proof)
                }
            }
            let outcome: Outcome = try cEnum(out, "paradox outcome")
            return Contained(outcome: outcome, proof: outcome == .contained ? try RepairProof(bytes: proof) : nil)
        }

        /// Settle a hold on decisive evidence or on `ownerPick` (a branch
        /// index); otherwise keep every branch. Outcomes: `.settled`,
        /// `.deferred`, `.rolledBack` (forbidden), `.refused` (kind not
        /// neutral). `.usage` `ZXV_E_NOT_FOUND` (`ownerPick` past the
        /// branches). Needs `.paradox`.
        public func neutral(_ hold: Hold, kind: Kind, tag: Hexagram, ownerPick: UInt32? = nil) throws(ZxvError) -> Settlement {
            var pick: Int32 = -1
            if let ownerPick {
                guard ownerPick <= UInt32(Int32.max) else { throw usage("paradox neutral: ownerPick out of range") }
                pick = Int32(ownerPick)
            }
            let h = try hold.cell.acquire()
            defer { hold.cell.release() }
            var out: Int32 = 0, settled: Int32 = -1
            let p = pick
            try gated { zxv_paradox_neutral($0, $1, h, kind.rawValue, tag.tag, p, &out, &settled) }
            return Settlement(outcome: try cEnum(out, "paradox outcome"), settled: settled)
        }

        /// Record an unclassified paradox: a permanent frontier record
        /// (`signature`, `subject` = the work's digest), never edited or
        /// erased. Outcomes: `.recorded` (with the frontier index),
        /// `.refused` (kind not special), `.rolledBack` (forbidden). The work
        /// stays sandboxed (not run) until a reviewed classification exists.
        /// Needs `.paradox`.
        public func special(kind: Kind, tag: Hexagram, signature: [UInt8], subject: [UInt8]) throws(ZxvError) -> FrontierEntry {
            try needSize(signature.count, 32, "paradox signature")
            try needSize(subject.count, 32, "subject digest")
            var out: Int32 = 0, rec: UInt32 = 0
            try gated { zxv_paradox_special($0, $1, kind.rawValue, tag.tag, signature, subject, &out, &rec) }
            let outcome: Outcome = try cEnum(out, "paradox outcome")
            return FrontierEntry(outcome: outcome, record: outcome == .recorded ? rec : nil)
        }

        /// A draft classification of frontier record `record` for review
        /// (kind constructive, destructive or neutral). `.usage`
        /// `ZXV_E_NOT_FOUND` (no such record). Needs `.paradox`.
        public func draft(record: UInt32, kind: Kind, reason: UInt8, flags: Flags = []) throws(ZxvError) -> Draft {
            var d = [UInt8](repeating: 0, count: Paradox.draftBytes)
            try gated { zxv_paradox_draft($0, $1, record, kind.rawValue, reason, flags.rawValue, &d) }
            return try Draft(bytes: d)
        }

        /// Review a draft (ours or a peer's; it must match this book's
        /// frontier record). A money or device draft needs `ownerOk`; others
        /// `ownerOk` or `consensusOk` (your quorum result). Outcomes:
        /// `.accepted` (a new registry entry), `.proposalForOwner` (not
        /// approved), `.refused` (already registered, or registry full).
        /// `.refused` `.verificationFailed` (the hash or signature does not
        /// match the record). Needs `.paradox`.
        public func review(_ draft: Draft, ownerOk: Bool, consensusOk: Bool) throws(ZxvError) -> Outcome {
            var out: Int32 = 0
            try gated { zxv_paradox_review($0, $1, draft.bytes, ownerOk ? 1 : 0, consensusOk ? 1 : 0, &out) }
            return try cEnum(out, "paradox outcome")
        }

        /// One pass over a graph (logged): `.resolved` or `.pending`. When
        /// the pass count reaches the step limit unresolved, the pass is
        /// logged and `.refused` `.paradoxStepLimit` (`ZXV_E_PARADOX_STEP`)
        /// is thrown: the graph stays readable and goes to the owner.
        /// `.usage` `ZXV_E_NOT_FOUND` (a dependency names no node; first pass
        /// only). Needs `.paradox`.
        public func pass(_ graph: Graph) throws(ZxvError) -> Outcome {
            let g = try graph.cell.acquire()
            defer { graph.cell.release() }
            var out: Int32 = 0
            try gated { zxv_paradox_graph_pass($0, $1, g, &out) }
            return try cEnum(out, "paradox outcome")
        }

        /// Release the book now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }
}
