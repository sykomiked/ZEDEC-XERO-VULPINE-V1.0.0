// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: one treaty, a batch of settlement work: who takes part, what
/// each party deposits and may claim back, how the batch's dividend is
/// shared (each party's weight num/den), and who owes whom. A treaty has
/// two exact spellings:
///
/// * the RECORD, a fixed `Treaty.recordBytes`-byte COBOL copybook record
///   (ZXVTRTY.cpy, all DISPLAY text, so plain ASCII everywhere), and
/// * the SUTRA, compact text such as
///
///       TREATY CORRIDOR-7 USD SETL
///       PARTY ACME 5000 4000 1/2
///       PARTY BETA 3000 3000 1/2
///       OWE ACME BETA 1200
///       END
///
/// Both are canonical: parse then emit gives the input back byte for byte,
/// and the record and Sutra forms of one treaty compare equal. Every parse
/// and emit checks R1 (weights in lowest terms adding up to exactly 1), R2
/// (claim <= deposit), R3 (amounts fit PIC S9(18)) and R4 (1..16 distinct
/// parties, 0..64 obligations between two different known parties). A
/// refusal is `.usage` with status `ZXV_E_TREATY` and a `treatyFault`
/// carrying the `Fault` and, for a parse, the byte offset of the first fault.
///
/// Everything here is pure: no context, no capability. Running a treaty
/// works in a private scratch engine freed before the call returns; its
/// numbers are a deterministic report, not a payment.
///
/// Not thread-safe: use one treaty from one thread at a time. `close()`
/// releases it at once; later calls throw `.usage` (`ZXV_E_ARG`).
///
/// Honest limits: a treaty is a batch description, not a contract in law
/// (signatures, consent and authority live elsewhere); one asset per
/// treaty; the dividend shares are reported only, nothing is paid out.
public final class Treaty {
    /// Length of a ZXVTRTY.cpy record.
    public static let recordBytes = Int(ZXV_TREATY_REC_LEN)
    /// Length of a ZXVRSLT.cpy result record.
    public static let resultRecordBytes = Int(ZXV_TREATY_RESULT_LEN)
    /// Longest canonical Sutra text.
    public static let sutraMax = Int(ZXV_TREATY_SUTRA_MAX)
    /// Parties per treaty.
    public static let maxParties = UInt32(ZXV_TREATY_MAX_PARTIES)
    /// Obligations per treaty.
    public static let maxObligations = UInt32(ZXV_TREATY_MAX_OWES)
    /// Longest treaty name: A-Z 0-9 '-', starting with a letter.
    public static let nameMax = Int(ZXV_TREATY_NAME_MAX)
    /// Longest party id, same alphabet.
    public static let idMax = Int(ZXV_TREATY_ID_MAX)
    /// Largest PIC S9(18) magnitude.
    public static let pic18Max: Int64 = 999_999_999_999_999_999
    /// Largest weight term, PIC 9(9).
    public static let weightMax = UInt32(ZXV_TREATY_WEIGHT_MAX)

    /// What a treaty asks the engine to do.
    public enum Op: Int32, Sendable, CaseIterable {
        /// An empty treaty.
        case none = 0
        /// Net the obligations only.
        case net = 1
        /// Net, post the legs with the fee, pay the claims.
        case settle = 2
    }

    /// Why a treaty was refused.
    public enum Fault: Int32, Sendable, CaseIterable, CustomStringConvertible {
        /// Valid.
        case none = 0
        /// The record does not start with "ZXT1".
        case magic = 2
        /// Sutra text outside the grammar (also non-ASCII text).
        case syntax = 3
        /// Treaty name not 1..30 of A-Z 0-9 '-'.
        case name = 4
        /// Asset not three capital letters.
        case asset = 5
        /// Operation not NET or SETL.
        case op = 6
        /// Party or obligation count out of range.
        case count = 7
        /// A malformed number, or one outside PIC S9(18).
        case number = 8
        /// A bad, duplicate or unknown party id.
        case party = 9
        /// R1: weights not in lowest terms or not summing to 1.
        case weight = 10
        /// R2: a claim above its deposit.
        case claim = 11
        /// An obligation to self, or not above zero.
        case owe = 12
        /// An unused record slot or field not blank.
        case pad = 13

        /// A short lower-case name ("weight").
        public var description: String {
            switch self {
            case .none: return "none"
            case .magic: return "magic"
            case .syntax: return "syntax"
            case .name: return "name"
            case .asset: return "asset"
            case .op: return "op"
            case .count: return "count"
            case .number: return "number"
            case .party: return "party"
            case .weight: return "weight"
            case .claim: return "claim"
            case .owe: return "owe"
            case .pad: return "pad"
            }
        }
    }

    /// The fault a `ZXV_E_TREATY` error carries (`ZxvUsageError.treatyFault`).
    public struct FaultInfo: Sendable, Equatable, CustomStringConvertible {
        /// The `ZXV_TREATY_FAULT_*` code.
        public let code: Int32
        /// For a parse, the byte offset of the first fault; 0 otherwise.
        public let offset: UInt32
        /// The fault, or nil for a code this SDK does not know.
        public var fault: Fault? { Fault(rawValue: code) }
        /// Build from a code and an offset.
        public init(code: Int32, offset: UInt32) {
            self.code = code
            self.offset = offset
        }
        /// "weight at 57".
        public var description: String { "\(fault.map { $0.description } ?? "fault \(code)") at \(offset)" }
    }

    /// One party of a treaty.
    public struct Party: Sendable, Equatable {
        /// 1..8 of A-Z 0-9 '-', starting with a letter.
        public var id: String
        /// Minor units of the asset deposited (>= 0).
        public var deposit: Int64
        /// Minor units it may claim back (<= deposit).
        public var claim: Int64
        /// Dividend weight numerator.
        public var weightNum: UInt32
        /// Dividend weight denominator (lowest terms).
        public var weightDen: UInt32
        /// Build a party.
        public init(id: String, deposit: Int64, claim: Int64, weightNum: UInt32, weightDen: UInt32) {
            self.id = id
            self.deposit = deposit
            self.claim = claim
            self.weightNum = weightNum
            self.weightDen = weightDen
        }
    }

    /// One obligation: party `from` owes party `to` `amount` (0-based indices).
    public struct Obligation: Sendable, Equatable {
        /// Index of the payer.
        public var from: UInt32
        /// Index of the payee.
        public var to: UInt32
        /// Minor units (> 0).
        public var amount: Int64
    }

    /// One party's line of a run result.
    public struct PartyResult: Sendable, Equatable {
        /// What it is owed minus what it owes (gross).
        public let net: Int64
        /// NET: its deposit; SETL: what it holds after legs, fee and claim.
        public let balance: Int64
        /// Its reported dividend share (0 for NET; reported, not paid).
        public let dividend: Int64
    }

    /// One netted leg: party `from` pays party `to` `amount` (0-based indices).
    public struct Leg: Sendable, Equatable {
        /// Index of the payer.
        public let from: UInt32
        /// Index of the payee.
        public let to: UInt32
        /// Minor units.
        public let amount: Int64
    }

    /// Quotient and remainder of `picDiv`.
    public struct Division: Sendable, Equatable {
        /// Truncated toward zero.
        public let quotient: Int64
        /// Has the dividend's sign.
        public let remainder: Int64
    }

    /// What running a treaty returned: immutable, independent of the treaty
    /// it came from, and safe to read from any thread.
    public final class Result: @unchecked Sendable {
        let cell: HandleCell

        init(_ r: OpaquePointer) {
            cell = HandleCell(r, "Treaty.Result") { zxv_treaty_result_destroy($0) }
        }

        private func use<R>(_ body: (OpaquePointer) -> (Int32, R)) throws(ZxvError) -> R {
            let r = try cell.acquire()
            defer { cell.release() }
            let (st, v) = body(r)
            try check(st)
            return v
        }

        private func summary() throws(ZxvError) -> (UInt32, UInt32, Int64) {
            try use { r in
                var p: UInt32 = 0, l: UInt32 = 0, f: Int64 = 0
                let st = zxv_treaty_result_summary(r, &p, &l, &f)
                return (st, (p, l, f))
            }
        }

        /// Number of parties (treaty order).
        public func partyCount() throws(ZxvError) -> UInt32 { try summary().0 }
        /// Number of netted legs (at most parties - 1).
        public func legCount() throws(ZxvError) -> UInt32 { try summary().1 }
        /// The total assurance fee charged (0 for NET).
        public func fees() throws(ZxvError) -> Int64 { try summary().2 }

        /// Party `index` (treaty order); `.usage` (`ZXV_E_NOT_FOUND`) past the end.
        public func party(at index: UInt32) throws(ZxvError) -> PartyResult {
            try use { r in
                var n: Int64 = 0, b: Int64 = 0, d: Int64 = 0
                let st = zxv_treaty_result_party(r, index, &n, &b, &d)
                return (st, PartyResult(net: n, balance: b, dividend: d))
            }
        }
        /// Every party, in treaty order.
        public func parties() throws(ZxvError) -> [PartyResult] {
            var v: [PartyResult] = []
            for i in 0..<(try partyCount()) { v.append(try party(at: i)) }
            return v
        }
        /// Netted leg `index`; `.usage` (`ZXV_E_NOT_FOUND`) past the end.
        public func leg(at index: UInt32) throws(ZxvError) -> Leg {
            try use { r in
                var f: UInt32 = 0, t: UInt32 = 0, a: Int64 = 0
                let st = zxv_treaty_result_leg(r, index, &f, &t, &a)
                return (st, Leg(from: f, to: t, amount: a))
            }
        }
        /// Every netted leg, in order.
        public func legs() throws(ZxvError) -> [Leg] {
            var v: [Leg] = []
            for i in 0..<(try legCount()) { v.append(try leg(at: i)) }
            return v
        }
        /// The ZXVRSLT.cpy result record (`Treaty.resultRecordBytes` bytes;
        /// party numbers in it are 1-based, as the copybook says).
        public func record() throws(ZxvError) -> [UInt8] {
            let r = try cell.acquire()
            defer { cell.release() }
            return try readBytes(guess: Treaty.resultRecordBytes) { zxv_treaty_result_record(r, $0, $1, $2) }
        }
        /// Release the result now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    let cell: HandleCell

    /// An empty treaty: no parties, and it fails validation until a parse
    /// fills it.
    public init() throws(ZxvError) {
        var t: OpaquePointer?
        try check(zxv_treaty_create(&t))
        cell = HandleCell(t!, "Treaty") { zxv_treaty_destroy($0) }
    }

    /// Parse Sutra text (at most `sutraMax` bytes). `.usage` with status
    /// `ZXV_E_TREATY` and a `treatyFault` (fault and byte offset) when the
    /// treaty is refused.
    public convenience init(sutra: String) throws(ZxvError) {
        try self.init()
        try parseSutra(sutra)
    }

    /// Parse a copybook record of exactly `recordBytes` bytes (`.usage`
    /// `ZXV_E_ARG` for another length, `ZXV_E_TREATY` when refused).
    public convenience init(record: [UInt8]) throws(ZxvError) {
        try self.init()
        try parseRecord(record)
    }

    private func use<R>(_ body: (OpaquePointer) throws(ZxvError) -> R) throws(ZxvError) -> R {
        let t = try cell.acquire()
        defer { cell.release() }
        return try body(t)
    }

    /// Map a parse status: `ZXV_E_TREATY` gains the fault and offset.
    private static func parsed(_ st: Int32, _ off: UInt32, _ fault: Int32) throws(ZxvError) {
        if st == ZXV_E_TREATY {
            throw .usage(ZxvUsageError(status: st, detail: lastError(),
                                       treatyFault: FaultInfo(code: fault, offset: off)))
        }
        try check(st)
    }

    /// Map an emit or run status: `ZXV_E_TREATY` gains the treaty's fault.
    private static func emitted(_ st: Int32, _ t: OpaquePointer) throws(ZxvError) {
        if st != ZXV_E_TREATY {
            try check(st)
            return
        }
        let why = lastError()
        var fault: Int32 = 0
        _ = zxv_treaty_validate(t, &fault)
        throw .usage(ZxvUsageError(status: st, detail: why, treatyFault: FaultInfo(code: fault, offset: 0)))
    }

    /// Replace the contents by parsed Sutra text. On any failure the treaty
    /// is unchanged. `.usage`: `ZXV_E_TREATY` (with fault and offset),
    /// `ZXV_E_UTF8`, `ZXV_E_TOO_LONG`.
    public func parseSutra(_ text: String) throws(ZxvError) {
        let s = try cText(text, "Sutra text")
        try use { t throws(ZxvError) in
            var off: UInt32 = 0, fault: Int32 = 0
            let st = zxv_treaty_parse_sutra(t, s, &off, &fault)
            try Treaty.parsed(st, off, fault)
        }
    }

    /// Replace the contents by a parsed copybook record. On any failure the
    /// treaty is unchanged.
    public func parseRecord(_ record: [UInt8]) throws(ZxvError) {
        try use { t throws(ZxvError) in
            var off: UInt32 = 0, fault: Int32 = 0
            let st = zxv_treaty_parse_record(t, record, record.count, &off, &fault)
            try Treaty.parsed(st, off, fault)
        }
    }

    /// Empty it again.
    public func clear() throws(ZxvError) {
        try use { t throws(ZxvError) in try check(zxv_treaty_clear(t)) }
    }

    /// The first rule this treaty breaks, or `.none`. Never throws for an
    /// invalid treaty (an empty one reads `.name`).
    public func fault() throws(ZxvError) -> Fault {
        try use { t throws(ZxvError) in
            var f: Int32 = 0
            let st = zxv_treaty_validate(t, &f)
            if st != ZXV_E_TREATY { try check(st) }
            return try cEnum(f, "treaty fault")
        }
    }

    /// True if the treaty validates.
    public func isValid() throws(ZxvError) -> Bool { try fault() == .none }

    /// Throw `.usage` (`ZXV_E_TREATY`, with its fault) unless it validates.
    public func validate() throws(ZxvError) {
        try use { t throws(ZxvError) in try Treaty.emitted(zxv_treaty_validate(t, nil), t) }
    }

    /// The canonical Sutra text. `.usage` (`ZXV_E_TREATY`) if it does not validate.
    public func sutra() throws(ZxvError) -> String {
        try use { t throws(ZxvError) in
            var need = 0
            let first = zxv_treaty_emit_sutra(t, nil, 0, &need)
            if first != ZXV_E_BUFFER {
                try Treaty.emitted(first, t)
                return ""
            }
            var buf = [CChar](repeating: 0, count: need + 1)
            let st = buf.withUnsafeMutableBufferPointer { zxv_treaty_emit_sutra(t, $0.baseAddress, $0.count, &need) }
            try Treaty.emitted(st, t)
            return decodeUTF8(buf, count: need)
        }
    }

    /// The canonical copybook record (`recordBytes` bytes). `.usage`
    /// (`ZXV_E_TREATY`) if it does not validate.
    public func record() throws(ZxvError) -> [UInt8] {
        try use { t throws(ZxvError) in
            var v = [UInt8](repeating: 0, count: Treaty.recordBytes)
            var n = 0
            let st = zxv_treaty_emit_record(t, &v, v.count, &n)
            try Treaty.emitted(st, t)
            return Array(v.prefix(n))
        }
    }

    /// The treaty name ("" when empty).
    public func name() throws(ZxvError) -> String {
        try use { t throws(ZxvError) in try readText { zxv_treaty_name(t, $0, $1, $2) } }
    }

    /// The asset, three capital letters ("" when empty).
    public func asset() throws(ZxvError) -> String {
        try use { t throws(ZxvError) in try readText { zxv_treaty_asset(t, $0, $1, $2) } }
    }

    private func header() throws(ZxvError) -> (Int32, UInt32, UInt32) {
        try use { t throws(ZxvError) in
            var op: Int32 = 0, p: UInt32 = 0, o: UInt32 = 0
            try check(zxv_treaty_header(t, &op, &p, &o))
            return (op, p, o)
        }
    }

    /// The operation (`.none` when empty).
    public func op() throws(ZxvError) -> Op { try cEnum(try header().0, "treaty op") }
    /// Number of parties.
    public func partyCount() throws(ZxvError) -> UInt32 { try header().1 }
    /// Number of obligations.
    public func obligationCount() throws(ZxvError) -> UInt32 { try header().2 }

    /// Party `index` (0-based, treaty order); `.usage` (`ZXV_E_NOT_FOUND`)
    /// past the last party.
    public func party(at index: UInt32) throws(ZxvError) -> Party {
        try use { t throws(ZxvError) in
            let id = try readText { zxv_treaty_party_id(t, index, $0, $1, $2) }
            var d: Int64 = 0, c: Int64 = 0, wn: UInt32 = 0, wd: UInt32 = 0
            try check(zxv_treaty_party(t, index, &d, &c, &wn, &wd))
            return Party(id: id, deposit: d, claim: c, weightNum: wn, weightDen: wd)
        }
    }

    /// Every party, in treaty order.
    public func parties() throws(ZxvError) -> [Party] {
        var v: [Party] = []
        for i in 0..<(try partyCount()) { v.append(try party(at: i)) }
        return v
    }

    /// Obligation `index` (0-based); `.usage` (`ZXV_E_NOT_FOUND`) past the end.
    public func obligation(at index: UInt32) throws(ZxvError) -> Obligation {
        try use { t throws(ZxvError) in
            var f: UInt32 = 0, to: UInt32 = 0, a: Int64 = 0
            try check(zxv_treaty_owe(t, index, &f, &to, &a))
            return Obligation(from: f, to: to, amount: a)
        }
    }

    /// Every obligation, in order.
    public func obligations() throws(ZxvError) -> [Obligation] {
        var v: [Obligation] = []
        for i in 0..<(try obligationCount()) { v.append(try obligation(at: i)) }
        return v
    }

    /// Run the treaty in a fresh private engine: fund each deposit, net the
    /// obligations, and for SETL post the legs with the 0.08889% assurance
    /// fee (all or nothing), pay each claim back and report each party's
    /// dividend share. Nothing persists and no capability is needed; the
    /// same treaty always gives the same result. Errors: `.usage`
    /// `ZXV_E_TREATY` (invalid) or `ZXV_E_RANGE` (a position leaves PIC
    /// S9(18)), `.refused` `.insufficientFunds` (a payer cannot cover its
    /// legs plus the fee, or a claim exceeds what is left).
    public func run() throws(ZxvError) -> Result {
        try use { t throws(ZxvError) in
            var r: OpaquePointer?
            try Treaty.emitted(zxv_treaty_run(t, &r), t)
            return Result(r!)
        }
    }

    /// True if both hold the same treaty (every field, every slot), whatever
    /// spelling each was parsed from. Two empty treaties are equal.
    public func isEqual(to other: Treaty) throws(ZxvError) -> Bool {
        try use { a throws(ZxvError) in
            try other.use { b throws(ZxvError) in
                var eq: Int32 = 0
                try check(zxv_treaty_equal(a, b, &eq))
                return eq != 0
            }
        }
    }

    /// Release the treaty now. Later calls throw `.usage` (`ZXV_E_ARG`).
    public func close() { cell.close() }

    /// a + b as COBOL computes PIC S9(18) with ON SIZE ERROR: exact, or
    /// `.usage` (`ZXV_E_RANGE`) when an operand or the result leaves
    /// [-pic18Max, pic18Max].
    public static func picAdd(_ a: Int64, _ b: Int64) throws(ZxvError) -> Int64 {
        var r: Int64 = 0
        try check(zxv_treaty_pic_add(a, b, &r))
        return r
    }
    /// a - b in PIC S9(18) (see `picAdd`).
    public static func picSub(_ a: Int64, _ b: Int64) throws(ZxvError) -> Int64 {
        var r: Int64 = 0
        try check(zxv_treaty_pic_sub(a, b, &r))
        return r
    }
    /// a * b in PIC S9(18) (see `picAdd`).
    public static func picMul(_ a: Int64, _ b: Int64) throws(ZxvError) -> Int64 {
        var r: Int64 = 0
        try check(zxv_treaty_pic_mul(a, b, &r))
        return r
    }
    /// DIVIDE a BY b GIVING q REMAINDER r: -7 / 2 = -3 rem -1, 7 / -2 = -3
    /// rem 1. `.usage` (`ZXV_E_RANGE`) for b = 0 or an operand out of range.
    public static func picDiv(_ a: Int64, _ b: Int64) throws(ZxvError) -> Division {
        var q: Int64 = 0, r: Int64 = 0
        try check(zxv_treaty_pic_div(a, b, &q, &r))
        return Division(quotient: q, remainder: r)
    }
}
