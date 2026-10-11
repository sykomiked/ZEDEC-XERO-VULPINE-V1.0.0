// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// One model's allotment this cycle.
public struct Allotment: Sendable, Equatable {
    /// Real + imaginary tokens.
    public let allotted: UInt64
    /// The emotional share.
    public let imaginary: UInt64
    /// Consumed this cycle.
    public let used: UInt64
}

/// Result of `Swarm.consume`: a partial grant is a value, not an error.
public struct SwarmGrant: Sendable, Equatable {
    /// Tokens granted.
    public let granted: UInt64
    /// True if fewer than requested were left.
    public let exhausted: Bool
}

/// Behaviour profile of an emotion of the palette.
public struct EmotionProfile: Sendable, Equatable {
    public let valence: Int32
    public let temperatureMilli: UInt32
    public let explorePermille: UInt32
    public let verifyPasses: UInt32
    public let priority: UInt32
}

/// The Fibonacci + emotional token economy (Chiglet swarm). Each cycle
/// splits `tokensPerCycle` across Fibonacci levels; the mood's charge funds
/// an imaginary share split by each model's feeling. Needs `.swarm`.
public final class Swarm: @unchecked Sendable {
    private let cap: Capability
    private let handle: OpaquePointer

    /// A swarm of `levels` levels issuing `tokensPerCycle` per cycle (needs
    /// `.swarm`).
    public init(_ cap: Capability, levels: UInt32, tokensPerCycle: UInt64) throws(ZxvError) {
        self.cap = cap
        var s: OpaquePointer?
        try cap.call { zxv_swarm_create_cap($0, levels, tokensPerCycle, &s) }
        handle = s!
    }
    deinit { zxv_swarm_destroy(handle) }

    /// Register a model at a level (level d holds F(d+2) models).
    public func register(model: UInt32, level: UInt32) throws(ZxvError) {
        try cap.call { zxv_swarm_register($0, handle, model, level) }
    }
    /// Activate or deactivate a model.
    public func setActive(model: UInt32, _ active: Bool) throws(ZxvError) {
        try cap.call { zxv_swarm_set_active($0, handle, model, active ? 1 : 0) }
    }
    /// Change the tokens issued per cycle.
    public func setRate(tokensPerCycle: UInt64) throws(ZxvError) {
        try cap.call { zxv_swarm_set_rate($0, handle, tokensPerCycle) }
    }
    /// Swarm mood: an emoji of the palette (e.g. "🤔") and intensity 0...5.
    public func setMood(_ emoji: Unicode.Scalar, intensity: UInt32) throws(ZxvError) {
        try cap.call { zxv_swarm_set_mood($0, handle, emoji.value, intensity) }
    }
    /// One model's feeling.
    public func setFeeling(model: UInt32, _ emoji: Unicode.Scalar, intensity: UInt32) throws(ZxvError) {
        try cap.call { zxv_swarm_set_feeling($0, handle, model, emoji.value, intensity) }
    }
    /// Start a cycle; returns its number.
    @discardableResult
    public func beginCycle() throws(ZxvError) -> UInt64 {
        var c: UInt64 = 0
        try cap.call { zxv_swarm_begin_cycle($0, handle, &c) }
        return c
    }
    /// Grant up to `requested` tokens.
    public func consume(model: UInt32, requested: UInt64) throws(ZxvError) -> SwarmGrant {
        try cap.context.ensureAlive()
        var g: UInt64 = 0
        let st = zxv_swarm_consume(cap.handle, handle, model, requested, &g)
        if st == ZXV_E_BUDGET { return SwarmGrant(granted: g, exhausted: true) }
        try check(st, cap.context)
        return SwarmGrant(granted: g, exhausted: false)
    }
    /// A model's allotment this cycle.
    public func allotment(model: UInt32) throws(ZxvError) -> Allotment {
        var a: UInt64 = 0, i: UInt64 = 0, u: UInt64 = 0
        try cap.call { _ in zxv_swarm_allotment(handle, model, &a, &i, &u) }
        return Allotment(allotted: a, imaginary: i, used: u)
    }
    /// Close the cycle; returns unused tokens (they never carry over).
    @discardableResult
    public func endCycle() throws(ZxvError) -> UInt64 {
        var e: UInt64 = 0
        try cap.call { zxv_swarm_end_cycle($0, handle, &e) }
        return e
    }
    /// Capacity of level d: F(d+2).
    public static func levelCapacity(_ level: UInt32) throws(ZxvUsageError) -> UInt32 {
        var c: UInt32 = 0
        try checkUsage(zxv_swarm_level_capacity(level, &c))
        return c
    }
    /// The behaviour profile of an emotion.
    public static func emotionProfile(_ emoji: Unicode.Scalar) throws(ZxvError) -> EmotionProfile {
        var v: Int32 = 0, t: UInt32 = 0, x: UInt32 = 0, p: UInt32 = 0, pr: UInt32 = 0
        try check(zxv_swarm_emotion_profile(emoji.value, &v, &t, &x, &p, &pr))
        return EmotionProfile(valence: v, temperatureMilli: t, explorePermille: x, verifyPasses: p, priority: pr)
    }
}
