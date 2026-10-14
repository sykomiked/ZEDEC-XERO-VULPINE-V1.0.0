// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: an IPFS content identifier in its binary form (version, codec,
/// multihash). A CID names bytes by what they are (a hash), not where they
/// live. The CIDs here are byte-for-byte the ones Kubo makes: `compute` is
/// `ipfs block put`, `unixfs` is `ipfs add --cid-version=1`, and
/// `text(base: .kubo)` prints what Kubo prints. Every `Cid` value was
/// checked by libzxv to be exactly one canonical CIDv0 or CIDv1. Pure.
///
/// Honest limits: a CID proves which bytes are meant; it does not make them
/// available (a CID resolves only while someone holds the bytes) and it
/// does not prove a function's claimed output is right.
public struct Cid: Sendable, Hashable, Comparable, CustomStringConvertible {
    /// Longest binary CID.
    public static let maxBytes = Int(ZXV_CID_MAX)
    /// Longest CID text.
    public static let textMax = Int(ZXV_CID_TEXT_MAX)
    /// Longest multihash digest.
    public static let digestMax = Int(ZXV_CID_DIGEST_MAX)
    /// Multicodec raw (0x55): plain bytes.
    public static let codecRaw = UInt32(ZXV_CID_CODEC_RAW)
    /// Multicodec dag-pb (0x70): UnixFS.
    public static let codecDagPb = UInt32(ZXV_CID_CODEC_DAG_PB)
    /// The identity multihash.
    public static let hashIdentity = UInt32(ZXV_CID_HASH_IDENTITY)
    /// The sha2-256 multihash.
    public static let hashSha2_256 = UInt32(ZXV_CID_HASH_SHA2_256)

    /// How a CID is written as text.
    public enum Base: UInt32, Sendable, CaseIterable {
        /// Kubo's form: CIDv1 "b..." base32, CIDv0 "Qm...".
        case kubo = 0
        /// "z..." base58btc (the AIPI form); CIDv1 only.
        case base58btc = 1
    }

    /// The parts of a CID.
    public struct Info: Sendable, Equatable {
        /// 0 or 1.
        public let version: UInt32
        /// The multicodec (`codecRaw`, `codecDagPb`, an AIPI codec).
        public let codec: UInt32
        /// The multihash code (`hashSha2_256`, `hashIdentity`).
        public let hash: UInt32
        /// The multihash digest.
        public let digest: [UInt8]
    }

    /// The binary form.
    public let bytes: [UInt8]

    /// Wrap bytes libzxv just produced (already canonical).
    init(checked bytes: [UInt8]) { self.bytes = bytes }

    /// Check and copy binary CID bytes. `.refused` (`.cidMismatch`,
    /// `ZXV_E_CID`) unless they are exactly one canonical CIDv0 or CIDv1.
    public init(bytes: [UInt8]) throws(ZxvError) {
        if bytes.count > Cid.maxBytes {
            throw .refused(ZxvRefusedError(status: ZXV_E_CID, detail: "CID longer than ZXV_CID_MAX bytes"))
        }
        try check(zxv_cid_info(bytes, bytes.count, nil, nil, nil, nil, nil))
        self.bytes = bytes
    }

    /// Parse CID text: "b..." (CIDv1 base32), "Qm..." (CIDv0) or "z..."
    /// (CIDv1 base58btc). Only canonical text is accepted: `.refused`
    /// (`.cidMismatch`) otherwise; `.usage` for "" (`ZXV_E_ARG`) or text over
    /// `textMax` (`ZXV_E_TOO_LONG`).
    public init(text: String) throws(ZxvError) {
        let t = try cText(text, "CID text")
        self = try cidOut { zxv_cid_from_text(t, $0, $1) }
    }

    /// The CIDv1 of `data` as one block: sha2-256 under multicodec `codec`
    /// (Kubo `ipfs block put`).
    public static func compute(_ data: [UInt8], codec: UInt32 = Cid.codecRaw) throws(ZxvError) -> Cid {
        try cidOut { zxv_cid_compute(codec, data, data.count, $0, $1) }
    }

    /// The root CID Kubo gives `data` as a file (`ipfs add --cid-version=1`:
    /// 256 KiB chunks, raw leaves): a raw CID up to 262144 bytes, a dag-pb
    /// root above. The blocks are not kept.
    public static func unixfs(_ data: [UInt8]) throws(ZxvError) -> Cid {
        try cidOut { zxv_cid_unixfs(data, data.count, $0, $1) }
    }

    /// Length of the binary form.
    public var count: Int { bytes.count }

    /// The text form. `.refused` (`.cidMismatch`) for a CIDv0 in
    /// `.base58btc` (it has no multibase form).
    public func text(base: Base = .kubo) throws(ZxvError) -> String {
        try readText { zxv_cid_to_text(bytes, bytes.count, base.rawValue, $0, $1, $2) }
    }

    /// Version, codec, hash code and digest.
    public func info() throws(ZxvError) -> Info {
        var v: UInt32 = 0, c: UInt32 = 0, h: UInt32 = 0
        var d = [UInt8](repeating: 0, count: Cid.digestMax)
        var dl = 0
        try check(zxv_cid_info(bytes, bytes.count, &v, &c, &h, &d, &dl))
        return Info(version: v, codec: c, hash: h, digest: Array(d.prefix(dl)))
    }

    /// The CIDv1 of this CID (a CIDv0 becomes dag-pb CIDv1 with the same
    /// hash, Kubo `ipfs cid base32`; a CIDv1 is returned unchanged).
    public func toV1() throws(ZxvError) -> Cid {
        try cidOut { zxv_cid_to_v1(bytes, bytes.count, $0, $1) }
    }

    /// True if `data` is exactly the block this CID names (re-hashed; an
    /// identity CID compares the bytes). A UnixFS root names its root BLOCK,
    /// not the file: check a file with `Cid.unixfs(file) == root` instead.
    public func matches(_ data: [UInt8]) throws(ZxvError) -> Bool {
        let st = zxv_cid_verify(bytes, bytes.count, data, data.count)
        if st == ZXV_E_CID { return false }
        try check(st)
        return true
    }

    /// Like `matches`, but throws `.refused` (`.cidMismatch`) when the bytes
    /// do not match.
    public func verify(_ data: [UInt8]) throws(ZxvError) {
        try check(zxv_cid_verify(bytes, bytes.count, data, data.count))
    }

    /// Byte-wise order (for sorted collections).
    public static func < (a: Cid, b: Cid) -> Bool { a.bytes.lexicographicallyPrecedes(b.bytes) }

    /// The Kubo text form (hex if it cannot be written).
    public var description: String { (try? text()) ?? hexString(bytes) }
}

/// ABI 1.5: AIPI, content-addressed functions (docs/AIPI.md). AIPI names
/// data, functions and calls by CIDs:
///
/// * data: raw (or dag-pb) CIDs of blobs, Z-Wasm modules and contracts;
/// * a function: the CID of its canonical function record (codec
///   `mcFn` + intent), so one function has exactly one CID;
/// * a call: the CID of a call record (function CID + input);
/// * a claim: an executor's answer, the bytes the executor signs.
///
/// The record functions are pure. `Aipi.Node` is an OFFLINE, in-process
/// IPFS block store with the AIPI context over it (needs `.aipi` to store).
///
/// Honest limits: no network transport (no mesh, IPFS peers, gateway or
/// libp2p: a get finds only what was put into this node); no EXECUTE
/// (running a function needs the Z-Wasm sandbox in sdk/kits/aipi, which is
/// not part of libzxv: `plan` only reports the route); no money moves here;
/// signing and checking claims is the caller's job. The 0x3A10xx codecs are
/// multicodec private-use values, not registered.
public enum Aipi {
    /// zxv-fn (intent 0).
    public static let mcFn = UInt32(ZXV_AIPI_MC_FN)
    /// zxv-tensor-infer (intent 1).
    public static let mcTensorInfer = UInt32(ZXV_AIPI_MC_TENSOR_INFER)
    /// zxv-settle-ledger (intent 2).
    public static let mcSettleLedger = UInt32(ZXV_AIPI_MC_SETTLE_LEDGER)
    /// zxv-swarm-route (intent 3).
    public static let mcSwarmRoute = UInt32(ZXV_AIPI_MC_SWARM_ROUTE)
    /// zxv-aipi-call.
    public static let mcCall = UInt32(ZXV_AIPI_MC_CALL)
    /// Export names: [A-Za-z0-9_]{1,63}.
    public static let nameMax = Int(ZXV_AIPI_NAME_MAX)
    /// Largest call input and claim output.
    public static let ioMax = Int(ZXV_AIPI_IO_MAX)
    /// Largest claim certificate.
    public static let certMax = Int(ZXV_AIPI_CERT_MAX)
    /// Largest QUORUM n.
    public static let quorumMax = UInt32(ZXV_AIPI_QUORUM_MAX)
    /// Longest function record.
    public static let fnMax = Int(ZXV_AIPI_FN_MAX)
    /// Longest call record.
    public static let callMax = Int(ZXV_AIPI_CALL_MAX)
    /// Longest claim record.
    public static let claimMax = Int(ZXV_AIPI_CLAIM_MAX)
    /// Largest blob a node stores (2 MiB).
    public static let blockMax = Int(ZXV_AIPI_BLOCK_MAX)
    /// Smallest node store.
    public static let storeMin = Int(ZXV_AIPI_STORE_MIN)
    /// Largest node store (256 MiB).
    public static let storeMax = Int(ZXV_AIPI_STORE_MAX)
    /// Most blocks per node.
    public static let blocksMax = UInt32(ZXV_AIPI_BLOCKS_MAX)

    /// What a function computes; it picks the function CID's codec.
    public enum Intent: UInt32, Sendable, CaseIterable {
        /// A general function.
        case fn = 0
        /// Tensor inference.
        case tensorInfer = 1
        /// Ledger settlement.
        case settleLedger = 2
        /// Swarm routing.
        case swarmRoute = 3
    }

    /// How a result is accepted.
    public enum Verify: UInt32, Sendable, CaseIterable {
        /// A checker export judges (input, output).
        case checker = 1
        /// As checker, with a certificate.
        case cert = 2
        /// k of n executors agree byte for byte.
        case quorum = 3
        /// Never accept a remote result.
        case local = 4
    }

    /// Whether the function may leave the node.
    public enum Privacy: UInt32, Sendable, CaseIterable {
        /// May be announced and fetched.
        case `public` = 0
        /// Private or key-bearing: never leaves the node (stored sealed).
        case local = 1
    }

    /// Whether two runs on the same input give the same bytes.
    public enum Determinism: UInt32, Sendable, CaseIterable {
        /// Outputs may differ.
        case nondeterministic = 0
        /// Same input, same output (needed by QUORUM).
        case deterministic = 1
    }

    /// The route a call would take (`Node.plan`). This offline library
    /// reports only `.local` or `.unavailable`; the others are the kernel's
    /// routes for a networked host.
    public enum Route: Int32, Sendable, CaseIterable {
        /// A cached result (networked hosts).
        case cache = 1
        /// The code is held here.
        case local = 2
        /// Fetch, then run here (networked hosts).
        case fetchLocal = 3
        /// Run on the mesh (networked hosts).
        case mesh = 4
        /// The module (or contract) is not held.
        case unavailable = 5
    }

    /// Every field of a function record. `encodeFunction` checks them all and
    /// masks none: `quorumN`/`quorumK` are for `.quorum` only (2 <= n <= 8,
    /// n/2 < k <= n, deterministic) and 0 otherwise; `checker` is for
    /// `.checker` and `.cert` only (and differs from `entry`); `certifier`
    /// is for `.cert` only.
    public struct FunctionSpec: Sendable, Equatable {
        /// Picks the function CID's codec.
        public var intent: Intent
        /// How a result is accepted.
        public var verify: Verify
        /// Public or node-local.
        public var privacy: Privacy
        /// The determinism class.
        public var determinism: Determinism
        /// QUORUM n, else 0.
        public var quorumN: UInt32
        /// QUORUM k, else 0.
        public var quorumK: UInt32
        /// > 0: the most fuel one run may use.
        public var fuelMax: UInt64
        /// CIDv1 raw sha2-256 of the Z-Wasm module.
        public var module: Cid
        /// CIDv1 raw sha2-256 of the z-contract text, or nil.
        public var contract: Cid?
        /// The export computing the result.
        public var entry: String
        /// The checker export, or nil for none.
        public var checker: String?
        /// The certifier export, or nil for none.
        public var certifier: String?

        /// Build a spec (defaults: a public, deterministic, locally verified `.fn`).
        public init(intent: Intent = .fn, verify: Verify = .local, privacy: Privacy = .public,
                    determinism: Determinism = .deterministic, quorumN: UInt32 = 0, quorumK: UInt32 = 0,
                    fuelMax: UInt64, module: Cid, contract: Cid? = nil, entry: String,
                    checker: String? = nil, certifier: String? = nil) {
            self.intent = intent
            self.verify = verify
            self.privacy = privacy
            self.determinism = determinism
            self.quorumN = quorumN
            self.quorumK = quorumK
            self.fuelMax = fuelMax
            self.module = module
            self.contract = contract
            self.entry = entry
            self.checker = checker
            self.certifier = certifier
        }
    }

    /// The canonical function record of `spec`. `.usage` (`ZXV_E_ARG` for a
    /// field out of range or breaking a rule, `ZXV_E_TOO_LONG` for a name
    /// over 63 bytes, `ZXV_E_UTF8`), `.refused` (`.cidMismatch`: a module or
    /// contract CID that is not CIDv1 raw sha2-256).
    public static func encodeFunction(_ spec: FunctionSpec) throws(ZxvError) -> [UInt8] {
        let entry = try cText(spec.entry, "entry")
        let checker: String? = try spec.checker.map { (s: String) throws(ZxvError) -> String in try cText(s, "checker") }
        let certifier: String? = try spec.certifier.map { (s: String) throws(ZxvError) -> String in try cText(s, "certifier") }
        let contract: [UInt8]? = spec.contract?.bytes
        return try readBytes(guess: fnMax) { b, c, n in
            zxv_aipi_fn_encode(spec.intent.rawValue, spec.verify.rawValue, spec.privacy.rawValue,
                               spec.determinism.rawValue, spec.quorumN, spec.quorumK, spec.fuelMax,
                               spec.module.bytes, spec.module.count, contract, contract?.count ?? 0,
                               entry, checker, certifier, b, c, n)
        }
    }

    /// Check a function record strictly (it must be the canonical encoding)
    /// and read every field. `.usage` (`ZXV_E_FORMAT`) otherwise.
    public static func decodeFunction(_ record: [UInt8]) throws(ZxvError) -> FunctionSpec {
        var it: UInt32 = 0, ve: UInt32 = 0, pr: UInt32 = 0, de: UInt32 = 0
        var qn: UInt32 = 0, qk: UInt32 = 0, fuel: UInt64 = 0
        try check(zxv_aipi_fn_decode(record, record.count, &it, &ve, &pr, &de, &qn, &qk, &fuel))
        func cid(_ field: UInt32) throws(ZxvError) -> Cid? {
            var b = [UInt8](repeating: 0, count: Cid.maxBytes)
            var n = 0
            let st = zxv_aipi_fn_get_cid(record, record.count, field, &b, &n)
            if st == ZXV_E_NOT_FOUND { return nil }
            try check(st)
            return try Cid(bytes: Array(b.prefix(n)))
        }
        func name(_ field: UInt32) throws(ZxvError) -> String? {
            let s = try readText { zxv_aipi_fn_get_name(record, record.count, field, $0, $1, $2) }
            return s.isEmpty ? nil : s
        }
        guard let module = try cid(UInt32(ZXV_AIPI_FIELD_MODULE)) else {
            throw usage("function record has no module", ZXV_E_FORMAT)
        }
        return FunctionSpec(intent: try cEnum(it, "AIPI intent"), verify: try cEnum(ve, "AIPI verify"),
                            privacy: try cEnum(pr, "AIPI privacy"), determinism: try cEnum(de, "AIPI determinism"),
                            quorumN: qn, quorumK: qk, fuelMax: fuel, module: module,
                            contract: try cid(UInt32(ZXV_AIPI_FIELD_CONTRACT)),
                            entry: try name(UInt32(ZXV_AIPI_FIELD_ENTRY)) ?? "",
                            checker: try name(UInt32(ZXV_AIPI_FIELD_CHECKER)),
                            certifier: try name(UInt32(ZXV_AIPI_FIELD_CERTIFIER)))
    }

    /// The function CID of a record: codec `mcFn` + intent, sha2-256 of the
    /// record bytes. `.usage` (`ZXV_E_FORMAT`) for a record that is not one.
    public static func functionCid(_ record: [UInt8]) throws(ZxvError) -> Cid {
        try cidOut { zxv_aipi_fn_cid(record, record.count, $0, $1) }
    }

    /// The call record naming one invocation of `function` on `input` (at
    /// most `ioMax` bytes). `.refused` (`.cidMismatch`: not a function CID),
    /// `.usage` (`ZXV_E_TOO_LONG`).
    public static func encodeCall(_ function: Cid, input: [UInt8]) throws(ZxvError) -> [UInt8] {
        try readBytes(guess: callMax) { zxv_aipi_call_encode(function.bytes, function.count, input, input.count, $0, $1, $2) }
    }

    /// The parts of a call record.
    public struct Call: Sendable, Equatable {
        /// The function CID.
        public let function: Cid
        /// The input bytes.
        public let input: [UInt8]
    }

    /// Check a call record strictly and copy out its parts. `.usage`
    /// (`ZXV_E_FORMAT`) otherwise.
    public static func decodeCall(_ record: [UInt8]) throws(ZxvError) -> Call {
        var fo = 0, fl = 0, io = 0, il = 0
        try check(zxv_aipi_call_decode(record, record.count, &fo, &fl, &io, &il))
        return Call(function: try Cid(bytes: Array(record[fo..<(fo + fl)])), input: Array(record[io..<(io + il)]))
    }

    /// The call CID (codec `mcCall`, sha2-256 of the call record) of
    /// (`function`, `input`). Errors as `encodeCall`.
    public static func callCid(_ function: Cid, input: [UInt8]) throws(ZxvError) -> Cid {
        try cidOut { zxv_aipi_call_cid(function.bytes, function.count, input, input.count, $0, $1) }
    }

    /// The claim record an executor signs: its `output` (and, for cert
    /// functions, `cert`) for the call `call`; `executor` is its 32-byte
    /// node id. Signing it is the caller's job. `.refused` (`.cidMismatch`:
    /// not a call CID), `.usage` (`ZXV_E_TOO_LONG`: output or cert over
    /// 1024 bytes).
    public static func encodeClaim(_ call: Cid, executor: [UInt8], output: [UInt8],
                                   cert: [UInt8] = []) throws(ZxvError) -> [UInt8] {
        try needSize(executor.count, 32, "executor id")
        return try readBytes(guess: claimMax) {
            zxv_aipi_claim_encode(call.bytes, call.count, executor, output, output.count, cert, cert.count, $0, $1, $2)
        }
    }

    /// The parts of a claim record.
    public struct Claim: Sendable, Equatable {
        /// The call CID it answers.
        public let call: Cid
        /// The executor's 32-byte node id.
        public let executor: [UInt8]
        /// Its answer.
        public let output: [UInt8]
        /// Its certificate (cert functions), else empty.
        public let cert: [UInt8]
    }

    /// Check a claim record strictly and copy out its parts. `.usage`
    /// (`ZXV_E_FORMAT`) otherwise. The signature over it is not checked here.
    public static func decodeClaim(_ record: [UInt8]) throws(ZxvError) -> Claim {
        var co = 0, cl = 0, eo = 0, oo = 0, ol = 0, xo = 0, xl = 0
        try check(zxv_aipi_claim_decode(record, record.count, &co, &cl, &eo, &oo, &ol, &xo, &xl))
        return Claim(call: try Cid(bytes: Array(record[co..<(co + cl)])), executor: Array(record[eo..<(eo + 32)]),
                     output: Array(record[oo..<(oo + ol)]), cert: Array(record[xo..<(xo + xl)]))
    }

    /// The Kademlia key under which providers of `cid` are announced on
    /// Vinea: SHA-256("zxv/aipi/v1/provider" || binary CID). Pure;
    /// announcing is not in this library.
    public static func providerKey(_ cid: Cid) throws(ZxvError) -> [UInt8] {
        var k = [UInt8](repeating: 0, count: 32)
        try check(zxv_aipi_provider_key(cid.bytes, cid.count, &k))
        return k
    }

    /// Audit counters of a `Node`.
    public struct NodeInfo: Sendable, Equatable {
        /// Blocks indexed.
        public let blocks: UInt32
        /// Store bytes used.
        public let storeUsed: UInt64
        /// Gets whose bytes re-hashed to their CID.
        public let getsVerified: UInt32
        /// Gets whose stored bytes failed their hash (dropped).
        public let forbidden: UInt32
    }

    /// An offline, in-memory IPFS node (block store and pins) with the AIPI
    /// context over it. It belongs to the context of the capability that
    /// created it and fails closed with it (`.fatal`, `ZXV_E_POISONED`).
    /// Storing needs `.aipi`; reads need only the node. Internally
    /// serialized: any thread may call it. `close()` (or deinit) destroys
    /// it and wipes the node key.
    public final class Node: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// A node holding at most `maxBlocks` blocks (1...`blocksMax`) in
        /// `storeBytes` (`storeMin`...`storeMax`; each block costs its length
        /// plus about 100 bytes). `nodeKey` (32 bytes, copied; nil for none)
        /// seals private blocks with ChaCha20-Poly1305; without it private
        /// puts are `.refused` (`ZXV_E_STATE`). Needs `.aipi`.
        public init(_ cap: Capability, storeBytes: Int, maxBlocks: UInt32, nodeKey: SecretKey? = nil) throws(ZxvError) {
            if let nodeKey { try needSize(nodeKey.count, 32, "AIPI node key") }
            self.cap = cap
            var a: OpaquePointer?
            try cap.call { c in
                if let nodeKey { return zxv_aipi_create(c, storeBytes, maxBlocks, nodeKey.pointer, &a) }
                return zxv_aipi_create(c, storeBytes, maxBlocks, nil, &a)
            }
            cell = HandleCell(a!, "Aipi.Node") { zxv_aipi_destroy($0) }
        }

        /// Store and pin `data` (at most `blockMax` bytes) as one raw block and
        /// return its CID. `isPrivate` stores it sealed and never
        /// announceable. The same bytes again is a no-op; the same bytes
        /// public after private (or the reverse) is `.refused`
        /// (`ZXV_E_STATE`): making something public is a deliberate act.
        /// `.refused` (`ZXV_E_CAPACITY`) when full.
        public func put(_ data: [UInt8], isPrivate: Bool = false) throws(ZxvError) -> Cid {
            let a = try cell.acquire()
            defer { cell.release() }
            var b = [UInt8](repeating: 0, count: Cid.maxBytes)
            var n = 0
            try cap.call { zxv_aipi_put($0, a, data, data.count, isPrivate ? 1 : 0, &b, &n) }
            return Cid(checked: Array(b.prefix(n)))
        }

        /// The bytes `cid` names, re-hashed before they are returned.
        /// `.usage` (`ZXV_E_NOT_FOUND`: not held here, and there is no
        /// network), `.refused` (`.cidMismatch`: an identity CID, or stored
        /// bytes that no longer match, which are dropped; `ZXV_E_STATE`:
        /// private and no key).
        public func get(_ cid: Cid) throws(ZxvError) -> [UInt8] {
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context) { zxv_aipi_get(a, cid.bytes, cid.count, $0, $1, $2) }
        }

        /// Store a function record (checked strictly; sealed when its privacy
        /// is `.local`) and return its function CID. The module and contract
        /// need not be held yet. Needs `.aipi`.
        public func publish(_ functionRecord: [UInt8]) throws(ZxvError) -> Cid {
            let a = try cell.acquire()
            defer { cell.release() }
            var b = [UInt8](repeating: 0, count: Cid.maxBytes)
            var n = 0
            try cap.call { zxv_aipi_publish_fn($0, a, functionRecord, functionRecord.count, &b, &n) }
            return Cid(checked: Array(b.prefix(n)))
        }

        /// The function record a function CID names, verified by hash,
        /// decoded strictly and its codec checked against its intent.
        public func load(_ function: Cid) throws(ZxvError) -> [UInt8] {
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context, guess: Aipi.fnMax) {
                zxv_aipi_load_fn(a, function.bytes, function.count, $0, $1, $2)
            }
        }

        /// The route a call of `function` on `input` would take now. It runs
        /// and changes nothing: `.local` when the module (and contract) are
        /// held, else `.unavailable`.
        public func plan(_ function: Cid, input: [UInt8] = [], inputPrivate: Bool = false) throws(ZxvError) -> Route {
            let a = try cell.acquire()
            defer { cell.release() }
            var r: Int32 = 0
            try cap.read { zxv_aipi_plan(a, function.bytes, function.count, input, input.count, inputPrivate ? 1 : 0, &r) }
            return try cEnum(r, "AIPI route")
        }

        /// The audit counters.
        public func info() throws(ZxvError) -> NodeInfo {
            let a = try cell.acquire()
            defer { cell.release() }
            var bl: UInt32 = 0, used: UInt64 = 0, gv: UInt32 = 0, fb: UInt32 = 0
            try cap.read { zxv_aipi_info(a, &bl, &used, &gv, &fb) }
            return NodeInfo(blocks: bl, storeUsed: used, getsVerified: gv, forbidden: fb)
        }

        /// Destroy the node now (its key is wiped). Later calls throw
        /// `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }
}
