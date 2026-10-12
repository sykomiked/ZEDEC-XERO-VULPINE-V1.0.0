# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: the paradox engine (zxv_paradox_*).
  #
  # A PARADOX here is work whose four-valued state is Both (the evidence
  # points both ways) or Neither (no evidence). This group does not decide
  # which side is "really" true. It reads the work's {Hexagram} tag, gives
  # the paradox one of four KINDS and runs a fixed, logged operator for it:
  #
  # :constructive:: Both, margin growing (polarity :negentropic): a bounded
  #                 parameter update, applied only inside published bounds
  #                 ({Book#constructive} with a {Policy});
  # :destructive:: Both, margin shrinking or stale (:entropic): contain,
  #                quarantine, roll back from a checkpoint and write a
  #                {RepairProof} ({Book#destructive});
  # :neutral:: Both and steady past the potential phase, or Neither: hold
  #            every candidate branch ({Book#neutral} with a {Hold});
  # :special:: Both, steady, potential phase, unknown signature: a permanent
  #            frontier record and a draft classification for review
  #            ({Book#special}, {Book#draft}, {Book#review}).
  #
  # A signature found in the registry overrides the trend. True and False
  # are not paradoxes (:none). Operators take the (kind, tag) pair that
  # {Book#classify} returned; a pair that breaks the class rules is
  # ZxvUsageError (ZXV_E_ARG). A FORBIDDEN tag (a real check found a broken
  # invariant) is accepted with any kind, and every operator refuses it and
  # rolls back.
  #
  # OUTCOMES. An operator that ran returns its decision as a Symbol from
  # {OUTCOMES} (:refused and :proposal_for_owner are decisions, not errors)
  # and has appended one record to the operator log. An exception means it
  # did not run. A full log raises ZxvRefusedError (ZXV_E_CAPACITY): halt
  # the context, since work that cannot be recorded must not continue.
  #
  # CAPABILITIES. Writing a log or the registry, applying a policy update
  # and creating any handle need :paradox (from the book's context).
  # Reading, classifying and adding branches, nodes and updates to a handle
  # you hold need none. Nothing here signs: the engine produces bytes and
  # hashes; sign them with {Ed25519} or {MlDsa87}.
  #
  # Honest limits, in plain words:
  # * Automatic changes happen only inside published bounds; anything else
  #   becomes a proposal for the owner. The operator has no balance input:
  #   it cannot move money or change a balance or a ledger invariant.
  # * Policies that touch money or devices need the owner: they are never
  #   changed automatically. +owner_ok+ is YOUR assertion; inside one
  #   process this library cannot check who the owner is.
  # * Detection is by the listed checks and is not instant; what they do
  #   not look at goes undetected.
  # * Destructive work is contained, never answered: there is no way to
  #   reply to, or act against, a sender.
  # * This is contradiction-tolerant bookkeeping: it contains, holds and
  #   routes contradictions; it never makes both sides true.
  # * Peers agree by applying the same fixed order to the same signed
  #   inputs, not through any shared clock. Integers only.
  module Paradox
    # Kinds, by ZXV_PDX_KIND_* value (index).
    KINDS = %i[constructive destructive neutral special none].freeze
    # What to do with a classified paradox, by ZXV_PDX_DO_* value:
    # :none (not a paradox: use Hexagram#action), :synthesize, :contain,
    # :hold, :record (sandbox), :rollback (forbidden).
    DOS = %i[none synthesize contain hold record rollback].freeze
    # Operator outcomes, by ZXV_PDX_OUT_* value (index).
    OUTCOMES = %i[applied kept proposal_for_owner rolled_back contained deferred settled recorded accepted pending resolved
                  escalate_to_owner refused log_full].freeze
    # Graph node states, by ZXV_PDX_NS_* value. Final: :clear, :contained,
    # :rolled_back, :tainted, :applied.
    NODE_STATES = %i[open clear contained rolled_back tainted sandboxed waiting held blocked applied].freeze
    # Flags of a policy, a registry entry or a draft: the owner decides.
    FLAGS = { money: 1, device: 2 }.freeze
    # Which checks fired (a RepairProof's trips; the quarantine reason).
    TRIPS = { steps: 1, budget: 2, cycle: 4, invariant: 8, forbidden: 16 }.freeze
    # The book's three logs.
    LOGS = { ops: 0, frontier: 1, quarantine: 2 }.freeze

    # Bytes of an operator or frontier log record.
    REC_BYTES = 144
    # Bytes of a quarantine log record.
    QREC_BYTES = 110
    # Bytes of a RepairProof.
    REPAIR_BYTES = 160
    # Bytes of a draft classification.
    DRAFT_BYTES = 71
    # Records per log.
    LOG_MAX = 65_536
    # Registry entries.
    REGISTRY_MAX = 65_536
    # Parameters per policy.
    POLICY_MAX = 64
    # Branches per hold.
    HOLD_MAX = 64
    # Nodes per graph.
    GRAPH_MAX = 64
    # Dependencies per node.
    MAX_DEPS = 4
    # Queued node updates per pass.
    UPDATES_MAX = 256
    # State fingerprints per detection.
    TRACE_MAX = 4096
    # The source of a registry entry not learned from the frontier.
    SRC_SEED = 2**64 - 1

    # What {Book#classify} returned: the +kind+ (a KINDS Symbol), the +tag+
    # with the kind's polarity (a {Hexagram}) and the registry index that
    # decided it (nil if none). Pass it to an operator as +pair+.
    Classification = Struct.new(:kind, :tag, :registry_index, keyword_init: true)

    # One registry entry: its 32-byte +signature+, +kind+, +reason+ code,
    # +flags+ (FLAGS keys) and +source+ (the frontier record it was learned
    # from, or nil for a seed).
    RegistryEntry = Struct.new(:signature, :kind, :reason, :flags, :source, keyword_init: true)

    # One operator or frontier log record (REC_BYTES, decoded). +op+: 1
    # constructive, 2 destructive, 3 neutral, 4 special, 5 draft, 6 review
    # or registry add, 7 graph pass. +a+, +b+, +prev+, +hash+ are 32 bytes.
    LogRecord = Struct.new(:seq, :op, :kind, :tag, :outcome, :aux, :a, :b, :prev, :hash, :bytes, keyword_init: true)
    # One quarantine log record (QREC_BYTES, decoded); +reason+ is the trips.
    QuarantineEntry = Struct.new(:seq, :tag, :reason, :submitter, :subject, :prev, :hash, :bytes, keyword_init: true)

    module_function

    # The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data), from
    # a canonical description you build.
    # @return [String] 32 bytes
    def signature(data)
      d = Call.bytes!(data, "data")
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.zxv_paradox_signature(Call.mem0(d), d.bytesize, out))
      out.read_bytes(32)
    end

    # The kind a tag reads as on its own (a forbidden tag reads as :none:
    # forbidden is not a kind).
    # @param tag [Hexagram, Integer]
    # @return [Symbol]
    def kind_of_tag(tag)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_paradox_kind_of_tag(tag!(tag), out))
      sym(KINDS, out.read_int32)
    end

    # What to do with a classified pair: a DOS Symbol (a forbidden tag
    # gives :rollback whatever the kind).
    # @raise [ZxvUsageError] ZXV_E_ARG (an invalid pair)
    def action(kind, tag)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_paradox_action(kind!(kind), tag!(tag), out))
      sym(DOS, out.read_int32)
    end

    # The Hexagram action an outcome amounts to (:applied -> :proceed,
    # :deferred -> :wait, :proposal_for_owner -> :escalate, ...).
    # @return [Symbol] a Hexagram::ACTIONS Symbol
    def outcome_action(outcome)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_paradox_outcome_action(enum!(OUTCOMES, outcome, "outcome"), out))
      sym(Hexagram::ACTIONS, out.read_int32)
    end

    # Classify +tag+ without a book (no registry lookup). See {Book#classify}.
    # @return [Classification]
    def classify(tag, evidence: nil, stale: false) = Book.classify_with(nil, tag, evidence, stale, nil)

    # The library's name of a kind, outcome or node state ("constructive",
    # "proposal-for-owner", "sandboxed"; "INVALID" for an unknown value).
    # @return [String]
    def kind_name(kind) = Native.lib.zxv_paradox_kind_name(enum!(KINDS, kind, "kind"))

    # @return [String]
    def outcome_name(outcome) = Native.lib.zxv_paradox_outcome_name(enum!(OUTCOMES, outcome, "outcome"))

    # @return [String]
    def node_state_name(state) = Native.lib.zxv_paradox_node_state_name(enum!(NODE_STATES, state, "state"))

    # @api private
    def tag!(tag) = tag.is_a?(Hexagram) ? tag.tag : Call.uint8!(tag, "tag")

    # @api private
    def kind!(kind) = enum!(KINDS, kind, "kind")

    # @api private
    def enum!(list, value, name)
      return Call.int32!(value, name) unless value.is_a?(Symbol)

      list.index(value) || raise(ZxvUsageError.new(Status::E_ARG, "unknown #{name} #{value.inspect}"))
    end

    # @api private
    def sym(list, value) = (value >= 0 && list[value]) || value

    # @api private
    def flags!(spec)
      case spec
      when Symbol then FLAGS.fetch(spec) { raise ZxvUsageError.new(Status::E_ARG, "unknown flag #{spec.inspect}") }
      when Array then spec.reduce(0) { |m, s| m | flags!(s) }
      else Call.uint32!(spec, "flags")
      end
    end

    # @api private
    def flag_names(map, bits) = map.select { |_, b| bits.anybits?(b) }.keys

    # @api private
    def id!(id, name = "id") = Call.mem(Call.bytes!(id, name, size: 32))

    # @api private: the (kind, tag) of a Classification or keywords
    def pair!(pair, kind, tag)
      if pair
        raise ZxvUsageError.new(Status::E_ARG, "pair must be a Zxv::Paradox::Classification") unless pair.is_a?(Classification)

        return [kind!(pair.kind), tag!(pair.tag)]
      end
      raise ZxvUsageError.new(Status::E_ARG, "give a Classification or both kind: and tag:") if kind.nil? || tag.nil?

      [kind!(kind), tag!(tag)]
    end

    # A RepairProof (REPAIR_BYTES), a frozen value: what containment found
    # and did, bound by a hash. Share #signature with peers so they can
    # recognise the trap; signing the proof is your job.
    class RepairProof
      # @return [String] the 160 bytes (frozen)
      attr_reader :bytes

      # @param bytes [String] REPAIR_BYTES
      def initialize(bytes)
        @bytes = Call.bytes!(bytes, "proof", size: REPAIR_BYTES).freeze
        freeze
      end

      # @return [Array<Symbol>] the checks that fired (TRIPS keys)
      def trips = Paradox.flag_names(TRIPS, u32(0))
      # @return [Integer]
      def steps = u32(4)
      # @return [Integer]
      def spent = u32(8)
      # @return [Integer] state fingerprints examined
      def trace_n = u32(12)
      # @return [Integer]
      def submitter = u32(16)
      # @return [Integer] state length
      def state_len = u32(20)
      # @return [String] SHA-256 of the state as found
      def state_hash_before = bytes.byteslice(24, 32)
      # @return [String] SHA-256 of the state after rollback
      def state_hash_after = bytes.byteslice(56, 32)
      # @return [String] the signature to share with peers
      def signature = bytes.byteslice(88, 32)
      # @return [String] the proof hash
      def proof_hash = bytes.byteslice(120, 32)
      # @return [Integer] the quarantine record's index
      def quarantine_index = bytes.byteslice(152, 8).unpack1("Q<")

      # Recompute the binding hash (integrity only; a signature over the
      # proof is yours to check).
      # @return [Boolean]
      def valid?
        rc = Native.lib.zxv_paradox_repair_verify(Call.mem(bytes))
        return true if rc == Status::OK
        return false if rc == Status::E_VERIFY

        Call.check(rc)
      end

      def ==(other) = other.is_a?(RepairProof) && other.bytes == bytes
      alias eql? ==
      def hash = [RepairProof, bytes].hash

      private

      def u32(off) = bytes.byteslice(off, 4).unpack1("L<")
    end

    # A draft classification of a frontier record (DRAFT_BYTES), a frozen
    # value: send it for review ({Book#review}).
    class Draft
      # @return [String] the 71 bytes (frozen)
      attr_reader :bytes

      # @param bytes [String] DRAFT_BYTES
      def initialize(bytes)
        @bytes = Call.bytes!(bytes, "draft", size: DRAFT_BYTES).freeze
        freeze
      end

      # @return [Integer] the frontier record index
      def record = bytes.byteslice(0, 4).unpack1("L<")
      # @return [String] the frontier record's hash (32 bytes)
      def record_hash = bytes.byteslice(4, 32)
      # @return [String] the record's signature (32 bytes)
      def signature = bytes.byteslice(36, 32)
      # @return [Symbol] the proposed kind
      def kind = Paradox.sym(KINDS, bytes.getbyte(68))
      # @return [Integer] the reason code
      def reason = bytes.getbyte(69)
      # @return [Array<Symbol>] FLAGS keys
      def flags = Paradox.flag_names(FLAGS, bytes.getbyte(70))

      def ==(other) = other.is_a?(Draft) && other.bytes == bytes
      alias eql? ==
      def hash = [Draft, bytes].hash
    end

    # The book: the operator log, the frontier log (special records, never
    # edited), the quarantine log (contained work) and the signature
    # registry. All three logs are append-only and SHA-256 hash-chained.
    # Belongs to its capability's context; internally serialized
    # (thread-safe).
    #
    #   book = Zxv::Paradox::Book.new(cap)            # cap needs :paradox
    #   c = book.classify(tag, evidence: [3, 1, 5, 1])
    #   book.constructive(policy, c, synth: [60, 20, 12], tension: 5)  # => :applied
    class Book < NativeHandle
      # What #destructive did: its +outcome+, the +state+ after the call
      # (restored from the checkpoint when contained) and the +proof+
      # (nil unless contained).
      Containment = Struct.new(:outcome, :state, :proof, keyword_init: true)

      # @return [Capability]
      attr_reader :capability

      # @api private
      def self.classify_with(book, tag, evidence, stale, sig)
        ev = nil
        unless evidence.nil?
          unless evidence.is_a?(Array) && evidence.size == 4
            raise ZxvUsageError.new(Status::E_ARG, "evidence must be [pos0, neg0, pos1, neg1]")
          end

          ev = FFI::MemoryPointer.new(:uint32, 4)
          ev.write_array_of_uint32(evidence.each_with_index.map { |n, i| Call.uint32!(n, "evidence[#{i}]") })
        end
        kind = Call.out(:int32)
        tag_out = Call.out(:uint8)
        idx = Call.out(:int32)
        Call.check(Native.lib.zxv_paradox_classify(book&.ptr, Paradox.tag!(tag), ev, Call.bool!(stale, "stale"),
                                                   sig && Paradox.id!(sig, "signature"), kind, tag_out, idx), book&.context)
        i = idx.read_int32
        Classification.new(kind: Paradox.sym(KINDS, kind.read_int32), tag: Hexagram.from_tag(tag_out.read_uint8),
                           registry_index: i.negative? ? nil : i)
      end

      # @param capability [Capability] needs :paradox
      # @param log_capacity [Integer] records per log, 1..LOG_MAX
      # @param registry_capacity [Integer] 1..REGISTRY_MAX
      # @raise [CapabilityDeniedError] without :paradox
      def initialize(capability, log_capacity: 1024, registry_capacity: 256)
        @capability = capability
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_paradox_create(capability.ptr, Call.uint32!(log_capacity, "log_capacity"),
                                                 Call.uint32!(registry_capacity, "registry_capacity"), out), capability.context)
        super(capability.context, out.read_pointer, :zxv_paradox_destroy)
      end

      # Classify a tag. Reads only: no capability.
      # @param tag [Hexagram, Integer]
      # @param evidence [Array(Integer, Integer, Integer, Integer), nil]
      #   [pos0, neg0, pos1, neg1], the evidence counts before and after; nil
      #   for no trend (polarity neutral)
      # @param stale [Boolean] the newest evidence is past your freshness
      #   limit (ignored without evidence)
      # @param signature [String, nil] 32-byte anomaly signature ({Paradox.signature});
      #   a registered one overrides the trend for Both and Neither
      # @return [Classification]
      def classify(tag, evidence: nil, stale: false, signature: nil) = Book.classify_with(self, tag, evidence, stale, signature)

      # Add a reviewed signature (a seed, or one a peer shared from its
      # RepairProof) to the registry. Logged.
      # @param kind [Symbol] :constructive, :destructive or :neutral
      # @param flags [Symbol, Array<Symbol>, Integer] FLAGS
      # @param owner_ok [Boolean] your assertion that the owner approved
      #   (required for :money or :device)
      # @return [Integer] the entry's index
      # @raise [OwnerApprovalRequiredError] flagged and not owner_ok
      # @raise [ZxvUsageError] ZXV_E_DUPLICATE (already registered)
      def registry_add(signature, kind:, reason: 0, flags: 0, owner_ok: false)
        out = Call.out(:uint32)
        check(Native.lib.zxv_paradox_registry_add(capability.ptr, ptr, Paradox.id!(signature, "signature"), Paradox.kind!(kind),
                                                  Call.uint8!(reason, "reason"), Paradox.flags!(flags),
                                                  Call.bool!(owner_ok, "owner_ok"), out))
        out.read_uint32
      end

      # @return [Integer, nil] the registry index of +signature+, or nil
      def registry_find(signature)
        out = Call.out(:uint32)
        rc = Native.lib.zxv_paradox_registry_find(ptr, Paradox.id!(signature, "signature"), out)
        return nil if rc == Status::E_NOT_FOUND

        check(rc)
        out.read_uint32
      end

      # @return [Integer] registry entries
      def registry_size
        out = Call.out(:uint32)
        check(Native.lib.zxv_paradox_registry_count(ptr, out))
        out.read_uint32
      end

      # @return [RegistryEntry] entry +index+
      def registry_entry(index)
        sig = FFI::MemoryPointer.new(:uint8, 32)
        kind = Call.out(:int32)
        reason = Call.out(:uint8)
        flags = Call.out(:uint8)
        src = Call.out(:uint64)
        check(Native.lib.zxv_paradox_registry_at(ptr, Call.uint32!(index, "index"), sig, kind, reason, flags, src))
        s = src.read_uint64
        RegistryEntry.new(signature: sig.read_bytes(32), kind: Paradox.sym(KINDS, kind.read_int32), reason: reason.read_uint8,
                          flags: Paradox.flag_names(FLAGS, flags.read_uint8), source: s == SRC_SEED ? nil : s)
      end

      # @return [Array<RegistryEntry>]
      def registry = Array.new(registry_size) { |i| registry_entry(i) }

      # @param which [Symbol] :ops, :frontier or :quarantine
      # @return [Integer] records in that log
      def log_count(which)
        out = Call.out(:uint32)
        check(Native.lib.zxv_paradox_log_count(ptr, log!(which), out))
        out.read_uint32
      end

      # @return [String] the chain head: the last record's hash (32 zeros when empty)
      def log_head(which)
        out = FFI::MemoryPointer.new(:uint8, 32)
        check(Native.lib.zxv_paradox_log_head(ptr, log!(which), out))
        out.read_bytes(32)
      end

      # Record +index+ of a log as its raw bytes (hash it yourself to
      # recompute the chain; see zxv_api.h for the layout).
      # @return [String]
      def log_record_bytes(which, index)
        Call.bytes_out(context, ptr, log!(which), Call.uint32!(index, "index"), hint: REC_BYTES) do |*a|
          Native.lib.zxv_paradox_log_record(*a)
        end
      end

      # Record +index+ of a log, decoded.
      # @return [LogRecord, QuarantineEntry] (QuarantineEntry for :quarantine)
      def log_record(which, index)
        b = log_record_bytes(which, index)
        if log!(which) == LOGS[:quarantine]
          seq, tag, reason, who = b.unpack("Q<CCL<")
          return QuarantineEntry.new(seq: seq, tag: Hexagram.from_tag(tag), reason: Paradox.flag_names(TRIPS, reason),
                                     submitter: who, subject: b.byteslice(14, 32), prev: b.byteslice(46, 32),
                                     hash: b.byteslice(78, 32), bytes: b)
        end
        seq, op, kind, tag, outcome, aux = b.unpack("Q<CCCCL<")
        LogRecord.new(seq: seq, op: op, kind: Paradox.sym(KINDS, kind), tag: Hexagram.from_tag(tag),
                      outcome: Paradox.sym(OUTCOMES, outcome), aux: aux, a: b.byteslice(16, 32), b: b.byteslice(48, 32),
                      prev: b.byteslice(80, 32), hash: b.byteslice(112, 32), bytes: b)
      end

      # Every record of a log, oldest first.
      # @return [Array<LogRecord>, Array<QuarantineEntry>]
      def log_records(which) = Array.new(log_count(which)) { |i| log_record(which, i) }

      # Recompute every link of a log.
      # @return [self]
      # @raise [ZxvFatal] ZXV_E_INVARIANT if a link no longer matches (the
      #   context halts: only a memory fault can alter a log)
      def verify_log(which)
        check(Native.lib.zxv_paradox_log_verify(ptr, log!(which)))
        self
      end

      # One evaluation of +policy+ and, if +synth+ is given, one proposed
      # update (your synthesis; this checks and applies it):
      # forbidden tag -> an armed update is restored, :rolled_back;
      # kind not :constructive -> :refused; armed and tension grew since it
      # was applied -> previous values, :rolled_back; no synth -> :kept;
      # a flagged policy (money/device) or any value outside its bounds ->
      # nothing changes, :proposal_for_owner; else applied and armed, :applied.
      # @param pair [Classification, nil] or give +kind:+ and +tag:+
      # @param synth [Array<Integer>, nil] one value per parameter
      # @param tension [Integer] your uint32 measure of the paradox (higher is worse)
      # @return [Symbol] the outcome
      def constructive(policy, pair = nil, tension:, synth: nil, kind: nil, tag: nil)
        k, t = Paradox.pair!(pair, kind, tag)
        pol = handle!(policy, Policy, "policy")
        n = pol.size
        s = synth && pol.send(:values_mem, synth, "synth")
        out = Call.out(:int32)
        check(Native.lib.zxv_paradox_constructive(capability.ptr, ptr, pol.ptr, k, t, s, n,
                                                  Call.uint32!(tension, "tension"), out))
        Paradox.sym(OUTCOMES, out.read_int32)
      end

      # Detect and contain. The checks: +steps+ >= +step_limit+ (0: no
      # limit), +spent+ >= +budget+ (0: none), a repeated value in +trace+
      # (state fingerprints in visit order), +invariant_ok+ false, and a
      # forbidden tag. When the kind is :destructive or any check fires, the
      # work is quarantined, +state+ is restored from +checkpoint+ and a
      # {RepairProof} is made (outcome :contained); otherwise :refused and
      # nothing changes. It contains; it never replies to or acts against
      # the submitter.
      # @param state [String] the work's state (not modified; see the result)
      # @param checkpoint [String] same length as +state+
      # @param pattern [String] the canonical description of the trap (its
      #   signature goes in the proof)
      # @return [Containment]
      # @raise [ZxvRefusedError] ZXV_E_CAPACITY (a log is full: halt)
      def destructive(pair = nil, state:, checkpoint:, kind: nil, tag: nil, steps: 0, step_limit: 0, spent: 0, budget: 0,
                      trace: [], invariant_ok: true, pattern: "", submitter: 0)
        k, t = Paradox.pair!(pair, kind, tag)
        st = Call.bytes!(state, "state")
        cp = Call.bytes!(checkpoint, "checkpoint")
        raise ZxvUsageError.new(Status::E_ARG, "state and checkpoint must have the same length") unless st.bytesize == cp.bytesize

        pat = Call.bytes!(pattern, "pattern")
        raise ZxvUsageError.new(Status::E_ARG, "trace must be an Array of Integers") unless trace.is_a?(Array)

        tr = nil
        unless trace.empty?
          tr = FFI::MemoryPointer.new(:uint64, trace.size)
          tr.write_array_of_uint64(trace.each_with_index.map { |v, i| Call.uint64!(v, "trace[#{i}]") })
        end
        smem = Call.mem0(st)
        out = Call.out(:int32)
        proof = FFI::MemoryPointer.new(:uint8, REPAIR_BYTES)
        check(Native.lib.zxv_paradox_destructive(capability.ptr, ptr, k, t, Call.uint32!(steps, "steps"),
                                                 Call.uint32!(step_limit, "step_limit"), Call.uint32!(spent, "spent"),
                                                 Call.uint32!(budget, "budget"), tr, Call.uint32!(trace.size, "trace size"),
                                                 Call.bool!(invariant_ok, "invariant_ok"), smem, Call.mem0(cp), st.bytesize,
                                                 Call.mem0(pat), pat.bytesize, Call.uint32!(submitter, "submitter"), out, proof))
        oc = Paradox.sym(OUTCOMES, out.read_int32)
        Containment.new(outcome: oc, state: smem ? smem.read_bytes(st.bytesize) : "".b,
                        proof: oc == :contained ? RepairProof.new(proof.read_bytes(REPAIR_BYTES)) : nil)
      end

      # Settle +hold+ on decisive evidence or on +owner_pick+ (a branch
      # index); otherwise keep every branch.
      # @return [Array(Symbol, Integer)] [outcome, settled branch or nil]:
      #   :settled, :deferred, :rolled_back (forbidden) or :refused
      def neutral(hold, pair = nil, owner_pick: nil, kind: nil, tag: nil)
        k, t = Paradox.pair!(pair, kind, tag)
        h = handle!(hold, Hold, "hold")
        pick = owner_pick.nil? ? -1 : Call.int!(owner_pick, "owner_pick", min: 0, max: 2**31 - 1)
        out = Call.out(:int32)
        settled = Call.out(:int32)
        check(Native.lib.zxv_paradox_neutral(capability.ptr, ptr, h.ptr, k, t, pick, out, settled))
        s = settled.read_int32
        [Paradox.sym(OUTCOMES, out.read_int32), s.negative? ? nil : s]
      end

      # Record an unclassified paradox: a permanent frontier record (never
      # edited or erased) and an operator record. The work stays sandboxed
      # (not run) until a reviewed classification exists.
      # @param signature [String] 32 bytes
      # @param subject [String] 32-byte digest of the work
      # @return [Array(Symbol, Integer)] [outcome, frontier index or nil]:
      #   :recorded, :refused (kind not :special) or :rolled_back (forbidden)
      def special(pair = nil, signature:, subject:, kind: nil, tag: nil)
        k, t = Paradox.pair!(pair, kind, tag)
        out = Call.out(:int32)
        rec = Call.out(:uint32)
        check(Native.lib.zxv_paradox_special(capability.ptr, ptr, k, t, Paradox.id!(signature, "signature"),
                                             Paradox.id!(subject, "subject"), out, rec))
        r = rec.read_uint32
        [Paradox.sym(OUTCOMES, out.read_int32), r == Call::UINT32_MAX ? nil : r]
      end

      # A draft classification of frontier record +record+, for review. Logged.
      # @param kind [Symbol] :constructive, :destructive or :neutral
      # @return [Draft]
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (no such record)
      def draft(record, kind:, reason: 0, flags: 0)
        out = FFI::MemoryPointer.new(:uint8, DRAFT_BYTES)
        check(Native.lib.zxv_paradox_draft(capability.ptr, ptr, Call.uint32!(record, "record"), Paradox.kind!(kind),
                                           Call.uint8!(reason, "reason"), Paradox.flags!(flags), out))
        Draft.new(out.read_bytes(DRAFT_BYTES))
      end

      # Review a draft (ours, or a peer's matching this book's frontier
      # record). A :money or :device draft needs +owner_ok+; others
      # +owner_ok+ or +consensus_ok+ (your quorum result). Logged.
      # @param draft [Draft, String]
      # @return [Symbol] :accepted (a new registry entry), :proposal_for_owner
      #   (not approved) or :refused (already registered, or registry full)
      # @raise [VerificationFailedError] the hash or signature does not match the record
      def review(draft, owner_ok: false, consensus_ok: false)
        d = draft.is_a?(Draft) ? draft.bytes : Call.bytes!(draft, "draft", size: DRAFT_BYTES)
        out = Call.out(:int32)
        check(Native.lib.zxv_paradox_review(capability.ptr, ptr, Call.mem(d), Call.bool!(owner_ok, "owner_ok"),
                                            Call.bool!(consensus_ok, "consensus_ok"), out))
        Paradox.sym(OUTCOMES, out.read_int32)
      end

      # One pass over +graph+, in the fixed order (destructive and forbidden
      # contained and their dependants tainted; special sandboxed; neutral
      # held; constructive applied where nothing blocks). Logged.
      # @return [Symbol] :resolved (every node final) or :pending
      # @raise [ZxvRefusedError] ZXV_E_PARADOX_STEP when the pass count
      #   reaches the step limit unresolved (logged; the graph stays
      #   readable and goes to the owner)
      def pass(graph)
        g = handle!(graph, Graph, "graph")
        out = Call.out(:int32)
        check(Native.lib.zxv_paradox_graph_pass(capability.ptr, ptr, g.ptr, out))
        Paradox.sym(OUTCOMES, out.read_int32)
      end

      def inspect = closed? ? "#<Zxv::Paradox::Book closed>" : "#<Zxv::Paradox::Book ops=#{log_count(:ops)}>"

      private

      def check(rc) = Call.check(rc, context)

      def log!(which)
        return Call.int32!(which, "which") unless which.is_a?(Symbol)

        LOGS.fetch(which) { raise ZxvUsageError.new(Status::E_ARG, "log must be :ops, :frontier or :quarantine") }
      end

      def handle!(obj, klass, name)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be a #{klass.name}") unless obj.is_a?(klass)

        obj
      end
    end

    # A constructive policy: n parameters (1..POLICY_MAX) with published
    # bounds lo[i] <= hi[i] that never change. The values are swarm
    # parameters (budget shares, routing weights, escrow timing), never
    # balances. A policy flagged :money or :device is never changed
    # automatically: every update becomes a proposal for the owner.
    # NOT thread-safe.
    class Policy < NativeHandle
      # @return [Integer] number of parameters
      attr_reader :size

      # @param capability [Capability] needs :paradox
      # @param lo [Array<Integer>] lower bounds (int64)
      # @param hi [Array<Integer>] upper bounds
      # @param initial [Array<Integer>] initial values, inside the bounds
      # @param flags [Symbol, Array<Symbol>, Integer] FLAGS
      # @raise [ZxvUsageError] ZXV_E_RANGE (lo > hi, or an initial value outside)
      def initialize(capability, lo:, hi:, initial:, flags: 0)
        unless [lo, hi, initial].all? { |a| a.is_a?(Array) } && lo.size == hi.size && hi.size == initial.size
          raise ZxvUsageError.new(Status::E_ARG, "lo, hi and initial must be Arrays of one length")
        end

        @size = lo.size
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_paradox_policy_create(capability.ptr, @size, values_mem(lo, "lo"), values_mem(hi, "hi"),
                                                        values_mem(initial, "initial"), Paradox.flags!(flags), out),
                   capability.context)
        super(capability.context, out.read_pointer, :zxv_paradox_policy_destroy)
      end

      # @return [Array<Integer>] the live values
      def values = read_values[0]

      # @return [Array<Integer>] the values before the last applied update
      def previous = read_values[1]

      # @return [Boolean] whether an applied update awaits evaluation
      def armed? = read_values[2]

      # @return [Integer] the tension the armed update was applied at
      def tension_at = read_values[3]

      # @return [Array(Array<Integer>, Array<Integer>)] [lo, hi]
      def bounds
        lo = FFI::MemoryPointer.new(:int64, size)
        hi = FFI::MemoryPointer.new(:int64, size)
        Call.check(Native.lib.zxv_paradox_policy_bounds(ptr, lo, hi, size, nil), context)
        [lo.read_array_of_int64(size), hi.read_array_of_int64(size)]
      end

      # @return [Array<Symbol>] FLAGS keys
      def flags
        f = Call.out(:uint32)
        Call.check(Native.lib.zxv_paradox_policy_bounds(ptr, nil, nil, size, f), context)
        Paradox.flag_names(FLAGS, f.read_uint32)
      end

      # A proposed update as bytes for the owner or for signing
      # (12 + 32 * size bytes, little-endian: n, how many values are out of
      # bounds, flags, then lo, hi, current, proposed per parameter). Reads only.
      # @return [String]
      def proposal(synth)
        s = values_mem(synth, "synth")
        Call.bytes_out(context, ptr, s, size, hint: 12 + 32 * size) { |*a| Native.lib.zxv_paradox_policy_proposal(*a) }
      end

      private

      def read_values
        v = FFI::MemoryPointer.new(:int64, size)
        p = FFI::MemoryPointer.new(:int64, size)
        armed = Call.out(:int32)
        at = Call.out(:uint32)
        Call.check(Native.lib.zxv_paradox_policy_values(ptr, v, p, size, armed, at), context)
        [v.read_array_of_int64(size), p.read_array_of_int64(size), armed.read_int32 != 0, at.read_uint32]
      end

      def values_mem(list, name)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be an Array of Integers") unless list.is_a?(Array)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must have #{@size} values") unless list.size == @size

        m = FFI::MemoryPointer.new(:int64, [@size, 1].max)
        m.write_array_of_int64(list.each_with_index.map { |x, i| Call.int!(x, "#{name}[#{i}]") })
        m
      end
    end

    # A neutral hold: up to +capacity+ candidate branches (32-byte ids).
    # A branch settles when its net evidence leads every other by at least
    # +decisive+, or alone reaches it, or the owner picks it. NOT thread-safe.
    class Hold < NativeHandle
      # One branch: its id and evidence counts.
      Branch = Struct.new(:id, :for, :against, keyword_init: true)

      # @param capability [Capability] needs :paradox
      # @param capacity [Integer] 1..HOLD_MAX
      # @param decisive [Integer] the lead that settles (0 means 1)
      def initialize(capability, capacity:, decisive: 0)
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_paradox_hold_create(capability.ptr, Call.uint32!(capacity, "capacity"),
                                                      Call.uint32!(decisive, "decisive"), out), capability.context)
        super(capability.context, out.read_pointer, :zxv_paradox_hold_destroy)
      end

      # Add a candidate (the same id again is the same candidate).
      # @return [self]
      # @raise [ZxvRefusedError] ZXV_E_STATE (settled), ZXV_E_CAPACITY
      def add(id)
        Call.check(Native.lib.zxv_paradox_hold_add(ptr, Paradox.id!(id)), context)
        self
      end

      # One piece of evidence for (+positive+ true) or against a candidate.
      # @return [self]
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (unknown id)
      # @raise [ZxvRefusedError] ZXV_E_STATE (settled)
      def evidence(id, positive:)
        Call.check(Native.lib.zxv_paradox_hold_evidence(ptr, Paradox.id!(id), Call.bool!(positive, "positive")), context)
        self
      end

      # @return [Integer] branch count
      def size = info[0]

      # @return [Integer, nil] the settled branch, or nil while deferred
      def settled = info[1]

      # @return [Integer, nil] the branch with decisive evidence now, or nil
      def decisive_branch = info[2]

      # @return [Branch] branch +index+
      def branch(index)
        id = FFI::MemoryPointer.new(:uint8, 32)
        pos = Call.out(:uint32)
        neg = Call.out(:uint32)
        Call.check(Native.lib.zxv_paradox_hold_branch(ptr, Call.uint32!(index, "index"), id, pos, neg), context)
        Branch.new(id: id.read_bytes(32), for: pos.read_uint32, against: neg.read_uint32)
      end

      # @return [Array<Branch>]
      def branches = Array.new(size) { |i| branch(i) }

      private

      def info
        n = Call.out(:uint32)
        s = Call.out(:int32)
        d = Call.out(:int32)
        Call.check(Native.lib.zxv_paradox_hold_info(ptr, n, s, d), context)
        [n.read_uint32, s.read_int32.negative? ? nil : s.read_int32, d.read_int32.negative? ? nil : d.read_int32]
      end
    end

    # A complex paradox: a dependency graph of up to GRAPH_MAX nodes that
    # escalates to the owner after +step_limit+ passes without resolving.
    # Node ids are 32-byte hashes of their signed input (the sort key:
    # insertion order never matters). NOT thread-safe.
    class Graph < NativeHandle
      # One node: id, current kind and state (NODE_STATES).
      Node = Struct.new(:id, :kind, :state, keyword_init: true)
      # Node count, passes run, net tension of the open nodes after the last
      # pass (+total_mag+, +total_dir+ -1/0/+1), whether it escalated, and
      # the SHA-256 chain over every pass.
      Info = Struct.new(:nodes, :passes, :total_mag, :total_dir, :escalated, :chain, keyword_init: true)

      # @param capability [Capability] needs :paradox
      # @param step_limit [Integer] >= 1
      def initialize(capability, step_limit:)
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_paradox_graph_create(capability.ptr, Call.uint32!(step_limit, "step_limit"), out),
                   capability.context)
        super(capability.context, out.read_pointer, :zxv_paradox_graph_destroy)
      end

      # Add a node before the first pass.
      # @param mag [Integer] tension magnitude (uint32)
      # @param dir [Integer] tension direction: -1, 0 or +1
      # @return [self]
      # @raise [ZxvUsageError] ZXV_E_DUPLICATE (id present)
      # @raise [ZxvRefusedError] ZXV_E_STATE (after the first pass), ZXV_E_CAPACITY
      def add_node(id, pair = nil, mag:, dir:, kind: nil, tag: nil)
        k, t = Paradox.pair!(pair, kind, tag)
        Call.check(Native.lib.zxv_paradox_graph_add_node(ptr, Paradox.id!(id), k, t, Call.uint32!(mag, "mag"),
                                                         Call.int32!(dir, "dir")), context)
        self
      end

      # Node +id+ depends on node +dep+ (which may be added later, but must
      # exist by the first pass).
      # @return [self]
      def add_dependency(id, dep)
        Call.check(Native.lib.zxv_paradox_graph_add_dep(ptr, Paradox.id!(id), Paradox.id!(dep, "dep")), context)
        self
      end

      # Queue new evidence for a node, applied at the start of the next
      # pass. Several in one batch merge "worst wins", so a batch gives the
      # same result in any order.
      # @return [self]
      def update(id, pair = nil, mag:, dir:, kind: nil, tag: nil)
        k, t = Paradox.pair!(pair, kind, tag)
        Call.check(Native.lib.zxv_paradox_graph_update(ptr, Paradox.id!(id), k, t, Call.uint32!(mag, "mag"),
                                                       Call.int32!(dir, "dir")), context)
        self
      end

      # @return [Symbol] the node's state (NODE_STATES; :open before the first pass)
      def state(id)
        out = Call.out(:int32)
        Call.check(Native.lib.zxv_paradox_graph_state(ptr, Paradox.id!(id), out), context)
        Paradox.sym(NODE_STATES, out.read_int32)
      end

      # @return [Node] node +index+ in id order (after the first pass)
      def node(index)
        id = FFI::MemoryPointer.new(:uint8, 32)
        kind = Call.out(:int32)
        st = Call.out(:int32)
        Call.check(Native.lib.zxv_paradox_graph_node_at(ptr, Call.uint32!(index, "index"), id, kind, st), context)
        Node.new(id: id.read_bytes(32), kind: Paradox.sym(KINDS, kind.read_int32), state: Paradox.sym(NODE_STATES, st.read_int32))
      end

      # @return [Array<Node>] every node in id order (after the first pass)
      def nodes = Array.new(info.nodes) { |i| node(i) }

      # @return [Info]
      def info
        n, p, m = Array.new(3) { Call.out(:uint32) }
        d = Call.out(:int32)
        e = Call.out(:int32)
        chain = FFI::MemoryPointer.new(:uint8, 32)
        Call.check(Native.lib.zxv_paradox_graph_info(ptr, n, p, m, d, e, chain), context)
        Info.new(nodes: n.read_uint32, passes: p.read_uint32, total_mag: m.read_uint32, total_dir: d.read_int32,
                 escalated: e.read_int32 != 0, chain: chain.read_bytes(32))
      end
    end
  end
end
