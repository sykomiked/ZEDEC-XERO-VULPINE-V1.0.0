// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: freight packets. A FREIGHT carries up to `maxPayload` bytes as
/// 168 packets of 21 bytes (one UBH-168 frame each): byte 0 is the row
/// index 0...167, bytes 1...20 are row data. Rows 0..<k hold the payload
/// verbatim; rows k...167 are parity of a systematic Cauchy Reed-Solomon
/// code over GF(2^8). ANY k distinct rows rebuild the payload, so up to
/// 168 - k rows may be lost: k trades payload (k * 20 bytes) against loss
/// tolerance.
///
/// The 64-byte `Header` travels separately (in the outer message). A
/// payload larger than one freight is split into a SET of up to `maxSet`
/// freights sharing a freight id; a `Join` reassembles it in any order.
///
/// A payload may be an OP STREAM (LITERAL, COPY back references, CIDREF
/// bytes the receiver resolves by content id, SEED generators) that the
/// receiver expands. Expansion checks the declared total against the
/// caller's limit before writing a byte.
///
/// Honest limits: NO NETWORK TRANSPORT here (carrying packets is the
/// caller's job) and erasure coding only. A lost row is recovered; a
/// CORRUPTED row is detected (`.verificationFailed`: the payload SHA-256
/// fails) but not located or repaired: drop rows you distrust and decode
/// again. The header is neither protected by the code nor authenticated:
/// sign it (or the outer message) if the sender matters. A freight is
/// larger than its payload (3528 wire bytes for at most 3360). NO
/// COMPRESSION PROMISE: random data encodes slightly LARGER as an op
/// stream, COPY helps only data that repeats, CIDREF only names bytes the
/// receiver must already hold, SEED only reproduces the built-in
/// generators, and no information is created.
///
/// Everything here is pure (no capability, no context). `Join` and
/// `OpsWriter` hold only their own memory and are not thread-safe.
public enum Freight {
    /// 21: one row on the wire.
    public static let packetBytes = Int(ZXV_FREIGHT_PACKET_BYTES)
    /// 20 payload bytes per row.
    public static let rowPayload = Int(ZXV_FREIGHT_ROW_PAYLOAD)
    /// 168 rows per freight.
    public static let rows = UInt32(ZXV_FREIGHT_ROWS)
    /// 3528: one encoded freight.
    public static let freightBytes = Int(ZXV_FREIGHT_BYTES)
    /// 3360, at k = 168.
    public static let maxPayload = Int(ZXV_FREIGHT_MAX_PAYLOAD)
    /// 64.
    public static let headerBytes = Int(ZXV_FREIGHT_HEADER_BYTES)
    /// Header version 1.
    public static let version = UInt8(ZXV_FREIGHT_VERSION)
    /// Freights in one set.
    public static let maxSet = UInt32(ZXV_FREIGHT_MAX_SET)
    /// Largest set payload.
    public static let setMaxBytes: UInt64 = 55_050_240
    /// Header flag: the payload is an op stream.
    public static let flagOpStream = UInt8(ZXV_FREIGHT_F_OPSTREAM)
    /// Longest CIDREF content id.
    public static let cidMax = Int(ZXV_FREIGHT_CID_MAX)
    /// Longest SEED params.
    public static let genParamMax = Int(ZXV_FREIGHT_GEN_PARAM_MAX)
    /// Largest op stream or expansion (64 MiB).
    public static let opsMax: UInt64 = 67_108_864

    /// The built-in SEED generators.
    public enum Generator: UInt8, Sendable, CaseIterable {
        /// 8 bytes: u64 LE seed, nonzero; xorshift64* bytes.
        case xorshift = 1
        /// 1...32 bytes: the pattern, repeated.
        case `repeat` = 2
        /// 2 distinct bytes a, b: the Fibonacci word abaab...
        case fibword = 3
        /// 3 bytes: log2 width (1...15), on, off.
        case sierpinski = 4
    }

    /// The fields of a freight header.
    public struct HeaderFields: Sendable, Equatable {
        /// Always 1 (ignored by `Header(fields:)`).
        public var version: UInt8 = 1
        /// Data rows, 1...168.
        public var k: UInt8 = 0
        /// 0 or `flagOpStream`.
        public var flags: UInt8 = 0
        /// This freight's payload bytes (<= k * 20).
        public var payloadLength: UInt16 = 0
        /// Member index in its set.
        public var seq: UInt16 = 0
        /// Members in the set, 1...16384.
        public var seqCount: UInt16 = 1
        /// Shared by a set's members.
        public var freightId: UInt64 = 0
        /// Op streams only, else 0.
        public var expandLimit: UInt64 = 0
        /// SHA-256 of this freight's payload (32 bytes).
        public var payloadSha256: [UInt8] = [UInt8](repeating: 0, count: 32)
        /// All defaults (an invalid header until filled in).
        public init() {}
    }

    /// A serialized 64-byte freight header: "ZXFH", version, k, flags,
    /// payload length, seq, seq count, freight id, expand limit, payload
    /// SHA-256. A value type; every `Header` made here passed libzxv's check.
    public struct Header: Sendable, Hashable {
        /// The 64 bytes.
        public let bytes: [UInt8]

        private init(checked: [UInt8]) { bytes = checked }

        /// The header of a single freight (seq 0 of 1) carrying `payload` with
        /// `k` data rows (1...168); it hashes the payload. `expandLimit` must
        /// be 0 unless `flags` has `flagOpStream`. `.usage` `ZXV_E_ARG` /
        /// `ZXV_E_TOO_LONG` (payload over k * 20 bytes).
        public static func forPayload(freightId: UInt64, k: UInt8, payload: [UInt8], flags: UInt8 = 0,
                                      expandLimit: UInt64 = 0) throws(ZxvError) -> Header {
            var h = [UInt8](repeating: 0, count: Freight.headerBytes)
            try check(zxv_freight_header_init(freightId, k, flags, expandLimit, payload, payload.count, &h))
            return Header(checked: h)
        }

        /// Serialize a header from its fields (a set member, or one received
        /// field by field). `.usage` (`ZXV_E_ARG`) for any field breaking the
        /// header rules; the version is always 1.
        public init(fields f: HeaderFields) throws(ZxvError) {
            try needSize(f.payloadSha256.count, 32, "payload SHA-256")
            var h = [UInt8](repeating: 0, count: Freight.headerBytes)
            try check(zxv_freight_header_serialize(f.k, f.flags, f.payloadLength, f.seq, f.seqCount, f.freightId,
                                                   f.expandLimit, f.payloadSha256, &h))
            bytes = h
        }

        /// Check 64 received bytes. `.usage` (`ZXV_E_FORMAT`: bad magic,
        /// reserved bytes or a field; `ZXV_E_ARG` for another length).
        public init(bytes: [UInt8]) throws(ZxvError) {
            try needSize(bytes.count, Freight.headerBytes, "freight header")
            try check(zxv_freight_header_check(bytes))
            self.bytes = bytes
        }

        /// The fields.
        public func fields() throws(ZxvError) -> HeaderFields {
            var f = HeaderFields()
            try check(zxv_freight_header_parse(bytes, &f.version, &f.k, &f.flags, &f.payloadLength, &f.seq,
                                               &f.seqCount, &f.freightId, &f.expandLimit, &f.payloadSha256))
            return f
        }
    }

    /// Encode the 168 packets (`freightBytes` bytes, rows in order) of the
    /// freight `header` describes. `payload` must be exactly what the header
    /// was built for: another length is `.usage` (`ZXV_E_ARG`), another hash
    /// `.verificationFailed`.
    public static func encode(_ header: Header, payload: [UInt8]) throws(ZxvError) -> [UInt8] {
        var p = [UInt8](repeating: 0, count: freightBytes)
        try check(zxv_freight_encode(header.bytes, payload, payload.count, &p))
        return p
    }

    /// Rebuild the payload from whichever packets arrived: `packets` holds
    /// count / 21 packets in any order; duplicates and row indices above 167
    /// are ignored and the first k distinct rows are used. `.refused`
    /// (`.erasure`: fewer than k distinct rows; `.verificationFailed`: a
    /// corrupted row or a header from another freight), `.usage`
    /// (`ZXV_E_ARG`: a length not a multiple of 21).
    public static func decode(_ header: Header, packets: [UInt8]) throws(ZxvError) -> [UInt8] {
        try readBytes(guess: maxPayload) { zxv_freight_decode(header.bytes, packets, packets.count, $0, $1, $2) }
    }

    /// Freights needed to carry `length` bytes at `k` (0 bytes still needs
    /// one). `.usage` (`ZXV_E_TOO_LONG`) above `maxSet`.
    public static func setCount(length: UInt64, k: UInt8) throws(ZxvError) -> UInt32 {
        var c: UInt32 = 0
        try check(zxv_freight_set_count(length, k, &c))
        return c
    }

    /// One member of a freight set: its header and its 168 packets.
    public struct Member: Sendable, Equatable {
        /// Travels in the outer message.
        public let header: Header
        /// `freightBytes` bytes.
        public let packets: [UInt8]
    }

    /// Member `seq` of the set carrying `data` (it carries bytes
    /// [seq * k * 20, ...)). `.usage` `ZXV_E_RANGE` (seq not below
    /// `setCount`), `ZXV_E_TOO_LONG` (more than `maxSet` freights).
    public static func split(freightId: UInt64, k: UInt8, data: [UInt8], seq: UInt32, flags: UInt8 = 0,
                             expandLimit: UInt64 = 0) throws(ZxvError) -> Member {
        var h = [UInt8](repeating: 0, count: headerBytes)
        var p = [UInt8](repeating: 0, count: freightBytes)
        try check(zxv_freight_split(freightId, k, flags, expandLimit, data, data.count, seq, &h, &p))
        return Member(header: try Header(bytes: h), packets: p)
    }

    /// Progress of a `Join`.
    public struct JoinStatus: Sendable, Equatable {
        /// Members accepted.
        public let received: UInt32
        /// Members in the set (0 until the first is accepted).
        public let count: UInt32
        /// Every member arrived.
        public let complete: Bool
        /// The joined length once complete, else 0.
        public let total: UInt64
    }

    /// Reassembles one freight set, member by member, in any order, into a
    /// buffer of `capacity` bytes. The first member fixes the set. Not
    /// thread-safe. `close()` releases it at once.
    public final class Join {
        let cell: HandleCell

        /// A join for a set of at most `capacity` bytes (0...`setMaxBytes`,
        /// else `.usage` `ZXV_E_RANGE`).
        public init(capacity: UInt64) throws(ZxvError) {
            var j: OpaquePointer?
            try check(zxv_freight_join_create(capacity, &j))
            cell = HandleCell(j!, "Freight.Join") { zxv_freight_join_destroy($0) }
        }

        /// Decode one member into its place; a member already received
        /// changes nothing. `.refused` `ZXV_E_STATE` (another set, or a
        /// non-last member not full), `ZXV_E_CAPACITY` (the set cannot fit),
        /// and the errors of `decode`. A failed member is not recorded: add it
        /// again when more rows arrive.
        public func add(_ header: Header, packets: [UInt8]) throws(ZxvError) {
            let j = try cell.acquire()
            defer { cell.release() }
            try check(zxv_freight_join_add(j, header.bytes, packets, packets.count))
        }

        /// Add a `Member` made by `split`.
        public func add(_ member: Member) throws(ZxvError) { try add(member.header, packets: member.packets) }

        /// Progress.
        public func status() throws(ZxvError) -> JoinStatus {
            let j = try cell.acquire()
            defer { cell.release() }
            var r: UInt32 = 0, n: UInt32 = 0, c: Int32 = 0, t: UInt64 = 0
            try check(zxv_freight_join_status(j, &r, &n, &c, &t))
            return JoinStatus(received: r, count: n, complete: c != 0, total: t)
        }

        /// The joined bytes. `.refused` (`ZXV_E_STATE`) until every member
        /// arrived.
        public func result() throws(ZxvError) -> [UInt8] {
            let j = try cell.acquire()
            defer { cell.release() }
            return try readBytes { zxv_freight_join_result(j, $0, $1, $2) }
        }

        /// Release the join now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// What an op stream declares.
    public struct OpsInfo: Sendable, Equatable {
        /// Bytes its expansion produces.
        public let total: UInt64
        /// It carries the SHA-256 of its output.
        public let hasSha: Bool
    }

    /// Read an op stream's declared total without expanding it. `.usage`
    /// (`ZXV_E_FORMAT`) for a bad stream header.
    public static func peek(_ ops: [UInt8]) throws(ZxvError) -> OpsInfo {
        var t: UInt64 = 0, s: Int32 = 0
        try check(zxv_freight_ops_peek(ops, ops.count, &t, &s))
        return OpsInfo(total: t, hasSha: s != 0)
    }

    /// A CIDREF resolver: return exactly `length` bytes of the content `cid`
    /// names, or nil if it cannot (a wrong length counts as nil). It must not
    /// expand op streams itself. An error it throws never crosses C: it ends
    /// the expansion and `expand` rethrows it (a `ZxvError` as is, any other
    /// error as `.system` with `ZXV_E_INTERNAL`). The type uses untyped
    /// `throws` because typed-throws function values need the macOS 15 /
    /// iOS 18 runtime, and the package supports macOS 13 and iOS 16.
    public typealias Resolver = (_ cid: [UInt8], _ length: Int) throws -> [UInt8]?

    /// Expand an op stream, accepting at most `limit` bytes (<= `opsMax`,
    /// else `.usage` `ZXV_E_RANGE`); a declared total above it is `.usage`
    /// `ZXV_E_TOO_LONG` before anything is written. `resolver` supplies
    /// CIDREF content (none: a CIDREF fails with `.usage` `ZXV_E_NOT_FOUND`).
    /// `.usage` (`ZXV_E_FORMAT`: a malformed stream), `.refused`
    /// (`.verificationFailed`: the output fails the stream's SHA-256).
    public static func expand(_ ops: [UInt8], limit: UInt64, resolver: Resolver? = nil) throws(ZxvError) -> [UInt8] {
        let info = try peek(ops)
        let fits = limit <= opsMax && info.total <= limit
        var out = [UInt8](repeating: 0, count: fits ? Int(info.total) : 0)
        var n = 0
        let box = resolver.map { ResolverBox($0) }
        let st: Int32 = withExtendedLifetime(box) {
            out.withUnsafeMutableBufferPointer { o in
                let dst = o.count == 0 ? nil : o.baseAddress
                guard let box else {
                    return zxv_freight_ops_expand(ops, ops.count, limit, nil, nil, dst, o.count, &n)
                }
                return zxv_freight_ops_expand(ops, ops.count, limit, { user, cid, cidLen, out, len in
                    Unmanaged<ResolverBox>.fromOpaque(user!).takeUnretainedValue().resolve(cid, cidLen, out, len)
                }, Unmanaged.passUnretained(box).toOpaque(), dst, o.count, &n)
            }
        }
        if let e = box?.error { throw e }
        try check(st)
        return Array(out.prefix(n))
    }

    /// Run one SEED generator directly: `length` bytes. `.usage`
    /// (`ZXV_E_ARG`) for params it refuses.
    public static func generate(_ gen: Generator, params: [UInt8], length: Int) throws(ZxvError) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: length)
        let st = out.withUnsafeMutableBufferPointer { o in
            zxv_freight_gen_run(gen.rawValue, params, params.count, o.count == 0 ? nil : o.baseAddress, o.count)
        }
        try check(st)
        return out
    }

    /// Encode bytes as a LITERAL/COPY op stream (greedy LZ77, window 65536,
    /// deterministic), with the output SHA-256 when `withSha`. The result may
    /// be LARGER than the input. `.usage` (`ZXV_E_TOO_LONG`) above `opsMax`.
    public static func encodeOps(_ input: [UInt8], withSha: Bool = false) throws(ZxvError) -> [UInt8] {
        try readBytes { zxv_freight_ops_encode(input, input.count, withSha ? 1 : 0, $0, $1, $2) }
    }

    /// The worst-case `encodeOps` size of `count` input bytes (all literals).
    public static func opsBound(_ count: UInt64, withSha: Bool = false) throws(ZxvError) -> UInt64 {
        var b: UInt64 = 0
        try check(zxv_freight_ops_bound(count, withSha ? 1 : 0, &b))
        return b
    }

    /// Builds an op stream op by op. `declaredTotal` is what the ops must
    /// produce in all; `sha256` (optional) is the SHA-256 of that output,
    /// which expansion will verify. Every put throws on its own error, and
    /// the first error is sticky: later puts and `finish()` throw it again.
    /// Not thread-safe. The put functions return `self` for chaining.
    public final class OpsWriter {
        let cell: HandleCell

        /// A writer whose buffer holds at most `maxBytes` (14, or 46 with a
        /// SHA-256, up to `opsMax`).
        public init(declaredTotal: UInt64, maxBytes: UInt64, sha256: [UInt8]? = nil) throws(ZxvError) {
            if let sha256 { try needSize(sha256.count, 32, "op stream SHA-256") }
            var w: OpaquePointer?
            try check(zxv_freight_ops_writer_create(declaredTotal, sha256, maxBytes, &w))
            cell = HandleCell(w!, "Freight.OpsWriter") { zxv_freight_ops_writer_destroy($0) }
        }

        private func put(_ body: (OpaquePointer) -> Int32) throws(ZxvError) -> OpsWriter {
            let w = try cell.acquire()
            defer { cell.release() }
            try check(body(w))
            return self
        }

        /// LITERAL: these bytes verbatim.
        @discardableResult
        public func literal(_ bytes: [UInt8]) throws(ZxvError) -> OpsWriter {
            try put { zxv_freight_ops_put_literal($0, bytes, bytes.count) }
        }

        /// COPY: `length` bytes from `distance` back (overlap allowed).
        @discardableResult
        public func copy(distance: UInt64, length: UInt64) throws(ZxvError) -> OpsWriter {
            try put { zxv_freight_ops_put_copy($0, distance, length) }
        }

        /// CIDREF: `length` bytes the receiver resolves by content id `cid`
        /// (1...`cidMax` bytes).
        @discardableResult
        public func cidref(_ cid: [UInt8], length: UInt64) throws(ZxvError) -> OpsWriter {
            try put { zxv_freight_ops_put_cidref($0, cid, cid.count, length) }
        }

        /// CIDREF by a binary CID.
        @discardableResult
        public func cidref(_ cid: Cid, length: UInt64) throws(ZxvError) -> OpsWriter {
            try cidref(cid.bytes, length: length)
        }

        /// SEED: `length` bytes of a built-in generator.
        @discardableResult
        public func seed(_ gen: Generator, params: [UInt8], length: UInt64) throws(ZxvError) -> OpsWriter {
            try put { zxv_freight_ops_put_seed($0, gen.rawValue, params, params.count, length) }
        }

        /// The finished stream. `.refused` (`ZXV_E_STATE`) while the ops
        /// produce fewer bytes than declared. The writer stays usable.
        public func finish() throws(ZxvError) -> [UInt8] {
            let w = try cell.acquire()
            defer { cell.release() }
            return try readBytes { zxv_freight_ops_writer_finish(w, $0, $1, $2) }
        }

        /// Release the writer now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }
}

/// Carries a Swift resolver across the C callback; an error it throws is
/// kept here and rethrown by `Freight.expand`, never unwound through C.
private final class ResolverBox {
    let fn: Freight.Resolver
    var error: ZxvError?

    init(_ fn: @escaping Freight.Resolver) { self.fn = fn }

    func resolve(_ cid: UnsafePointer<UInt8>?, _ cidLen: Int, _ out: UnsafeMutablePointer<UInt8>?,
                 _ len: UInt64) -> Int32 {
        guard len <= UInt64(Int.max) else { return ZXV_E_RANGE }
        let id = cid.map { Array(UnsafeBufferPointer(start: $0, count: cidLen)) } ?? []
        do {
            guard let bytes = try fn(id, Int(len)), bytes.count == Int(len) else { return ZXV_E_NOT_FOUND }
            if let out, len > 0 {
                bytes.withUnsafeBufferPointer { out.update(from: $0.baseAddress!, count: Int(len)) }
            }
            return ZXV_OK
        } catch let e as ZxvError {
            self.error = e
            return ZXV_E_INTERNAL
        } catch {
            self.error = .system(ZxvSystemError(status: ZXV_E_INTERNAL, detail: "freight resolver: \(error)"))
            return ZXV_E_INTERNAL
        }
    }
}
