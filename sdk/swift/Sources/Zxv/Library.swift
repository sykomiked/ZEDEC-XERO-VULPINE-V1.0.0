// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI version of the loaded libzxv.
public struct Version: Sendable, Equatable, Comparable, CustomStringConvertible {
    /// Breaking-change counter.
    public let major: UInt32
    /// Additive-change counter.
    public let minor: UInt32
    /// Fixes only.
    public let patch: UInt32
    /// Ordered by major, minor, patch.
    public static func < (a: Version, b: Version) -> Bool {
        (a.major, a.minor, a.patch) < (b.major, b.minor, b.patch)
    }
    /// "1.1.0".
    public var description: String { "\(major).\(minor).\(patch)" }
}

/// Feature groups live in a libzxv build (`ZXV_FEAT_*`).
public struct Features: OptionSet, Sendable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let ledger = Features(rawValue: ZXV_FEAT_LEDGER)
    public static let mxPacs008 = Features(rawValue: ZXV_FEAT_MX_PACS008)
    public static let mxCamt053 = Features(rawValue: ZXV_FEAT_MX_CAMT053)
    public static let iso4217 = Features(rawValue: ZXV_FEAT_ISO4217)
    public static let cardCheck = Features(rawValue: ZXV_FEAT_CARD_CHECK)
    public static let rational = Features(rawValue: ZXV_FEAT_RATIONAL)
    public static let lpres = Features(rawValue: ZXV_FEAT_LPRES)
    public static let caps = Features(rawValue: ZXV_FEAT_CAPS)
    public static let rmag = Features(rawValue: ZXV_FEAT_RMAG)
    public static let phase = Features(rawValue: ZXV_FEAT_PHASE)
    public static let wire = Features(rawValue: ZXV_FEAT_WIRE)
    public static let swarm = Features(rawValue: ZXV_FEAT_SWARM)
    public static let hash = Features(rawValue: ZXV_FEAT_HASH)
    public static let mlkem1024 = Features(rawValue: ZXV_FEAT_MLKEM1024)
    public static let mldsa87 = Features(rawValue: ZXV_FEAT_MLDSA87)
    public static let ed25519 = Features(rawValue: ZXV_FEAT_ED25519)
    public static let tensor = Features(rawValue: ZXV_FEAT_TENSOR)
    /// LoRA adapters: never set yet.
    public static let lora = Features(rawValue: ZXV_FEAT_LORA)
    public static let evm = Features(rawValue: ZXV_FEAT_EVM)
    /// The hexagram tag and quarantine log (ABI 1.3).
    public static let hex = Features(rawValue: ZXV_FEAT_HEX)
    /// The emulation matrix, ZHP-1 profiles and hardware updates (ABI 1.4).
    public static let hw = Features(rawValue: ZXV_FEAT_HW)
}

/// The library entry point: version, features and process-wide policy.
/// (Named `ZxvLibrary`, not `Zxv`, because a type with the module's name
/// would break module-qualified names such as `Zxv.Model`.)
public enum ZxvLibrary {
    /// ABI version of the linked libzxv.
    public static var version: Version {
        var a: UInt32 = 0, b: UInt32 = 0, c: UInt32 = 0
        _ = zxv_version(&a, &b, &c)
        return Version(major: a, minor: b, patch: c)
    }

    /// Feature groups live in the linked libzxv.
    public static var features: Features {
        var m: UInt32 = 0
        _ = zxv_features(&m)
        return Features(rawValue: m)
    }

    private static let policy = Locked(FatalPolicy.throwError)
    /// What the SDK does on a FATAL status (process-wide). `.abort` is the
    /// only uncatchable form of fail-closed.
    public static var fatalPolicy: FatalPolicy {
        get { policy.withLock { $0 } }
        set { policy.withLock { $0 = newValue } }
    }

    private static let compatibility: Result<Void, ZxvError> = {
        let v = version
        if v.major != UInt32(ZXV_ABI_VERSION_MAJOR) || v.minor < 4 {
            return .failure(.system(ZxvSystemError(
                status: ZXV_E_INTERNAL,
                detail: "libzxv ABI \(v) is not compatible with this SDK (needs 1.4 or later 1.x)")))
        }
        return .success(())
    }()

    /// Refuse a library whose major version differs or whose minor is below
    /// 4 (SDK_DESIGN.md section 1; ABI 1.4 added the hardware group, 1.3 the hexagram tag, 1.2 the `*_cap` forms). Called once by `ZxvContext.init`.
    public static func ensureCompatible() throws(ZxvError) {
        if case .failure(let e) = compatibility { throw e }
    }

    /// Symbolic name of a status ("ZXV_E_FUNDS").
    public static func statusName(_ status: Int32) -> String { String(cString: zxv_status_name(status)) }
}

/// One harmonic fundamental, in phase ticks (27,720 = lcm(1..11)).
public let phaseCycle: UInt64 = UInt64(ZXV_PHASE_CYCLE)
