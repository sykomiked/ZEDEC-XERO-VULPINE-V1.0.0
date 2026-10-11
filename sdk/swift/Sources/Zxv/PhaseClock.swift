// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// Decision of the K6 coordinator.
public enum Decision: Int32, Sendable {
    case admit = 0, veto = 1, `defer` = 2, retry = 3
}

/// Step flags for `PhaseClock.admit`.
public struct StepFlags: OptionSet, Sendable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    /// The phase must be healthy.
    public static let requiresHealth = StepFlags(rawValue: 0x1)
    /// The step touches hardware.
    public static let hardware = StepFlags(rawValue: 0x2)
    /// Park the step in S0.
    public static let s0Pending = StepFlags(rawValue: 0x4)
}

/// Health of a phase.
public enum Health: Int32, Sendable {
    case unknown = 0, healthy = 1, degraded = 2, unhealthy = 3
}

/// State of a deferred S0 event.
public enum EventState: Int32, Sendable {
    case pending = 0, cleared = 1, cancelled = 2
}

/// Result of an admission request.
public struct Admission: Sendable, Equatable {
    /// What the coordinator decided.
    public let decision: Decision
    /// Non-zero only on `.admit`.
    public let token: UInt64
}

/// The phase clock (K6). ZXV time is a logical tick count; one fundamental
/// is `phaseCycle` (27,720) ticks. Drive it manually with `advance` (tests,
/// simulations, game lockstep) or in real time with `startRealtime`.
/// Needs `.phase`. Thread-safe.
public final class PhaseClock: @unchecked Sendable {
    private let cap: Capability
    private let handle: OpaquePointer
    private struct State {
        var waiters: [UInt64: [CheckedContinuation<Void, Error>]] = [:]
        var driver: Task<Void, Never>?
        var closed = false
    }
    private let state = Locked(State())
    private let cLock = Locked(()) // serializes C calls with waiter settlement

    /// A clock in the capability's context, at tick 0 (needs `.phase`).
    public init(_ cap: Capability) throws(ZxvError) {
        self.cap = cap
        var p: OpaquePointer?
        try cap.call { zxv_phase_create_cap($0, &p) }
        handle = p!
    }

    deinit {
        close()
        zxv_phase_destroy(handle)
    }

    /// Stop the real-time driver and fail pending waiters with `ZxvCancelled`.
    public func close() {
        let (driver, ws) = state.withLock { s -> (Task<Void, Never>?, [CheckedContinuation<Void, Error>]) in
            s.closed = true
            defer {
                s.driver = nil
                s.waiters.removeAll()
            }
            return (s.driver, s.waiters.values.flatMap { $0 })
        }
        driver?.cancel()
        for c in ws { c.resume(throwing: ZxvCancelled(description: "the PhaseClock was closed")) }
    }

    private func locked<R>(_ body: () throws(ZxvError) -> R) throws(ZxvError) -> R {
        // Locked.withLock takes a non-throwing body; carry the result out.
        let r: Result<R, ZxvError> = cLock.withLock { _ in
            do throws(ZxvError) { return .success(try body()) } catch { return .failure(error) }
        }
        return try r.get()
    }

    /// The current tick.
    public func now() throws(ZxvError) -> UInt64 {
        var t: UInt64 = 0
        try locked { () throws(ZxvError) in try cap.call { _ in zxv_phase_now(handle, &t) } }
        return t
    }

    /// Advance by `delta` ticks. Due events clear and their awaiters resume.
    @discardableResult
    public func advance(_ delta: UInt64) throws(ZxvError) -> (now: UInt64, cleared: UInt32) {
        var now: UInt64 = 0, cleared: UInt32 = 0
        do throws(ZxvError) {
            try locked { () throws(ZxvError) in try cap.call { zxv_phase_advance($0, handle, delta, &now, &cleared) } }
        } catch {
            if case .fatal = error { failAll(error) }
            throw error
        }
        settle()
        return (now, cleared)
    }

    /// Ask the coordinator to admit a step of phase `phaseId` (1...13).
    public func admit(phase phaseId: UInt32, step: UInt32, flags: StepFlags = []) throws(ZxvError) -> Admission {
        var d: Int32 = 0, tok: UInt64 = 0
        try locked { () throws(ZxvError) in try cap.call { zxv_phase_admit($0, handle, phaseId, step, flags.rawValue, &d, &tok) } }
        return Admission(decision: Decision(rawValue: d) ?? .veto, token: tok)
    }

    /// Report the health of a phase.
    public func setHealth(phase phaseId: UInt32, _ health: Health) throws(ZxvError) {
        try locked { () throws(ZxvError) in try cap.call { zxv_phase_set_health($0, handle, phaseId, health.rawValue) } }
    }

    /// Park an S0 event until absolute tick `dueTick` (> now).
    public func `defer`(until dueTick: UInt64) throws(ZxvError) -> DeferredEvent {
        var ev: UInt64 = 0
        try locked { () throws(ZxvError) in try cap.call { zxv_phase_defer($0, handle, dueTick, &ev) } }
        return DeferredEvent(clock: self, id: ev, dueTick: dueTick)
    }

    /// Suspend until absolute tick `tick` (defer + awaitCleared).
    public func untilTick(_ tick: UInt64) async throws {
        try await self.defer(until: tick).awaitCleared()
    }

    /// Earliest due tick of a pending event, or nil.
    public func nextDue() throws(ZxvError) -> UInt64? {
        var t: UInt64 = 0
        let st = cLock.withLock { _ in zxv_phase_next_due(handle, &t) }
        if st == ZXV_E_NOT_FOUND { return nil }
        try check(st, cap.context)
        return t
    }

    /// Start the real-time driver: a task advancing the clock at
    /// `ticksPerSecond` (batched, waking at most every millisecond).
    /// Replaces a running driver; a FATAL status stops it and fails waiters.
    public func startRealtime(ticksPerSecond: UInt64) throws(ZxvError) {
        guard ticksPerSecond > 0 else {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "ticksPerSecond must be > 0"))
        }
        stopRealtime()
        let task = Task.detached { [weak self] in
            let clock = ContinuousClock()
            let start = clock.now
            var issued: UInt64 = 0
            while !Task.isCancelled {
                let el = start.duration(to: clock.now).components
                let ns = UInt64(el.seconds) * 1_000_000_000 + UInt64(el.attoseconds / 1_000_000_000)
                let target = (ns / 1_000_000_000) * ticksPerSecond + (ns % 1_000_000_000) * ticksPerSecond / 1_000_000_000
                if target > issued {
                    guard let self else { return }
                    do { try self.advance(target - issued) } catch { return }
                    issued = target
                }
                let nt = issued + 1
                let nextNs = (nt / ticksPerSecond) * 1_000_000_000 + ((nt % ticksPerSecond) * 1_000_000_000 + ticksPerSecond - 1) / ticksPerSecond
                let wait = max(Int64(nextNs) - Int64(ns), 1_000_000)
                try? await Task.sleep(for: .nanoseconds(wait))
            }
        }
        state.withLock { $0.driver = task }
    }

    /// Stop the real-time driver (pending events stay pending).
    public func stopRealtime() {
        let t = state.withLock { s -> Task<Void, Never>? in
            defer { s.driver = nil }
            return s.driver
        }
        t?.cancel()
    }

    // MARK: waiters

    fileprivate func eventState(_ id: UInt64) throws(ZxvError) -> EventState {
        var st: Int32 = 0
        try locked { () throws(ZxvError) in try cap.call { _ in zxv_phase_event(handle, id, &st, nil) } }
        return EventState(rawValue: st) ?? .pending
    }

    fileprivate func cancel(_ id: UInt64) throws(ZxvError) {
        try locked { () throws(ZxvError) in try cap.call { zxv_phase_cancel($0, handle, id) } }
        settle()
    }

    fileprivate func wait(_ id: UInt64) async throws {
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            let closed = state.withLock { s -> Bool in
                if s.closed { return true }
                s.waiters[id, default: []].append(c)
                return false
            }
            if closed { c.resume(throwing: ZxvCancelled(description: "the PhaseClock was closed")) }
            else { settle() } // it may already be cleared
        }
    }

    private func settle() {
        let ids = state.withLock { Array($0.waiters.keys) }
        for id in ids {
            var st: Int32 = ZXV_EV_PENDING
            let rc = cLock.withLock { _ in zxv_phase_event(handle, id, &st, nil) }
            guard rc == ZXV_OK, st != ZXV_EV_PENDING else { continue }
            let cs = state.withLock { $0.waiters.removeValue(forKey: id) } ?? []
            for c in cs {
                if st == ZXV_EV_CLEARED { c.resume() } else { c.resume(throwing: ZxvCancelled(description: "the event was cancelled")) }
            }
        }
    }

    private func failAll(_ error: Error) {
        let ws = state.withLock { s -> [CheckedContinuation<Void, Error>] in
            defer { s.waiters.removeAll() }
            return s.waiters.values.flatMap { $0 }
        }
        for c in ws { c.resume(throwing: error) }
    }
}

/// A deferred S0 event that clears when the clock passes its tick.
public struct DeferredEvent: Sendable {
    /// The clock it belongs to.
    public let clock: PhaseClock
    /// Event id (as the C ABI numbers it).
    public let id: UInt64
    /// The absolute tick it is due at.
    public let dueTick: UInt64

    /// Current state.
    public func state() throws(ZxvError) -> EventState { try clock.eventState(id) }
    /// Suspend until the event clears. Throws `ZxvCancelled` if it is
    /// cancelled or the clock is closed, `ZxvError.fatal` if the context halts.
    public func awaitCleared() async throws { try await clock.wait(id) }
    /// Cancel the event; awaiters throw `ZxvCancelled`.
    public func cancel() throws(ZxvError) { try clock.cancel(id) }
}
