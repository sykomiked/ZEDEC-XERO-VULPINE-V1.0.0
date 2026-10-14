# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: the Web 4 agent layer (zxv_w4_*): agents with ML-DSA-65 keys
  # (FIPS 204), signed tool manifests, a directory of verified agent cards,
  # human consent tokens and signed envelopes.
  #
  # IDENTITY:: agent_id = SHA3-256(agent public key). An agent is bound to
  #            the node hosting it by a binding its node signs (peer_id =
  #            SHA3-256(node public key)); a self-bound agent signs its own
  #            binding and has peer_id == agent_id.
  # CONTEXTS:: every signature carries a FIPS 204 context naming its
  #            purpose, so a signature for one purpose cannot be replayed
  #            as another.
  # CONSENT:: an agent never moves money or shares personal data without a
  #           token signed by the HUMAN's key over the exact action
  #           ({.action_digest}), scope, amount, validity and a nonce.
  #           Tokens are single-use ({ConsentLog}).
  # SECRETS:: a private key never leaves its {Agent} handle: there is no
  #           call that returns it, and #close wipes it.
  # TIME:: there is no clock in here: every call that needs the time takes
  #        +now_ms+ (Unix milliseconds) from you.
  #
  # Capabilities: creating an {Agent} and everything it signs (manifests,
  # envelopes) need :crypto_sign; issuing consent needs :attest. Parsing,
  # verifying, digests, manifests, the directory and the consent log need
  # none.
  #
  # Honest limits: envelopes are signed, not encrypted. A consent token
  # proves that a key holder signed; that the holder is the right human
  # and understood what they signed depends on the device and UI that
  # showed the action, which this library cannot see. The receiving runtime
  # (replay window, rate limits), carriage over freight, and the Web 2
  # (OIDC, OAuth, HTTP) and Web 3 (EVM, payments, identity links) bridges
  # are NOT here. Interoperability is between ZXV instances only; no other
  # agent protocol is implemented or claimed.
  module Web4
    # ML-DSA-65 public key size.
    PK_BYTES = 1952
    # ML-DSA-65 signature size.
    SIG_BYTES = 3309
    # agent_id, peer_id, human_id size.
    ID_BYTES = 32
    # Longest agent or tool name: 1..31 of [a-z0-9._-].
    NAME_MAX = 31
    # Most tools in one manifest.
    MAX_TOOLS = 16
    # Largest envelope payload.
    MSG_MAX_PAYLOAD = 2048
    # Size of an encoded consent token.
    CONSENT_BYTES = 3453
    # Longest agent card (a node-bound one; self-bound cards are 1952 shorter).
    CARD_MAX = 7259
    # Longest encoded manifest.
    MANIFEST_MAX = 4349
    # Longest encoded envelope.
    ENV_MAX = 8970
    # Most directory entries.
    DIR_MAX = 4096
    # Most consent log entries.
    CONSENT_LOG_MAX = 65_536

    # Tool flags: :money (moves money), :pii (reveals personal data),
    # :stream (answers with STREAM chunks).
    TOOL_FLAGS = { money: 0x01, pii: 0x02, stream: 0x04 }.freeze
    # Consent scopes: :money and :pii.
    CONSENT_SCOPES = { money: 0x01, pii: 0x02 }.freeze
    # Envelope kinds.
    KINDS = { request: 1, response: 2, stream: 3, cancel: 4, error: 5 }.freeze
    # Envelope flag: last STREAM chunk / final response.
    MF_FINAL = 0x01
    # Envelope flag: a consent token is attached (set by sealing, never by you).
    MF_CONSENT = 0x02

    # What a verified card says: the ids (derived from the keys), the
    # agent's name and when it was bound (Unix ms).
    CardInfo = Struct.new(:agent_id, :peer_id, :name, :bound_ms, keyword_init: true) do
      # @return [Boolean] whether the agent signed its own binding
      def self_bound? = agent_id == peer_id
    end

    module_function

    # Verify a card: well formed, and its binding signature valid under the
    # node key (or the agent's own key when self-bound). Pure.
    # @param card [String]
    # @return [CardInfo]
    # @raise [VerificationFailedError] the signature fails
    # @raise [ZxvUsageError] ZXV_E_FORMAT (malformed card)
    def verify_card(card)
      c = Call.bytes!(card, "card")
      aid = FFI::MemoryPointer.new(:uint8, 32)
      pid = FFI::MemoryPointer.new(:uint8, 32)
      name = FFI::MemoryPointer.new(:char, 32)
      ms = Call.out(:uint64)
      Call.check(Native.lib.zxv_w4_card_verify(Call.mem0(c), c.bytesize, aid, pid, name, ms))
      CardInfo.new(agent_id: aid.read_bytes(32), peer_id: pid.read_bytes(32),
                   name: name.read_string.force_encoding(Encoding::UTF_8), bound_ms: ms.read_uint64)
    end

    # The digest a consent token for one request must carry: SHA3-256 over
    # "web4/v1/action", the tool, from, to, req_id and the payload (each
    # length-prefixed). It binds the exact tool, parties, request and payload.
    # @param tool [String] a valid tool name
    # @param from [String] 32-byte agent id of the requester
    # @param to [String] 32-byte agent id of the tool's agent
    # @param req_id [Integer] uint64
    # @param payload [String] at most MSG_MAX_PAYLOAD bytes
    # @return [String] 32 bytes
    def action_digest(tool:, from:, to:, req_id:, payload: "")
      p = Call.bytes!(payload, "payload")
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.zxv_w4_action_digest(Call.cstr!(tool, "tool"), id32(from, "from"), id32(to, "to"),
                                                 Call.uint64!(req_id, "req_id"), Call.mem0(p), p.bytesize, out))
      out.read_bytes(32)
    end

    # Read a consent token's fields (no signature check).
    # @return [ConsentInfo]
    # @raise [ZxvUsageError] ZXV_E_FORMAT
    def consent_info(token)
      t = token!(token)
      h, a, act = Array.new(3) { FFI::MemoryPointer.new(:uint8, 32) }
      scope = Call.out(:uint32)
      max, iss, exp = Array.new(3) { Call.out(:uint64) }
      nonce = FFI::MemoryPointer.new(:uint8, 16)
      Call.check(Native.lib.zxv_w4_consent_info(t, h, a, act, scope, max, iss, exp, nonce))
      ConsentInfo.new(human_id: h.read_bytes(32), agent_id: a.read_bytes(32), action: act.read_bytes(32),
                      scope: flag_names(CONSENT_SCOPES, scope.read_uint32), max_vfv: max.read_uint64,
                      issued_ms: iss.read_uint64, expires_ms: exp.read_uint64, nonce: nonce.read_bytes(16))
    end

    # @api private
    def id32(value, name) = Call.mem(Call.bytes!(value, name, size: ID_BYTES))

    # @api private
    def token!(token) = Call.mem(Call.bytes!(token, "token", size: CONSENT_BYTES))

    # Flags from a Symbol, an Array of Symbols or an Integer.
    # @api private
    def flags(map, spec, name)
      case spec
      when Symbol then map.fetch(spec) { raise ZxvUsageError.new(Status::E_ARG, "unknown #{name} #{spec.inspect}") }
      when Array then spec.reduce(0) { |m, s| m | flags(map, s, name) }
      else Call.uint32!(spec, name)
      end
    end

    # @api private
    def flag_names(map, bits) = map.select { |_, b| bits.anybits?(b) }.keys

    # The fields of a consent token. +scope+ is an Array of CONSENT_SCOPES
    # keys; the ids, +action+ and +nonce+ are binary Strings.
    ConsentInfo = Struct.new(:human_id, :agent_id, :action, :scope, :max_vfv, :issued_ms, :expires_ms, :nonce,
                             keyword_init: true)

    # A key handle: an ML-DSA-65 key pair and the agent's signed card. It
    # is also used for a human's key (the human_id is the handle's
    # agent_id). It belongs to its capability's context and fails closed
    # with it. NOT thread-safe: sealing advances its envelope sequence.
    #
    #   alice = Zxv::Web4::Agent.new(cap, name: "alice", now_ms: now)
    #   env = alice.seal(to: bob.agent_id, kind: :request, tool: "pay",
    #                    req_id: 7, payload: "amount=100", now_ms: now)
    #
    # SECRETS: the private key stays inside the handle; #close wipes it.
    class Agent < NativeHandle
      # @return [Capability] the capability it signs with (:crypto_sign)
      attr_reader :capability

      # @param capability [Capability] needs :crypto_sign
      # @param name [String] 1..31 of [a-z0-9._-]
      # @param now_ms [Integer] the binding time (Unix ms)
      # @param seed [String, SecretKey, nil] 32 bytes for a reproducible key, or nil for a fresh one
      # @param node [Agent, nil] the node hosting this agent (it signs the
      #   binding; same context), or nil for a self-bound agent
      # @raise [CapabilityDeniedError] without :crypto_sign
      # @raise [ZxvUsageError] ZXV_E_ARG (bad name characters), ZXV_E_TOO_LONG
      def initialize(capability, name:, now_ms:, seed: nil, node: nil)
        @capability = capability
        raise ZxvUsageError.new(Status::E_ARG, "node must be a Zxv::Web4::Agent") unless node.nil? || node.is_a?(Agent)

        s = case seed
            when nil then nil
            when SecretKey
              raise ZxvUsageError.new(Status::E_ARG, "seed must be 32 bytes") unless seed.bytesize == 32

              seed.pointer
            else Call.mem(Call.bytes!(seed, "seed", size: 32))
            end
        out = Call.out(:pointer)
        rc = Native.lib.zxv_w4_agent_create(capability.ptr, s, Call.cstr!(name, "name"), node&.ptr, Call.uint64!(now_ms, "now_ms"), out)
        s.put_bytes(0, "\0".b * 32) if s && !seed.is_a?(SecretKey)
        Call.check(rc, capability.context)
        super(capability.context, out.read_pointer, :zxv_w4_agent_destroy)
      end

      # @return [String] SHA3-256 of the public key (32 bytes)
      def agent_id = facts[0]

      # @return [String] the hosting node's id (== agent_id when self-bound)
      def peer_id = facts[1]

      # @return [String] the ML-DSA-65 public key (PK_BYTES)
      def public_key = facts[2]

      # The agent's card: what a peer needs to verify it ({Web4.verify_card}).
      # @return [String]
      def card = Call.bytes_out(context, ptr, hint: CARD_MAX) { |*a| Native.lib.zxv_w4_agent_card(*a) }

      # Build, sign and encode an envelope from this agent. Sealing assigns
      # the next sequence number.
      # @param to [String] the recipient's 32-byte agent id
      # @param kind [Symbol, Integer] a key of KINDS
      # @param tool [String] a valid tool name
      # @param req_id [Integer] uint64
      # @param payload [String] at most MSG_MAX_PAYLOAD bytes
      # @param chunk [Integer] chunk index (STREAM)
      # @param final [Boolean] set MF_FINAL (last STREAM chunk / final response)
      # @param consent [String, nil] a consent token to attach
      # @param now_ms [Integer]
      # @return [String] the envelope bytes
      def seal(to:, kind:, tool:, req_id:, now_ms:, payload: "", chunk: 0, final: false, consent: nil)
        p = Call.bytes!(payload, "payload")
        tok = consent && Web4.token!(consent)
        Call.bytes_out(context, capability.ptr, ptr, Web4.flags(KINDS, kind, "kind"), Web4.id32(to, "to"),
                       Call.uint64!(req_id, "req_id"), Call.uint32!(chunk, "chunk"), Call.cstr!(tool, "tool"),
                       final ? MF_FINAL : 0, Call.mem0(p), p.bytesize, tok, Call.uint64!(now_ms, "now_ms"),
                       hint: ENV_MAX) { |*a| Native.lib.zxv_w4_env_seal(*a) }
      end

      # Seal a reply to a REQUEST envelope addressed to this agent: to/from
      # swapped, same request id and tool. The request's signature is not
      # checked here: verify it first ({Envelope#verify}).
      # @param request [String, Envelope]
      # @param kind [Symbol, Integer] :response, :stream or :error
      # @return [String] the envelope bytes
      # @raise [ZxvUsageError] ZXV_E_ARG (not a REQUEST, or not addressed to this agent)
      def reply(request, kind:, now_ms:, payload: "", chunk: 0, final: false)
        r = request.is_a?(Envelope) ? request.bytes : Call.bytes!(request, "request")
        p = Call.bytes!(payload, "payload")
        Call.bytes_out(context, capability.ptr, ptr, Call.mem0(r), r.bytesize, Web4.flags(KINDS, kind, "kind"),
                       Call.uint32!(chunk, "chunk"), final ? MF_FINAL : 0, Call.mem0(p), p.bytesize,
                       Call.uint64!(now_ms, "now_ms"), hint: ENV_MAX) { |*a| Native.lib.zxv_w4_env_reply(*a) }
      end

      # Issue a consent token on the HUMAN's side (this handle is the
      # human's key), after showing them the action: lets +agent_id+ do
      # +action+ within +scope+, money up to +max_vfv+ minor units, valid in
      # [issued_ms, expires_ms). Single-use through its nonce.
      # @param action [String] 32 bytes ({Web4.action_digest})
      # @param scope [Symbol, Array<Symbol>, Integer] CONSENT_SCOPES (nonzero)
      # @param nonce [String, nil] 16 bytes, or nil for a random one
      # @param capability [Capability] needs :attest (default: this handle's)
      # @return [String] the token (CONSENT_BYTES)
      # @raise [CapabilityDeniedError] without :attest
      def issue_consent(agent_id:, action:, scope:, max_vfv:, issued_ms:, expires_ms:, nonce: nil, capability: self.capability)
        n = nonce && Call.mem(Call.bytes!(nonce, "nonce", size: 16))
        out = FFI::MemoryPointer.new(:uint8, CONSENT_BYTES)
        Call.check(Native.lib.zxv_w4_consent_issue(capability.ptr, ptr, Web4.id32(agent_id, "agent_id"),
                                                   Web4.id32(action, "action"), Web4.flags(CONSENT_SCOPES, scope, "scope"),
                                                   Call.uint64!(max_vfv, "max_vfv"), Call.uint64!(issued_ms, "issued_ms"),
                                                   Call.uint64!(expires_ms, "expires_ms"), n, out), context)
        out.read_bytes(CONSENT_BYTES)
      end

      def inspect = closed? ? "#<Zxv::Web4::Agent closed>" : "#<Zxv::Web4::Agent #{agent_id.unpack1('H16')}...>"

      private

      def facts
        a = FFI::MemoryPointer.new(:uint8, 32)
        p = FFI::MemoryPointer.new(:uint8, 32)
        k = FFI::MemoryPointer.new(:uint8, PK_BYTES)
        Call.check(Native.lib.zxv_w4_agent_info(ptr, a, p, k), context)
        [a.read_bytes(32), p.read_bytes(32), k.read_bytes(PK_BYTES)]
      end
    end

    # A directory of verified peers: cards keyed by agent id. Pure; NOT
    # thread-safe.
    class Directory < PureHandle
      # @param capacity [Integer] 1..DIR_MAX
      # @raise [ZxvUsageError] ZXV_E_RANGE
      def initialize(capacity:)
        super()
        create_handle(:zxv_w4_dir_destroy) { |o| Native.lib.zxv_w4_dir_create(Call.uint32!(capacity, "capacity"), o) }
      end

      # Verify a card, then add it or replace the entry for the same agent.
      # @return [self]
      # @raise [VerificationFailedError] the signature fails
      # @raise [ZxvRefusedError] ZXV_E_STATE (the binding is older than the
      #   one held: no rollback), ZXV_E_CAPACITY
      def add(card)
        c = Call.bytes!(card, "card")
        Call.check(Native.lib.zxv_w4_dir_add(ptr, Call.mem0(c), c.bytesize))
        self
      end
      alias << add

      # @return [String, nil] the card held for +agent_id+, or nil
      def find(agent_id)
        id = Web4.id32(agent_id, "agent_id")
        Call.bytes_out(nil, ptr, id, hint: CARD_MAX) do |*a|
          rc = Native.lib.zxv_w4_dir_find(*a)
          return nil if rc == Status::E_NOT_FOUND

          rc
        end
      end
      alias [] find

      # @return [Integer] number of cards held
      def count
        out = Call.out(:uint32)
        Call.check(Native.lib.zxv_w4_dir_count(ptr, out))
        out.read_uint32
      end
      alias size count
    end

    # A tool manifest: up to MAX_TOOLS tools, each with a name, a price in
    # VFV minor units (2 digits), a rate limit and flags. Signing fixes the
    # agent and peer ids; adding a tool afterwards drops the signature.
    # Pure; NOT thread-safe.
    #
    #   m = Zxv::Web4::Manifest.new(version: 1, issued_ms: t0, expires_ms: t1)
    #   m.add_tool("pay", price_vfv: 150, rate_per_min: 60, burst: 10, flags: :money, max_input: 256)
    #   m.sign(alice)
    #   m.valid?(alice.public_key, now_ms: t0)   # => true
    class Manifest < PureHandle
      # The header: ids (32 zero bytes until signed), version, validity,
      # tool count and whether it is signed.
      Info = Struct.new(:agent_id, :peer_id, :version, :issued_ms, :expires_ms, :tool_count, :signed, keyword_init: true)
      # One tool. +flags+ is an Array of TOOL_FLAGS keys.
      Tool = Struct.new(:name, :price_vfv, :rate_per_min, :burst, :flags, :max_input, keyword_init: true)

      # Parse the wire form. The signature is kept but NOT checked: call #verify!.
      # @return [Manifest]
      # @raise [ZxvUsageError] ZXV_E_FORMAT (malformed, trailing bytes, an invalid tool)
      def self.decode(bytes)
        b = Call.bytes!(bytes, "bytes")
        from_out(:zxv_w4_manifest_destroy) { |o| Native.lib.zxv_w4_manifest_decode(Call.mem0(b), b.bytesize, o) }
      end

      # An empty, unsigned manifest valid in [issued_ms, expires_ms).
      # @raise [ZxvUsageError] ZXV_E_ARG (expires_ms <= issued_ms)
      def initialize(version:, issued_ms:, expires_ms:)
        super()
        create_handle(:zxv_w4_manifest_destroy) do |o|
          Native.lib.zxv_w4_manifest_create(Call.uint32!(version, "version"), Call.uint64!(issued_ms, "issued_ms"),
                                            Call.uint64!(expires_ms, "expires_ms"), o)
        end
      end

      # Add a tool.
      # @param name [String] 1..31 of [a-z0-9._-]
      # @param flags [Symbol, Array<Symbol>, Integer] TOOL_FLAGS
      # @param max_input [Integer] <= MSG_MAX_PAYLOAD
      # @return [self]
      # @raise [ZxvUsageError] ZXV_E_DUPLICATE (name listed)
      # @raise [ZxvRefusedError] ZXV_E_CAPACITY (16 tools)
      def add_tool(name, price_vfv:, rate_per_min:, burst:, flags: 0, max_input: 0)
        Call.check(Native.lib.zxv_w4_manifest_add_tool(ptr, Call.cstr!(name, "name"), Call.uint64!(price_vfv, "price_vfv"),
                                                       Call.uint32!(rate_per_min, "rate_per_min"), Call.uint32!(burst, "burst"),
                                                       Web4.flags(TOOL_FLAGS, flags, "flags"), Call.uint32!(max_input, "max_input")))
        self
      end

      # Sign with +agent+'s key.
      # @param capability [Capability] needs :crypto_sign, in the agent's context
      # @return [self]
      def sign(agent, capability: agent.capability)
        raise ZxvUsageError.new(Status::E_ARG, "agent must be a Zxv::Web4::Agent") unless agent.is_a?(Agent)

        Call.check(Native.lib.zxv_w4_manifest_sign(capability.ptr, agent.ptr, ptr), agent.context)
        self
      end

      # Check that it is signed by +public_key+, that key is the manifest's
      # agent, and issued_ms <= now_ms < expires_ms.
      # @return [self]
      # @raise [VerificationFailedError] wrong key, or a bad or missing signature
      # @raise [ZxvRefusedError] ZXV_E_STATE (valid signature, not valid at now_ms)
      def verify!(public_key, now_ms:)
        pk = Call.bytes!(public_key, "public_key", size: PK_BYTES)
        Call.check(Native.lib.zxv_w4_manifest_verify(ptr, Call.mem(pk), Call.uint64!(now_ms, "now_ms")))
        self
      end

      # @return [Boolean] whether #verify! would pass
      def valid?(public_key, now_ms:)
        verify!(public_key, now_ms: now_ms)
        true
      rescue VerificationFailedError
        false
      rescue ZxvRefusedError => e
        raise unless e.status == Status::E_STATE

        false
      end

      # The signed wire form.
      # @return [String]
      # @raise [ZxvRefusedError] ZXV_E_STATE (not signed)
      def encode = Call.bytes_out(nil, ptr, hint: MANIFEST_MAX) { |*a| Native.lib.zxv_w4_manifest_encode(*a) }

      # The Web 2 discovery face (e.g. /.well-known/web4-agent.json). Prices
      # stay integers in minor units.
      # @return [String] JSON text
      def to_json(*) = Call.text_out(ptr) { |*a| Native.lib.zxv_w4_manifest_to_json(*a) }

      # @return [Info]
      def info
        a = FFI::MemoryPointer.new(:uint8, 32)
        p = FFI::MemoryPointer.new(:uint8, 32)
        v = Call.out(:uint32)
        i = Call.out(:uint64)
        e = Call.out(:uint64)
        n = Call.out(:uint32)
        s = Call.out(:int32)
        Call.check(Native.lib.zxv_w4_manifest_info(ptr, a, p, v, i, e, n, s))
        Info.new(agent_id: a.read_bytes(32), peer_id: p.read_bytes(32), version: v.read_uint32, issued_ms: i.read_uint64,
                 expires_ms: e.read_uint64, tool_count: n.read_uint32, signed: s.read_int32 != 0)
      end

      # @return [Boolean]
      def signed? = info.signed

      # @return [Tool] tool +index+ (0-based)
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND past the last tool
      def tool(index)
        name = FFI::MemoryPointer.new(:char, 32)
        price = Call.out(:uint64)
        rate, burst, flags, max = Array.new(4) { Call.out(:uint32) }
        Call.check(Native.lib.zxv_w4_manifest_tool(ptr, Call.uint32!(index, "index"), name, price, rate, burst, flags, max))
        Tool.new(name: name.read_string.force_encoding(Encoding::UTF_8), price_vfv: price.read_uint64,
                 rate_per_min: rate.read_uint32, burst: burst.read_uint32,
                 flags: Web4.flag_names(TOOL_FLAGS, flags.read_uint32), max_input: max.read_uint32)
      end

      # @return [Array<Tool>]
      def tools = Array.new(info.tool_count) { |i| tool(i) }

      # @return [Integer, nil] the index of tool +name+, or nil if not listed
      def find(name)
        out = Call.out(:uint32)
        rc = Native.lib.zxv_w4_manifest_find(ptr, Call.cstr!(name, "name"), out)
        return nil if rc == Status::E_NOT_FOUND

        Call.check(rc)
        out.read_uint32
      end
    end

    # A single-use log of consumed and revoked consent tokens. Pure; NOT
    # thread-safe.
    class ConsentLog < PureHandle
      # @param capacity [Integer] 1..CONSENT_LOG_MAX
      def initialize(capacity:)
        super()
        create_handle(:zxv_w4_consent_log_destroy) do |o|
          Native.lib.zxv_w4_consent_log_create(Call.uint32!(capacity, "capacity"), o)
        end
      end

      # Check that +token+ lets +agent_id+ do +action+ with scope +need+ for
      # +amount+ VFV minor units, signed by the human whose key is
      # +human_public_key+, valid at +now_ms+; then record it as used.
      # Returning means consent was given AND is now spent.
      # @param token [String, nil] nil means "no token" (always refused)
      # @param need [Symbol, Array<Symbol>, Integer] CONSENT_SCOPES (nonzero)
      # @return [self]
      # @raise [ConsentRequiredError] no token, a mismatch (human, agent,
      #   action, scope, amount), expired or not yet valid, a bad signature,
      #   or a token already used or revoked; the reason is in #detail
      # @raise [ZxvRefusedError] ZXV_E_CAPACITY (the log is full even after
      #   pruning: fail closed, nothing is allowed)
      def check!(token, human_public_key:, agent_id:, action:, need:, amount:, now_ms:)
        t = token.nil? ? nil : Web4.token!(token)
        pk = Call.bytes!(human_public_key, "human_public_key", size: PK_BYTES)
        Call.check(Native.lib.zxv_w4_consent_check(ptr, t, Call.mem(pk), Web4.id32(agent_id, "agent_id"),
                                                   Web4.id32(action, "action"), Web4.flags(CONSENT_SCOPES, need, "need"),
                                                   Call.uint64!(amount, "amount"), Call.uint64!(now_ms, "now_ms")))
        self
      end

      # Revoke a token before use: later checks of it fail. Needs no
      # capability (it only takes authority away). Revoking twice is fine.
      # @return [self]
      def revoke(token, now_ms:)
        Call.check(Native.lib.zxv_w4_consent_revoke(ptr, Web4.token!(token), Call.uint64!(now_ms, "now_ms")))
        self
      end

      # Drop entries whose token expired at or before +now_ms+.
      # @return [Integer] entries left
      def prune(now_ms)
        out = Call.out(:uint32)
        Call.check(Native.lib.zxv_w4_consent_log_prune(ptr, Call.uint64!(now_ms, "now_ms"), out))
        out.read_uint32
      end
    end

    # A decoded envelope, a frozen value. Decoding does not check the
    # signature: call #verify with the sender's public key. Replay,
    # timestamp and rate checks are the receiver's job.
    class Envelope
      # @return [String] the encoded bytes (frozen)
      attr_reader :bytes
      # @return [Symbol] a key of KINDS (or the raw Integer)
      attr_reader :kind
      # @return [Integer] MF_* bits
      attr_reader :flags
      # @return [String] sender agent id (32 bytes)
      attr_reader :from
      # @return [String] recipient agent id (32 bytes)
      attr_reader :to
      # @return [Integer] the sender's sequence number
      attr_reader :seq
      # @return [Integer] the timestamp (Unix ms)
      attr_reader :ts_ms
      # @return [Integer] request id
      attr_reader :req_id
      # @return [Integer] chunk index
      attr_reader :chunk
      # @return [String] tool name
      attr_reader :tool
      # @return [String] the payload
      attr_reader :payload
      # @return [String, nil] the attached consent token, or nil
      attr_reader :consent

      # @param bytes [String]
      # @raise [ZxvUsageError] ZXV_E_FORMAT (malformed)
      def initialize(bytes)
        b = Call.bytes!(bytes, "envelope")
        m = Call.mem(b)
        kind = Call.out(:int32)
        flags = Call.out(:uint32)
        from = FFI::MemoryPointer.new(:uint8, 32)
        to = FFI::MemoryPointer.new(:uint8, 32)
        seq, ts, req = Array.new(3) { Call.out(:uint64) }
        chunk = Call.out(:uint32)
        tool = FFI::MemoryPointer.new(:char, 32)
        plen = Call.out(:uint32)
        Call.check(Native.lib.zxv_w4_env_decode(m, b.bytesize, kind, flags, from, to, seq, ts, req, chunk, tool, plen))
        @bytes = b.freeze
        k = kind.read_int32
        @kind = KINDS.key(k) || k
        @flags = flags.read_uint32
        @from = from.read_bytes(32).freeze
        @to = to.read_bytes(32).freeze
        @seq = seq.read_uint64
        @ts_ms = ts.read_uint64
        @req_id = req.read_uint64
        @chunk = chunk.read_uint32
        @tool = tool.read_string.force_encoding(Encoding::UTF_8).freeze
        @payload = Call.bytes_out(nil, m, b.bytesize, hint: plen.read_uint32) { |*a| Native.lib.zxv_w4_env_payload(*a) }.freeze
        tok = FFI::MemoryPointer.new(:uint8, CONSENT_BYTES)
        rc = Native.lib.zxv_w4_env_consent(m, b.bytesize, tok)
        Call.check(rc) unless rc == Status::E_NOT_FOUND
        @consent = rc == Status::OK ? tok.read_bytes(CONSENT_BYTES).freeze : nil
        freeze
      end

      # @return [Boolean] whether MF_FINAL is set
      def final? = flags.anybits?(MF_FINAL)

      # Whether +sender_public_key+ is the sender's (from == its SHA3-256)
      # and the signature verifies.
      # @return [Boolean]
      def verify(sender_public_key)
        pk = Call.bytes!(sender_public_key, "sender_public_key", size: PK_BYTES)
        rc = Native.lib.zxv_w4_env_verify(Call.mem(bytes), bytes.bytesize, Call.mem(pk))
        return true if rc == Status::OK
        return false if rc == Status::E_VERIFY

        Call.check(rc)
      end

      # Like #verify, but raises VerificationFailedError.
      # @return [self]
      def verify!(sender_public_key)
        verify(sender_public_key) or raise VerificationFailedError.new(Status::E_VERIFY, "envelope signature did not verify")
        self
      end

      def ==(other) = other.is_a?(Envelope) && other.bytes == bytes
      alias eql? ==
      def hash = [Envelope, bytes].hash
      def inspect = "#<Zxv::Web4::Envelope #{kind} tool=#{tool} seq=#{seq} req=#{req_id}>"
    end
  end
end
