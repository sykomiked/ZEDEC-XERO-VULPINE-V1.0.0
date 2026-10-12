# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: AIPI, content-addressed functions (zxv_aipi_*).
  #
  # AIPI names data and functions by what they are. There are four
  # identifiers, all CIDv1 with a full sha2-256 ({Cid}):
  #
  # data:: codec raw (or dag-pb): blobs, Z-Wasm modules, contracts;
  # function:: codec {MC_FN} + intent, the hash of a {FunctionRecord};
  # call:: codec {MC_CALL}, the hash of a {CallRecord} (function + input);
  # claim:: not a CID: an executor's answer ({ClaimRecord}), which the
  #         executor signs (signing and checking that signature are the
  #         caller's job, e.g. with {MlDsa87}).
  #
  # The codes 0x3A10xx are multicodec PRIVATE-USE values, not registered.
  # Records are decoded strictly (exact lengths, no trailing bytes, a
  # function record must be canonical), so one function has exactly one CID.
  #
  # {Node} is an OFFLINE, in-process store (blocks and pins) for one
  # context.
  #
  # Honest limits:
  # * No network transport: no Vinea mesh, no IPFS peers, no HTTP gateway,
  #   no libp2p. A node finds only what was put into it.
  # * No EXECUTE: running a function needs the Z-Wasm sandbox
  #   (sdk/kits/aipi), which is not part of libzxv. {Node#plan} reports the
  #   route a call would take here; it runs nothing.
  # * No money moves in this group (bounties are escrowed by host hooks
  #   that are not exposed).
  module Aipi
    # Multicodec of a plain function (intent :fn).
    MC_FN = 0x3A1000
    # Multicodec of a tensor-inference function (intent :tensor_infer).
    MC_TENSOR_INFER = 0x3A1001
    # Multicodec of a ledger-settlement function (intent :settle_ledger).
    MC_SETTLE_LEDGER = 0x3A1002
    # Multicodec of a swarm-routing function (intent :swarm_route).
    MC_SWARM_ROUTE = 0x3A1003
    # Multicodec of a call CID.
    MC_CALL = 0x3A10C0

    # What a function is for; it picks the function CID's codec.
    INTENTS = { fn: 0, tensor_infer: 1, settle_ledger: 2, swarm_route: 3 }.freeze
    # How a remote result is accepted: :checker (a checker export judges
    # input and output), :cert (as :checker, with a certificate), :quorum
    # (k of n executors agree byte for byte), :local (never accept a remote
    # result).
    VERIFY_MODES = { checker: 1, cert: 2, quorum: 3, local: 4 }.freeze
    # :local functions are private or key-bearing and never leave the node.
    PRIVACY = { public: 0, local: 1 }.freeze
    # Whether a function always gives the same output for the same input.
    DETERMINISM = { nondeterministic: 0, deterministic: 1 }.freeze
    # The routes Node#plan can report. This library is offline, so it only
    # reports :local or :unavailable; the others are a networked host's.
    ROUTES = { 1 => :cache, 2 => :local, 3 => :fetch_local, 4 => :mesh, 5 => :unavailable }.freeze

    # Longest export name ([A-Za-z0-9_]{1,63}).
    NAME_MAX = 63
    # Longest call input or claim output, in bytes.
    IO_MAX = 1024
    # Longest claim certificate, in bytes.
    CERT_MAX = 1024
    # Largest quorum n.
    QUORUM_MAX = 8
    # Longest function record.
    FN_MAX = 373
    # Longest call record.
    CALL_MAX = 1112
    # Longest claim record.
    CLAIM_MAX = 2170
    # Largest blob one node stores (2 MiB).
    BLOCK_MAX = 2_097_152

    module_function

    # The call CID of (function CID, input): codec {MC_CALL}, sha2-256 of
    # the call record.
    # @param fn_cid [Cid] a function CID
    # @param input [String] at most {IO_MAX} bytes
    # @return [Cid]
    # @raise [ZxvRefusedError] ZXV_E_CID if +fn_cid+ is not a function CID
    def call_cid(fn_cid, input = "")
      f = Cid.coerce(fn_cid, "fn_cid")
      i = Call.bytes!(input, "input")
      Cid.from_out { |b, l| Native.lib.zxv_aipi_call_cid(Call.mem(f.bytes), f.bytesize, Call.mem0(i), i.bytesize, b, l) }
    end

    # @api private
    def enum!(map, value, name)
      return map.fetch(value) { raise ZxvUsageError.new(Status::E_ARG, "unknown #{name} #{value.inspect}") } if value.is_a?(Symbol)

      Call.uint32!(value, name)
    end

    # @api private
    def sym(map, value) = map.key(value) || value

    # A canonical function record: what a function is, how its result is
    # accepted, and which module runs it. A frozen value; #cid is its
    # function CID.
    class FunctionRecord
      # @return [String] the record bytes (frozen)
      attr_reader :bytes
      # @return [Symbol] a key of INTENTS
      attr_reader :intent
      # @return [Symbol] a key of VERIFY_MODES
      attr_reader :verify
      # @return [Symbol] :public or :local
      attr_reader :privacy
      # @return [Symbol] :deterministic or :nondeterministic
      attr_reader :determinism
      # @return [Integer] quorum n (0 unless :quorum)
      attr_reader :quorum_n
      # @return [Integer] quorum k (0 unless :quorum)
      attr_reader :quorum_k
      # @return [Integer] the most fuel one run may use
      attr_reader :fuel_max

      # Encode a function record. Every field is checked; none is masked.
      # @param intent [Symbol, Integer] a key of INTENTS
      # @param verify [Symbol, Integer] a key of VERIFY_MODES
      # @param privacy [Symbol, Integer] :public or :local
      # @param determinism [Symbol, Integer] :deterministic or :nondeterministic
      # @param quorum_n [Integer] :quorum only: 2..8 (and :deterministic); 0 otherwise
      # @param quorum_k [Integer] :quorum only: n/2 < k <= n; 0 otherwise
      # @param fuel_max [Integer] > 0, the most fuel one run may use (uint64)
      # @param module_cid [Cid] CIDv1 raw sha2-256 of the Z-Wasm module
      # @param contract_cid [Cid, nil] CIDv1 raw sha2-256 of the z-contract text, or nil
      # @param entry [String] the export computing the result
      # @param checker [String, nil] :checker/:cert only (differs from entry)
      # @param certifier [String, nil] :cert only (differs from entry and checker)
      # @return [FunctionRecord]
      # @raise [ZxvUsageError] ZXV_E_ARG for a field out of range or breaking
      #   a rule, ZXV_E_TOO_LONG for a name over 63
      # @raise [ZxvRefusedError] ZXV_E_CID for a CID that is not CIDv1 raw sha2-256
      def self.encode(intent:, verify:, determinism:, fuel_max:, module_cid:, entry:, privacy: :public, quorum_n: 0,
                      quorum_k: 0, contract_cid: nil, checker: nil, certifier: nil)
        m = Cid.coerce(module_cid, "module_cid")
        c = contract_cid && Cid.coerce(contract_cid, "contract_cid")
        names = [entry, checker, certifier].each_with_index.map do |n, i|
          n.nil? ? nil : FFI::MemoryPointer.from_string(Call.cstr!(n, %w[entry checker certifier][i]))
        end
        args = [Aipi.enum!(INTENTS, intent, "intent"), Aipi.enum!(VERIFY_MODES, verify, "verify"),
                Aipi.enum!(PRIVACY, privacy, "privacy"), Aipi.enum!(DETERMINISM, determinism, "determinism"),
                Call.uint32!(quorum_n, "quorum_n"), Call.uint32!(quorum_k, "quorum_k"), Call.uint64!(fuel_max, "fuel_max"),
                Call.mem(m.bytes), m.bytesize, c && Call.mem(c.bytes), c ? c.bytesize : 0, *names]
        new(Call.bytes_out(nil, *args, hint: FN_MAX) { |*a| Native.lib.zxv_aipi_fn_encode(*a) })
      end

      # Decode (strictly) a function record.
      # @param bytes [String]
      # @raise [ZxvUsageError] ZXV_E_FORMAT if not a valid, canonical record
      def initialize(bytes)
        b = Call.bytes!(bytes, "record")
        @mem = Call.mem(b)
        o = Array.new(6) { Call.out(:uint32) }
        fuel = Call.out(:uint64)
        Call.check(Native.lib.zxv_aipi_fn_decode(@mem, b.bytesize, *o, fuel))
        @bytes = b.freeze
        @intent = Aipi.sym(INTENTS, o[0].read_uint32)
        @verify = Aipi.sym(VERIFY_MODES, o[1].read_uint32)
        @privacy = Aipi.sym(PRIVACY, o[2].read_uint32)
        @determinism = Aipi.sym(DETERMINISM, o[3].read_uint32)
        @quorum_n = o[4].read_uint32
        @quorum_k = o[5].read_uint32
        @fuel_max = fuel.read_uint64
        @module_cid = cid_field(0)
        @contract_cid = cid_field(1)
        @entry, @checker, @certifier = (0..2).map { |f| Call.text_out(@mem, b.bytesize, f) { |*a| Native.lib.zxv_aipi_fn_get_name(*a) } }
        @mem = nil
        freeze
      end

      # @return [Cid] the Z-Wasm module's CID
      attr_reader :module_cid
      # @return [Cid, nil] the z-contract's CID, or nil
      attr_reader :contract_cid
      # @return [String] the entry export
      attr_reader :entry

      # @return [String, nil] the checker export (nil when the mode has none)
      def checker = @checker.empty? ? nil : @checker

      # @return [String, nil] the certifier export (nil unless :cert)
      def certifier = @certifier.empty? ? nil : @certifier

      # The function CID: codec MC_FN + intent, sha2-256 of the record.
      # @return [Cid]
      def cid = Cid.from_out { |b, l| Native.lib.zxv_aipi_fn_cid(Call.mem(bytes), bytes.bytesize, b, l) }

      def ==(other) = other.is_a?(FunctionRecord) && other.bytes == bytes
      alias eql? ==
      def hash = [FunctionRecord, bytes].hash
      def inspect = "#<Zxv::Aipi::FunctionRecord #{intent} #{verify} entry=#{entry}>"

      private

      def cid_field(field)
        buf = FFI::MemoryPointer.new(:uint8, Cid::MAX)
        len = Call.out(:size_t)
        rc = Native.lib.zxv_aipi_fn_get_cid(@mem, @bytes.bytesize, field, buf, len)
        return nil if rc == Status::E_NOT_FOUND

        Call.check(rc)
        Cid.new(buf.read_bytes(len.read(:size_t)))
      end
    end

    # The call record naming one invocation: a function CID and its input.
    # A frozen value.
    class CallRecord
      # @return [String] the record bytes (frozen)
      attr_reader :bytes
      # @return [Cid] the function called
      attr_reader :fn_cid
      # @return [String] the input bytes
      attr_reader :input

      # @param fn_cid [Cid] a function CID
      # @param input [String] at most {IO_MAX} bytes
      # @return [CallRecord]
      # @raise [ZxvRefusedError] ZXV_E_CID (not a function CID)
      # @raise [ZxvUsageError] ZXV_E_TOO_LONG (input)
      def self.encode(fn_cid, input = "")
        f = Cid.coerce(fn_cid, "fn_cid")
        i = Call.bytes!(input, "input")
        new(Call.bytes_out(nil, Call.mem(f.bytes), f.bytesize, Call.mem0(i), i.bytesize,
                           hint: CALL_MAX) { |*a| Native.lib.zxv_aipi_call_encode(*a) })
      end

      # Decode (strictly) a call record.
      # @raise [ZxvUsageError] ZXV_E_FORMAT
      def initialize(bytes)
        b = Call.bytes!(bytes, "record")
        o = Array.new(4) { Call.out(:size_t) }
        Call.check(Native.lib.zxv_aipi_call_decode(Call.mem(b), b.bytesize, *o))
        fo, fl, io, il = o.map { |p| p.read(:size_t) }
        @bytes = b.freeze
        @fn_cid = Cid.new(b.byteslice(fo, fl))
        @input = b.byteslice(io, il).freeze
        freeze
      end

      # @return [Cid] the call CID
      def cid = Aipi.call_cid(fn_cid, input)

      def ==(other) = other.is_a?(CallRecord) && other.bytes == bytes
      alias eql? ==
      def hash = [CallRecord, bytes].hash
    end

    # The claim record an executor signs: its +output+ (and, for :cert
    # functions, +cert+) for the call +call_cid+, with its 32-byte node id.
    # A frozen value.
    class ClaimRecord
      # @return [String] the record bytes (frozen)
      attr_reader :bytes
      # @return [Cid] the call answered
      attr_reader :call_cid
      # @return [String] the executor's 32-byte node id
      attr_reader :executor
      # @return [String] the claimed output
      attr_reader :output
      # @return [String] the certificate ("" if none)
      attr_reader :cert

      # @param call_cid [Cid] a call CID
      # @param executor [String] 32 bytes
      # @param output [String] at most {IO_MAX} bytes
      # @param cert [String] at most {CERT_MAX} bytes
      # @return [ClaimRecord]
      def self.encode(call_cid:, executor:, output:, cert: "")
        c = Cid.coerce(call_cid, "call_cid")
        e = Call.bytes!(executor, "executor", size: 32)
        o = Call.bytes!(output, "output")
        z = Call.bytes!(cert, "cert")
        new(Call.bytes_out(nil, Call.mem(c.bytes), c.bytesize, Call.mem(e), Call.mem0(o), o.bytesize, Call.mem0(z), z.bytesize,
                           hint: CLAIM_MAX) { |*a| Native.lib.zxv_aipi_claim_encode(*a) })
      end

      # Decode (strictly) a claim record.
      # @raise [ZxvUsageError] ZXV_E_FORMAT
      def initialize(bytes)
        b = Call.bytes!(bytes, "record")
        o = Array.new(7) { Call.out(:size_t) }
        Call.check(Native.lib.zxv_aipi_claim_decode(Call.mem(b), b.bytesize, *o))
        co, cl, eo, oo, ol, zo, zl = o.map { |p| p.read(:size_t) }
        @bytes = b.freeze
        @call_cid = Cid.new(b.byteslice(co, cl))
        @executor = b.byteslice(eo, 32).freeze
        @output = b.byteslice(oo, ol).freeze
        @cert = b.byteslice(zo, zl).freeze
        freeze
      end

      def ==(other) = other.is_a?(ClaimRecord) && other.bytes == bytes
      alias eql? ==
      def hash = [ClaimRecord, bytes].hash
    end

    # An offline, in-memory IPFS node (block store and pins) with the AIPI
    # context over it. It belongs to the capability's context and fails
    # closed with it (ZxvFatal). Storing (#put, #publish) needs :aipi;
    # reads take only the node.
    #
    # Thread-safe: the node is internally serialized.
    #
    #   node = Zxv::Aipi::Node.new(cap)             # cap needs :aipi
    #   mod = node.put(wasm_bytes)
    #   rec = Zxv::Aipi::FunctionRecord.encode(intent: :fn, verify: :local,
    #           determinism: :deterministic, fuel_max: 1_000, module_cid: mod, entry: "main")
    #   fn = node.publish(rec)
    #   node.plan(fn, "input")                      # => :local
    class Node < NativeHandle
      # Smallest store size in bytes.
      STORE_MIN = 4096
      # Largest store size in bytes (256 MiB).
      STORE_MAX = 268_435_456
      # Most blocks one node indexes.
      BLOCKS_MAX = 65_536

      # Audit counters: blocks indexed, store bytes used, verified GETs and
      # forbidden GETs (bytes that failed their hash and were dropped).
      Info = Struct.new(:blocks, :store_used, :gets_verified, :forbidden, keyword_init: true)

      # @return [Capability]
      attr_reader :capability

      # @param capability [Capability] needs :aipi
      # @param store_bytes [Integer] STORE_MIN..STORE_MAX (each block costs
      #   its length plus about 100 bytes)
      # @param max_blocks [Integer] 1..BLOCKS_MAX
      # @param node_key [SecretKey, String, nil] 32 bytes (copied, and wiped
      #   on close) that seal private blocks with ChaCha20-Poly1305; without
      #   it, private puts raise ZXV_E_STATE
      # @raise [CapabilityDeniedError] without :aipi
      def initialize(capability, store_bytes: 1 << 20, max_blocks: 1024, node_key: nil)
        @capability = capability
        key = case node_key
              when nil then nil
              when SecretKey
                raise ZxvUsageError.new(Status::E_ARG, "node_key must be 32 bytes") unless node_key.bytesize == 32

                node_key.pointer
              else Call.mem(Call.bytes!(node_key, "node_key", size: 32))
              end
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_aipi_create(capability.ptr, Call.uint64!(store_bytes, "store_bytes"),
                                              Call.uint32!(max_blocks, "max_blocks"), key, out), capability.context)
        key.put_bytes(0, "\0".b * 32) if key && !node_key.is_a?(SecretKey)
        super(capability.context, out.read_pointer, :zxv_aipi_destroy)
      end

      # Store and pin +data+ as one raw block. Putting the same bytes again
      # is a no-op; the same bytes public after private (or the reverse) is
      # refused (ZXV_E_STATE): making something public is a deliberate act.
      # @param data [String] at most {BLOCK_MAX} bytes
      # @param private [Boolean] store sealed, never announceable (needs a node key)
      # @return [Cid] the block's CID (raw codec)
      # @raise [ZxvRefusedError] ZXV_E_STATE, ZXV_E_CAPACITY
      def put(data, private: false)
        d = Call.bytes!(data, "data")
        flag = Call.bool!(private, "private")
        Cid.from_out { |b, l| check(Native.lib.zxv_aipi_put(capability.ptr, ptr, Call.mem0(d), d.bytesize, flag, b, l)) }
      end

      # The bytes +cid+ names, re-hashed before they are returned.
      # @return [String, nil] the bytes, or nil when not held here (there is no network)
      # @raise [ZxvRefusedError] ZXV_E_CID (stored bytes no longer match;
      #   they are dropped), ZXV_E_STATE (a private block and no node key)
      def get(cid)
        c = Cid.coerce(cid)
        Call.bytes_out(context, ptr, Call.mem(c.bytes), c.bytesize, hint: 4096) do |*a|
          rc = Native.lib.zxv_aipi_get(*a)
          return nil if rc == Status::E_NOT_FOUND

          rc
        end
      end

      # Store a function record (sealed when its privacy is :local) and
      # return its function CID. The module and contract need not be held yet.
      # @param record [FunctionRecord]
      # @return [Cid]
      def publish(record)
        raise ZxvUsageError.new(Status::E_ARG, "record must be a Zxv::Aipi::FunctionRecord") unless record.is_a?(FunctionRecord)

        r = record.bytes
        Cid.from_out { |b, l| check(Native.lib.zxv_aipi_publish_fn(capability.ptr, ptr, Call.mem(r), r.bytesize, b, l)) }
      end

      # GET a function record by function CID, verified by hash, decoded
      # strictly and its codec checked against its intent.
      # @return [FunctionRecord, nil] nil when not held here
      # @raise [ZxvRefusedError] ZXV_E_CID (not a function CID, codec/intent mismatch)
      # @raise [ZxvUsageError] ZXV_E_FORMAT (the CID names bytes that are not a function record)
      def load_function(fn_cid)
        c = Cid.coerce(fn_cid, "fn_cid")
        b = Call.bytes_out(context, ptr, Call.mem(c.bytes), c.bytesize, hint: FN_MAX) do |*a|
          rc = Native.lib.zxv_aipi_load_fn(*a)
          return nil if rc == Status::E_NOT_FOUND

          rc
        end
        FunctionRecord.new(b)
      end

      # The route a call of +fn_cid+ on +input+ would take now. Pure: it runs
      # and changes nothing. In this offline library: :local when the module
      # (and contract) are held here, else :unavailable.
      # @return [Symbol] a value of ROUTES
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND when the function record is not held
      def plan(fn_cid, input = "", input_private: false)
        c = Cid.coerce(fn_cid, "fn_cid")
        i = Call.bytes!(input, "input")
        out = Call.out(:int32)
        check(Native.lib.zxv_aipi_plan(ptr, Call.mem(c.bytes), c.bytesize, Call.mem0(i), i.bytesize,
                                       Call.bool!(input_private, "input_private"), out))
        r = out.read_int32
        ROUTES.fetch(r, r)
      end

      # @return [Info] the audit counters
      def info
        b = Call.out(:uint32)
        u = Call.out(:uint64)
        g = Call.out(:uint32)
        f = Call.out(:uint32)
        check(Native.lib.zxv_aipi_info(ptr, b, u, g, f))
        Info.new(blocks: b.read_uint32, store_used: u.read_uint64, gets_verified: g.read_uint32, forbidden: f.read_uint32)
      end

      def inspect = closed? ? "#<Zxv::Aipi::Node closed>" : "#<Zxv::Aipi::Node blocks=#{info.blocks}>"

      private

      def check(rc) = Call.check(rc, context)
    end
  end
end
