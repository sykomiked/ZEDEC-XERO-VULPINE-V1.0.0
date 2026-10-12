# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.4: hardware emulation and hardware updates (zxv_hw_*).
  #
  # * {HwProfile}: ZHP-1 machine profiles, the "hardware ROM" (plain data,
  #   no capability).
  # * {HwMatrix}: functions served by interchangeable, proven
  #   implementations; every matrix starts with the standard engines
  #   ({FN_SHA256} ...), so every call works on any device. Needs
  #   :hw_matrix (and :hw_reconf for contract packages).
  # * {Hw.keygen} / {Hw.build_image}: dual-signed (ML-DSA-87 AND
  #   SLH-DSA-SHAKE-256s) release keys and packages. Need :crypto_sign.
  #   SLH-DSA signing takes seconds: a release step.
  # * {HwRegion}: hardware updates as signed images, staged, tested,
  #   committed or rolled back, every step logged. Needs :hw_reconf.
  #
  # Everything is an Integer or a binary String; no Float is accepted.
  module Hw
    # Implementation kinds, in the order the matrix prefers them at equal cost.
    KIND_HARD = 0
    KIND_FABRIC = 1
    KIND_MICRO = 2
    KIND_PEER = 3
    KIND_SOFT = 4
    KIND_REMOTE = 5
    KINDS = %i[hard fabric micro peer soft remote].freeze

    # Resource states (HwMatrix#set_resource).
    RES_OK = 0
    RES_FAULTED = 1
    RES_RECONFIGURING = 2
    RES_OFFLINE = 3
    RESOURCE_STATES = %i[ok faulted reconfiguring offline].freeze
    # The resource every matrix starts with: this machine's CPU.
    RES_HOST = 0xFFFFFFF0

    # The standard engines (contracts every matrix starts with).
    FN_SHA256 = 0x5A450001
    FN_KECCAK = 0x5A450002
    FN_MAC = 0x5A450003
    FN_GCD = 0x5A450004
    FN_LOGIC64 = 0x5A450005
    FN_TENSOR = 0x5A450006
    STANDARD_ENGINES = {
      sha256: FN_SHA256, keccak: FN_KECCAK, mac: FN_MAC, gcd: FN_GCD, logic64: FN_LOGIC64, tensor: FN_TENSOR
    }.freeze

    # Placement policy flags.
    POLICY_KEY_BEARING = 0x1
    POLICY_LOCAL_ONLY = 0x2
    POLICIES = { key_bearing: POLICY_KEY_BEARING, local_only: POLICY_LOCAL_ONLY }.freeze

    # Implementation status flags (HwMatrix#provider_info).
    PF_FAULT = 0x01
    PF_DISPUTED = 0x02
    PF_FAILED = 0x04
    PF_UNPROVEN = 0x08
    PF_DISABLED = 0x10
    PF_POLICY = 0x20
    PF_SERVING = 0x40
    PROVIDER_FLAGS = {
      fault: PF_FAULT, disputed: PF_DISPUTED, failed: PF_FAILED, unproven: PF_UNPROVEN,
      disabled: PF_DISABLED, policy: PF_POLICY, serving: PF_SERVING
    }.freeze

    # Coarse power classes.
    POWER_UNKNOWN = 0
    POWER_10MW = 1
    POWER_100MW = 2
    POWER_1W = 3
    POWER_10W = 4
    POWER_100W = 5
    POWER_MORE = 6

    # Image and package kinds.
    IMG_BITSTREAM = 1
    IMG_PARTIAL = 2
    IMG_MICROCODE = 3
    IMG_FIRMWARE = 4
    IMG_VECTORS = 5
    IMAGE_KINDS = {
      bitstream: IMG_BITSTREAM, partial: IMG_PARTIAL, microcode: IMG_MICROCODE, firmware: IMG_FIRMWARE,
      vectors: IMG_VECTORS
    }.freeze

    # Key and package sizes.
    PK_BYTES = 2656
    MLDSA_SK_BYTES = 4896
    SLHDSA_SK_BYTES = 128
    SIG_BYTES = 34_419
    HEADER_BYTES = 96

    # Region states.
    REGION_EMPTY = 0
    REGION_ACTIVE = 1
    REGION_STAGED = 2
    REGION_PROBATION = 3
    REGION_HALTED = 4
    REGION_STATES = %i[empty active staged probation halted].freeze

    # Events and audit records.
    EV_STAGED = 1
    EV_TESTED = 2
    EV_ACTIVATED = 3
    EV_ROLLED_BACK = 4
    EV_REJECTED = 5
    EV_QUARANTINED = 6
    EV_CONTRACT = 7
    EVENTS = {
      EV_STAGED => :staged, EV_TESTED => :tested, EV_ACTIVATED => :activated, EV_ROLLED_BACK => :rolled_back,
      EV_REJECTED => :rejected, EV_QUARANTINED => :quarantined, EV_CONTRACT => :contract
    }.freeze

    # Component classes (ZHP-1). Classes from CLASS_PRIVATE up are an implementer's own.
    CLASS_MACHINE = 1
    CLASS_CPU_CLUSTER = 2
    CLASS_CPU_CORE = 3
    CLASS_CACHE = 4
    CLASS_MEMORY = 5
    CLASS_INTERCONNECT = 6
    CLASS_MATRIX_UNIT = 7
    CLASS_VECTOR_UNIT = 8
    CLASS_TERNARY_UNIT = 9
    CLASS_HASH_UNIT = 10
    CLASS_CRYPTO_UNIT = 11
    CLASS_CODEC_UNIT = 12
    CLASS_RECONF_FABRIC = 13
    CLASS_STORAGE = 14
    CLASS_NETWORK = 15
    CLASS_RADIO = 16
    CLASS_SENSOR = 17
    CLASS_ACTUATOR = 18
    CLASS_CLOCK = 19
    CLASS_POWER = 20
    CLASS_SECURE_ELEMENT = 21
    CLASS_DISPLAY = 22
    CLASS_AUDIO = 23
    CLASS_WIRE = 24
    CLASS_GPU = 25
    CLASS_PACKAGE = 26
    CLASS_LANE = 27
    CLASS_LOGIC_UNIT = 28
    CLASS_NTT_UNIT = 29
    CLASS_FRAME_UNIT = 30
    CLASS_GCD_UNIT = 31
    CLASS_MEM_CHANNEL = 32
    CLASS_SECURE_ISLAND = 33
    CLASS_IO = 34
    CLASS_PRIVATE = 0x80000000

    # Capability keys (bit 15 critical, bits 14-13 comparison, bits 12-0 id).
    CAP_WORD_BITS = 0x0001
    CAP_COUNT = 0x0002
    CAP_FREQ_KHZ = 0x6003
    CAP_BYTES = 0x0004
    CAP_LINE_BYTES = 0x6005
    CAP_ISA = 0x2006
    CAP_INT_WIDTHS = 0x4007
    CAP_RADIX = 0x2008
    CAP_MACS_PER_CYCLE = 0x0009
    CAP_SIMD = 0x400A
    CAP_TICK_NS = 0x600B
    CAP_REGIONS = 0x000C
    CAP_BANDWIDTH_MBPS = 0x000D
    CAP_LATENCY_NS = 0x600E
    CAP_PQ_ALGS = 0x400F
    CAP_HASH_ALGS = 0x4010
    CAP_WIRE_BITS = 0x2011
    CAP_PEAK_OPS_MHZ = 0x6012
    CAP_CONTRACT_ID = 0x2013
    CAP_CONTRACT_VER = 0x0014
    CAP_KIND = 0x6015
    CAP_CAPACITY_CLASS = 0x0016
    CAP_PERF_BUCKET = 0x0017
    CAP_POWER_BUCKET = 0x6018

    ISA_X86_64 = 1
    ISA_ARM64 = 2
    ISA_RISCV64 = 3
    ISA_WASM32 = 4

    # Parent id of a root node, and the private extension limit per node.
    ROOT = 0xFFFF
    EXT_MAX = 256

    # How a target component is served on a host (HwProfile#pair).
    NATIVE = 0
    EMULATE = 1
    GROUP = 2
    PAIRINGS = %i[native emulate group].freeze

    # A release key: the public key (ML-DSA-87 || SLH-DSA, a String) and the
    # two secret keys, held outside the Ruby heap and wiped on #close.
    ReleaseKey = Struct.new(:public_key, :mldsa_secret, :slhdsa_secret, keyword_init: true) do
      # Wipe both secret keys.
      def close
        mldsa_secret.close
        slhdsa_secret.close
      end
    end

    # One audit record (regions and matrices). +event+ is a Symbol
    # (EVENTS); +id+ is the region id (the function id for :contract);
    # +tag+ is the hexagram tag 0..63; +subject+ (SHA-256 of the package
    # header) and +hash+ (the chain hash) are 32-byte Strings.
    LogEntry = Struct.new(:event, :reason, :id, :version, :tag, :subject, :hash, keyword_init: true) do
      # @return [Hexagram]
      def hexagram = Hexagram.from_tag(tag)
    end

    module_function

    # A fresh release key pair. The secret keys never touch the Ruby heap.
    # @param capability [Capability] needs :crypto_sign
    # @return [ReleaseKey] close it to wipe the secrets
    def keygen(capability)
      pk = FFI::MemoryPointer.new(:uint8, PK_BYTES)
      msk = SecretKey.new(MLDSA_SK_BYTES)
      ssk = SecretKey.new(SLHDSA_SK_BYTES)
      begin
        Call.check(Native.lib.zxv_hw_keygen(capability.ptr, pk, msk.pointer, ssk.pointer), capability.context)
      rescue Exception # rubocop:disable Lint/RescueException
        msk.close
        ssk.close
        raise
      end
      ReleaseKey.new(public_key: pk.read_bytes(PK_BYTES), mldsa_secret: msk, slhdsa_secret: ssk)
    end

    # Build and sign a package: header || payload || signatures.
    # @param capability [Capability] needs :crypto_sign
    # @param kind [Symbol, Integer] IMAGE_KINDS (:partial, :vectors, ...)
    # @param target_class [Integer] the hardware class the image is for
    # @param fn_id [Integer] the function the region will serve (for :vectors, the function)
    # @param version [Integer] image or contract version
    # @param svn [Integer] the anti-rollback floor
    # @param payload [String]
    # @param key [ReleaseKey]
    # @param target_digest [String, nil] 32 bytes, or nil
    # @return [String] the signed package
    def build_image(capability, kind:, target_class:, fn_id:, version:, svn:, payload:, key:, target_digest: nil)
      raise ZxvUsageError.new(Status::E_ARG, "key must be a Zxv::Hw::ReleaseKey") unless key.is_a?(ReleaseKey)

      k = kind.is_a?(Symbol) ? IMAGE_KINDS.fetch(kind) { raise ZxvUsageError.new(Status::E_ARG, "unknown image kind #{kind.inspect}") } : Call.uint32!(kind, "kind")
      body = Call.bytes!(payload, "payload")
      td = target_digest && Call.mem(Call.bytes!(target_digest, "target_digest", size: 32))
      msk = secret!(key.mldsa_secret, MLDSA_SK_BYTES)
      ssk = secret!(key.slhdsa_secret, SLHDSA_SK_BYTES)
      args = [capability.ptr, k, Call.uint32!(target_class, "target_class"), td, Call.uint32!(fn_id, "fn_id"),
              Call.uint32!(version, "version"), Call.uint32!(svn, "svn"), Call.mem(body, allow_null: true), body.bytesize,
              msk, ssk]
      cap = HEADER_BYTES + body.bytesize + SIG_BYTES
      len = Call.out(:size_t)
      2.times do
        buf = FFI::MemoryPointer.new(:uint8, cap)
        rc = Native.lib.zxv_hw_image_build(*args, buf, cap, len)
        need = len.read(:size_t)
        if rc == Status::E_BUFFER && need > cap
          cap = need
          next
        end
        Call.check(rc, capability.context)
        return buf.read_bytes(need)
      end
      Call.check(Status::E_BUFFER, capability.context)
    end

    # The payload of an IMG_VECTORS package: each vector as op (u32 LE),
    # input length (u32 LE), input, expected length (u32 LE), expected.
    # @param vectors [Array<Array(Integer, String, String)>] [op, input, expected]
    # @return [String]
    def vector_list(vectors)
      vectors.map do |op, input, expect|
        i = Call.bytes!(input, "input")
        e = Call.bytes!(expect, "expected")
        [Call.uint32!(op, "op"), i.bytesize].pack("VV") + i + [e.bytesize].pack("V") + e
      end.join.b
    end

    # Map a symbol or Array of symbols (or an Integer) to policy flags.
    def policy_flags(spec)
      case spec
      when Symbol then POLICIES.fetch(spec) { raise ZxvUsageError.new(Status::E_ARG, "unknown policy #{spec.inspect}") }
      when Array then spec.reduce(0) { |m, s| m | policy_flags(s) }
      else Call.uint32!(spec, "flags")
      end
    end

    # A kind Symbol (or Integer) as the ZXV_HW_KIND_* value.
    def kind_value(kind)
      return Call.int!(kind, "kind", min: -(2**31), max: 2**31 - 1) unless kind.is_a?(Symbol)

      KINDS.index(kind) || raise(ZxvUsageError.new(Status::E_ARG, "unknown kind #{kind.inspect}"))
    end

    # Run a callback body for libzxv: never let a Ruby exception unwind
    # through C (that would skip its lock release). A negative Integer the
    # body returns is passed on as a status; an exception with a status
    # passes its status; any other exception becomes ZXV_E_IO.
    # @api private
    def guard
      rc = yield
      rc.is_a?(Integer) && rc.negative? ? rc : Status::OK
    rescue Exception => e # rubocop:disable Lint/RescueException
      s = e.respond_to?(:status) ? e.status : nil
      s.is_a?(Integer) && s.negative? ? s : Status::E_IO
    end

    def secret!(key, size)
      unless key.is_a?(SecretKey) && key.bytesize == size
        raise ZxvUsageError.new(Status::E_ARG, "expected a #{size}-byte Zxv::SecretKey")
      end

      key.pointer
    end
    private_class_method :secret!

    # Shared reader of the hash-chained audit logs.
    # @api private
    module AuditLog
      # The chain head and the record count.
      # @return [Array(String, Integer)]
      def log
        head = FFI::MemoryPointer.new(:uint8, 32)
        n = Call.out(:uint32)
        Call.check(Native.lib.public_send(log_fns[0], capability.ptr, ptr, head, n), context)
        [head.read_bytes(32), n.read_uint32]
      end

      # Record +index+ (0 = oldest).
      # @return [LogEntry]
      def log_entry(index)
        ev = Call.out(:int32)
        reason, id, ver, tag = Array.new(4) { Call.out(:uint32) }
        subj, hash = Array.new(2) { FFI::MemoryPointer.new(:uint8, 32) }
        Call.check(Native.lib.public_send(log_fns[1], capability.ptr, ptr, Call.uint32!(index, "index"), ev, reason, id, ver,
                                          tag, subj, hash), context)
        e = ev.read_int32
        LogEntry.new(event: EVENTS.fetch(e, e), reason: reason.read_uint32, id: id.read_uint32, version: ver.read_uint32,
                     tag: tag.read_uint32, subject: subj.read_bytes(32), hash: hash.read_bytes(32))
      end

      # Every record, oldest first.
      # @return [Array<LogEntry>]
      def log_entries = Array.new(log[1]) { |i| log_entry(i) }
    end
  end

  # A ZHP-1 machine profile: a tree of abstract components with integer
  # capability descriptors and an opaque per-node extension. Plain data:
  # no context and no capability; freed by the GC or by #close.
  #
  #   host = Zxv::HwProfile.probe_host
  #   r1 = Zxv::HwProfile.reference
  #   native, emulated = host.match(r1)
  class HwProfile
    # One node: id, parent (Hw::ROOT for a root), class, number of caps.
    Node = Struct.new(:id, :parent, :cls, :ncaps, keyword_init: true)
    # How target node +index+ is served on a host: +kind+ is :native (with
    # +have_id+), :emulate (with the first unmet cap key, 0 if the host has
    # no component of that class) or :group.
    Pair = Struct.new(:need_id, :kind, :have_id, :unmet_key, keyword_init: true)

    class << self
      # Describe this machine (only what the OS reports reliably).
      def probe_host = from_out { |o| Native.lib.zxv_hw_profile_probe_host(o) }

      # A copy of the public VULPINE-class reference machine R1.
      def reference = from_out { |o| Native.lib.zxv_hw_profile_reference(o) }

      # +target+ as served on +host+: every running node carries Hw::CAP_KIND.
      def present(host, target) = from_out { |o| Native.lib.zxv_hw_profile_present(host.ptr, target.ptr, o) }

      # Parse a canonical ZHP-1 encoding (non-canonical: ZXV_E_FORMAT).
      def decode(bytes)
        b = Call.bytes!(bytes, "bytes")
        from_out { |o| Native.lib.zxv_hw_profile_decode(Call.mem(b, allow_null: true), b.bytesize, o) }
      end

      private

      def from_out
        out = Call.out(:pointer)
        Call.check(yield(out))
        allocate.tap { |p| p.send(:adopt, out.read_pointer) }
      end
    end

    # An empty profile.
    def initialize
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_hw_profile_create(out))
      adopt(out.read_pointer)
    end

    # Free it now. Idempotent.
    def close
      return if @ptr.nil?

      @ptr.free
      @ptr = nil
    end

    # @return [Boolean]
    def closed? = @ptr.nil?

    # The canonical encoding.
    # @return [String]
    def encode
      len = Call.out(:size_t)
      rc = Native.lib.zxv_hw_profile_encode(ptr, nil, 0, len)
      raise Call.error_for(rc) unless rc == Status::E_BUFFER || rc == Status::OK

      n = len.read(:size_t)
      buf = FFI::MemoryPointer.new(:uint8, [n, 1].max)
      Call.check(Native.lib.zxv_hw_profile_encode(ptr, buf, n, len))
      buf.read_bytes(len.read(:size_t))
    end
    alias to_bytes encode

    # SHA-256 of the canonical encoding: the profile's identity.
    # @return [String] 32 bytes
    def digest
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.zxv_hw_profile_digest(ptr, out))
      out.read_bytes(32)
    end

    # Add a node: ids strictly increase, parent first (or Hw::ROOT).
    # @return [self]
    def add_node(id, parent:, cls:)
      Call.check(Native.lib.zxv_hw_profile_node_add(ptr, Call.uint32!(id, "id"), Call.uint32!(parent, "parent"),
                                                    Call.uint32!(cls, "cls")))
      self
    end

    # Set capability +key+ of node +node_id+.
    # @return [self]
    def set_cap(node_id, key, value)
      Call.check(Native.lib.zxv_hw_profile_cap_set(ptr, Call.uint32!(node_id, "node_id"), Call.uint32!(key, "key"),
                                                   Call.uint64!(value, "value")))
      self
    end

    # @return [Integer, nil] the cap value, or nil if the node lacks it
    def cap(node_id, key)
      out = Call.out(:uint64)
      rc = Native.lib.zxv_hw_profile_cap_get(ptr, Call.uint32!(node_id, "node_id"), Call.uint32!(key, "key"), out)
      return nil if rc == Status::E_NOT_FOUND

      Call.check(rc)
      out.read_uint64
    end

    # Set the node's private extension (opaque, at most Hw::EXT_MAX bytes).
    # @return [self]
    def set_ext(node_id, bytes)
      b = Call.bytes!(bytes, "ext")
      Call.check(Native.lib.zxv_hw_profile_ext_set(ptr, Call.uint32!(node_id, "node_id"), Call.mem(b, allow_null: true),
                                                   b.bytesize))
      self
    end

    # @return [String] the node's private extension ("" if none)
    def ext(node_id)
      id = Call.uint32!(node_id, "node_id")
      len = Call.out(:size_t)
      buf = FFI::MemoryPointer.new(:uint8, Hw::EXT_MAX)
      Call.check(Native.lib.zxv_hw_profile_ext_get(ptr, id, buf, Hw::EXT_MAX, len))
      buf.read_bytes(len.read(:size_t))
    end

    # @return [Integer] number of nodes
    def count
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_hw_profile_count(ptr, out))
      out.read_uint32
    end
    alias size count

    # Node at +index+ (in id order).
    # @return [Node]
    def node(index)
      id, parent, cls, ncaps = Array.new(4) { Call.out(:uint32) }
      Call.check(Native.lib.zxv_hw_profile_node(ptr, Call.uint32!(index, "index"), id, parent, cls, ncaps))
      Node.new(id: id.read_uint32, parent: parent.read_uint32, cls: cls.read_uint32, ncaps: ncaps.read_uint32)
    end

    # @return [Array<Node>]
    def nodes = Array.new(count) { |i| node(i) }

    # Match +target+ against this host profile.
    # @return [Array(Integer, Integer)] [native, emulated] component counts
    def match(target)
      n = Call.out(:uint32)
      e = Call.out(:uint32)
      Call.check(Native.lib.zxv_hw_profile_match(ptr, target.ptr, n, e))
      [n.read_uint32, e.read_uint32]
    end

    # How target node +index+ (target order) is served on this host.
    # @return [Pair]
    def pair(target, index)
      need = Call.out(:uint32)
      kind = Call.out(:int32)
      have = Call.out(:uint32)
      unmet = Call.out(:uint32)
      Call.check(Native.lib.zxv_hw_profile_pair(ptr, target.ptr, Call.uint32!(index, "index"), need, kind, have, unmet))
      k = kind.read_int32
      Pair.new(need_id: need.read_uint32, kind: Hw::PAIRINGS.fetch(k, k), have_id: have.read_uint32,
               unmet_key: unmet.read_uint32)
    end

    def inspect = closed? ? "#<Zxv::HwProfile closed>" : "#<Zxv::HwProfile #{count} nodes>"

    # @api private
    def ptr
      raise ZxvUsageError.new(Status::E_STATE, "HwProfile is closed") if @ptr.nil?

      @ptr
    end

    private

    def adopt(raw)
      @ptr = FFI::AutoPointer.new(raw, Native.method(:zxv_hw_profile_destroy))
    end
  end

  # The emulation matrix: functions (engine contracts) served by
  # interchangeable implementations, each serving only after it reproduces
  # the function's golden vectors bit for bit. A new matrix already holds
  # the standard engines (Hw::FN_*), each with a proven SOFT
  # implementation on Hw::RES_HOST.
  #
  #   mx = Zxv::HwMatrix.new(cap)                      # cap needs :hw_matrix
  #   mx.call(Zxv::Hw::FN_SHA256, "abc")               # 32 bytes
  class HwMatrix < NativeHandle
    include Hw::AuditLog

    # A function's contract version, policy flags and vector count.
    FunctionInfo = Struct.new(:contract, :policy, :vectors, keyword_init: true)
    # What discovery reports for one implementation. +kind+ is a Symbol
    # (Hw::KINDS), +proof+ a Truth (only TRUE serves), +flags+ Hw::PF_*.
    ProviderInfo = Struct.new(:index, :fn_id, :kind, :proof, :contract, :flags, :throughput_class, :power_class,
                              :calls, :cost_spent, keyword_init: true) do
      # @param name [Symbol] a key of Hw::PROVIDER_FLAGS
      def flag?(name) = flags & Hw::PROVIDER_FLAGS.fetch(name) != 0
      def serving? = flag?(:serving)
    end

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :hw_matrix
    def initialize(capability)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_hw_matrix_create_cap(capability.ptr, out), capability.context)
      super(capability.context, out.read_pointer, :zxv_hw_matrix_destroy)
    end

    # Add a function. +name+: UTF-8, at most 23 bytes.
    # @return [self]
    def add_function(fn_id, name = nil)
      check(Native.lib.zxv_hw_function_add(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), name && Call.text!(name, "name")))
      self
    end

    # Add a golden vector of the function's current contract (output at
    # most 512 bytes). Every implementation becomes unproven.
    # @return [self]
    def add_vector(fn_id, input, expected, op: 0)
      i = Call.bytes!(input, "input")
      e = Call.bytes!(expected, "expected")
      check(Native.lib.zxv_hw_vector_add(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), Call.uint32!(op, "op"),
                                         Call.mem(i, allow_null: true), i.bytesize, Call.mem(e, allow_null: true), e.bytesize))
      self
    end

    # Start contract +version+ (strictly newer, else ZXV_E_ROLLBACK).
    # @return [self]
    def start_contract(fn_id, version)
      check(Native.lib.zxv_hw_function_contract(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"),
                                                Call.uint32!(version, "version")))
      self
    end

    # Placement: flags (:key_bearing, :local_only, an Array of them or an
    # Integer), a latency ceiling and a power budget (0: none). Clearing
    # :key_bearing once set raises ZxvRefusedError (ZXV_E_POLICY).
    # @return [self]
    def set_policy(fn_id, flags: 0, max_latency_ns: 0, power_budget_mw: 0)
      check(Native.lib.zxv_hw_policy_set(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), Hw.policy_flags(flags),
                                         Call.uint32!(max_latency_ns, "max_latency_ns"),
                                         Call.uint32!(power_budget_mw, "power_budget_mw")))
      self
    end

    # @return [FunctionInfo]
    def function_info(fn_id)
      c, p, v = Array.new(3) { Call.out(:uint32) }
      check(Native.lib.zxv_hw_function_info(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), c, p, v))
      FunctionInfo.new(contract: c.read_uint32, policy: p.read_uint32, vectors: v.read_uint32)
    end

    # @return [self]
    def add_resource(res_id)
      check(Native.lib.zxv_hw_resource_add(capability.ptr, ptr, Call.uint32!(res_id, "res_id")))
      self
    end

    # @param state [Symbol, Integer] :ok, :faulted, :reconfiguring or :offline
    # @return [self]
    def set_resource(res_id, state)
      s = state.is_a?(Symbol) ? Hw::RESOURCE_STATES.index(state) : Call.int!(state, "state", min: -(2**31), max: 2**31 - 1)
      raise ZxvUsageError.new(Status::E_ARG, "unknown resource state #{state.inspect}") if s.nil?

      check(Native.lib.zxv_hw_resource_set(capability.ptr, ptr, Call.uint32!(res_id, "res_id"), s))
      self
    end

    # Add an implementation of +fn_id+ on resource +resource+. The block
    # is the implementation: it receives (op, input) and returns the output
    # String; raising an error with a status (or returning a negative
    # Integer) returns that status to the caller of #call. The block runs on
    # the calling thread inside libzxv; it may #call only the functions it
    # declared with #add_dependency. It stays referenced as long as the
    # context, so libzxv never calls freed code.
    # @param kind [Symbol, Integer] Hw::KINDS (:hard, :fabric, ...)
    # @return [Integer] the implementation's index
    def add_provider(fn_id, resource:, kind:, cost:, &impl)
      raise ZxvUsageError.new(Status::E_ARG, "add_provider needs a block") unless impl

      fn = FFI::Function.new(:int32, %i[pointer uint32 pointer size_t pointer size_t pointer]) do |_u, op, inp, n, out, cap, out_len|
        Hw.guard do
          r = impl.call(op, n.zero? ? "".b : inp.read_bytes(n))
          next r if r.is_a?(Integer)

          b = Call.bytes!(r, "provider output")
          out_len.write(:size_t, b.bytesize)
          next Status::E_BUFFER if b.bytesize > cap

          out.put_bytes(0, b) unless b.empty?
          Status::OK
        end
      end
      context.retain(fn)
      idx = Call.out(:uint32)
      check(Native.lib.zxv_hw_provider_add(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), Call.uint32!(resource, "resource"),
                                           Hw.kind_value(kind), Call.uint32!(cost, "cost"), fn, nil, idx))
      idx.read_uint32
    end

    # Declare traits used by placement and discovery.
    # @return [self]
    def set_traits(provider, latency_ns:, power_mw:, throughput_class:)
      check(Native.lib.zxv_hw_provider_traits(capability.ptr, ptr, Call.uint32!(provider, "provider"),
                                              Call.uint32!(latency_ns, "latency_ns"), Call.uint32!(power_mw, "power_mw"),
                                              Call.uint32!(throughput_class, "throughput_class")))
      self
    end

    # Let +provider+ call +dep_fn_id+ through this matrix.
    # @return [self]
    def add_dependency(provider, dep_fn_id)
      check(Native.lib.zxv_hw_provider_dep(capability.ptr, ptr, Call.uint32!(provider, "provider"),
                                           Call.uint32!(dep_fn_id, "dep_fn_id")))
      self
    end

    # @return [self]
    def enable_provider(provider, on = true)
      check(Native.lib.zxv_hw_provider_enable(capability.ptr, ptr, Call.uint32!(provider, "provider"), on ? 1 : 0))
      self
    end

    # Run the golden vectors. TRUE: serves; FALSE: a vector failed;
    # NEITHER: no vectors.
    # @return [Truth]
    def attest(provider)
      lp = Call.out(:int32)
      check(Native.lib.zxv_hw_attest(capability.ptr, ptr, Call.uint32!(provider, "provider"), lp))
      Truth.of(lp.read_int32)
    end

    # @return [Integer] number of implementations (indexed 0...count)
    def provider_count
      n = Call.out(:uint32)
      check(Native.lib.zxv_hw_provider_count(capability.ptr, ptr, n))
      n.read_uint32
    end

    # @return [ProviderInfo]
    def provider_info(provider)
      i = Call.uint32!(provider, "provider")
      fn, con, flags, tput, pwr = Array.new(5) { Call.out(:uint32) }
      kind, lp = Array.new(2) { Call.out(:int32) }
      calls, spent = Array.new(2) { Call.out(:uint64) }
      check(Native.lib.zxv_hw_provider_info(capability.ptr, ptr, i, fn, kind, lp, con, flags, tput, pwr, calls, spent))
      k = kind.read_int32
      ProviderInfo.new(index: i, fn_id: fn.read_uint32, kind: Hw::KINDS.fetch(k, k), proof: Truth.of(lp.read_int32),
                       contract: con.read_uint32, flags: flags.read_uint32, throughput_class: tput.read_uint32,
                       power_class: pwr.read_uint32, calls: calls.read_uint64, cost_spent: spent.read_uint64)
    end

    # Discovery: every implementation.
    # @return [Array<ProviderInfo>]
    def providers = Array.new(provider_count) { |i| provider_info(i) }

    # The implementation serving +fn_id+ now, or nil if none may.
    # @return [Integer, nil]
    def bound(fn_id)
      out = Call.out(:uint32)
      rc = Native.lib.zxv_hw_bound(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), out)
      return nil if rc == Status::E_NOPROVIDER

      check(rc)
      out.read_uint32
    end

    # Call a function. Raises ZxvRefusedError (ZXV_E_NOPROVIDER) when no
    # proven implementation may serve it.
    # @return [String] the output
    def call(fn_id, input = "".b, op: 0, max_output: 4096)
      run(:zxv_hw_call, fn_id, input, op, max_output)
    end

    # #call that also runs the next-best implementation and compares; a
    # mismatch takes both out of service (ZXV_E_DISPUTE).
    # @return [Array(String, Boolean)] the output and whether a second implementation ran
    def shadow(fn_id, input = "".b, op: 0, max_output: 4096)
      checked = Call.out(:int32)
      out = run(:zxv_hw_shadow, fn_id, input, op, max_output, checked)
      [out, checked.read_int32 == 1]
    end

    # Give a pure function a memo of +entries+ (a power of two up to 65536;
    # 0 removes it). Key-bearing functions: ZXV_E_POLICY.
    # @return [Array(Integer, Integer)] [hits, misses]
    def memo(fn_id, entries)
      h = Call.out(:uint64)
      m = Call.out(:uint64)
      check(Native.lib.zxv_hw_memo(capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), Call.uint32!(entries, "entries"), h, m))
      [h.read_uint64, m.read_uint64]
    end

    # Trust a release key for contract packages (at most 4). Needs :hw_reconf.
    # @return [self]
    def trust_key(public_key)
      check(Native.lib.zxv_hw_matrix_trust_key(capability.ptr, ptr, Call.mem(Call.bytes!(public_key, "public_key",
                                                                                         size: Hw::PK_BYTES))))
      self
    end

    # Apply a signed IMG_VECTORS package. Needs :hw_reconf.
    # @return [Integer] how many implementations prove the new contract
    def apply_contract(package)
      b = Call.bytes!(package, "package")
      n = Call.out(:uint32)
      check(Native.lib.zxv_hw_contract_apply(capability.ptr, ptr, Call.mem(b, allow_null: true), b.bytesize, n))
      n.read_uint32
    end

    def inspect = closed? ? "#<Zxv::HwMatrix closed>" : "#<Zxv::HwMatrix #{provider_count} implementations>"

    private

    def log_fns = %i[zxv_hw_matrix_log zxv_hw_matrix_log_entry]

    def check(rc) = Call.check(rc, context)

    def run(fn, fn_id, input, op, max_output, *extra)
      b = Call.bytes!(input, "input")
      cap = Call.int!(max_output, "max_output", min: 1, max: 1 << 24)
      len = Call.out(:size_t)
      2.times do
        out = FFI::MemoryPointer.new(:uint8, cap)
        rc = Native.lib.public_send(fn, capability.ptr, ptr, Call.uint32!(fn_id, "fn_id"), Call.uint32!(op, "op"),
                                    Call.mem(b, allow_null: true), b.bytesize, out, cap, len, *extra)
        need = len.read(:size_t)
        if rc == Status::E_BUFFER && need > cap
          cap = need
          next
        end
        check(rc)
        return out.read_bytes(need)
      end
      check(Status::E_BUFFER)
    end
  end

  # A reconfigurable region: hardware updates shipped as signed images,
  # staged into the inactive slot, read back, tested against the golden
  # vectors of its implementation in a matrix, committed or rolled back.
  # Every step is an event and a record in a hash-chained audit log.
  #
  # The device is any object answering
  #   load(slot, payload)   # write the payload into slot 0 or 1
  #   readback(slot)        # => the SHA-256 (32 bytes) of what slot holds
  #   activate(slot)        # switch to slot
  # Each may raise an error with a status (or return a negative Integer) to
  # refuse; any other exception becomes ZXV_E_IO.
  class HwRegion < NativeHandle
    include Hw::AuditLog

    # +state+ is a Symbol (Hw::REGION_STATES).
    Info = Struct.new(:state, :version, :floor, :quarantined, keyword_init: true)

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :hw_reconf
    # @param matrix [HwMatrix, nil] whose golden vectors test each image (nil: no test)
    # @param provider [Integer] the region's FABRIC or MICRO implementation in +matrix+
    # @param region_id [Integer]
    # @param target_class [Integer] images must name this class
    # @param device [#load, #readback, #activate]
    # @param target_digest [String, nil] 32 bytes, or nil (zeros)
    # @param log_cap [Integer] audit log size; when it fills the region halts
    #   and the context is poisoned (fail closed)
    def initialize(capability, region_id:, target_class:, device:, matrix: nil, provider: 0, target_digest: nil, log_cap: 64)
      @capability = capability
      @device = device
      td = target_digest && Call.mem(Call.bytes!(target_digest, "target_digest", size: 32))
      ctx = capability.context
      load = ctx.retain(FFI::Function.new(:int32, %i[pointer uint32 pointer size_t]) do |_u, slot, p, n|
        Hw.guard { device.load(slot, n.zero? ? "".b : p.read_bytes(n)) }
      end)
      readback = ctx.retain(FFI::Function.new(:int32, %i[pointer uint32 pointer]) do |_u, slot, d|
        Hw.guard { d.put_bytes(0, Call.bytes!(device.readback(slot), "readback digest", size: 32)) }
      end)
      activate = ctx.retain(FFI::Function.new(:int32, %i[pointer uint32]) do |_u, slot|
        Hw.guard { device.activate(slot) }
      end)
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_hw_region_create_cap(capability.ptr, matrix&.ptr, Call.uint32!(provider, "provider"),
                                                     Call.uint32!(region_id, "region_id"),
                                                     Call.uint32!(target_class, "target_class"), td, load, readback,
                                                     activate, nil, Call.uint32!(log_cap, "log_cap"), out), ctx)
      super(ctx, out.read_pointer, :zxv_hw_region_destroy)
    end

    # Receive every recorded step: the block gets (event Symbol, reason,
    # version), after the region call released its locks. It may call
    # into the region again. Exceptions it raises are swallowed (they
    # cannot cross libzxv); the last one is kept in #event_error.
    # @return [self]
    def on_event(&blk)
      raise ZxvUsageError.new(Status::E_ARG, "on_event needs a block") unless blk

      fn = FFI::Function.new(:void, %i[pointer uint32 int32 uint32 uint32]) do |_u, _rid, ev, reason, version|
        blk.call(Hw::EVENTS.fetch(ev, ev), reason, version)
      rescue Exception => e # rubocop:disable Lint/RescueException
        @event_error = e
      end
      context.retain(fn)
      check(Native.lib.zxv_hw_region_events(capability.ptr, ptr, fn, nil))
      self
    end

    # @return [Exception, nil] the last exception an event block raised
    attr_reader :event_error

    # Trust a release key (at most 4).
    # @return [self]
    def trust_key(public_key)
      check(Native.lib.zxv_hw_region_trust_key(capability.ptr, ptr, Call.mem(Call.bytes!(public_key, "public_key",
                                                                                        size: Hw::PK_BYTES))))
      self
    end

    # Verify and load an image into the inactive slot.
    # @raise [VerificationFailedError, ZxvRefusedError] ZXV_E_VERIFY, ZXV_E_ROLLBACK,
    #   ZXV_E_QUARANTINED, ZXV_E_BLOCKED; ZxvUsageError for ZXV_E_TARGET
    # @return [self]
    def stage(image)
      b = Call.bytes!(image, "image")
      check(Native.lib.zxv_hw_region_stage(capability.ptr, ptr, Call.mem(b, allow_null: true), b.bytesize))
      self
    end

    # Switch to the staged image and prove it; passing starts a probation
    # of +ticks+. ZXV_E_QUARANTINED if it fails (the previous image is back).
    # @return [self]
    def trial(ticks)
      check(Native.lib.zxv_hw_region_trial(capability.ptr, ptr, Call.uint32!(ticks, "ticks")))
      self
    end

    # Make the tested image permanent and raise the floor.
    # @return [self]
    def commit
      check(Native.lib.zxv_hw_region_commit(capability.ptr, ptr))
      self
    end

    # @return [self]
    def rollback
      check(Native.lib.zxv_hw_region_rollback(capability.ptr, ptr))
      self
    end

    # One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED).
    # @return [self]
    def tick
      check(Native.lib.zxv_hw_region_tick(capability.ptr, ptr))
      self
    end

    # @return [Info]
    def info
      st = Call.out(:int32)
      ver, floor, nq = Array.new(3) { Call.out(:uint32) }
      check(Native.lib.zxv_hw_region_info(capability.ptr, ptr, st, ver, floor, nq))
      s = st.read_int32
      Info.new(state: Hw::REGION_STATES.fetch(s, s), version: ver.read_uint32, floor: floor.read_uint32,
               quarantined: nq.read_uint32)
    end

    def inspect = closed? ? "#<Zxv::HwRegion closed>" : "#<Zxv::HwRegion>"

    private

    def log_fns = %i[zxv_hw_region_log zxv_hw_region_log_entry]

    def check(rc) = Call.check(rc, context)
  end
end
