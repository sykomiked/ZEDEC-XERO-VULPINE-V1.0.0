// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.4: hardware emulation and hardware updates (zxv_hw_*), documented
// in sdk/docs/HW_EMULATION.md.
//
//   HwProfile  ZHP-1 machine profiles, the "hardware ROM" (plain data).
//   HwMatrix   functions served by interchangeable, proven implementations;
//              every matrix starts with the standard engines (HwEngine),
//              so every call works on any device. Needs `.hwMatrix`.
//   Hw         release keys and packages signed with ML-DSA-87 AND
//              SLH-DSA-SHAKE-256s (needs `.cryptoSign`; signing takes
//              seconds: a release step).
//   HwRegion   hardware updates as signed images: staged, tested,
//              committed or rolled back, every step logged. Needs `.hwReconf`.
//
// Everything is an integer or a byte array; no floating point.

import CZxv

// MARK: - Constants

/// Implementation kinds, in the order the matrix prefers them at equal cost.
public enum HwKind: Int32, Sendable, CaseIterable {
    /// Silicon built for the function.
    case hard = 0
    /// A reconfigurable fabric region.
    case fabric = 1
    /// A microcoded unit or soft core.
    case micro = 2
    /// Another local component standing in.
    case peer = 3
    /// A software emulator.
    case soft = 4
    /// Another machine.
    case remote = 5
}

/// The state of a matrix resource.
public enum HwResourceState: Int32, Sendable, CaseIterable {
    case ok = 0, faulted = 1, reconfiguring = 2, offline = 3
}

/// The standard engines: contracts every matrix starts with, each with a
/// proven SOFT implementation and version-1 golden vectors.
public enum HwEngine {
    /// op 0: SHA-256.
    public static let sha256 = UInt32(ZXV_HW_FN_SHA256)
    /// ops 0/1: SHA3-256/512; ops 2/3: SHAKE128/256 (u16 output length prefix).
    public static let keccak = UInt32(ZXV_HW_FN_KECCAK)
    /// op 0: HMAC-SHA256 (u16 key length, key, message); key-bearing.
    public static let mac = UInt32(ZXV_HW_FN_MAC)
    /// op 0: n pairs of u64 to n u64 gcds.
    public static let gcd = UInt32(ZXV_HW_FN_GCD)
    /// Four-valued AND / OR / NOT on 64 two-bit lanes.
    public static let logic64 = UInt32(ZXV_HW_FN_LOGIC64)
    /// op 0: one Q8_0 row dot product (absent without the tensor engine).
    public static let tensor = UInt32(ZXV_HW_FN_TENSOR)
}

/// Placement policy flags (`HwMatrix.setPolicy`).
public struct HwPolicy: OptionSet, Sendable, Hashable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    /// Always local, never memoized; cannot be cleared once set.
    public static let keyBearing = HwPolicy(rawValue: ZXV_HW_POLICY_KEY_BEARING)
    /// No REMOTE implementation.
    public static let localOnly = HwPolicy(rawValue: ZXV_HW_POLICY_LOCAL_ONLY)
}

/// Implementation status flags (`HwProviderInfo.flags`).
public struct HwProviderFlags: OptionSet, Sendable, Hashable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    /// Its resource is faulted or offline.
    public static let fault = HwProviderFlags(rawValue: ZXV_HW_PF_FAULT)
    /// A shadow check found it disagreeing.
    public static let disputed = HwProviderFlags(rawValue: ZXV_HW_PF_DISPUTED)
    /// It failed the golden vectors.
    public static let failed = HwProviderFlags(rawValue: ZXV_HW_PF_FAILED)
    /// Not proven against the current contract.
    public static let unproven = HwProviderFlags(rawValue: ZXV_HW_PF_UNPROVEN)
    public static let disabled = HwProviderFlags(rawValue: ZXV_HW_PF_DISABLED)
    /// The function's policy excludes it.
    public static let policy = HwProviderFlags(rawValue: ZXV_HW_PF_POLICY)
    /// Serving the function now.
    public static let serving = HwProviderFlags(rawValue: ZXV_HW_PF_SERVING)
}

/// Coarse power classes (`HwCapKey.powerBucket`, `HwProviderInfo.powerClass`).
public enum HwPowerClass: UInt32, Sendable, CaseIterable {
    case unknown = 0, upTo10mW = 1, upTo100mW = 2, upTo1W = 3, upTo10W = 4, upTo100W = 5, more = 6
}

/// Image and package kinds.
public enum HwImageKind: UInt32, Sendable, CaseIterable {
    case bitstream = 1, partial = 2, microcode = 3, firmware = 4
    /// A new golden-vector contract for a function (`HwMatrix.applyContract`).
    case vectors = 5
}

/// Region states.
public enum HwRegionState: Int32, Sendable, CaseIterable {
    case empty = 0, active = 1, staged = 2, probation = 3, halted = 4
}

/// A region event or audit record kind (`ZXV_HW_EV_*`). A struct, so a
/// value from a newer library is still representable.
public struct HwEvent: RawRepresentable, Sendable, Hashable, CustomStringConvertible {
    public let rawValue: Int32
    public init(rawValue: Int32) { self.rawValue = rawValue }
    /// Verified and loaded into the inactive slot.
    public static let staged = HwEvent(rawValue: ZXV_HW_EV_STAGED)
    /// Switched to and proven: probation begins.
    public static let tested = HwEvent(rawValue: ZXV_HW_EV_TESTED)
    /// Committed: permanent, floor raised.
    public static let activated = HwEvent(rawValue: ZXV_HW_EV_ACTIVATED)
    /// Back to the previous image (or to S0).
    public static let rolledBack = HwEvent(rawValue: ZXV_HW_EV_ROLLED_BACK)
    /// Refused before loading (signature, replay).
    public static let rejected = HwEvent(rawValue: ZXV_HW_EV_REJECTED)
    /// Failed in hardware; payload blocked.
    public static let quarantined = HwEvent(rawValue: ZXV_HW_EV_QUARANTINED)
    /// A function's golden vectors replaced.
    public static let contract = HwEvent(rawValue: ZXV_HW_EV_CONTRACT)
    public var description: String {
        switch self {
        case .staged: return "staged"
        case .tested: return "tested"
        case .activated: return "activated"
        case .rolledBack: return "rolledBack"
        case .rejected: return "rejected"
        case .quarantined: return "quarantined"
        case .contract: return "contract"
        default: return "event(\(rawValue))"
        }
    }
}

/// How a target component is served on a host (`HwProfile.pair`).
public enum HwPairing: Int32, Sendable, CaseIterable {
    /// A host component satisfies it.
    case native = 0
    /// Nothing does: the matrix emulates it.
    case emulate = 1
    /// A device or package node: nothing to run.
    case group = 2
}

/// ZHP-1 component classes. Classes from `privateBase` up are an implementer's own.
public enum HwClass {
    public static let machine = UInt32(ZXV_HW_CLASS_MACHINE)
    public static let cpuCluster = UInt32(ZXV_HW_CLASS_CPU_CLUSTER)
    public static let cpuCore = UInt32(ZXV_HW_CLASS_CPU_CORE)
    public static let cache = UInt32(ZXV_HW_CLASS_CACHE)
    public static let memory = UInt32(ZXV_HW_CLASS_MEMORY)
    public static let interconnect = UInt32(ZXV_HW_CLASS_INTERCONNECT)
    public static let matrixUnit = UInt32(ZXV_HW_CLASS_MATRIX_UNIT)
    public static let vectorUnit = UInt32(ZXV_HW_CLASS_VECTOR_UNIT)
    public static let ternaryUnit = UInt32(ZXV_HW_CLASS_TERNARY_UNIT)
    public static let hashUnit = UInt32(ZXV_HW_CLASS_HASH_UNIT)
    public static let cryptoUnit = UInt32(ZXV_HW_CLASS_CRYPTO_UNIT)
    public static let codecUnit = UInt32(ZXV_HW_CLASS_CODEC_UNIT)
    public static let reconfFabric = UInt32(ZXV_HW_CLASS_RECONF_FABRIC)
    public static let storage = UInt32(ZXV_HW_CLASS_STORAGE)
    public static let network = UInt32(ZXV_HW_CLASS_NETWORK)
    public static let radio = UInt32(ZXV_HW_CLASS_RADIO)
    public static let sensor = UInt32(ZXV_HW_CLASS_SENSOR)
    public static let actuator = UInt32(ZXV_HW_CLASS_ACTUATOR)
    public static let clock = UInt32(ZXV_HW_CLASS_CLOCK)
    public static let power = UInt32(ZXV_HW_CLASS_POWER)
    public static let secureElement = UInt32(ZXV_HW_CLASS_SECURE_ELEMENT)
    public static let display = UInt32(ZXV_HW_CLASS_DISPLAY)
    public static let audio = UInt32(ZXV_HW_CLASS_AUDIO)
    public static let wire = UInt32(ZXV_HW_CLASS_WIRE)
    public static let gpu = UInt32(ZXV_HW_CLASS_GPU)
    public static let package = UInt32(ZXV_HW_CLASS_PACKAGE)
    public static let lane = UInt32(ZXV_HW_CLASS_LANE)
    public static let logicUnit = UInt32(ZXV_HW_CLASS_LOGIC_UNIT)
    public static let nttUnit = UInt32(ZXV_HW_CLASS_NTT_UNIT)
    public static let frameUnit = UInt32(ZXV_HW_CLASS_FRAME_UNIT)
    public static let gcdUnit = UInt32(ZXV_HW_CLASS_GCD_UNIT)
    public static let memChannel = UInt32(ZXV_HW_CLASS_MEM_CHANNEL)
    public static let secureIsland = UInt32(ZXV_HW_CLASS_SECURE_ISLAND)
    public static let io = UInt32(ZXV_HW_CLASS_IO)
    /// `ZXV_HW_CLASS_PRIVATE`: the first implementer-private class.
    public static let privateBase = UInt32(ZXV_HW_CLASS_PRIVATE)
}

/// ZHP-1 capability keys (bit 15 critical, bits 14-13 comparison, bits 12-0 id).
public enum HwCapKey {
    public static let wordBits = UInt32(ZXV_HW_CAP_WORD_BITS)
    public static let count = UInt32(ZXV_HW_CAP_COUNT)
    public static let freqKhz = UInt32(ZXV_HW_CAP_FREQ_KHZ)
    public static let bytes = UInt32(ZXV_HW_CAP_BYTES)
    public static let lineBytes = UInt32(ZXV_HW_CAP_LINE_BYTES)
    public static let isa = UInt32(ZXV_HW_CAP_ISA)
    public static let intWidths = UInt32(ZXV_HW_CAP_INT_WIDTHS)
    public static let radix = UInt32(ZXV_HW_CAP_RADIX)
    public static let macsPerCycle = UInt32(ZXV_HW_CAP_MACS_PER_CYCLE)
    public static let simd = UInt32(ZXV_HW_CAP_SIMD)
    public static let tickNs = UInt32(ZXV_HW_CAP_TICK_NS)
    public static let regions = UInt32(ZXV_HW_CAP_REGIONS)
    public static let bandwidthMbps = UInt32(ZXV_HW_CAP_BANDWIDTH_MBPS)
    public static let latencyNs = UInt32(ZXV_HW_CAP_LATENCY_NS)
    public static let pqAlgs = UInt32(ZXV_HW_CAP_PQ_ALGS)
    public static let hashAlgs = UInt32(ZXV_HW_CAP_HASH_ALGS)
    public static let wireBits = UInt32(ZXV_HW_CAP_WIRE_BITS)
    public static let peakOpsMhz = UInt32(ZXV_HW_CAP_PEAK_OPS_MHZ)
    /// The engine contract (`HwEngine`).
    public static let contractId = UInt32(ZXV_HW_CAP_CONTRACT_ID)
    /// Its golden-vector version.
    public static let contractVer = UInt32(ZXV_HW_CAP_CONTRACT_VER)
    /// `HwKind` serving the node.
    public static let kind = UInt32(ZXV_HW_CAP_KIND)
    public static let capacityClass = UInt32(ZXV_HW_CAP_CAPACITY_CLASS)
    public static let perfBucket = UInt32(ZXV_HW_CAP_PERF_BUCKET)
    public static let powerBucket = UInt32(ZXV_HW_CAP_POWER_BUCKET)
}

/// Instruction sets (`HwCapKey.isa`).
public enum HwIsa {
    public static let x86_64 = UInt32(ZXV_HW_ISA_X86_64)
    public static let arm64 = UInt32(ZXV_HW_ISA_ARM64)
    public static let riscv64 = UInt32(ZXV_HW_ISA_RISCV64)
    public static let wasm32 = UInt32(ZXV_HW_ISA_WASM32)
}

// MARK: - Callbacks

/// An implementation of a matrix function: the operation and its input in,
/// the output out. Throw a `ZxvError` (or any `ZxvFailure`) to return its
/// status to the caller of `HwMatrix.call`; any other error is `ZXV_E_IO`.
/// It runs on the calling thread inside libzxv and may call only the
/// functions it declared with `HwMatrix.addDependency`. Capture the matrix
/// `unowned` to avoid a reference cycle.
public typealias HwImplementation = (_ op: UInt32, _ input: [UInt8]) throws -> [UInt8]

/// The device side of a region. Each method may throw a `ZxvError` (its
/// status is returned to libzxv; any other error is `ZXV_E_IO`). Called on
/// the calling thread inside libzxv.
public protocol HwDevice: AnyObject {
    /// Write `payload` into slot 0 or 1.
    func load(slot: UInt32, payload: [UInt8]) throws
    /// The SHA-256 (32 bytes) of what `slot` really holds.
    func readback(slot: UInt32) throws -> [UInt8]
    /// Switch to `slot`.
    func activate(slot: UInt32) throws
}

/// The status a callback error returns to libzxv.
func hwStatus(of error: any Error) -> Int32 {
    if let e = error as? ZxvError { return e.status < 0 ? e.status : ZXV_E_IO }
    if let e = error as? any ZxvFailure { return e.status < 0 ? e.status : ZXV_E_IO }
    return ZXV_E_IO
}

/// Holds one implementation for the C trampoline (kept alive by its matrix).
final class HwCallBox {
    let body: HwImplementation
    init(_ body: @escaping HwImplementation) { self.body = body }

    func run(_ op: UInt32, _ input: UnsafePointer<UInt8>?, _ n: Int, _ out: UnsafeMutablePointer<UInt8>?,
             _ cap: Int, _ outLen: UnsafeMutablePointer<Int>?) -> Int32 {
        let bytes = (n > 0 && input != nil) ? Array(UnsafeBufferPointer(start: input, count: n)) : []
        let result: [UInt8]
        do { result = try body(op, bytes) } catch { return hwStatus(of: error) }
        outLen?.pointee = result.count
        if result.count > cap { return ZXV_E_BUFFER }
        if let out, !result.isEmpty {
            result.withUnsafeBufferPointer { out.update(from: $0.baseAddress!, count: result.count) }
        }
        return ZXV_OK
    }
}

/// Holds a region's device and event handler for the C trampolines.
final class HwRegionBox {
    let device: any HwDevice
    let onEvent = Locked<((HwEvent, UInt32, UInt32) -> Void)?>(nil)
    init(_ device: any HwDevice) { self.device = device }

    static func of(_ user: UnsafeMutableRawPointer?) -> HwRegionBox {
        Unmanaged<HwRegionBox>.fromOpaque(user!).takeUnretainedValue()
    }

    func load(_ slot: UInt32, _ p: UnsafePointer<UInt8>?, _ n: Int) -> Int32 {
        let bytes = (n > 0 && p != nil) ? Array(UnsafeBufferPointer(start: p, count: n)) : []
        do { try device.load(slot: slot, payload: bytes) } catch { return hwStatus(of: error) }
        return ZXV_OK
    }

    func readback(_ slot: UInt32, _ d: UnsafeMutablePointer<UInt8>?) -> Int32 {
        let digest: [UInt8]
        do { digest = try device.readback(slot: slot) } catch { return hwStatus(of: error) }
        guard digest.count == 32, let d else { return ZXV_E_ARG }
        digest.withUnsafeBufferPointer { d.update(from: $0.baseAddress!, count: 32) }
        return ZXV_OK
    }

    func activate(_ slot: UInt32) -> Int32 {
        do { try device.activate(slot: slot) } catch { return hwStatus(of: error) }
        return ZXV_OK
    }

    func event(_ ev: Int32, _ reason: UInt32, _ version: UInt32) {
        onEvent.withLock { $0 }?(HwEvent(rawValue: ev), reason, version)
    }
}

/// Call `body` with a pointer to `bytes`, or nil.
private func withOptionalBytes<R>(_ bytes: [UInt8]?, _ body: (UnsafePointer<UInt8>?) -> R) -> R {
    guard let bytes else { return body(nil) }
    return bytes.withUnsafeBufferPointer { body($0.baseAddress) }
}

private func needBytes(_ b: [UInt8], _ n: Int, _ what: String) throws(ZxvError) {
    if b.count != n {
        throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "\(what) must be \(n) bytes, got \(b.count)"))
    }
}

// MARK: - Profiles

/// One node of a profile.
public struct HwNode: Sendable, Equatable {
    public let id: UInt32
    /// `Hw.root` for a root node.
    public let parent: UInt32
    /// `HwClass` value.
    public let cls: UInt32
    /// Number of capability descriptors.
    public let capCount: UInt32
}

/// How target node `needId` is served on a host.
public struct HwPair: Sendable, Equatable {
    public let needId: UInt32
    public let pairing: HwPairing
    /// The host node, for `.native`.
    public let haveId: UInt32
    /// For `.emulate`: the first unmet cap key (0 if the host has no
    /// component of that class).
    public let unmetKey: UInt32
}

/// A ZHP-1 machine profile, the "hardware ROM": a tree of abstract
/// components with integer capability descriptors and an opaque per-node
/// extension. Plain data: no context and no capability.
///
///     let host = try HwProfile.probeHost()
///     let r1 = try HwProfile.reference()
///     let (native, emulated) = try host.match(r1)
public final class HwProfile: @unchecked Sendable {
    let handle: OpaquePointer

    private init(handle: OpaquePointer) { self.handle = handle }
    deinit { zxv_hw_profile_destroy(handle) }

    private static func make(_ body: (UnsafeMutablePointer<OpaquePointer?>) -> Int32) throws(ZxvError) -> HwProfile {
        try ZxvLibrary.ensureCompatible()
        var p: OpaquePointer?
        try check(body(&p))
        return HwProfile(handle: p!)
    }

    /// An empty profile.
    public convenience init() throws(ZxvError) {
        try ZxvLibrary.ensureCompatible()
        var p: OpaquePointer?
        try check(zxv_hw_profile_create(&p))
        self.init(handle: p!)
    }

    /// Describe this machine: only what the OS reports reliably.
    public static func probeHost() throws(ZxvError) -> HwProfile { try make { zxv_hw_profile_probe_host($0) } }

    /// A copy of the public VULPINE-class reference machine R1.
    public static func reference() throws(ZxvError) -> HwProfile { try make { zxv_hw_profile_reference($0) } }

    /// `target` as served on `host`: every running node carries `HwCapKey.kind`.
    public static func present(host: HwProfile, target: HwProfile) throws(ZxvError) -> HwProfile {
        try make { zxv_hw_profile_present(host.handle, target.handle, $0) }
    }

    /// Parse a canonical ZHP-1 encoding (anything non-canonical: ZXV_E_FORMAT).
    public static func decode(_ bytes: [UInt8]) throws(ZxvError) -> HwProfile {
        try make { out in bytes.withUnsafeBufferPointer { zxv_hw_profile_decode($0.baseAddress, $0.count, out) } }
    }

    /// The canonical encoding.
    public func encode() throws(ZxvError) -> [UInt8] {
        var n = 0
        let first = zxv_hw_profile_encode(handle, nil, 0, &n)
        if first != ZXV_E_BUFFER { try check(first) }
        var buf = [UInt8](repeating: 0, count: max(n, 1))
        let size = n
        try check(zxv_hw_profile_encode(handle, &buf, size, &n))
        return Array(buf.prefix(n))
    }

    /// SHA-256 of the canonical encoding: the profile's identity.
    public func digest() throws(ZxvError) -> [UInt8] {
        var d = [UInt8](repeating: 0, count: 32)
        try check(zxv_hw_profile_digest(handle, &d))
        return d
    }

    /// Add a node: ids strictly increase, parent first (or `Hw.root`).
    public func addNode(id: UInt32, parent: UInt32, cls: UInt32) throws(ZxvError) {
        try check(zxv_hw_profile_node_add(handle, id, parent, cls))
    }

    /// Set capability `key` of node `node`.
    public func setCap(node: UInt32, key: UInt32, value: UInt64) throws(ZxvError) {
        try check(zxv_hw_profile_cap_set(handle, node, key, value))
    }

    /// The cap value, or nil if the node lacks it.
    public func cap(node: UInt32, key: UInt32) throws(ZxvError) -> UInt64? {
        var v: UInt64 = 0
        let st = zxv_hw_profile_cap_get(handle, node, key, &v)
        if st == ZXV_E_NOT_FOUND { return nil }
        try check(st)
        return v
    }

    /// Set the node's private extension: opaque bytes (at most `Hw.extMax`).
    public func setExtension(node: UInt32, _ ext: [UInt8]) throws(ZxvError) {
        try check(ext.withUnsafeBufferPointer { zxv_hw_profile_ext_set(handle, node, $0.baseAddress, $0.count) })
    }

    /// The node's private extension (empty if none).
    public func ext(node: UInt32) throws(ZxvError) -> [UInt8] {
        var buf = [UInt8](repeating: 0, count: Hw.extMax)
        var n = 0
        let size = buf.count
        try check(zxv_hw_profile_ext_get(handle, node, &buf, size, &n))
        return Array(buf.prefix(n))
    }

    /// Number of nodes.
    public func count() throws(ZxvError) -> UInt32 {
        var n: UInt32 = 0
        try check(zxv_hw_profile_count(handle, &n))
        return n
    }

    /// Node `index` (id order).
    public func node(at index: UInt32) throws(ZxvError) -> HwNode {
        var id: UInt32 = 0, parent: UInt32 = 0, cls: UInt32 = 0, ncaps: UInt32 = 0
        try check(zxv_hw_profile_node(handle, index, &id, &parent, &cls, &ncaps))
        return HwNode(id: id, parent: parent, cls: cls, capCount: ncaps)
    }

    /// Every node, in id order.
    public func nodes() throws(ZxvError) -> [HwNode] {
        var out: [HwNode] = []
        for i in 0..<(try count()) { out.append(try node(at: i)) }
        return out
    }

    /// How many `target` components run natively on this host, and how many are emulated.
    public func match(_ target: HwProfile) throws(ZxvError) -> (native: UInt32, emulated: UInt32) {
        var n: UInt32 = 0, e: UInt32 = 0
        try check(zxv_hw_profile_match(handle, target.handle, &n, &e))
        return (n, e)
    }

    /// How target node `index` (target order) is served on this host.
    public func pair(_ target: HwProfile, index: UInt32) throws(ZxvError) -> HwPair {
        var need: UInt32 = 0, kind: Int32 = 0, have: UInt32 = 0, unmet: UInt32 = 0
        try check(zxv_hw_profile_pair(handle, target.handle, index, &need, &kind, &have, &unmet))
        return HwPair(needId: need, pairing: HwPairing(rawValue: kind) ?? .group, haveId: have, unmetKey: unmet)
    }
}

// MARK: - The matrix

/// A function's contract version, policy and vector count.
public struct HwFunctionInfo: Sendable, Equatable {
    public let contract: UInt32
    public let policy: HwPolicy
    public let vectors: UInt32
}

/// What discovery reports for one implementation.
public struct HwProviderInfo: Sendable, Equatable {
    /// Its index in the matrix.
    public let index: UInt32
    public let fnId: UInt32
    /// Nil for a kind this SDK does not know.
    public let kind: HwKind?
    /// Only `.true` serves.
    public let proof: Truth
    /// The contract version it last proved.
    public let contract: UInt32
    public let flags: HwProviderFlags
    /// 0 (slowest) ... 15.
    public let throughputClass: UInt32
    public let powerClass: HwPowerClass?
    public let calls: UInt64
    public let costSpent: UInt64
}

/// One record of a hash-chained audit log (regions and matrices).
public struct HwLogEntry: Sendable, Equatable {
    public let event: HwEvent
    /// The failure code (0 for none).
    public let reason: UInt32
    /// The region id (the function id for `.contract`).
    public let id: UInt32
    public let version: UInt32
    /// The hexagram tag 0...63 (forbidden for rejections and quarantines).
    public let tag: UInt32
    /// SHA-256 of the package header.
    public let subject: [UInt8]
    /// The record's chain hash.
    public let hash: [UInt8]
}

/// The emulation matrix: functions (engine contracts) served by
/// interchangeable implementations, each serving only after reproducing the
/// function's golden vectors bit for bit. A new matrix already holds the
/// standard engines (`HwEngine`), each with a proven SOFT implementation on
/// `Hw.resHost`. Needs `.hwMatrix` (`.hwReconf` for contract packages).
///
///     let mx = try HwMatrix(cap)
///     let digest = try mx.call(HwEngine.sha256, Array("abc".utf8))
public final class HwMatrix: @unchecked Sendable {
    let cap: Capability
    let handle: OpaquePointer
    private let boxes = Locked<[HwCallBox]>([]) // released after the C matrix

    /// A matrix in the capability's context.
    public init(_ cap: Capability) throws(ZxvError) {
        self.cap = cap
        var m: OpaquePointer?
        try cap.call { zxv_hw_matrix_create_cap($0, &m) }
        handle = m!
    }

    deinit { zxv_hw_matrix_destroy(handle) }

    /// Add a function; `name`: UTF-8, at most 23 bytes.
    public func addFunction(_ fnId: UInt32, name: String? = nil) throws(ZxvError) {
        try cap.call { zxv_hw_function_add($0, handle, fnId, name) }
    }

    /// Add a golden vector of the function's current contract (output at
    /// most 512 bytes); every implementation becomes unproven.
    public func addVector(_ fnId: UInt32, op: UInt32 = 0, input: [UInt8], expected: [UInt8]) throws(ZxvError) {
        try cap.call { zxv_hw_vector_add($0, handle, fnId, op, input, input.count, expected, expected.count) }
    }

    /// Start contract `version` (strictly newer, else ZXV_E_ROLLBACK).
    public func startContract(_ fnId: UInt32, version: UInt32) throws(ZxvError) {
        try cap.call { zxv_hw_function_contract($0, handle, fnId, version) }
    }

    /// Placement: flags, a latency ceiling and a power budget (0: none).
    /// Clearing `.keyBearing` once set is refused (ZXV_E_POLICY).
    public func setPolicy(_ fnId: UInt32, flags: HwPolicy = [], maxLatencyNs: UInt32 = 0,
                          powerBudgetMw: UInt32 = 0) throws(ZxvError) {
        try cap.call { zxv_hw_policy_set($0, handle, fnId, flags.rawValue, maxLatencyNs, powerBudgetMw) }
    }

    /// The function's contract version, policy and vector count.
    public func functionInfo(_ fnId: UInt32) throws(ZxvError) -> HwFunctionInfo {
        var c: UInt32 = 0, p: UInt32 = 0, v: UInt32 = 0
        try cap.call { zxv_hw_function_info($0, handle, fnId, &c, &p, &v) }
        return HwFunctionInfo(contract: c, policy: HwPolicy(rawValue: p), vectors: v)
    }

    public func addResource(_ resId: UInt32) throws(ZxvError) {
        try cap.call { zxv_hw_resource_add($0, handle, resId) }
    }

    public func setResource(_ resId: UInt32, state: HwResourceState) throws(ZxvError) {
        try cap.call { zxv_hw_resource_set($0, handle, resId, state.rawValue) }
    }

    /// Add an implementation of `fnId` on resource `resource`; returns its
    /// index. The closure lives as long as this matrix.
    @discardableResult
    public func addProvider(_ fnId: UInt32, resource: UInt32, kind: HwKind, cost: UInt32,
                            _ implementation: @escaping HwImplementation) throws(ZxvError) -> UInt32 {
        try addProvider(fnId, resource: resource, rawKind: kind.rawValue, cost: cost, implementation)
    }

    /// `addProvider` with a raw `ZXV_HW_KIND_*` value (out of range: ZXV_E_ARG).
    @discardableResult
    public func addProvider(_ fnId: UInt32, resource: UInt32, rawKind: Int32, cost: UInt32,
                            _ implementation: @escaping HwImplementation) throws(ZxvError) -> UInt32 {
        let box = HwCallBox(implementation)
        boxes.withLock { $0.append(box) }
        let user = Unmanaged.passUnretained(box).toOpaque()
        var idx: UInt32 = 0
        try cap.call {
            zxv_hw_provider_add($0, handle, fnId, resource, rawKind, cost, { user, op, input, n, out, outCap, outLen in
                Unmanaged<HwCallBox>.fromOpaque(user!).takeUnretainedValue().run(op, input, n, out, outCap, outLen)
            }, user, &idx)
        }
        return idx
    }

    /// Declare traits used by placement and discovery.
    public func setTraits(_ provider: UInt32, latencyNs: UInt32, powerMw: UInt32, throughputClass: UInt32) throws(ZxvError) {
        try cap.call { zxv_hw_provider_traits($0, handle, provider, latencyNs, powerMw, throughputClass) }
    }

    /// Let `provider` call `dependency` through this matrix.
    public func addDependency(_ provider: UInt32, on dependency: UInt32) throws(ZxvError) {
        try cap.call { zxv_hw_provider_dep($0, handle, provider, dependency) }
    }

    public func enableProvider(_ provider: UInt32, _ on: Bool = true) throws(ZxvError) {
        try cap.call { zxv_hw_provider_enable($0, handle, provider, on ? 1 : 0) }
    }

    /// Run the golden vectors: `.true` serves, `.false` a vector failed,
    /// `.neither` no vectors.
    public func attest(_ provider: UInt32) throws(ZxvError) -> Truth {
        var lp: Int32 = 0
        try cap.call { zxv_hw_attest($0, handle, provider, &lp) }
        return Truth(c: lp) ?? .neither
    }

    /// Number of implementations (indexed 0..<count).
    public func providerCount() throws(ZxvError) -> UInt32 {
        var n: UInt32 = 0
        try cap.call { zxv_hw_provider_count($0, handle, &n) }
        return n
    }

    public func providerInfo(_ provider: UInt32) throws(ZxvError) -> HwProviderInfo {
        var fn: UInt32 = 0, kind: Int32 = 0, lp: Int32 = 0, con: UInt32 = 0, flags: UInt32 = 0
        var tput: UInt32 = 0, pwr: UInt32 = 0, calls: UInt64 = 0, spent: UInt64 = 0
        try cap.call {
            zxv_hw_provider_info($0, handle, provider, &fn, &kind, &lp, &con, &flags, &tput, &pwr, &calls, &spent)
        }
        return HwProviderInfo(index: provider, fnId: fn, kind: HwKind(rawValue: kind), proof: Truth(c: lp) ?? .neither,
                              contract: con, flags: HwProviderFlags(rawValue: flags), throughputClass: tput,
                              powerClass: HwPowerClass(rawValue: pwr), calls: calls, costSpent: spent)
    }

    /// Discovery: every implementation.
    public func providers() throws(ZxvError) -> [HwProviderInfo] {
        var out: [HwProviderInfo] = []
        for i in 0..<(try providerCount()) { out.append(try providerInfo(i)) }
        return out
    }

    /// The implementation serving `fnId` now, or nil if none may.
    public func bound(_ fnId: UInt32) throws(ZxvError) -> UInt32? {
        try cap.context.ensureAlive()
        var p: UInt32 = 0
        let st = zxv_hw_bound(cap.handle, handle, fnId, &p)
        if st == ZXV_E_NOPROVIDER { return nil }
        try check(st, cap.context)
        return p
    }

    /// Call a function (`.refused` ZXV_E_NOPROVIDER when no proven
    /// implementation may serve it).
    public func call(_ fnId: UInt32, _ input: [UInt8] = [], op: UInt32 = 0, maxOutput: Int = 4096) throws(ZxvError) -> [UInt8] {
        try run(fnId, input, op, maxOutput, shadow: false).output
    }

    /// `call` that also runs the next-best implementation and compares; a
    /// mismatch takes both out of service (ZXV_E_DISPUTE).
    public func shadow(_ fnId: UInt32, _ input: [UInt8] = [], op: UInt32 = 0,
                       maxOutput: Int = 4096) throws(ZxvError) -> (output: [UInt8], checked: Bool) {
        try run(fnId, input, op, maxOutput, shadow: true)
    }

    private func run(_ fnId: UInt32, _ input: [UInt8], _ op: UInt32, _ maxOutput: Int,
                     shadow: Bool) throws(ZxvError) -> (output: [UInt8], checked: Bool) {
        var size = max(maxOutput, 1)
        for _ in 0..<2 {
            var out = [UInt8](repeating: 0, count: size)
            var n = 0
            var checked: Int32 = 0
            let outCap = size
            try cap.context.ensureAlive()
            let st = shadow
                ? zxv_hw_shadow(cap.handle, handle, fnId, op, input, input.count, &out, outCap, &n, &checked)
                : zxv_hw_call(cap.handle, handle, fnId, op, input, input.count, &out, outCap, &n)
            if st == ZXV_E_BUFFER && n > size {
                size = n
                continue
            }
            try check(st, cap.context)
            return (Array(out.prefix(n)), checked == 1)
        }
        throw .usage(ZxvUsageError(status: ZXV_E_BUFFER, detail: "the output grew between two calls"))
    }

    /// Give a pure function a memo of `entries` (a power of two up to
    /// 65536; 0 removes it). Key-bearing functions: ZXV_E_POLICY.
    @discardableResult
    public func memo(_ fnId: UInt32, entries: UInt32) throws(ZxvError) -> (hits: UInt64, misses: UInt64) {
        var h: UInt64 = 0, m: UInt64 = 0
        try cap.call { zxv_hw_memo($0, handle, fnId, entries, &h, &m) }
        return (h, m)
    }

    /// Trust a release key for contract packages (at most 4). Needs `.hwReconf`.
    public func trustKey(_ publicKey: [UInt8]) throws(ZxvError) {
        try needBytes(publicKey, Hw.pkBytes, "publicKey")
        try cap.call { zxv_hw_matrix_trust_key($0, handle, publicKey) }
    }

    /// Apply a signed `.vectors` package; returns how many implementations
    /// prove the new contract. Needs `.hwReconf`.
    @discardableResult
    public func applyContract(_ package: [UInt8]) throws(ZxvError) -> UInt32 {
        var n: UInt32 = 0
        try cap.call { zxv_hw_contract_apply($0, handle, package, package.count, &n) }
        return n
    }

    /// The audit log's head hash and record count.
    public func log() throws(ZxvError) -> (head: [UInt8], count: UInt32) {
        var head = [UInt8](repeating: 0, count: 32)
        var n: UInt32 = 0
        try cap.call { zxv_hw_matrix_log($0, handle, &head, &n) }
        return (head, n)
    }

    /// Audit record `index` (0 = oldest).
    public func logEntry(at index: UInt32) throws(ZxvError) -> HwLogEntry {
        var ev: Int32 = 0, reason: UInt32 = 0, id: UInt32 = 0, ver: UInt32 = 0, tag: UInt32 = 0
        var subject = [UInt8](repeating: 0, count: 32), hash = subject
        try cap.call {
            zxv_hw_matrix_log_entry($0, handle, index, &ev, &reason, &id, &ver, &tag, &subject, &hash)
        }
        return HwLogEntry(event: HwEvent(rawValue: ev), reason: reason, id: id, version: ver, tag: tag,
                          subject: subject, hash: hash)
    }

    /// Every audit record, oldest first.
    public func logEntries() throws(ZxvError) -> [HwLogEntry] {
        var out: [HwLogEntry] = []
        for i in 0..<(try log().count) { out.append(try logEntry(at: i)) }
        return out
    }
}

// MARK: - Signed packages

/// A release key: the public key (ML-DSA-87 key then SLH-DSA key) and the
/// two secret keys, wiped when released (or by `wipe()`).
public struct HwReleaseKey: Sendable {
    public let publicKey: [UInt8]
    public let mldsaSecret: SecretKey
    public let slhdsaSecret: SecretKey
    /// Wipe both secret keys now.
    public func wipe() {
        mldsaSecret.wipe()
        slhdsaSecret.wipe()
    }
}

/// Hardware-group sizes, limits, and the package functions.
public enum Hw {
    /// Parent id of a root node.
    public static let root = UInt32(ZXV_HW_ROOT)
    /// Private extension bytes per node.
    public static let extMax = Int(ZXV_HW_EXT_MAX)
    /// The resource every matrix starts with: this machine's CPU.
    public static let resHost = UInt32(ZXV_HW_RES_HOST)
    public static let pkBytes = Int(ZXV_HW_PK_BYTES)
    public static let mldsaSecretBytes = Int(ZXV_HW_MLDSA_SK_BYTES)
    public static let slhdsaSecretBytes = Int(ZXV_HW_SLHDSA_SK_BYTES)
    public static let signatureBytes = Int(ZXV_HW_SIG_BYTES)
    public static let headerBytes = Int(ZXV_HW_HEADER_BYTES)

    /// A fresh release key pair (needs `.cryptoSign`).
    public static func keygen(_ cap: Capability) throws(ZxvError) -> HwReleaseKey {
        var pk = [UInt8](repeating: 0, count: pkBytes)
        let msk = SecretKey(count: mldsaSecretBytes)
        let ssk = SecretKey(count: slhdsaSecretBytes)
        try cap.call { zxv_hw_keygen($0, &pk, msk.mutablePointer, ssk.mutablePointer) }
        return HwReleaseKey(publicKey: pk, mldsaSecret: msk, slhdsaSecret: ssk)
    }

    /// Build and sign a package: header || payload || signatures (needs
    /// `.cryptoSign`). For `.vectors`, `fnId` is the function and `version`
    /// the contract version; `svn` is the anti-rollback floor. SLH-DSA
    /// signing takes seconds.
    public static func buildImage(_ cap: Capability, kind: HwImageKind, targetClass: UInt32, fnId: UInt32,
                                  version: UInt32, svn: UInt32, payload: [UInt8], key: HwReleaseKey,
                                  targetDigest: [UInt8]? = nil) throws(ZxvError) -> [UInt8] {
        if let targetDigest { try needBytes(targetDigest, 32, "targetDigest") }
        if key.mldsaSecret.count != mldsaSecretBytes || key.slhdsaSecret.count != slhdsaSecretBytes {
            throw .usage(ZxvUsageError(status: ZXV_E_ARG, detail: "not a hardware release key"))
        }
        var size = headerBytes + payload.count + signatureBytes
        for _ in 0..<2 {
            var buf = [UInt8](repeating: 0, count: size)
            var n = 0
            let bufCap = size
            try cap.context.ensureAlive()
            let st = withOptionalBytes(targetDigest) { td in
                zxv_hw_image_build(cap.handle, kind.rawValue, targetClass, td, fnId, version, svn, payload,
                                   payload.count, key.mldsaSecret.pointer, key.slhdsaSecret.pointer, &buf, bufCap, &n)
            }
            if st == ZXV_E_BUFFER && n > size {
                size = n
                continue
            }
            try check(st, cap.context)
            return Array(buf.prefix(n))
        }
        throw .usage(ZxvUsageError(status: ZXV_E_BUFFER, detail: "the package grew between two calls"))
    }

    /// The payload of a `.vectors` package: each vector as op (u32 LE),
    /// input length (u32 LE), input, expected length (u32 LE), expected.
    public static func vectorList(_ vectors: [(op: UInt32, input: [UInt8], expected: [UInt8])]) -> [UInt8] {
        func le32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8(truncatingIfNeeded: v >> (8 * $0)) } }
        var out: [UInt8] = []
        for v in vectors {
            out += le32(v.op) + le32(UInt32(v.input.count)) + v.input + le32(UInt32(v.expected.count)) + v.expected
        }
        return out
    }
}

// MARK: - Regions

/// A region's state, version, security floor and quarantined-image count.
public struct HwRegionInfo: Sendable, Equatable {
    public let state: HwRegionState?
    public let version: UInt32
    public let floor: UInt32
    public let quarantined: UInt32
}

/// A reconfigurable region: hardware updates shipped as signed images,
/// staged into the inactive slot, read back, tested against the golden
/// vectors of its implementation in a matrix, then committed or rolled
/// back. Every step is an event and a record in a hash-chained audit log;
/// when the log fills, the region halts and the context is poisoned (fail
/// closed). Needs `.hwReconf`.
public final class HwRegion: @unchecked Sendable {
    let cap: Capability
    let handle: OpaquePointer
    private let matrix: HwMatrix? // the C region uses it; it must outlive the region
    private let box: HwRegionBox

    /// - Parameters:
    ///   - matrix: whose golden vectors test each image (nil: no test)
    ///   - provider: the region's FABRIC or MICRO implementation in `matrix`
    ///   - targetDigest: 32 bytes or nil (zeros)
    ///   - logCapacity: audit log size
    public init(_ cap: Capability, matrix: HwMatrix?, provider: UInt32, regionId: UInt32, targetClass: UInt32,
                device: any HwDevice, targetDigest: [UInt8]? = nil, logCapacity: UInt32 = 64) throws(ZxvError) {
        if let targetDigest { try needBytes(targetDigest, 32, "targetDigest") }
        self.cap = cap
        self.matrix = matrix
        box = HwRegionBox(device)
        let user = Unmanaged.passUnretained(box).toOpaque()
        var r: OpaquePointer?
        try cap.context.ensureAlive()
        let st = withOptionalBytes(targetDigest) { td in
            zxv_hw_region_create_cap(cap.handle, matrix?.handle, provider, regionId, targetClass, td,
                                     { u, slot, p, n in HwRegionBox.of(u).load(slot, p, n) },
                                     { u, slot, d in HwRegionBox.of(u).readback(slot, d) },
                                     { u, slot in HwRegionBox.of(u).activate(slot) },
                                     user, logCapacity, &r)
        }
        try check(st, cap.context)
        handle = r!
    }

    deinit { zxv_hw_region_destroy(handle) }

    /// Receive every recorded step (event, reason, image version), after
    /// the region call released its locks. It may call into the region.
    public func onEvent(_ handler: @escaping (HwEvent, UInt32, UInt32) -> Void) throws(ZxvError) {
        box.onEvent.withLock { $0 = handler }
        let user = Unmanaged.passUnretained(box).toOpaque()
        try cap.call {
            zxv_hw_region_events($0, handle, { u, _, ev, reason, version in
                HwRegionBox.of(u).event(ev, reason, version)
            }, user)
        }
    }

    /// Trust a release key (at most 4).
    public func trustKey(_ publicKey: [UInt8]) throws(ZxvError) {
        try needBytes(publicKey, Hw.pkBytes, "publicKey")
        try cap.call { zxv_hw_region_trust_key($0, handle, publicKey) }
    }

    /// Verify and load an image into the inactive slot.
    public func stage(_ image: [UInt8]) throws(ZxvError) {
        try cap.call { zxv_hw_region_stage($0, handle, image, image.count) }
    }

    /// Switch to the staged image and prove it; passing starts a probation
    /// of `ticks` (ZXV_E_QUARANTINED if it fails: the previous image is back).
    public func trial(ticks: UInt32) throws(ZxvError) {
        try cap.call { zxv_hw_region_trial($0, handle, ticks) }
    }

    /// Make the tested image permanent and raise the floor.
    public func commit() throws(ZxvError) { try cap.call { zxv_hw_region_commit($0, handle) } }

    public func rollback() throws(ZxvError) { try cap.call { zxv_hw_region_rollback($0, handle) } }

    /// One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED).
    public func tick() throws(ZxvError) { try cap.call { zxv_hw_region_tick($0, handle) } }

    public func info() throws(ZxvError) -> HwRegionInfo {
        var st: Int32 = 0, ver: UInt32 = 0, floor: UInt32 = 0, nq: UInt32 = 0
        try cap.call { zxv_hw_region_info($0, handle, &st, &ver, &floor, &nq) }
        return HwRegionInfo(state: HwRegionState(rawValue: st), version: ver, floor: floor, quarantined: nq)
    }

    /// The audit log's head hash and record count.
    public func log() throws(ZxvError) -> (head: [UInt8], count: UInt32) {
        var head = [UInt8](repeating: 0, count: 32)
        var n: UInt32 = 0
        try cap.call { zxv_hw_region_log($0, handle, &head, &n) }
        return (head, n)
    }

    /// Audit record `index` (0 = oldest).
    public func logEntry(at index: UInt32) throws(ZxvError) -> HwLogEntry {
        var ev: Int32 = 0, reason: UInt32 = 0, id: UInt32 = 0, ver: UInt32 = 0, tag: UInt32 = 0
        var subject = [UInt8](repeating: 0, count: 32), hash = subject
        try cap.call {
            zxv_hw_region_log_entry($0, handle, index, &ev, &reason, &id, &ver, &tag, &subject, &hash)
        }
        return HwLogEntry(event: HwEvent(rawValue: ev), reason: reason, id: id, version: ver, tag: tag,
                          subject: subject, hash: hash)
    }

    /// Every audit record, oldest first.
    public func logEntries() throws(ZxvError) -> [HwLogEntry] {
        var out: [HwLogEntry] = []
        for i in 0..<(try log().count) { out.append(try logEntry(at: i)) }
        return out
    }
}
