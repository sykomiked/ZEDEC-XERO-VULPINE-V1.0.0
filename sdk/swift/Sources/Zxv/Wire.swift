// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// Resolved truth of a frame (the W5 correlation resolver).
public enum WireTruth: Int32, Sendable {
    case unknown = 0, `true` = 1, `false` = 2, neutral = 3, glut = 4, paradox = 5
}

/// One 21-octet UBH-168 frame: a tag, then five 32-bit words in the fixed
/// L-B-L-B-L byte order (W0 LE S+, W1 BE S-, W2 LE S+, W3 BE S-, W4 LE S+).
/// Packing and unpacking are pure Swift, so a service can frame without
/// the native library; `packNative` cross-checks against C.
public struct Ubh168Frame: Sendable, Hashable {
    /// Frame size in octets.
    public static let size = 21
    /// The raw octets.
    public let bytes: [UInt8]

    /// A frame from exactly 21 octets.
    public init(bytes: [UInt8]) throws(ZxvUsageError) {
        guard bytes.count == Self.size else { throw ZxvUsageError(status: ZXV_E_FORMAT, detail: "a UBH-168 frame is 21 octets") }
        self.bytes = bytes
    }

    /// tag = 100*sync + 10*shell + trunk (trunk, shell 0...9).
    public static func tag(trunk: UInt32, shell: UInt32, sync: Bool) throws(ZxvUsageError) -> UInt8 {
        guard trunk <= 9, shell <= 9 else { throw ZxvUsageError(status: ZXV_E_ARG, detail: "trunk and shell must be 0..9") }
        return UInt8((sync ? 100 : 0) + 10 * shell + trunk)
    }

    /// Pack a tag, three S+ words and two S- words (pure Swift).
    public static func pack(tag: UInt8, sPlus: (UInt32, UInt32, UInt32), sMinus: (UInt32, UInt32)) -> Ubh168Frame {
        var b = [UInt8](repeating: 0, count: size)
        b[0] = tag
        let words = [sPlus.0, sMinus.0, sPlus.1, sMinus.1, sPlus.2]
        for (k, w) in words.enumerated() {
            for i in 0..<4 {
                let shift = k % 2 == 0 ? 8 * i : 8 * (3 - i) // LE : BE
                b[1 + 4 * k + i] = UInt8(truncatingIfNeeded: w >> UInt32(shift))
            }
        }
        return Ubh168Frame(unchecked: b)
    }

    private init(unchecked b: [UInt8]) { bytes = b }

    private func word(_ k: Int) -> UInt32 {
        var v: UInt32 = 0
        for i in 0..<4 {
            let shift = k % 2 == 0 ? 8 * i : 8 * (3 - i)
            v |= UInt32(bytes[1 + 4 * k + i]) << UInt32(shift)
        }
        return v
    }

    /// The tag octet.
    public var tag: UInt8 { bytes[0] }
    /// The three S+ words (W0, W2, W4).
    public var sPlus: [UInt32] { [word(0), word(2), word(4)] }
    /// The two S- words (W1, W3).
    public var sMinus: [UInt32] { [word(1), word(3)] }

    /// Resolve the frame's truth (the C resolver).
    public func resolve() -> WireTruth {
        var t: Int32 = 0
        _ = zxv_wire_resolve(bytes, &t)
        return WireTruth(rawValue: t) ?? .unknown
    }

    /// The idle pilot frame of a trunk (never all zeros), from C.
    public static func idle(trunk: UInt32) throws(ZxvUsageError) -> Ubh168Frame {
        var b = [UInt8](repeating: 0, count: size)
        try checkUsage(zxv_wire_idle(trunk, &b))
        return Ubh168Frame(unchecked: b)
    }

    /// The same pack done by the C library, for cross-checks.
    public static func packNative(tag: UInt8, sPlus: (UInt32, UInt32, UInt32), sMinus: (UInt32, UInt32)) -> Ubh168Frame {
        var b = [UInt8](repeating: 0, count: size)
        let p = [sPlus.0, sPlus.1, sPlus.2], m = [sMinus.0, sMinus.1]
        _ = zxv_wire_pack(tag, p, m, &b)
        return Ubh168Frame(unchecked: b)
    }
}
