// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// Subject kinds of an attestation (the kernel's lpres_subject_type_t).
public enum Subject: Int32, Sendable {
    case source = 0, binary, config, dependency, dataset, device, model, identity, policy, build
    case artifact, network, sensor, actuator, firmware
}

/// State of one attestation.
public struct AttestationState: Sendable, Equatable {
    /// Follows `Truth(positive:negative:)`.
    public let truth: Truth
    /// Positive evidence count.
    public let positive: UInt32
    /// Negative evidence count.
    public let negative: UInt32
}

/// An attestation registry (lpres). Writes need `.attest`. The gate is clear
/// only when every attestation is certain: no `.both`, no `.neither`.
public final class Attestations: @unchecked Sendable {
    private let cap: Capability
    private let handle: OpaquePointer

    /// A registry in the capability's context (needs `.attest`).
    public init(_ cap: Capability) throws(ZxvError) {
        self.cap = cap
        var r: OpaquePointer?
        try cap.call { zxv_lpres_create_cap($0, &r) }
        handle = r!
    }
    deinit { zxv_lpres_destroy(handle) }

    /// Record an attestation (name, attester <= 31 bytes; digest <= 32 bytes).
    @discardableResult
    public func attest(_ name: String, subject: Subject, state: Truth, attester: String,
                       digest: [UInt8] = []) throws(ZxvError) -> UInt32 {
        var i: UInt32 = 0
        try cap.call {
            zxv_lpres_attest($0, handle, name, subject.rawValue, state.cValue, attester,
                             digest.isEmpty ? nil : digest, UInt32(digest.count), &i)
        }
        return i
    }

    /// Add one piece of evidence.
    public func evidence(_ index: UInt32, positive: Bool) throws(ZxvError) {
        try cap.call { zxv_lpres_evidence($0, handle, index, positive ? 1 : 0) }
    }

    /// Index of a named attestation, or nil.
    public func find(_ name: String) throws(ZxvError) -> UInt32? {
        var i: UInt32 = 0
        let st = zxv_lpres_find(handle, name, &i)
        if st == ZXV_E_NOT_FOUND { return nil }
        try check(st, cap.context)
        return i
    }

    /// State and evidence counts of an attestation.
    public func state(_ index: UInt32) throws(ZxvError) -> AttestationState {
        var s: Int32 = 0, p: UInt32 = 0, n: UInt32 = 0
        try cap.call { _ in zxv_lpres_state(handle, index, &s, &p, &n) }
        return AttestationState(truth: Truth(c: s) ?? .neither, positive: p, negative: n)
    }

    /// True if every attestation is `.true` or `.false`.
    public func gate() throws(ZxvError) -> Bool {
        var clear: Int32 = 0
        try cap.call { _ in zxv_lpres_gate(handle, &clear) }
        return clear != 0
    }
}
