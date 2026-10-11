# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # The 64-state hexagram tag of ABI 1.3 (the kernel's +lpres_hex+): a
  # frozen value tagging one unit of work with a four-valued base, the
  # polarity of its evidence and the phase of its lifecycle, packed as
  # <tt>tag = base << 4 | polarity << 2 | phase</tt> (0..63).
  #
  #   h = Zxv::Hexagram.of(:true, :negentropic, :potential)
  #   h.action                 # => :proceed
  #   h = h.advance            # phase :emerging
  #   h.step(:dissolving)      # ZxvRefusedError (ZXV_E_STATE): a jump
  #   h.forbid.action          # => :rollback (and it stays forbidden)
  #
  # The fields are Symbols:
  # base:: +:false+, +:neither+, +:both+, +:true+ (0..3, by line pattern)
  # polarity:: +:entropic+, +:neutral+, +:forbidden+, +:negentropic+
  # phase:: +:potential+, +:emerging+, +:dissolving+, +:manifest+; the
  #         cycle is potential -> emerging -> manifest -> dissolving -> potential
  # action:: +:proceed+, +:wait+, +:refuse+, +:escalate+, +:rollback+
  #
  # The base is numbered differently from {Truth} (TRUE is base 3 but
  # Truth value 1): convert only with {#to_truth} and {.from_truth}.
  # Like Truth, a Hexagram has no boolean meaning: +!h+ raises
  # TruthConversionError. Every rule is computed by libzxv.
  class Hexagram
    BASES = %i[false neither both true].freeze
    POLARITIES = %i[entropic neutral forbidden negentropic].freeze
    PHASES = %i[potential emerging dissolving manifest].freeze
    ACTIONS = %i[proceed wait refuse escalate rollback].freeze

    # @return [Integer] 0..63
    attr_reader :tag

    def initialize(tag)
      @tag = tag
      freeze
    end
    private_class_method :new

    @all = nil
    @lock = Mutex.new

    class << self
      # All 64 tags, in tag order (built on first use).
      # @return [Array<Hexagram>]
      def all
        @all || @lock.synchronize { @all ||= Array.new(64) { |t| new(t) }.freeze }
      end

      # The tag with this byte value.
      # @raise [ZxvUsageError] (ZXV_E_ARG) outside 0..63
      def from_tag(tag)
        Call.int!(tag, "tag", min: 0, max: 255)
        Call.check(Native.lib.zxv_hex_unpack(tag, nil, nil, nil))
        all[tag]
      end

      # The tag from its three fields (Symbols).
      def of(base, polarity, phase)
        all[out8(:zxv_hex_pack, field(BASES, base, "base"), field(POLARITIES, polarity, "polarity"),
                 field(PHASES, phase, "phase"))]
      end

      # A tag whose base is +truth+ (mapped by zxv_hex_from_lp).
      def from_truth(truth, polarity, phase)
        raise ZxvUsageError.new(Status::E_ARG, "expected a Zxv::Truth, got #{truth.class}") unless truth.is_a?(Truth)

        of(BASES[out8(:zxv_hex_from_lp, truth.value)], polarity, phase)
      end

      # Polarity from evidence counts before (+pos0+, +neg0+) and after
      # (+pos1+, +neg1+): +stale+ gives :entropic; otherwise the margin
      # |pos - neg| growing gives :negentropic, shrinking :entropic, and
      # unchanged :neutral. Never :forbidden.
      # @return [Symbol]
      def polarity_from_evidence(pos0, neg0, pos1, neg1, stale:)
        raise ZxvUsageError.new(Status::E_ARG, "stale must be true or false") unless [true, false].include?(stale)

        POLARITIES[out8(:zxv_hex_polarity, Call.uint32!(pos0, "pos0"), Call.uint32!(neg0, "neg0"),
                        Call.uint32!(pos1, "pos1"), Call.uint32!(neg1, "neg1"), stale ? 1 : 0)]
      end

      # The phase after +phase+ on the cycle.
      # @return [Symbol]
      def next_phase(phase) = PHASES[out8(:zxv_hex_phase_next, field(PHASES, phase, "phase"))]

      # @api private
      def out8(fn, *args)
        out = Call.out(:uint8)
        Call.check(Native.lib.public_send(fn, *args, out))
        out.read_uint8
      end

      # @api private
      def field(names, value, what)
        i = names.index(value)
        return i if i

        raise ZxvUsageError.new(Status::E_ARG, "#{what} must be one of #{names.map(&:inspect).join(', ')}, got #{value.inspect}")
      end
    end

    # @return [Symbol]
    def base = BASES[tag >> 4]
    # @return [Symbol]
    def polarity = POLARITIES[(tag >> 2) & 3]
    # @return [Symbol]
    def phase = PHASES[tag & 3]
    # @return [Boolean] whether the polarity is :forbidden (absorbing)
    def forbidden? = polarity == :forbidden

    # What happens next: one lookup in the kernel's 64-entry table.
    # @return [Symbol]
    def action
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_hex_action(tag, out))
      ACTIONS[out.read_int32]
    end

    # "base/polarity/phase", e.g. "true/negentropic/manifest".
    def name = Native.lib.zxv_hex_name(tag)
    def to_s = name
    def inspect = "#<Zxv::Hexagram #{name} tag=#{tag}>"

    # The base as a {Truth}: the explicit conversion.
    def to_truth
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_hex_to_lp(tag >> 4, out))
      Truth.of(out.read_int32)
    end

    # The same work in +phase+: the same phase or the next one only.
    # @raise [ZxvRefusedError] (ZXV_E_STATE) for a jump
    def step(phase) = Hexagram.all[Hexagram.out8(:zxv_hex_step, tag, Hexagram.field(PHASES, phase, "phase"))]

    # One step along the cycle.
    def advance = step(Hexagram.next_phase(phase))

    # New base and polarity, same phase.
    # @raise [ZxvRefusedError] (ZXV_E_STATE) if this tag is forbidden and
    #   +polarity+ is not :forbidden (forbidden is absorbing)
    def with_state(base:, polarity:)
      Hexagram.all[Hexagram.out8(:zxv_hex_set, tag, Hexagram.field(BASES, base, "base"),
                                 Hexagram.field(POLARITIES, polarity, "polarity"))]
    end

    # The same tag with polarity :forbidden.
    def forbid = Hexagram.all[Hexagram.out8(:zxv_hex_forbid, tag)]

    # Conjunction: four-valued AND on the base, worst polarity, least advanced phase.
    def and(other) = Hexagram.all[Hexagram.out8(:zxv_hex_and, tag, other!(other).tag)]
    # Disjunction: four-valued OR on the base, worst polarity, least advanced phase.
    def or(other) = Hexagram.all[Hexagram.out8(:zxv_hex_or, tag, other!(other).tag)]
    # Negation of the base; polarity and phase are kept.
    def not = Hexagram.all[Hexagram.out8(:zxv_hex_not, tag)]

    alias & and
    alias | or
    alias ~ not

    # Refuses: a Hexagram has no boolean meaning.
    # @raise [TruthConversionError]
    def !
      raise TruthConversionError, "Zxv::Hexagram has no boolean value; use #action or #to_truth"
    end

    def ==(other) = other.is_a?(Hexagram) && other.tag == tag
    alias eql? ==
    def hash = [Hexagram, tag].hash

    private

    def other!(other)
      return other if other.is_a?(Hexagram)

      raise ZxvUsageError.new(Status::E_ARG, "expected a Zxv::Hexagram, got #{other.class}")
    end
  end

  # The quarantine log of ABI 1.3 (zxv_hexlog): an append-only, SHA-256
  # hash-chained record of forbidden work, read for audit and never
  # replayed. Creating and appending need the :attest capability.
  #
  # Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
  # reason || submitter (u32 LE) || subject || prev), so the chain can be
  # recomputed from the records. A full log refuses the append with
  # ZXV_E_CAPACITY (ZxvRefusedError); the caller must then halt the context
  # (Context#halt), since forbidden work it cannot record must not
  # continue. Signing #head is the caller's job.
  #
  #   log = Zxv::QuarantineLog.new(cap, capacity: 1024)
  #   seq = log.append(h.forbid, reason: 7, submitter: 42, subject: digest32)
  #   log.verify
  class QuarantineLog < NativeHandle
    include Enumerable

    # Largest capacity (ZXV_HEXLOG_MAX_RECORDS).
    MAX_RECORDS = 65_536

    # One record; +subject+, +prev+ and +hash+ are 32-byte binary Strings.
    Record = Struct.new(:seq, :tag, :reason, :submitter, :subject, :prev, :hash, keyword_init: true)

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :attest
    # @param capacity [Integer] 1..MAX_RECORDS
    def initialize(capability, capacity:)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_hexlog_create(capability.ptr, Call.uint32!(capacity, "capacity"), out), capability.context)
      super(capability.context, out.read_pointer, :zxv_hexlog_destroy)
    end

    # Quarantine forbidden work.
    # @param tag [Hexagram] a forbidden tag (any other is ZXV_E_ARG)
    # @param reason [Integer] 0..255
    # @param submitter [Integer] 0..2**32-1
    # @param subject [String] a 32-byte digest of the work
    # @return [Integer] the record's sequence number
    def append(tag, reason:, submitter:, subject:)
      raise ZxvUsageError.new(Status::E_ARG, "tag must be a Zxv::Hexagram") unless tag.is_a?(Hexagram)

      seq = Call.out(:uint64)
      Call.check(Native.lib.zxv_hexlog_append(capability.ptr, ptr, tag.tag, Call.int!(reason, "reason", min: 0, max: 255),
                                              Call.uint32!(submitter, "submitter"),
                                              Call.mem(Call.bytes!(subject, "subject", size: 32)), seq), context)
      seq.read_uint64
    end

    # @return [Integer] number of records
    def count(*args, &blk)
      return super if !args.empty? || blk

      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_hexlog_count(ptr, out), context)
      out.read_uint32
    end
    alias size count
    alias length count

    # The chain head: the last record's hash (32 zero bytes when empty).
    # @return [String]
    def head
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.zxv_hexlog_head(ptr, out), context)
      out.read_bytes(32)
    end

    # Record +index+, oldest first.
    # @return [Record]
    def [](index)
      seq = Call.out(:uint64)
      tag = Call.out(:uint8)
      reason = Call.out(:uint8)
      who = Call.out(:uint32)
      subj, prev, hash = Array.new(3) { FFI::MemoryPointer.new(:uint8, 32) }
      Call.check(Native.lib.zxv_hexlog_at(ptr, Call.uint32!(index, "index"), seq, tag, reason, who, subj, prev, hash), context)
      Record.new(seq: seq.read_uint64, tag: Hexagram.from_tag(tag.read_uint8), reason: reason.read_uint8,
                 submitter: who.read_uint32, subject: subj.read_bytes(32), prev: prev.read_bytes(32), hash: hash.read_bytes(32))
    end
    alias record []

    # Yield each record, oldest first.
    def each
      return enum_for(:each) { count } unless block_given?

      count.times { |i| yield self[i] }
      self
    end

    # Recompute every link.
    # @return [self]
    # @raise [ZxvFatal] (ZXV_E_INVARIANT) if the chain is broken; the context halts
    def verify
      Call.check(Native.lib.zxv_hexlog_verify(ptr), context)
      self
    end

    def inspect = closed? ? "#<Zxv::QuarantineLog closed>" : "#<Zxv::QuarantineLog #{count} records>"
  end
end
