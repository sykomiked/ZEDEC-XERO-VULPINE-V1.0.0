// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv
import Foundation

/// What a budget measures (rmag_resource_t).
public enum Resource: Int32, Sendable {
    case computeCycles = 0, memory = 1, bandwidth = 2, energy = 3, storage = 4
    /// One model token = one event.
    case tokens = 12
}

/// Exact status of a budget.
public struct BudgetStatus: Sendable, Equatable {
    /// limit - used, clamped at zero.
    public let remaining: Rational
    /// Consumed so far.
    public let used: Rational
    /// Counts replenishments.
    public let epoch: UInt64
}

/// A cancelled wait (task cancellation, or the owner was closed).
public struct ZxvCancelled: Error, Sendable, CustomStringConvertible {
    /// Why.
    public let description: String
}

/// A registry of rational resource budgets (rmag, K2). Needs `.budget`.
/// Thread-safe. `close()` (or deinit) cancels pending `consumeAsync` waits.
public final class Budgets: @unchecked Sendable {
    let cap: Capability
    let handle: OpaquePointer

    private struct Waiter {
        let continuation: CheckedContinuation<Void, Error>
    }
    private struct State {
        var epoch: UInt64 = 0
        var closed = false
        var nextId: UInt64 = 0
        var waiters: [UInt64: Waiter] = [:]
        var onExhausted: [UInt32: [@Sendable (UInt32) -> Void]] = [:]
    }
    private let state = Locked(State())

    /// A registry in the capability's context (needs `.budget`).
    public init(_ cap: Capability) throws(ZxvError) {
        self.cap = cap
        var r: OpaquePointer?
        try cap.call { zxv_rmag_create_cap($0, &r) }
        handle = r!
        // The listener holds an unretained pointer; deinit removes it first.
        zxv_rmag_listen(handle, { user, budget, event in
            Unmanaged<Budgets>.fromOpaque(user!).takeUnretainedValue().onEvent(budget: budget, event: event)
        }, Unmanaged.passUnretained(self).toOpaque())
    }

    deinit {
        zxv_rmag_listen(handle, nil, nil)
        close()
        zxv_rmag_destroy(handle)
    }

    /// Cancel every pending wait (they throw `ZxvCancelled`); later waits fail at once.
    public func close() {
        let ws = state.withLock { s -> [Waiter] in
            s.closed = true
            defer { s.waiters.removeAll() }
            return Array(s.waiters.values)
        }
        for w in ws { w.continuation.resume(throwing: ZxvCancelled(description: "the Budgets registry was closed")) }
    }

    private func onEvent(budget: UInt32, event: Int32) {
        if event == ZXV_RMAG_EV_REPLENISHED {
            let ws = state.withLock { s -> [Waiter] in
                s.epoch &+= 1
                defer { s.waiters.removeAll() }
                return Array(s.waiters.values)
            }
            for w in ws { w.continuation.resume() } // each retries its consume
        } else if event == ZXV_RMAG_EV_DENIED {
            let fns = state.withLock { $0.onExhausted[budget] ?? [] }
            for f in fns { f(budget) }
        }
    }

    /// Create a budget with an exact limit (>= 0); `enforce: false` only counts.
    public func create(_ name: String, resource: Resource, limit: Rational, enforce: Bool = true) throws(ZxvError) -> Budget {
        var id: UInt32 = 0
        try cap.call {
            zxv_rmag_budget($0, handle, name, resource.rawValue, limit.numerator, limit.denominator, enforce ? 1 : 0, &id)
        }
        return Budget(registry: self, id: id)
    }

    // MARK: waiting

    fileprivate var epoch: UInt64 { state.withLock { $0.epoch } }

    fileprivate func registerExhausted(_ id: UInt32, _ fn: @escaping @Sendable (UInt32) -> Void) {
        state.withLock { $0.onExhausted[id, default: []].append(fn) }
    }

    /// Suspend until a replenishment after `seen`, cancellation, close or deadline.
    fileprivate func waitForReplenish(after seen: UInt64, deadline: ContinuousClock.Instant?) async throws {
        let id = state.withLock { s -> UInt64 in
            s.nextId += 1
            return s.nextId
        }
        let timer: Task<Void, Never>? = deadline.map { d in
            Task { [weak self] in
                try? await Task.sleep(until: d, clock: .continuous)
                guard !Task.isCancelled else { return }
                self?.resume(id, throwing: ZxvError.refused(ZxvRefusedError(
                    status: ZXV_E_BUDGET, detail: "timed out waiting for the budget to be replenished")))
            }
        }
        defer { timer?.cancel() }
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
                enum Now { case wait, go, closed }
                let now = state.withLock { s -> Now in
                    if s.closed { return .closed }
                    if s.epoch != seen { return .go }
                    s.waiters[id] = Waiter(continuation: c)
                    return .wait
                }
                switch now {
                case .go: c.resume()
                case .closed: c.resume(throwing: ZxvCancelled(description: "the Budgets registry was closed"))
                case .wait:
                    if Task.isCancelled { resume(id, throwing: CancellationError()) }
                }
            }
        } onCancel: {
            resume(id, throwing: CancellationError())
        }
    }

    private func resume(_ id: UInt64, throwing error: Error) {
        let w = state.withLock { $0.waiters.removeValue(forKey: id) }
        w?.continuation.resume(throwing: error)
    }
}

/// One budget with an exact rational limit; a handle into its `Budgets`.
public struct Budget: Sendable {
    /// The owning registry.
    public let registry: Budgets
    /// Index of this budget (as the C ABI numbers it).
    public let id: UInt32

    /// Consume `amount` (> 0). Exhaustion throws `.refused` (budgetExhausted;
    /// nothing consumed); a malformed amount throws `.usage`
    /// (`ZXV_E_RMAG_INVALID`; never retry the same value).
    public func consume(_ amount: Rational) throws(ZxvError) {
        try registry.cap.call { zxv_rmag_consume($0, registry.handle, id, amount.numerator, amount.denominator) }
    }

    /// Consume, suspending while the budget is exhausted until it is
    /// replenished (a release or a larger limit), then retrying. Throws
    /// `CancellationError` if the task is cancelled, `ZxvCancelled` if the
    /// registry is closed, `ZxvError.refused` (budgetExhausted) when
    /// `timeout` elapses, and a usage error at once for a malformed amount.
    public func consumeAsync(_ amount: Rational, timeout: Duration? = nil) async throws {
        let deadline = timeout.map { ContinuousClock.now + $0 }
        while true {
            try Task.checkCancellation()
            let seen = registry.epoch
            do {
                try consume(amount)
                return
            } catch let e where e.refusal == .budgetExhausted {
                try await registry.waitForReplenish(after: seen, deadline: deadline)
            }
        }
    }

    /// Give back (clamped at zero used). Wakes waiters.
    public func release(_ amount: Rational) throws(ZxvError) {
        try registry.cap.call { zxv_rmag_release($0, registry.handle, id, amount.numerator, amount.denominator) }
    }

    /// Replace the limit (>= 0). Growing it wakes waiters.
    public func setLimit(_ limit: Rational) throws(ZxvError) {
        try registry.cap.call { zxv_rmag_set_limit($0, registry.handle, id, limit.numerator, limit.denominator) }
    }

    /// Remaining, used (exact) and the replenish epoch.
    public func status() throws(ZxvError) -> BudgetStatus {
        var rn: Int64 = 0, rd: Int64 = 1, un: Int64 = 0, ud: Int64 = 1, ep: UInt64 = 0
        try registry.cap.call { _ in zxv_rmag_status(registry.handle, id, &rn, &rd, &un, &ud, &ep) }
        do throws(ZxvUsageError) {
            return BudgetStatus(remaining: try Rational(rn, rd), used: try Rational(un, ud), epoch: ep)
        } catch {
            throw .usage(error)
        }
    }

    /// Run `fn` (on the denied caller's thread) whenever a consume of this
    /// budget is refused, including by a bound `Session`.
    public func onExhausted(_ fn: @escaping @Sendable (UInt32) -> Void) {
        registry.registerExhausted(id, fn)
    }
}
