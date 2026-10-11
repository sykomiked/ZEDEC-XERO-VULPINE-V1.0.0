// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// The kernel's four-valued truth (Belnap's FOUR, lpres), numbered exactly
/// as the C ABI. `.both` is a held contradiction: not an error, not nil,
/// and never silently "true". `Truth` does not convert to `Bool`; choose a
/// policy with `toBool(onBoth:onNeither:)`, `isCertainlyTrue` or
/// `requireTrue()`.
///
/// Forward compatibility: the kernel is adding an orthogonal 2-bit `mode`
/// beside these four states; it will arrive as a separate accessor or a
/// companion type, never by renumbering these cases.
public enum Truth: UInt8, Sendable, CaseIterable, CustomStringConvertible {
    /// S0: unknown, unattested, pending.
    case neither = 0
    /// S+: attested present and valid.
    case `true` = 1
    /// S-: attested absent or invalid.
    case `false` = 2
    /// Contradictory evidence, held.
    case both = 3

    private static let notTable: [UInt8] = [0, 2, 1, 3]
    private static let andTable: [[UInt8]] = [[0, 0, 2, 0], [0, 1, 2, 3], [2, 2, 2, 2], [0, 3, 2, 3]]
    private static let orTable: [[UInt8]] = [[0, 1, 0, 3], [1, 1, 1, 1], [0, 1, 2, 3], [3, 1, 3, 3]]

    /// From the C value; nil for anything outside 0...3.
    public init?(c value: Int32) {
        guard (0...3).contains(value) else { return nil }
        self.init(rawValue: UInt8(value))
    }

    /// From evidence counts: both -> `.both`, only positive -> `.true`,
    /// only negative -> `.false`, none -> `.neither`.
    public init(positive: UInt32, negative: UInt32) {
        self = positive > 0 && negative > 0 ? .both : positive > 0 ? .true : negative > 0 ? .false : .neither
    }

    /// The C value.
    public var cValue: Int32 { Int32(rawValue) }

    /// Four-valued NOT (swaps true and false).
    public func not() -> Truth { Truth(rawValue: Self.notTable[Int(rawValue)])! }
    /// Four-valued AND, identical to `zxv_lp_and`.
    public func and(_ other: Truth) -> Truth { Truth(rawValue: Self.andTable[Int(rawValue)][Int(other.rawValue)])! }
    /// Four-valued OR, identical to `zxv_lp_or`.
    public func or(_ other: Truth) -> Truth { Truth(rawValue: Self.orTable[Int(rawValue)][Int(other.rawValue)])! }

    /// True only for `.true` (`.both` is not certainly true).
    public var isCertainlyTrue: Bool { self == .true }
    /// True only for `.false`.
    public var isCertainlyFalse: Bool { self == .false }
    /// True for `.true` or `.false`: no contradiction, no gap.
    public var isCertain: Bool { self == .true || self == .false }

    /// Collapse to `Bool` with an explicit policy for `.both` and `.neither`.
    public func toBool(onBoth: Bool, onNeither: Bool) -> Bool {
        switch self {
        case .true: return true
        case .false: return false
        case .both: return onBoth
        case .neither: return onNeither
        }
    }

    /// Return normally only for `.true`; otherwise throw a refusal naming the state.
    public func requireTrue(_ what: String = "value") throws(ZxvError) {
        if self != .true {
            throw .refused(ZxvRefusedError(status: ZXV_E_STATE, detail: "\(what) is \(description), not TRUE"))
        }
    }

    /// "NEITHER", "TRUE", "FALSE" or "BOTH".
    public var description: String { ["NEITHER", "TRUE", "FALSE", "BOTH"][Int(rawValue)] }
}
