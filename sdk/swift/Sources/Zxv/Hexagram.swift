// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// The four-valued base of a `Hexagram` (bits 5-4), numbered by line
/// pattern. NOT `Truth`'s numbering (`.true` is 1 there and 3 here):
/// convert only with `Hexagram.toTruth()` and `Hexagram(truth:polarity:phase:)`.
public enum HexBase: UInt8, Sendable, CaseIterable, CustomStringConvertible {
    case `false` = 0, neither = 1, both = 2, `true` = 3
    /// "false", "neither", "both" or "true".
    public var description: String { ["false", "neither", "both", "true"][Int(rawValue)] }
}

/// The polarity of a `Hexagram`'s evidence (bits 3-2). `.forbidden` is absorbing.
public enum HexPolarity: UInt8, Sendable, CaseIterable, CustomStringConvertible {
    /// The evidence margin is shrinking, or the evidence is stale.
    case entropic = 0
    /// The margin is unchanged.
    case neutral = 1
    /// Forbidden work: rolled back, quarantined, never replayed.
    case forbidden = 2
    /// The margin is growing.
    case negentropic = 3
    /// "entropic", "neutral", "forbidden" or "negentropic".
    public var description: String { ["entropic", "neutral", "forbidden", "negentropic"][Int(rawValue)] }
}

/// The lifecycle phase of a `Hexagram` (bits 1-0). The cycle is
/// potential -> emerging -> manifest -> dissolving -> potential.
public enum HexPhase: UInt8, Sendable, CaseIterable, CustomStringConvertible {
    case potential = 0, emerging = 1, dissolving = 2, manifest = 3
    /// The next phase on the cycle (`zxv_hex_phase_next`).
    public var next: HexPhase {
        var o: UInt8 = 0
        _ = zxv_hex_phase_next(rawValue, &o)
        return HexPhase(rawValue: o)!
    }
    /// "potential", "emerging", "dissolving" or "manifest".
    public var description: String { ["potential", "emerging", "dissolving", "manifest"][Int(rawValue)] }
}

/// What happens next to work with a given `Hexagram`.
public enum HexAction: UInt8, Sendable, CaseIterable, CustomStringConvertible {
    case proceed = 0, wait = 1, refuse = 2, escalate = 3, rollback = 4
    /// "proceed", "wait", "refuse", "escalate" or "rollback" (`zxv_hex_action_name`).
    public var description: String { String(cString: zxv_hex_action_name(Int32(rawValue))) }
}

/// The 64-state hexagram tag of ABI 1.3 (the kernel's lpres_hex): an
/// immutable value tagging one unit of work with a four-valued `HexBase`,
/// the `HexPolarity` of its evidence and the `HexPhase` of its lifecycle,
/// packed as `tag = base << 4 | polarity << 2 | phase` (0...63).
///
///     var h = Hexagram(base: .true, polarity: .negentropic, phase: .potential)
///     h.action                         // .proceed
///     h = h.advance()                  // .emerging
///     try h.step(to: .dissolving)      // throws .refused (ZXV_E_STATE): a jump
///
/// Every rule is computed by libzxv. There is no conversion to `Bool` and
/// the only one to `Truth` is `toTruth()`.
public struct Hexagram: Hashable, Sendable, CustomStringConvertible, CustomDebugStringConvertible {
    /// The byte value, 0...63.
    public let tag: UInt8

    /// Number of tags (`ZXV_HEX_STATES`).
    public static let states = Int(ZXV_HEX_STATES)

    private init(raw: UInt8) { tag = raw }

    /// The tag from its three fields (`zxv_hex_pack`).
    public init(base: HexBase, polarity: HexPolarity, phase: HexPhase) {
        var o: UInt8 = 0
        _ = zxv_hex_pack(base.rawValue, polarity.rawValue, phase.rawValue, &o)
        tag = o
    }

    /// The tag with this byte value; `.usage` (ZXV_E_ARG) above 63.
    public init(tag: UInt8) throws(ZxvError) {
        try check(zxv_hex_unpack(tag, nil, nil, nil))
        self.tag = tag
    }

    /// A tag whose base is `truth` (`zxv_hex_from_lp`).
    public init(truth: Truth, polarity: HexPolarity, phase: HexPhase) {
        var b: UInt8 = 0
        _ = zxv_hex_from_lp(truth.cValue, &b)
        self.init(base: HexBase(rawValue: b)!, polarity: polarity, phase: phase)
    }

    /// All 64 tags, in tag order.
    public static let all: [Hexagram] = (0..<64).map { Hexagram(raw: UInt8($0)) }

    /// Polarity from evidence counts before (`pos0`, `neg0`) and after
    /// (`pos1`, `neg1`): `stale` gives `.entropic`; otherwise the margin
    /// |pos - neg| growing gives `.negentropic`, shrinking `.entropic`,
    /// unchanged `.neutral`. Never `.forbidden`.
    public static func polarity(pos0: UInt32, neg0: UInt32, pos1: UInt32, neg1: UInt32,
                                stale: Bool) -> HexPolarity {
        var o: UInt8 = 0
        _ = zxv_hex_polarity(pos0, neg0, pos1, neg1, stale ? 1 : 0, &o)
        return HexPolarity(rawValue: o)!
    }

    /// Bits 5-4.
    public var base: HexBase { HexBase(rawValue: tag >> 4)! }
    /// Bits 3-2.
    public var polarity: HexPolarity { HexPolarity(rawValue: (tag >> 2) & 3)! }
    /// Bits 1-0.
    public var phase: HexPhase { HexPhase(rawValue: tag & 3)! }
    /// True if the polarity is `.forbidden` (absorbing).
    public var isForbidden: Bool { polarity == .forbidden }

    /// What happens next: one lookup in the kernel's 64-entry table.
    public var action: HexAction {
        var a: Int32 = 0
        _ = zxv_hex_action(tag, &a)
        return HexAction(rawValue: UInt8(a))!
    }

    /// "base/polarity/phase", e.g. "true/negentropic/manifest".
    public var name: String { String(cString: zxv_hex_name(tag)) }
    public var description: String { name }
    public var debugDescription: String { "Hexagram(\(name), tag=\(tag))" }

    /// The base as a `Truth`: the explicit conversion (`zxv_hex_to_lp`).
    public func toTruth() -> Truth {
        var t: Int32 = 0
        _ = zxv_hex_to_lp(tag >> 4, &t)
        return Truth(c: t)!
    }

    /// The same work in `phase`: the same phase or the next one only
    /// (`.refused`, ZXV_E_STATE, for a jump).
    public func step(to phase: HexPhase) throws(ZxvError) -> Hexagram {
        var o: UInt8 = 0
        try check(zxv_hex_step(tag, phase.rawValue, &o))
        return Hexagram(raw: o)
    }

    /// One step along the cycle.
    public func advance() -> Hexagram {
        var o: UInt8 = 0
        _ = zxv_hex_step(tag, phase.next.rawValue, &o)
        return Hexagram(raw: o)
    }

    /// New base and polarity, same phase. `.refused` (ZXV_E_STATE) if this
    /// tag is forbidden and `polarity` is not (forbidden is absorbing).
    public func with(base: HexBase, polarity: HexPolarity) throws(ZxvError) -> Hexagram {
        var o: UInt8 = 0
        try check(zxv_hex_set(tag, base.rawValue, polarity.rawValue, &o))
        return Hexagram(raw: o)
    }

    /// The same tag with polarity `.forbidden`.
    public func forbid() -> Hexagram { pure { zxv_hex_forbid(tag, $0) } }

    /// Conjunction: four-valued AND on the base, worst polarity, least advanced phase.
    public func and(_ other: Hexagram) -> Hexagram { pure { zxv_hex_and(tag, other.tag, $0) } }
    /// Disjunction: four-valued OR on the base, worst polarity, least advanced phase.
    public func or(_ other: Hexagram) -> Hexagram { pure { zxv_hex_or(tag, other.tag, $0) } }
    /// Negation of the base; polarity and phase are kept.
    public func not() -> Hexagram { pure { zxv_hex_not(tag, $0) } }

    /// `a.and(b)`.
    public static func & (a: Hexagram, b: Hexagram) -> Hexagram { a.and(b) }
    /// `a.or(b)`.
    public static func | (a: Hexagram, b: Hexagram) -> Hexagram { a.or(b) }
    /// `a.not()`.
    public static prefix func ~ (a: Hexagram) -> Hexagram { a.not() }

    private func pure(_ f: (UnsafeMutablePointer<UInt8>) -> Int32) -> Hexagram {
        var o: UInt8 = 0
        _ = f(&o)
        return Hexagram(raw: o)
    }
}

/// One record of a `QuarantineLog`.
public struct QuarantineRecord: Sendable, Equatable {
    /// Its index.
    public let seq: UInt64
    /// The forbidden tag.
    public let tag: Hexagram
    /// The caller's reason code.
    public let reason: UInt8
    /// Who submitted the work.
    public let submitter: UInt32
    /// A 32-byte digest of the refused work.
    public let subject: [UInt8]
    /// The previous record's hash (zeros for the first).
    public let prev: [UInt8]
    /// This record's hash.
    public let hash: [UInt8]
}

/// The quarantine log of ABI 1.3 (`zxv_hexlog`): an append-only, SHA-256
/// hash-chained record of forbidden work, read for audit and never
/// replayed. Creating and appending need `.attest`. Thread-safe (the C log
/// has its own mutex).
///
/// Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
/// reason || submitter (u32 LE) || subject || prev). A full log refuses the
/// append with ZXV_E_CAPACITY (`.refused`); the caller must then halt the
/// context (`ZxvContext.halt`), since forbidden work it cannot record must
/// not continue. Signing `head()` is the caller's job.
public final class QuarantineLog: @unchecked Sendable {
    /// Largest capacity (`ZXV_HEXLOG_MAX_RECORDS`).
    public static let maxRecords = UInt32(ZXV_HEXLOG_MAX_RECORDS)

    private let cap: Capability
    private let handle: OpaquePointer

    /// A log of at most `capacity` (1...maxRecords) records (needs `.attest`).
    public init(_ cap: Capability, capacity: UInt32) throws(ZxvError) {
        self.cap = cap
        var l: OpaquePointer?
        try cap.call { zxv_hexlog_create($0, capacity, &l) }
        handle = l!
    }
    deinit { zxv_hexlog_destroy(handle) }

    /// Quarantine forbidden work (`tag` must be forbidden, else `.usage`);
    /// returns the record's sequence number.
    @discardableResult
    public func append(_ tag: Hexagram, reason: UInt8, submitter: UInt32, subject: [UInt8]) throws(ZxvError) -> UInt64 {
        guard subject.count == 32 else {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "subject must be 32 bytes"))
        }
        var seq: UInt64 = 0
        try cap.call { zxv_hexlog_append($0, handle, tag.tag, reason, submitter, subject, &seq) }
        return seq
    }

    /// Number of records.
    public func count() throws(ZxvError) -> UInt32 {
        var n: UInt32 = 0
        try cap.call { _ in zxv_hexlog_count(handle, &n) }
        return n
    }

    /// The chain head: the last record's hash (zeros when empty).
    public func head() throws(ZxvError) -> [UInt8] {
        var h = [UInt8](repeating: 0, count: 32)
        try cap.call { _ in zxv_hexlog_head(handle, &h) }
        return h
    }

    /// Record `index`, oldest first.
    public func record(at index: UInt32) throws(ZxvError) -> QuarantineRecord {
        var seq: UInt64 = 0, tag: UInt8 = 0, reason: UInt8 = 0, who: UInt32 = 0
        var subject = [UInt8](repeating: 0, count: 32), prev = subject, hash = subject
        try cap.call { _ in zxv_hexlog_at(handle, index, &seq, &tag, &reason, &who, &subject, &prev, &hash) }
        return QuarantineRecord(seq: seq, tag: try Hexagram(tag: tag), reason: reason, submitter: who,
                                subject: subject, prev: prev, hash: hash)
    }

    /// Every record, oldest first.
    public func records() throws(ZxvError) -> [QuarantineRecord] {
        var out: [QuarantineRecord] = []
        for i in 0..<(try count()) { out.append(try record(at: i)) }
        return out
    }

    /// Recompute every link; a broken chain is ZXV_E_INVARIANT (`.fatal`:
    /// the context halts).
    public func verify() throws(ZxvError) {
        try cap.call { _ in zxv_hexlog_verify(handle) }
    }
}
