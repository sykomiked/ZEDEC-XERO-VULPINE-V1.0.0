// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Exact amounts. What the user types ("125.50") becomes whole minor units
// with integer arithmetic only; more decimals than the currency has are
// refused, never rounded, the same rule zxvd applies to amount_minor.
import Foundation

public enum Money {
    /// ISO 4217 currencies whose minor unit is not 2 decimals. zxvd does not
    /// serve per-currency minor units yet, so the app keeps this table;
    /// everything else (and private codes such as VFV) uses 2.
    static let exponents: [String: Int] = [
        "BHD": 3, "IQD": 3, "JOD": 3, "KWD": 3, "LYD": 3, "OMR": 3, "TND": 3,
        "BIF": 0, "CLP": 0, "DJF": 0, "GNF": 0, "ISK": 0, "JPY": 0, "KMF": 0, "KRW": 0,
        "PYG": 0, "RWF": 0, "UGX": 0, "UYI": 0, "VND": 0, "VUV": 0, "XAF": 0, "XOF": 0, "XPF": 0,
        "CLF": 4, "UYW": 4,
    ]

    public static func exponent(_ code: String) -> Int {
        exponents[code.uppercased()] ?? 2
    }

    public enum ParseError: Error, Equatable {
        case empty
        case notANumber
        /// More decimals than the currency's minor unit.
        case fraction(allowed: Int)
        case zero
        case tooLarge
    }

    /// "1,250.5" in a 2-decimal currency -> 125050. Commas between digit
    /// groups are allowed; a sign, spaces or exponents are not.
    public static func parse(_ text: String, currency: String) -> Result<Int64, ParseError> {
        let exp = exponent(currency)
        let t = text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: "")
        if t.isEmpty { return .failure(.empty) }
        let parts = t.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count <= 2, let whole = parts.first, !whole.isEmpty,
              whole.allSatisfy({ $0.isASCII && $0.isNumber })
        else { return .failure(.notANumber) }
        var frac = parts.count == 2 ? String(parts[1]) : ""
        guard frac.allSatisfy({ $0.isASCII && $0.isNumber }) else { return .failure(.notANumber) }
        if parts.count == 2 && frac.isEmpty { return .failure(.notANumber) }
        // trailing zeros past the minor unit carry no value: 1.500 USD is 150
        while frac.count > exp && frac.hasSuffix("0") { frac.removeLast() }
        if frac.count > exp { return .failure(.fraction(allowed: exp)) }
        frac += String(repeating: "0", count: exp - frac.count)
        var v: Int64 = 0
        for ch in whole + frac {
            let d = Int64(ch.wholeNumberValue!)
            let (m, o1) = v.multipliedReportingOverflow(by: 10)
            let (a, o2) = m.addingReportingOverflow(d)
            if o1 || o2 { return .failure(.tooLarge) }
            v = a
        }
        if v == 0 { return .failure(.zero) }
        // zxvd takes integers up to 2^53-1 as numbers; above that, as a decimal string
        return .success(v)
    }

    /// 125050 USD -> "1,250.50 USD"; negatives get a minus sign.
    public static func format(_ minor: Int64, currency: String, withCode: Bool = true) -> String {
        let exp = exponent(currency)
        let neg = minor < 0
        let mag = minor.magnitude
        var div: UInt64 = 1
        for _ in 0..<exp { div *= 10 }
        let whole = mag / div
        let frac = mag % div
        var s = groupThousands(String(whole))
        if exp > 0 {
            let f = String(frac)
            s += "." + String(repeating: "0", count: exp - f.count) + f
        }
        if neg { s = "\u{2212}" + s }
        return withCode ? s + " " + currency : s
    }

    public static func groupThousands(_ digits: String) -> String {
        var out = ""
        for (i, ch) in digits.reversed().enumerated() {
            if i > 0 && i % 3 == 0 { out.append(",") }
            out.append(ch)
        }
        return String(out.reversed())
    }

    /// The amount_minor param: a number while exact in JSON doubles, else a string.
    public static func param(_ minor: Int64) -> JSON {
        minor <= 9_007_199_254_740_991 ? .int(minor) : .string(String(minor))
    }
}
