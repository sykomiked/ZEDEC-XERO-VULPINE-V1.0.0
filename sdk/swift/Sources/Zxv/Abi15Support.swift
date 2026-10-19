// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.5: shared plumbing for the eight new groups (ABI 1.6 Vino uses it too).
//
//   Treaty       COBOL copybook treaties and their Sutra text (pure)
//   Cid / Aipi   IPFS content identifiers; AIPI records and an offline node
//   Coin         the VFV Fibonacci coin ladder (pure)
//   SwarmToken   token tiers, octane, Fibonacci codes; a wallet
//   Fee          the 0.08889% assurance fee and its four buckets (pure)
//   VHub         a demo V-Bill hub: test currencies, signed pegs, swaps
//   Freight      erasure-coded 168-row freight packets and op streams
//   Web4         agents, signed manifests, consent, envelopes
//   Paradox      the paradox engine: kinds, operators, logs, graphs
//
// Everything is an integer or a byte array (64-bit values stay 64-bit; no
// floating point). Variable-length results use the C two-call buffer
// pattern, hidden here: every call returns an array or a String. Every ABI
// 1.5 call throws `ZxvError`; the class of the error tells what to do.

import CZxv
import Foundation

/// Owns one C handle of an ABI 1.5 group and releases it exactly once.
///
/// `close()` releases it now (deterministically); `deinit` releases it if
/// nobody did. A call that is running when `close()` is called keeps the
/// handle until it returns, so a close from another thread never frees a
/// handle in use. Any call after `close()` throws `.usage` (`ZXV_E_ARG`,
/// "use of a closed ...") instead of reaching C.
final class HandleCell: @unchecked Sendable {
    private let lock = NSLock()
    private var pointer: OpaquePointer?
    private var users = 0
    private var closed = false
    private let destroy: @Sendable (OpaquePointer) -> Void
    /// What the handle is, for error text ("Paradox.Book").
    let what: String

    /// Own `pointer`; `destroy` is the C destroy function.
    init(_ pointer: OpaquePointer, _ what: String, destroy: @escaping @Sendable (OpaquePointer) -> Void) {
        self.pointer = pointer
        self.what = what
        self.destroy = destroy
    }

    deinit {
        if let p = pointer { destroy(p) }
    }

    /// The handle, counted as in use until `release()`. Throws `.usage`
    /// (`ZXV_E_ARG`) once closed.
    func acquire() throws(ZxvError) -> OpaquePointer {
        lock.lock()
        defer { lock.unlock() }
        guard !closed, let p = pointer else {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "use of a closed \(what)"))
        }
        users += 1
        return p
    }

    /// End one use; the last use after `close()` destroys the handle.
    func release() {
        lock.lock()
        users -= 1
        var dead: OpaquePointer?
        if closed && users == 0 {
            dead = pointer
            pointer = nil
        }
        lock.unlock()
        if let dead { destroy(dead) }
    }

    /// Release the handle (now, or when the call using it returns). Idempotent.
    func close() {
        lock.lock()
        var dead: OpaquePointer?
        if !closed {
            closed = true
            if users == 0 {
                dead = pointer
                pointer = nil
            }
        }
        lock.unlock()
        if let dead { destroy(dead) }
    }

    /// True once `close()` was called.
    var isClosed: Bool {
        lock.lock()
        defer { lock.unlock() }
        return closed
    }
}

/// A usage error (`ZXV_E_ARG` unless given) raised by the SDK itself.
func usage(_ detail: String, _ status: Int32 = ZXV_E_ARG) -> ZxvError {
    .usage(ZxvUsageError(status: status, detail: detail))
}

/// Refuse a byte array that is not exactly `n` bytes.
func needSize(_ count: Int, _ n: Int, _ what: String) throws(ZxvError) {
    if count != n { throw usage("\(what) must be \(n) bytes, got \(count)") }
}

/// Refuse text with an embedded NUL (C would silently treat it as the end).
func cText(_ s: String, _ what: String) throws(ZxvError) -> String {
    if s.utf8.contains(0) { throw usage("\(what) contains a NUL byte") }
    return s
}

/// The text of a fixed C char array (up to its first NUL).
func cArrayText(_ buf: [CChar]) -> String {
    let end = buf.firstIndex(of: 0) ?? buf.count
    return decodeUTF8(buf, count: end)
}

/// A C enum value as a Swift enum; an unknown value is `.system`
/// (`ZXV_E_INTERNAL`): this library and its header disagree.
func cEnum<E: RawRepresentable>(_ raw: E.RawValue, _ what: String) throws(ZxvError) -> E {
    guard let e = E(rawValue: raw) else {
        throw .system(ZxvSystemError(status: ZXV_E_INTERNAL, detail: "unknown \(what) value \(raw)"))
    }
    return e
}

/// "R4 for bytes": try a buffer of `guess` bytes, grow it once to the
/// length the library reports, and return exactly the result's bytes.
func readBytes(_ context: ZxvContext? = nil, guess: Int = 256,
               _ call: (UnsafeMutablePointer<UInt8>?, Int, UnsafeMutablePointer<Int>) -> Int32) throws(ZxvError) -> [UInt8] {
    var buf = [UInt8](repeating: 0, count: guess)
    var need = 0
    var st = buf.withUnsafeMutableBufferPointer { b in
        call(b.count == 0 ? nil : b.baseAddress, b.count, &need)
    }
    if st == ZXV_E_BUFFER {
        buf = [UInt8](repeating: 0, count: need)
        st = buf.withUnsafeMutableBufferPointer { b in
            call(b.count == 0 ? nil : b.baseAddress, b.count, &need)
        }
    }
    try check(st, context)
    return Array(buf.prefix(need))
}

/// A CID written by a C call into a `ZXV_CID_MAX` buffer.
func cidOut(_ context: ZxvContext? = nil,
            _ call: (UnsafeMutablePointer<UInt8>, UnsafeMutablePointer<Int>) -> Int32) throws(ZxvError) -> Cid {
    var b = [UInt8](repeating: 0, count: Int(ZXV_CID_MAX))
    var n = 0
    let st = b.withUnsafeMutableBufferPointer { call($0.baseAddress!, &n) }
    try check(st, context)
    return Cid(checked: Array(b.prefix(n)))
}

/// Little-endian readers for the record layouts the C header documents.
func le32(_ b: [UInt8], _ at: Int) -> UInt32 {
    UInt32(b[at]) | UInt32(b[at + 1]) << 8 | UInt32(b[at + 2]) << 16 | UInt32(b[at + 3]) << 24
}
/// 64-bit little-endian at `at`.
func le64(_ b: [UInt8], _ at: Int) -> UInt64 {
    UInt64(le32(b, at)) | UInt64(le32(b, at + 4)) << 32
}
/// The 32 bytes at `at`.
func digest32(_ b: [UInt8], _ at: Int) -> [UInt8] { Array(b[at..<(at + 32)]) }

extension Capability {
    /// Run an ungated C call on a handle of this capability's context: refuse
    /// when the context is dead, map the status (FATAL marks it dead).
    func read(_ body: () -> Int32) throws(ZxvError) {
        try context.ensureAlive()
        try Zxv.check(body(), context)
    }
}
