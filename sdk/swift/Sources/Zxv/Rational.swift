// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// An exact rational number: an `Int64` numerator over a positive `Int64`
/// denominator, always reduced (the kernel's numeric normal form).
///
/// Build it from integers (`Rational(3)`, `Rational(3, 4)`, an integer
/// literal), from exact text (`Rational("12.75")`), from Q16.16, or through
/// the one deliberately named float entry point, `fromDoubleExact`.
/// No other API takes a `Double` or `Float`.
///
/// Arithmetic never rounds and never wraps: the operators are `throws` and
/// report overflow as `ZxvUsageError` (`ZXV_E_RANGE`); the `adding(_:)`
/// family returns `nil` instead.
public struct Rational: Sendable, Hashable, Comparable, CustomStringConvertible, ExpressibleByIntegerLiteral {
    /// The numerator (carries the sign).
    public let numerator: Int64
    /// The denominator (always > 0).
    public let denominator: Int64

    /// Zero.
    public static let zero = Rational(unchecked: 0, 1)

    private init(unchecked n: Int64, _ d: Int64) {
        numerator = n
        denominator = d
    }

    /// `numerator / denominator`, reduced. A zero denominator is `ZXV_E_ARG`;
    /// `Int64.min` is `ZXV_E_RANGE`.
    public init(_ numerator: Int64, _ denominator: Int64 = 1) throws(ZxvUsageError) {
        var n: Int64 = 0, d: Int64 = 1
        try checkUsage(zxv_rat_normalize(numerator, denominator, &n, &d))
        self.init(unchecked: n, d)
    }

    /// An integer literal (exact). `Int64.min` traps, as a literal overflow would.
    public init(integerLiteral value: Int64) {
        precondition(value != Int64.min, "Int64.min has no Int64 negation")
        self.init(unchecked: value, 1)
    }

    /// Parse "12.75", "-0.1", "3/4" or "42" exactly. Exponents, spaces and
    /// "NaN" are `ZXV_E_FORMAT`; too large is `ZXV_E_RANGE`.
    public init(_ text: String) throws(ZxvUsageError) {
        var n: Int64 = 0, d: Int64 = 1
        try checkUsage(zxv_rat_parse(text, &n, &d))
        self.init(unchecked: n, d)
    }

    /// `q / 65536` exactly (Q16.16 fixed point, as the engine uses).
    public static func fromQ16(_ q16: Int32) -> Rational {
        var n: Int64 = 0, d: Int64 = 1
        _ = zxv_rat_normalize(Int64(q16), 65536, &n, &d)
        return Rational(unchecked: n, d)
    }

    /// THE FLOAT BOUNDARY. The exact value of a double: 0.1 becomes
    /// 3602879701896397/36028797018963968, never 1/10. NaN and infinity
    /// throw with `isFloatRejected`; values needing more than 63 bits throw
    /// `ZXV_E_RANGE`.
    public static func fromDoubleExact(_ x: Double) throws(ZxvUsageError) -> Rational {
        var n: Int64 = 0, d: Int64 = 1
        try checkUsage(zxv_rat_from_ieee754(x.bitPattern, &n, &d))
        return Rational(unchecked: n, d)
    }

    /// Exact decimal when one exists ("12.75"), otherwise "n/d" ("1/3").
    public var description: String {
        (try? readText { zxv_rat_format(numerator, denominator, $0, $1, $2) }) ?? "\(numerator)/\(denominator)"
    }

    /// Round to Q16.16 (half away from zero). `ZXV_E_RANGE` outside Int32.
    public func toQ16() throws(ZxvUsageError) -> Int32 {
        var q: Int32 = 0
        try checkUsage(zxv_rat_to_q16(numerator, denominator, &q))
        return q
    }

    /// True for 0.
    public var isZero: Bool { numerator == 0 }
    /// True when the denominator is 1.
    public var isInteger: Bool { denominator == 1 }

    private typealias Op = (Int64, Int64, Int64, Int64, UnsafeMutablePointer<Int64>?, UnsafeMutablePointer<Int64>?) -> Int32
    private func apply(_ op: Op, _ b: Rational) throws(ZxvUsageError) -> Rational {
        var n: Int64 = 0, d: Int64 = 1
        try checkUsage(op(numerator, denominator, b.numerator, b.denominator, &n, &d))
        return Rational(unchecked: n, d)
    }

    /// `self + b`, or nil if the result does not fit.
    public func adding(_ b: Rational) -> Rational? { try? apply(zxv_rat_add, b) }
    /// `self - b`, or nil if the result does not fit.
    public func subtracting(_ b: Rational) -> Rational? { try? apply(zxv_rat_sub, b) }
    /// `self * b`, or nil if the result does not fit.
    public func multiplied(by b: Rational) -> Rational? { try? apply(zxv_rat_mul, b) }
    /// `self / b`, or nil if b is zero or the result does not fit.
    public func divided(by b: Rational) -> Rational? { try? apply(zxv_rat_div, b) }

    /// Exact sum; overflow throws `ZXV_E_RANGE`.
    public static func + (a: Rational, b: Rational) throws(ZxvUsageError) -> Rational { try a.apply(zxv_rat_add, b) }
    /// Exact difference; overflow throws `ZXV_E_RANGE`.
    public static func - (a: Rational, b: Rational) throws(ZxvUsageError) -> Rational { try a.apply(zxv_rat_sub, b) }
    /// Exact product; overflow throws `ZXV_E_RANGE`.
    public static func * (a: Rational, b: Rational) throws(ZxvUsageError) -> Rational { try a.apply(zxv_rat_mul, b) }
    /// Exact quotient; division by zero is `ZXV_E_ARG`, overflow `ZXV_E_RANGE`.
    public static func / (a: Rational, b: Rational) throws(ZxvUsageError) -> Rational { try a.apply(zxv_rat_div, b) }
    /// Negation (always exact: the numerator is never `Int64.min`).
    public static prefix func - (a: Rational) -> Rational { Rational(unchecked: -a.numerator, a.denominator) }

    /// Exact order; no epsilon.
    public static func < (a: Rational, b: Rational) -> Bool {
        var c: Int32 = 0
        _ = zxv_rat_cmp(a.numerator, a.denominator, b.numerator, b.denominator, &c)
        return c < 0
    }
}
