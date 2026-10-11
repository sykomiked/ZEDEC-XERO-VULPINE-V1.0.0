// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv
import Foundation

/// The class of a status code (`zxv_status_class`). Every SDK maps errors
/// the same way.
public enum StatusClass: Int32, Sendable {
    /// Success.
    case ok = 0
    /// A caller bug: fix the arguments; retrying the same call fails again.
    case usage = 1
    /// A well-formed request the platform declined; it may succeed later.
    case refused = 2
    /// The context halted after a broken invariant; never retry.
    case fatal = 3
    /// Out of memory, I/O, internal or unknown status.
    case system = 4

    /// The class of a C status. Unknown negative codes are `.system` (R8).
    public init(status: Int32) {
        self = StatusClass(rawValue: zxv_status_class(status)) ?? .system
    }
}

/// Fields every ZXV error carries.
public protocol ZxvFailure: Error, CustomStringConvertible, Sendable {
    /// The negative `ZXV_E_*` status.
    var status: Int32 { get }
    /// The C library's detail text (diagnostic only; never branch on it).
    var detail: String { get }
}

extension ZxvFailure {
    /// The status's symbolic name ("ZXV_E_FUNDS").
    public var statusName: String { String(cString: zxv_status_name(status)) }
    /// "ZXV_E_FUNDS: detail".
    public var description: String { detail.isEmpty ? statusName : "\(statusName): \(detail)" }
}

/// USAGE class: a caller bug. Fix the call; retrying unchanged fails again.
public struct ZxvUsageError: ZxvFailure, Equatable {
    public let status: Int32
    public let detail: String
    /// True for `ZXV_E_FLOAT`: a NaN or infinity reached the float boundary.
    public var isFloatRejected: Bool { status == ZXV_E_FLOAT }
    /// Build from a status and detail text.
    public init(status: Int32, detail: String) {
        self.status = status
        self.detail = detail
    }
}

/// REFUSED class: a well-formed request the platform declined.
public struct ZxvRefusedError: ZxvFailure, Equatable {
    /// Why the platform refused.
    public enum Reason: Sendable, Equatable {
        /// The capability lacks a bit, is revoked, or is not attested TRUE.
        case capabilityDenied
        /// An rmag or swarm budget is exhausted.
        case budgetExhausted
        /// A holder account would go below zero.
        case insufficientFunds
        /// A signature or tag did not verify.
        case verificationFailed
        /// The feature is not built into this library.
        case notImplemented
        /// Another refusal (capacity, state, declined...).
        case other
    }
    public let status: Int32
    public let detail: String
    /// The reason, derived from the status.
    public var reason: Reason {
        switch status {
        case ZXV_E_CAP: return .capabilityDenied
        case ZXV_E_BUDGET: return .budgetExhausted
        case ZXV_E_FUNDS: return .insufficientFunds
        case ZXV_E_VERIFY: return .verificationFailed
        case ZXV_E_NOTIMPL: return .notImplemented
        default: return .other
        }
    }
    /// Build from a status and detail text.
    public init(status: Int32, detail: String) {
        self.status = status
        self.detail = detail
    }
}

/// FATAL class: the context halted. Every later call on it fails; the SDK
/// marks its handles dead. Never retry. For a truly terminal form, set
/// `ZxvLibrary.fatalPolicy = .abort`.
public struct ZxvFatalError: ZxvFailure, Equatable {
    public let status: Int32
    public let detail: String
    /// Build from a status and detail text.
    public init(status: Int32, detail: String) {
        self.status = status
        self.detail = detail
    }
}

/// SYSTEM class: out of memory, I/O, internal, or an unknown status.
public struct ZxvSystemError: ZxvFailure, Equatable {
    public let status: Int32
    public let detail: String
    /// Build from a status and detail text.
    public init(status: Int32, detail: String) {
        self.status = status
        self.detail = detail
    }
}

/// Every error a ZXV call can throw, one case per status class. APIs use
/// typed throws: `throws(ZxvError)` for calls that touch state, and
/// `throws(ZxvUsageError)` for pure functions that can only fail by misuse.
///
///     do { try ledger.post(...) }
///     catch .refused(let e) where e.reason == .insufficientFunds { ... }
///     catch .fatal(let e) { /* context halted: rebuild, never retry */ }
public enum ZxvError: Error, CustomStringConvertible, Sendable, Equatable {
    /// A caller bug.
    case usage(ZxvUsageError)
    /// A refusal to handle.
    case refused(ZxvRefusedError)
    /// The context halted.
    case fatal(ZxvFatalError)
    /// An environmental failure.
    case system(ZxvSystemError)

    /// The error for a status and detail, picked by its class.
    public init(status: Int32, detail: String) {
        switch StatusClass(status: status) {
        case .usage: self = .usage(ZxvUsageError(status: status, detail: detail))
        case .refused: self = .refused(ZxvRefusedError(status: status, detail: detail))
        case .fatal: self = .fatal(ZxvFatalError(status: status, detail: detail))
        case .ok, .system: self = .system(ZxvSystemError(status: status, detail: detail))
        }
    }

    /// The wrapped error.
    public var failure: any ZxvFailure {
        switch self {
        case .usage(let e): return e
        case .refused(let e): return e
        case .fatal(let e): return e
        case .system(let e): return e
        }
    }
    /// The negative status.
    public var status: Int32 { failure.status }
    /// The status's symbolic name.
    public var statusName: String { failure.statusName }
    /// The C detail text.
    public var detail: String { failure.detail }
    /// The status class.
    public var statusClass: StatusClass { StatusClass(status: status) }
    /// "ZXV_E_CAP: detail".
    public var description: String { failure.description }

    /// The refusal reason, or nil for other classes.
    public var refusal: ZxvRefusedError.Reason? {
        if case .refused(let e) = self { return e.reason }
        return nil
    }
}

/// What the SDK does when a call reports a FATAL status.
public enum FatalPolicy: Sendable {
    /// Mark the handle dead and throw `ZxvError.fatal` (default).
    case throwError
    /// Print the detail and terminate the process at once (uncatchable).
    case abort
}

/// The calling thread's last error detail (R7).
public func lastError() -> String {
    var need = 0
    guard zxv_last_error(nil, 0, &need) == ZXV_E_BUFFER else { return "" }
    var buf = [CChar](repeating: 0, count: need + 1)
    guard zxv_last_error(&buf, buf.count, &need) == ZXV_OK else { return "" }
    return decodeUTF8(buf, count: need)
}

/// Decode `count` bytes of a C buffer as UTF-8 (lossless for valid input).
func decodeUTF8(_ buf: [CChar], count: Int) -> String {
    let bytes = buf.prefix(count).map { UInt8(bitPattern: $0) }
    return String(decoding: bytes, as: UTF8.self)
}

/// Map a status to nothing (OK) or a typed error. FATAL marks `context` dead
/// and honours `ZxvLibrary.fatalPolicy`.
@inline(__always)
func check(_ status: Int32, _ context: ZxvContext? = nil) throws(ZxvError) {
    if status == ZXV_OK { return }
    try raise(status, detail: lastError(), context)
}

func raise(_ status: Int32, detail: String, _ context: ZxvContext? = nil) throws(ZxvError) -> Never {
    let error = ZxvError(status: status, detail: detail)
    if case .fatal = error {
        context?.markDead()
        if ZxvLibrary.fatalPolicy == .abort {
            FileHandle.standardError.write(Data("zxv: FATAL \(error) (FatalPolicy.abort)\n".utf8))
            Foundation.abort()
        }
    }
    throw error
}

/// Pure functions can fail only by misuse; anything else is reported as a
/// usage error carrying its own status.
@inline(__always)
func checkUsage(_ status: Int32) throws(ZxvUsageError) {
    if status == ZXV_OK { return }
    throw ZxvUsageError(status: status, detail: lastError())
}

/// Two-call (R4) text read.
func readText(_ context: ZxvContext? = nil,
              _ call: (UnsafeMutablePointer<CChar>?, Int, UnsafeMutablePointer<Int>) -> Int32) throws(ZxvError) -> String {
    var need = 0
    let first = call(nil, 0, &need)
    if first != ZXV_E_BUFFER {
        try check(first, context)
        return ""
    }
    var buf = [CChar](repeating: 0, count: need + 1)
    let status = buf.withUnsafeMutableBufferPointer { call($0.baseAddress, $0.count, &need) }
    try check(status, context)
    return decodeUTF8(buf, count: need)
}

/// A value guarded by a lock (portable to Linux; no OS 15 `Mutex` needed).
final class Locked<Value>: @unchecked Sendable {
    private var value: Value
    private let lock = NSLock()
    init(_ value: Value) { self.value = value }
    func withLock<R>(_ body: (inout Value) -> R) -> R {
        lock.lock()
        defer { lock.unlock() }
        return body(&value)
    }
}
