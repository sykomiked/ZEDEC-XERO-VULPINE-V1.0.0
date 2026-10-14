// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: the Web 4 agent layer. Agents hold ML-DSA-65 keys (FIPS 204)
/// and publish signed tool manifests; peers keep a `Directory` of verified
/// agent cards; a human's consent token is required before an agent moves
/// money or shares personal data; requests and replies travel as signed
/// envelopes. The kernel's rules, kept here:
///
/// * IDENTITY: agent_id = SHA3-256(agent public key). An agent is bound to
///   the node hosting it by a record signed by the node's key (peer_id =
///   SHA3-256(node public key)); a self-bound agent has peer_id == agent_id.
/// * CONTEXTS: every signature carries a purpose string, so a signature for
///   one purpose cannot be replayed as another.
/// * CONSENT: a token signed by the HUMAN's key binds the exact tool,
///   parties, request id and payload; tokens are single-use.
///
/// SECRETS: a private key never crosses the ABI. It is generated inside an
/// `Agent` (also used for a human's key: human_id is its agent_id), used
/// there for signing, and wiped when the `Agent` is closed or released.
/// There is no call that returns it, and nothing here logs it.
///
/// TIME: there is no clock here; every call that needs the time takes
/// `nowMs` (Unix milliseconds) from the caller.
///
/// Honest limits: NO NETWORK TRANSPORT (envelopes are bytes; carrying them
/// is the caller's job). Envelopes are signed, not encrypted. A consent
/// token proves that a key holder signed; that the holder is the right
/// human and understood what they signed depends on the device and UI that
/// showed the action. Replay windows, rate limits, freight carriage and the
/// Web 2 / Web 3 bridges are not in this ABI; interoperability is between
/// ZXV instances only. Built without PQC, every call is `.refused`
/// (`.notImplemented`).
public enum Web4 {
    /// ML-DSA-65 public key length.
    public static let pkBytes = Int(ZXV_W4_PK_BYTES)
    /// ML-DSA-65 signature length.
    public static let sigBytes = Int(ZXV_W4_SIG_BYTES)
    /// Length of an agent_id, peer_id or human_id.
    public static let idBytes = Int(ZXV_W4_ID_BYTES)
    /// Agent and tool names: 1...31 of [a-z0-9._-].
    public static let nameMax = Int(ZXV_W4_NAME_MAX)
    /// Tools per manifest.
    public static let maxTools = UInt32(ZXV_W4_MAX_TOOLS)
    /// Largest envelope payload.
    public static let maxPayload = Int(ZXV_W4_MSG_MAX_PAYLOAD)
    /// Length of an encoded consent token.
    public static let consentBytes = Int(ZXV_W4_CONSENT_BYTES)
    /// Longest node-bound card.
    public static let cardMax = Int(ZXV_W4_CARD_MAX)
    /// Longest signed manifest.
    public static let manifestMax = Int(ZXV_W4_MANIFEST_MAX)
    /// Longest signed envelope.
    public static let envelopeMax = Int(ZXV_W4_ENV_MAX)
    /// Directory entries.
    public static let directoryMax = UInt32(ZXV_W4_DIR_MAX)
    /// Consent log entries.
    public static let consentLogMax = UInt32(ZXV_W4_CONSENT_LOG_MAX)

    /// Tool flags (`Tool.flags`).
    public struct ToolFlags: OptionSet, Sendable, Hashable {
        public let rawValue: UInt32
        /// Wrap raw `ZXV_W4_TOOL_*` bits.
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        /// The tool moves money.
        public static let money = ToolFlags(rawValue: ZXV_W4_TOOL_MONEY)
        /// The tool reveals personal data.
        public static let pii = ToolFlags(rawValue: ZXV_W4_TOOL_PII)
        /// It answers with STREAM chunks.
        public static let stream = ToolFlags(rawValue: ZXV_W4_TOOL_STREAM)
    }

    /// What a consent token allows.
    public struct ConsentScope: OptionSet, Sendable, Hashable {
        public let rawValue: UInt32
        /// Wrap raw `ZXV_W4_CONSENT_*` bits.
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        /// Move money up to `maxVfv`.
        public static let money = ConsentScope(rawValue: ZXV_W4_CONSENT_MONEY)
        /// Share personal data.
        public static let pii = ConsentScope(rawValue: ZXV_W4_CONSENT_PII)
    }

    /// Envelope flags.
    public struct MessageFlags: OptionSet, Sendable, Hashable {
        public let rawValue: UInt32
        /// Wrap raw `ZXV_W4_MF_*` bits.
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        /// The last STREAM chunk, or the final response.
        public static let final = MessageFlags(rawValue: ZXV_W4_MF_FINAL)
        /// A consent token is attached (set by sealing, never by the caller).
        public static let consent = MessageFlags(rawValue: ZXV_W4_MF_CONSENT)
    }

    /// The kind of an envelope.
    public enum MessageKind: Int32, Sendable, CaseIterable {
        /// Ask an agent to run a tool.
        case request = 1
        /// The answer.
        case response = 2
        /// One chunk of a streamed answer.
        case stream = 3
        /// Cancel a request.
        case cancel = 4
        /// The request failed.
        case error = 5
    }

    /// What a verified card says.
    public struct CardInfo: Sendable, Equatable {
        /// SHA3-256 of the agent's key (32 bytes).
        public let agentId: [UInt8]
        /// SHA3-256 of the hosting node's key (== `agentId` when self-bound).
        public let peerId: [UInt8]
        /// The agent's name.
        public let name: String
        /// When the binding was signed.
        public let boundMs: UInt64
    }

    /// Verify a card (well formed, binding signature valid under the node
    /// key, or the agent's own key when self-bound) and read it. `.usage`
    /// (`ZXV_E_FORMAT`), `.refused` (`.verificationFailed`).
    public static func verifyCard(_ card: [UInt8]) throws(ZxvError) -> CardInfo {
        var a = [UInt8](repeating: 0, count: idBytes), p = a
        var nm = [CChar](repeating: 0, count: 32)
        var ms: UInt64 = 0
        try check(zxv_w4_card_verify(card, card.count, &a, &p, &nm, &ms))
        return CardInfo(agentId: a, peerId: p, name: cArrayText(nm), boundMs: ms)
    }

    /// The SHA3-256 digest a consent token for one request must carry: over
    /// "web4/v1/action", tool, from, to, requestId and the payload (at most
    /// `maxPayload` bytes, else `.usage` `ZXV_E_TOO_LONG`). Pure.
    public static func actionDigest(tool: String, from: [UInt8], to: [UInt8], requestId: UInt64,
                                    payload: [UInt8]) throws(ZxvError) -> [UInt8] {
        try needSize(from.count, idBytes, "from id")
        try needSize(to.count, idBytes, "to id")
        let t = try cText(tool, "tool name")
        var d = [UInt8](repeating: 0, count: 32)
        try check(zxv_w4_action_digest(t, from, to, requestId, payload, payload.count, &d))
        return d
    }

    /// The fields of a consent token (read without checking its signature).
    public struct ConsentInfo: Sendable, Equatable {
        /// Who consented.
        public let humanId: [UInt8]
        /// Which agent may act.
        public let agentId: [UInt8]
        /// The action digest it covers.
        public let action: [UInt8]
        /// What it allows.
        public let scope: ConsentScope
        /// The most VFV minor units it allows.
        public let maxVfv: UInt64
        /// Valid from.
        public let issuedMs: UInt64
        /// Valid before.
        public let expiresMs: UInt64
        /// Makes it single-use (16 bytes).
        public let nonce: [UInt8]
    }

    /// An encoded consent token (`consentBytes` bytes, "W4CS"): a value type.
    public struct ConsentToken: Sendable, Hashable {
        /// The encoded bytes.
        public let bytes: [UInt8]

        init(checked: [UInt8]) { bytes = checked }

        /// Check the format of received bytes (`.usage` `ZXV_E_FORMAT` or
        /// `ZXV_E_ARG`). The signature is checked by `ConsentLog.check`.
        public init(bytes: [UInt8]) throws(ZxvError) {
            try needSize(bytes.count, Web4.consentBytes, "consent token")
            self.bytes = bytes
            _ = try info()
        }

        /// The fields (no signature check).
        public func info() throws(ZxvError) -> ConsentInfo {
            var h = [UInt8](repeating: 0, count: 32), a = h, act = h
            var sc: UInt32 = 0, mx: UInt64 = 0, iss: UInt64 = 0, exp: UInt64 = 0
            var nonce = [UInt8](repeating: 0, count: 16)
            try check(zxv_w4_consent_info(bytes, &h, &a, &act, &sc, &mx, &iss, &exp, &nonce))
            return ConsentInfo(humanId: h, agentId: a, action: act, scope: ConsentScope(rawValue: sc),
                               maxVfv: mx, issuedMs: iss, expiresMs: exp, nonce: nonce)
        }
    }

    /// A key handle: an agent's (or a human's) ML-DSA-65 key pair and its
    /// signed card. The private key never leaves it and is wiped by
    /// `close()` (or when the handle is released). It belongs to the
    /// capability's context and fails closed with it. The capability it
    /// keeps signs for it: `.cryptoSign` for the card, manifests and
    /// envelopes; `.attest` as well to issue consent. Not thread-safe
    /// (sealing advances its envelope sequence).
    public final class Agent {
        let cap: Capability
        let cell: HandleCell

        /// Create a key handle named `name` (1...31 of [a-z0-9._-]) bound at
        /// `nowMs`. `node`: the agent hosting it (it signs the binding; same
        /// context), or nil for a self-bound agent. `seed`: 32 bytes for a
        /// reproducible key, or nil for a fresh one. Needs `.cryptoSign`.
        public init(_ cap: Capability, name: String, nowMs: UInt64, node: Agent? = nil,
                    seed: [UInt8]? = nil) throws(ZxvError) {
            if let seed { try needSize(seed.count, 32, "agent seed") }
            let n = try cText(name, "agent name")
            self.cap = cap
            let nodePtr = try node?.cell.acquire()
            defer { if nodePtr != nil { node?.cell.release() } }
            var a: OpaquePointer?
            try cap.call { zxv_w4_agent_create($0, seed, n, nodePtr, nowMs, &a) }
            cell = HandleCell(a!, "Web4.Agent") { zxv_w4_agent_destroy($0) }
        }

        private func info(_ body: (OpaquePointer) -> Int32) throws(ZxvError) {
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.read { body(a) }
        }

        /// SHA3-256 of the public key.
        public func agentId() throws(ZxvError) -> [UInt8] {
            var i = [UInt8](repeating: 0, count: Web4.idBytes)
            try info { zxv_w4_agent_info($0, &i, nil, nil) }
            return i
        }

        /// SHA3-256 of the hosting node's public key (== `agentId()` when
        /// self-bound).
        public func peerId() throws(ZxvError) -> [UInt8] {
            var i = [UInt8](repeating: 0, count: Web4.idBytes)
            try info { zxv_w4_agent_info($0, nil, &i, nil) }
            return i
        }

        /// The ML-DSA-65 public key (`pkBytes` bytes).
        public func publicKey() throws(ZxvError) -> [UInt8] {
            var pk = [UInt8](repeating: 0, count: Web4.pkBytes)
            try info { zxv_w4_agent_info($0, nil, nil, &pk) }
            return pk
        }

        /// The agent's card: what a peer needs to verify it (`verifyCard`).
        public func card() throws(ZxvError) -> [UInt8] {
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context, guess: Web4.cardMax) { zxv_w4_agent_card(a, $0, $1, $2) }
        }

        /// Issue a consent token on the HUMAN's side (this handle is the
        /// human's key), after showing them the action: it lets `agent` (an
        /// agent_id) do `action` (an `actionDigest`) within `scope`
        /// (nonempty), money up to `maxVfv` minor units, valid in
        /// [issuedMs, expiresMs). `nonce`: 16 bytes, or nil for a random one.
        /// Needs `.attest`.
        public func issueConsent(agent: [UInt8], action: [UInt8], scope: ConsentScope, maxVfv: UInt64,
                                 issuedMs: UInt64, expiresMs: UInt64, nonce: [UInt8]? = nil) throws(ZxvError) -> ConsentToken {
            try needSize(agent.count, Web4.idBytes, "agent id")
            try needSize(action.count, 32, "action digest")
            if let nonce { try needSize(nonce.count, 16, "consent nonce") }
            let h = try cell.acquire()
            defer { cell.release() }
            var t = [UInt8](repeating: 0, count: Web4.consentBytes)
            try cap.call { zxv_w4_consent_issue($0, h, agent, action, scope.rawValue, maxVfv, issuedMs, expiresMs, nonce, &t) }
            return ConsentToken(checked: t)
        }

        /// Build, sign and encode an envelope from this agent to `to` (an
        /// agent_id), using the next sequence number. `flags`: empty or
        /// `.final`; `tool`: a valid tool name; `payload` at most
        /// `maxPayload` bytes; `consent`: a token to attach, or nil. Needs
        /// `.cryptoSign`.
        public func seal(_ kind: MessageKind, to: [UInt8], requestId: UInt64, chunk: UInt32, tool: String,
                         flags: MessageFlags = [], payload: [UInt8], nowMs: UInt64,
                         consent: ConsentToken? = nil) throws(ZxvError) -> [UInt8] {
            try needSize(to.count, Web4.idBytes, "recipient id")
            let t = try cText(tool, "tool name")
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            let tok: [UInt8]? = consent?.bytes
            return try readBytes(cap.context, guess: Web4.envelopeMax) { b, c, n in
                zxv_w4_env_seal(cap.handle, a, kind.rawValue, to, requestId, chunk, t, flags.rawValue,
                                payload, payload.count, tok, nowMs, b, c, n)
            }
        }

        /// Seal a reply to a REQUEST envelope addressed to this agent (to and
        /// from swapped, same request id and tool). `kind`: `.response`,
        /// `.stream` or `.error`. The request's signature is not checked here
        /// (`verifyEnvelope`).
        public func reply(to request: [UInt8], _ kind: MessageKind, chunk: UInt32, flags: MessageFlags = [],
                          payload: [UInt8], nowMs: UInt64) throws(ZxvError) -> [UInt8] {
            let a = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            return try readBytes(cap.context, guess: Web4.envelopeMax) { b, c, n in
                zxv_w4_env_reply(cap.handle, a, request, request.count, kind.rawValue, chunk, flags.rawValue,
                                 payload, payload.count, nowMs, b, c, n)
            }
        }

        /// Destroy the key handle now: the private key is wiped. Later calls
        /// throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// A directory of verified agent cards, keyed by agent_id. Not
    /// thread-safe. `close()` releases it at once.
    public final class Directory {
        let cell: HandleCell

        /// A directory of at most `capacity` cards (1...`directoryMax`).
        public init(capacity: UInt32) throws(ZxvError) {
            var d: OpaquePointer?
            try check(zxv_w4_dir_create(capacity, &d))
            cell = HandleCell(d!, "Web4.Directory") { zxv_w4_dir_destroy($0) }
        }

        /// Verify a card, then add it or replace the entry for the same agent.
        /// `.refused` (`.verificationFailed`; `ZXV_E_STATE`: its binding is
        /// older than the one held, no rollback; `ZXV_E_CAPACITY`: full),
        /// `.usage` (`ZXV_E_FORMAT`).
        public func add(_ card: [UInt8]) throws(ZxvError) {
            let d = try cell.acquire()
            defer { cell.release() }
            try check(zxv_w4_dir_add(d, card, card.count))
        }

        /// The card held for `agentId`, or nil.
        public func find(_ agentId: [UInt8]) throws(ZxvError) -> [UInt8]? {
            try needSize(agentId.count, Web4.idBytes, "agent id")
            let d = try cell.acquire()
            defer { cell.release() }
            var v = [UInt8](repeating: 0, count: Web4.cardMax)
            var n = 0
            let st = zxv_w4_dir_find(d, agentId, &v, v.count, &n)
            if st == ZXV_E_NOT_FOUND { return nil }
            try check(st)
            return Array(v.prefix(n))
        }

        /// Number of cards held.
        public func count() throws(ZxvError) -> UInt32 {
            let d = try cell.acquire()
            defer { cell.release() }
            var n: UInt32 = 0
            try check(zxv_w4_dir_count(d, &n))
            return n
        }

        /// Release the directory now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// One tool of a manifest.
    public struct Tool: Sendable, Equatable {
        /// 1...31 of [a-z0-9._-].
        public var name: String
        /// Price in VFV minor units (2 digits).
        public var priceVfv: UInt64
        /// Calls per minute (>= 1).
        public var ratePerMin: UInt32
        /// Burst (>= 1).
        public var burst: UInt32
        /// What the tool touches.
        public var flags: ToolFlags
        /// Largest input (<= `maxPayload`).
        public var maxInput: UInt32
        /// Build a tool.
        public init(name: String, priceVfv: UInt64, ratePerMin: UInt32 = 1, burst: UInt32 = 1,
                    flags: ToolFlags = [], maxInput: UInt32 = 0) {
            self.name = name
            self.priceVfv = priceVfv
            self.ratePerMin = ratePerMin
            self.burst = burst
            self.flags = flags
            self.maxInput = maxInput
        }
    }

    /// A manifest's header fields.
    public struct ManifestInfo: Sendable, Equatable {
        /// The signer (zeros until signed).
        public let agentId: [UInt8]
        /// The signer's node (zeros until signed).
        public let peerId: [UInt8]
        /// The caller's manifest version.
        public let version: UInt32
        /// Valid from.
        public let issuedMs: UInt64
        /// Valid before.
        public let expiresMs: UInt64
        /// Tools listed.
        public let toolCount: UInt32
        /// Signed and unchanged since.
        public let isSigned: Bool
    }

    /// A signed tool manifest: up to `maxTools` tools with prices, rate
    /// limits and flags. Signing fixes the agent and peer ids; adding a tool
    /// afterwards drops the signature. Not thread-safe.
    public final class Manifest {
        let cell: HandleCell

        private init(handle: OpaquePointer) {
            cell = HandleCell(handle, "Web4.Manifest") { zxv_w4_manifest_destroy($0) }
        }

        /// An empty, unsigned manifest valid in [issuedMs, expiresMs).
        public convenience init(version: UInt32, issuedMs: UInt64, expiresMs: UInt64) throws(ZxvError) {
            var m: OpaquePointer?
            try check(zxv_w4_manifest_create(version, issuedMs, expiresMs, &m))
            self.init(handle: m!)
        }

        /// Parse a signed manifest's wire form. The signature is kept but NOT
        /// checked: call `verify`. `.usage` (`ZXV_E_FORMAT`).
        public static func decode(_ bytes: [UInt8]) throws(ZxvError) -> Manifest {
            var m: OpaquePointer?
            try check(zxv_w4_manifest_decode(bytes, bytes.count, &m))
            return Manifest(handle: m!)
        }

        private func use<R>(_ body: (OpaquePointer) throws(ZxvError) -> R) throws(ZxvError) -> R {
            let m = try cell.acquire()
            defer { cell.release() }
            return try body(m)
        }

        /// Add a tool. `.usage` `ZXV_E_DUPLICATE` (name listed), `ZXV_E_ARG`,
        /// `ZXV_E_TOO_LONG`; `.refused` `ZXV_E_CAPACITY` (16 tools).
        public func addTool(_ t: Tool) throws(ZxvError) {
            let n = try cText(t.name, "tool name")
            try use { m throws(ZxvError) in
                try check(zxv_w4_manifest_add_tool(m, n, t.priceVfv, t.ratePerMin, t.burst, t.flags.rawValue, t.maxInput))
            }
        }

        /// Sign with `agent`'s key (through the capability it keeps, which
        /// needs `.cryptoSign`).
        public func sign(with agent: Agent) throws(ZxvError) {
            let a = try agent.cell.acquire()
            defer { agent.cell.release() }
            try use { m throws(ZxvError) in try agent.cap.call { zxv_w4_manifest_sign($0, a, m) } }
        }

        /// Check that `publicKey` signed it, that key is the manifest's agent,
        /// and issuedMs <= nowMs < expiresMs. `.refused`
        /// (`.verificationFailed`: wrong key, bad or no signature;
        /// `ZXV_E_STATE`: valid signature, not valid at `nowMs`).
        public func verify(publicKey: [UInt8], nowMs: UInt64) throws(ZxvError) {
            try needSize(publicKey.count, Web4.pkBytes, "agent public key")
            try use { m throws(ZxvError) in try check(zxv_w4_manifest_verify(m, publicKey, nowMs)) }
        }

        /// The signed wire form. `.refused` (`ZXV_E_STATE`) if not signed.
        public func encode() throws(ZxvError) -> [UInt8] {
            try use { m throws(ZxvError) in
                try readBytes(guess: Web4.manifestMax) { zxv_w4_manifest_encode(m, $0, $1, $2) }
            }
        }

        /// The Web 2 discovery face (e.g. /.well-known/web4-agent.json) as
        /// JSON text; prices stay integers in minor units.
        public func toJSON() throws(ZxvError) -> String {
            try use { m throws(ZxvError) in try readText { zxv_w4_manifest_to_json(m, $0, $1, $2) } }
        }

        /// The header fields.
        public func info() throws(ZxvError) -> ManifestInfo {
            try use { m throws(ZxvError) in
                var a = [UInt8](repeating: 0, count: 32), p = a
                var v: UInt32 = 0, iss: UInt64 = 0, exp: UInt64 = 0, tc: UInt32 = 0, s: Int32 = 0
                try check(zxv_w4_manifest_info(m, &a, &p, &v, &iss, &exp, &tc, &s))
                return ManifestInfo(agentId: a, peerId: p, version: v, issuedMs: iss, expiresMs: exp,
                                    toolCount: tc, isSigned: s != 0)
            }
        }

        /// Tool `index` (0-based). `.usage` (`ZXV_E_NOT_FOUND`) past the last.
        public func tool(at index: UInt32) throws(ZxvError) -> Tool {
            try use { m throws(ZxvError) in
                var nm = [CChar](repeating: 0, count: 32)
                var price: UInt64 = 0, rate: UInt32 = 0, burst: UInt32 = 0, flags: UInt32 = 0, mi: UInt32 = 0
                try check(zxv_w4_manifest_tool(m, index, &nm, &price, &rate, &burst, &flags, &mi))
                return Tool(name: cArrayText(nm), priceVfv: price, ratePerMin: rate, burst: burst,
                            flags: ToolFlags(rawValue: flags), maxInput: mi)
            }
        }

        /// Every tool, in order.
        public func tools() throws(ZxvError) -> [Tool] {
            var v: [Tool] = []
            for i in 0..<(try info().toolCount) { v.append(try tool(at: i)) }
            return v
        }

        /// The index of the tool named `name`, or nil.
        public func find(_ name: String) throws(ZxvError) -> UInt32? {
            let n = try cText(name, "tool name")
            return try use { m throws(ZxvError) in
                var i: UInt32 = 0
                let st = zxv_w4_manifest_find(m, n, &i)
                if st == ZXV_E_NOT_FOUND { return nil }
                try check(st)
                return i
            }
        }

        /// Release the manifest now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// A single-use log of consumed and revoked consent tokens. Not
    /// thread-safe. `close()` releases it at once.
    public final class ConsentLog {
        let cell: HandleCell

        /// A log of at most `capacity` entries (1...`consentLogMax`).
        public init(capacity: UInt32) throws(ZxvError) {
            var l: OpaquePointer?
            try Zxv.check(zxv_w4_consent_log_create(capacity, &l))
            cell = HandleCell(l!, "Web4.ConsentLog") { zxv_w4_consent_log_destroy($0) }
        }

        /// Check that `token` lets `agent` do `action` with scope `need`
        /// (nonempty) for `amount` VFV minor units (checked against `maxVfv`
        /// when `need` has `.money`), signed by the human whose key is
        /// `humanPublicKey`, valid at `nowMs`; then record it as used.
        /// Returning normally means consent was given and is now spent.
        /// `.refused` (`.consentDenied`: no token, a mismatch, expired or not
        /// yet valid, a bad signature, used or revoked; `ZXV_E_CAPACITY`: the
        /// log is full even after pruning, fail closed).
        public func check(_ token: ConsentToken?, humanPublicKey: [UInt8], agent: [UInt8], action: [UInt8],
                          need: ConsentScope, amount: UInt64, nowMs: UInt64) throws(ZxvError) {
            try needSize(humanPublicKey.count, Web4.pkBytes, "human public key")
            try needSize(agent.count, Web4.idBytes, "agent id")
            try needSize(action.count, 32, "action digest")
            let l = try cell.acquire()
            defer { cell.release() }
            let tok: [UInt8]? = token?.bytes
            try Zxv.check(zxv_w4_consent_check(l, tok, humanPublicKey, agent, action, need.rawValue, amount, nowMs))
        }

        /// Revoke a token before use; later checks of it fail with
        /// `.consentDenied`. Needs no capability (it only takes authority away).
        public func revoke(_ token: ConsentToken, nowMs: UInt64) throws(ZxvError) {
            let l = try cell.acquire()
            defer { cell.release() }
            try Zxv.check(zxv_w4_consent_revoke(l, token.bytes, nowMs))
        }

        /// Drop entries whose token expired at or before `nowMs`; returns how
        /// many REMAIN.
        @discardableResult
        public func prune(nowMs: UInt64) throws(ZxvError) -> UInt32 {
            let l = try cell.acquire()
            defer { cell.release() }
            var n: UInt32 = 0
            try Zxv.check(zxv_w4_consent_log_prune(l, nowMs, &n))
            return n
        }

        /// Release the log now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }

    /// The fields of an envelope (read without checking its signature).
    public struct Envelope: Sendable, Equatable {
        /// What it is.
        public let kind: MessageKind
        /// Its flags.
        public let flags: MessageFlags
        /// The sender's agent_id.
        public let from: [UInt8]
        /// The recipient's agent_id.
        public let to: [UInt8]
        /// The sender's sequence number.
        public let seq: UInt64
        /// When it was sealed.
        public let timestampMs: UInt64
        /// The request id.
        public let requestId: UInt64
        /// The chunk index.
        public let chunk: UInt32
        /// The tool name.
        public let tool: String
        /// Payload bytes.
        public let payloadLength: UInt32
    }

    /// Parse an envelope (no signature check). `.usage` (`ZXV_E_FORMAT`).
    public static func decodeEnvelope(_ env: [UInt8]) throws(ZxvError) -> Envelope {
        var k: Int32 = 0, fl: UInt32 = 0, from = [UInt8](repeating: 0, count: 32), to = from
        var seq: UInt64 = 0, ts: UInt64 = 0, req: UInt64 = 0, chunk: UInt32 = 0, pl: UInt32 = 0
        var tool = [CChar](repeating: 0, count: 32)
        try check(zxv_w4_env_decode(env, env.count, &k, &fl, &from, &to, &seq, &ts, &req, &chunk, &tool, &pl))
        return Envelope(kind: try cEnum(k, "envelope kind"), flags: MessageFlags(rawValue: fl), from: from, to: to,
                        seq: seq, timestampMs: ts, requestId: req, chunk: chunk, tool: cArrayText(tool),
                        payloadLength: pl)
    }

    /// An envelope's payload.
    public static func envelopePayload(_ env: [UInt8]) throws(ZxvError) -> [UInt8] {
        try readBytes(guess: maxPayload) { zxv_w4_env_payload(env, env.count, $0, $1, $2) }
    }

    /// The consent token attached to an envelope, or nil.
    public static func envelopeConsent(_ env: [UInt8]) throws(ZxvError) -> ConsentToken? {
        var t = [UInt8](repeating: 0, count: consentBytes)
        let st = zxv_w4_env_consent(env, env.count, &t)
        if st == ZXV_E_NOT_FOUND { return nil }
        try check(st)
        return try ConsentToken(bytes: t)
    }

    /// True if `senderPublicKey` is the envelope's sender (from == its
    /// SHA3-256) and the signature verifies; false otherwise. Replay,
    /// timestamp and rate checks are the receiver's job.
    public static func verifyEnvelope(_ env: [UInt8], senderPublicKey: [UInt8]) throws(ZxvError) -> Bool {
        try needSize(senderPublicKey.count, pkBytes, "sender public key")
        let st = zxv_w4_env_verify(env, env.count, senderPublicKey)
        if st == ZXV_E_VERIFY { return false }
        try check(st)
        return true
    }
}
