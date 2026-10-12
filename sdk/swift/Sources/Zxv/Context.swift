// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// One platform instance: a ledger, its currencies, and everything created
/// from it. Thread-safe (the C context is internally serialized). Children
/// (capabilities, budgets, models...) hold a strong reference, so the C
/// handle is destroyed only after every child is gone.
public final class ZxvContext: @unchecked Sendable {
    let handle: OpaquePointer
    private let dead = Locked(false)

    /// Create an empty context (checks the library version first).
    public init() throws(ZxvError) {
        try ZxvLibrary.ensureCompatible()
        var c: OpaquePointer?
        try check(zxv_ctx_create(&c))
        handle = c!
    }

    deinit { zxv_ctx_destroy(handle) }

    func markDead() { dead.withLock { $0 = true } }

    /// Refuse a call on a context that already reported FATAL.
    func ensureAlive() throws(ZxvError) {
        if dead.withLock({ $0 }) {
            try raise(ZXV_E_POISONED, detail: "the context halted earlier; this handle is dead", self)
        }
    }

    /// Run a C call on this context: refuse when dead, map the status.
    func call(_ body: () -> Int32) throws(ZxvError) {
        try ensureAlive()
        try check(body(), self)
    }

    /// A root capability. `bits` defaults to `.all` (the ABI 1.1 bits; pass
    /// `.all1_4` for the hardware group); `identity` is the
    /// 21-octet (168-bit) identity, zeros if nil. Attestation starts TRUE.
    public func rootCapability(_ bits: CapabilitySet = .all, processId: UInt32 = 0,
                               identity: [UInt8]? = nil) throws(ZxvError) -> Capability {
        if let identity, identity.count != Int(ZXV_IDENTITY_BYTES) {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "identity must be 21 bytes"))
        }
        var cap: OpaquePointer?
        try call { zxv_cap_create(handle, processId, identity, bits.rawValue, &cap) }
        return Capability(handle: cap!, context: self, parent: nil)
    }

    /// True once the context halted (a broken invariant or `halt`).
    public var isPoisoned: Bool {
        if dead.withLock({ $0 }) { return true }
        var p: Int32 = 0
        return zxv_ctx_is_poisoned(handle, &p) == ZXV_OK && p != 0
    }

    /// Halt on purpose (operator kill switch). Every later call fails with
    /// `ZxvError.fatal`. `reason` <= 150 bytes.
    public func halt(_ reason: String? = nil) throws(ZxvError) {
        try check(zxv_ctx_halt(handle, reason), self)
    }

    /// Enable an ISO 4217 currency for ledger accounts ("NGN") through
    /// `capability`, which needs `.ledgerPost`. Pass `minorUnits` to assert
    /// the expected value. A capability of another context is refused
    /// (`.capabilityDenied`, `ZXV_E_CAP`).
    public func enableCurrency(_ alpha: String, minorUnits: UInt8? = nil,
                               capability: Capability) throws(ZxvError) {
        try own(capability)
        if let minorUnits {
            try capability.call { zxv_ccy_enable_with_minor_cap($0, alpha, minorUnits) }
        } else {
            try capability.call { zxv_ccy_enable_cap($0, alpha) }
        }
    }

    /// Register a platform-private unit ("VFV") through `capability`
    /// (`.ledgerPost`); never valid in ISO 20022.
    public func registerPrivateUnit(_ code: String, minorUnits: UInt8,
                                    capability: Capability) throws(ZxvError) {
        try own(capability)
        try capability.call { zxv_ccy_register_private_cap($0, code, minorUnits) }
    }

    /// Refuse a capability of another context: the C `*_cap` forms act on
    /// the capability's own context, not on this one.
    private func own(_ capability: Capability) throws(ZxvError) {
        try ensureAlive()
        if capability.context !== self {
            throw .refused(ZxvRefusedError(status: ZXV_E_CAP,
                                           detail: "the capability belongs to a different context"))
        }
    }

    /// Enable an ISO 4217 currency for ledger accounts ("NGN"). Pass
    /// `minorUnits` to assert the expected value.
    @available(*, deprecated, message: "ABI 1.2: pass a .ledgerPost capability: enableCurrency(_:minorUnits:capability:)")
    public func enableCurrency(_ alpha: String, minorUnits: UInt8? = nil) throws(ZxvError) {
        if let minorUnits {
            try call { zxv_ccy_enable_with_minor(handle, alpha, minorUnits) }
        } else {
            try call { zxv_ccy_enable(handle, alpha) }
        }
    }

    /// Register a platform-private unit ("VFV"); never valid in ISO 20022.
    @available(*, deprecated, message: "ABI 1.2: pass a .ledgerPost capability: registerPrivateUnit(_:minorUnits:capability:)")
    public func registerPrivateUnit(_ code: String, minorUnits: UInt8) throws(ZxvError) {
        try call { zxv_ccy_register_private(handle, code, minorUnits) }
    }

    /// Currencies enabled in this context, in enable order.
    public func enabledCurrencies() throws(ZxvError) -> [String] {
        var n: UInt32 = 0
        try call { zxv_ccy_enabled_count(handle, &n) }
        var out: [String] = []
        for i in 0..<n {
            out.append(try readText(self) { zxv_ccy_enabled_at(handle, i, $0, $1, $2) })
        }
        return out
    }
}

/// Capability bits (`ZXV_CAP_*`).
public struct CapabilitySet: OptionSet, Sendable, Hashable {
    public let rawValue: UInt64
    public init(rawValue: UInt64) { self.rawValue = rawValue }
    /// Read balances and journals.
    public static let ledgerRead = CapabilitySet(rawValue: 1 << 0)
    /// Post transfers.
    public static let ledgerPost = CapabilitySet(rawValue: 1 << 1)
    /// Sign (ML-DSA, Ed25519).
    public static let cryptoSign = CapabilitySet(rawValue: 1 << 2)
    /// ML-KEM.
    public static let cryptoKem = CapabilitySet(rawValue: 1 << 3)
    /// Open models and generate.
    public static let aiInference = CapabilitySet(rawValue: 1 << 4)
    /// rmag budgets.
    public static let budget = CapabilitySet(rawValue: 1 << 5)
    /// Admit, defer, advance.
    public static let phase = CapabilitySet(rawValue: 1 << 6)
    /// Attestations.
    public static let attest = CapabilitySet(rawValue: 1 << 7)
    /// Swarm economy.
    public static let swarm = CapabilitySet(rawValue: 1 << 8)
    /// EVM state-channel encoding.
    public static let evm = CapabilitySet(rawValue: 1 << 9)
    /// Build and call the emulation matrix (ABI 1.4).
    public static let hwMatrix = CapabilitySet(rawValue: 1 << 10)
    /// Load hardware images and contracts (ABI 1.4).
    public static let hwReconf = CapabilitySet(rawValue: 1 << 11)
    /// `ZXV_CAP_ALL`: the ten ABI 1.1 bits, a released value that never
    /// changes (it does not include `.hwMatrix` or `.hwReconf`).
    public static let all = CapabilitySet(rawValue: (1 << 10) - 1)
    /// `ZXV_CAP_ALL_1_4`: every bit as of ABI 1.4.
    public static let all1_4 = CapabilitySet(rawValue: (1 << 12) - 1)
}

/// A snapshot of a capability (`zxv_cap_info`).
public struct CapabilityInfo: Sendable, Equatable {
    /// Bits held.
    public let bits: CapabilitySet
    /// Process id it was created with.
    public let processId: UInt32
    /// Gated calls need `.true`.
    public let attestation: Truth
    /// Revoked itself or through an ancestor.
    public let revoked: Bool
}

/// What a part of the program may do. Every gated SDK object holds one and
/// passes it on every call; narrow it with `attenuate` (never widened).
///
/// Honest limit: inside one process this is least-privilege discipline,
/// not a sandbox; code in the same address space can create its own.
public final class Capability: @unchecked Sendable {
    let handle: OpaquePointer
    /// The context this capability belongs to.
    public let context: ZxvContext
    private let parent: Capability? // the C parent must outlive its child

    init(handle: OpaquePointer, context: ZxvContext, parent: Capability?) {
        self.handle = handle
        self.context = context
        self.parent = parent
    }

    deinit { zxv_cap_destroy(handle) }

    /// Run a gated C call.
    func call(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
        try context.ensureAlive()
        try Zxv.check(body(handle), context)
    }

    /// A child holding `self.bits ∩ keep`. Revoking self revokes the child.
    public func attenuate(_ keep: CapabilitySet) throws(ZxvError) -> Capability {
        var c: OpaquePointer?
        try call { zxv_cap_attenuate($0, keep.rawValue, &c) }
        return Capability(handle: c!, context: context, parent: self)
    }

    /// Permanently revoke this capability and every descendant.
    public func revoke() throws(ZxvError) { try call { zxv_cap_revoke($0) } }

    /// Set the attestation; anything but `.true` blocks gated calls.
    public func setAttestation(_ t: Truth) throws(ZxvError) { try call { zxv_cap_set_attestation($0, t.cValue) } }

    /// Bits, process id, attestation and revocation.
    public func info() throws(ZxvError) -> CapabilityInfo {
        var bits: UInt64 = 0, pid: UInt32 = 0, att: Int32 = 0, rev: Int32 = 0
        try call { zxv_cap_info($0, &bits, &pid, &att, &rev) }
        return CapabilityInfo(bits: CapabilitySet(rawValue: bits), processId: pid,
                              attestation: Truth(c: att) ?? .neither, revoked: rev != 0)
    }

    /// True if a call needing `needed` would pass now.
    public func check(_ needed: CapabilitySet) throws(ZxvError) -> Bool {
        try context.ensureAlive()
        let st = zxv_cap_check(handle, needed.rawValue)
        if st == ZXV_E_CAP { return false }
        try Zxv.check(st, context)
        return true
    }

    /// Like `check`, but throws `.refused` (capabilityDenied) with the detail.
    public func require(_ needed: CapabilitySet) throws(ZxvError) {
        try call { zxv_cap_check($0, needed.rawValue) }
    }
}
