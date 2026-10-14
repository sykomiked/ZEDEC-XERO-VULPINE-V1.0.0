# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: a treaty, one batch of settlement work (zxv_treaty_*).
  #
  # A treaty says who takes part, what each party deposits and may claim
  # back, how the batch's dividend is shared (each party's weight
  # num/den), and who owes whom. It has two exact spellings that hold the
  # same treaty:
  #
  # record:: the fixed {REC_LEN}-byte COBOL copybook record (ZXVTRTY.cpy),
  #          plain ASCII DISPLAY text on every machine ({#to_record});
  # Sutra:: compact text ({#to_sutra}), for example
  #
  #   TREATY CORRIDOR-7 USD SETL
  #   PARTY ACME 5000 4000 1/2
  #   PARTY BETA 3000 3000 1/2
  #   OWE ACME BETA 1200
  #   END
  #
  # Each spelling is canonical: parsing and emitting gives the input back
  # byte for byte, and the record and Sutra forms of one treaty compare
  # equal (#==). Every parse and emit checks the rules R1 to R4 of
  # zxv_api.h (weights in lowest terms adding up to exactly 1, claim <=
  # deposit, amounts within PIC S9(18), 1..16 parties and 0..64
  # obligations). A refusal raises {TreatyError} (ZXV_E_TREATY, class
  # USAGE) whose +fault+ is a Symbol from {FAULTS} and +offset+ the byte
  # offset of the first fault.
  #
  #   t = Zxv::Treaty.parse(sutra_text)
  #   t.parties.first.deposit     # => 5000
  #   result = t.run              # a deterministic report, not a payment
  #   result.legs                 # netted legs
  #
  # Everything here is pure: no context and no capability. #run works in a
  # private scratch engine freed before it returns; it changes no ledger.
  #
  # Thread safety: a Treaty is NOT thread-safe (use it from one thread at
  # a time); a {Result} is immutable and may be read from any thread.
  #
  # Honest limits: a treaty is a batch description, not a contract in law;
  # signatures, consent and authority live elsewhere. One asset per treaty.
  # The dividend shares are reported only: nothing is paid out.
  class Treaty < PureHandle
    # Length of a treaty record (ZXV_TREATY_REC_LEN).
    REC_LEN = 2542
    # Length of a result record (ZXV_TREATY_RESULT_LEN).
    RESULT_LEN = 2422
    # Longest canonical Sutra text in bytes (ZXV_TREATY_SUTRA_MAX).
    SUTRA_MAX = 3843
    # Most parties in one treaty.
    MAX_PARTIES = 16
    # Most obligations in one treaty.
    MAX_OWES = 64
    # Longest treaty name (A-Z, 0-9 and '-', starting with a letter).
    NAME_MAX = 30
    # Longest party id (same alphabet).
    ID_MAX = 8
    # Largest magnitude of any amount: PIC S9(18).
    PIC18_MAX = 999_999_999_999_999_999
    # Largest weight numerator or denominator: PIC 9(9).
    WEIGHT_MAX = 999_999_999

    # The operation, by ZXV_TREATY_OP_* value: :net nets the obligations
    # only; :setl nets, posts the legs with the assurance fee and pays the
    # claims back.
    OPS = { 1 => :net, 2 => :setl }.freeze

    # Why a treaty was refused (ZXV_TREATY_FAULT_*), by value.
    FAULTS = {
      0 => :none, 2 => :magic, 3 => :syntax, 4 => :name, 5 => :asset, 6 => :op, 7 => :count, 8 => :number,
      9 => :party, 10 => :weight, 11 => :claim, 12 => :owe, 13 => :pad
    }.freeze

    # One party: its id, deposit and claim (minor units of the asset) and
    # its dividend weight +weight_num+/+weight_den+ (in lowest terms).
    Party = Struct.new(:id, :deposit, :claim, :weight_num, :weight_den, keyword_init: true)
    # One obligation: party index +from+ owes party index +to+ +amount+
    # (0-based indices, in treaty order).
    Obligation = Struct.new(:from, :to, :amount, keyword_init: true)

    class << self
      # Parse canonical Sutra text into a new Treaty.
      # @param text [String] at most {SUTRA_MAX} bytes of ASCII
      # @return [Treaty]
      # @raise [TreatyError] text outside the grammar or breaking a rule
      #   (+fault+ and +offset+ say where)
      # @raise [ZxvUsageError] ZXV_E_TOO_LONG, ZXV_E_UTF8
      def parse(text) = new.parse_sutra!(text)
      alias parse_sutra parse

      # Parse a copybook record of exactly {REC_LEN} bytes into a new Treaty.
      # @param bytes [String]
      # @return [Treaty]
      # @raise [TreatyError] (+fault+, +offset+ in the record)
      def from_record(bytes) = new.parse_record!(bytes)

      # @api private
      def fault_error(rc, fault, offset)
        err = Call.error_for(rc)
        if err.is_a?(TreatyError)
          f = fault.read_int32
          err.fault = FAULTS.fetch(f, f)
          err.offset = offset ? offset.read_uint32 : 0
        end
        err
      end
    end

    # A new, empty treaty: it fails validation until a parse fills it.
    def initialize
      super()
      create_handle(:zxv_treaty_destroy) { |o| Native.lib.zxv_treaty_create(o) }
    end

    # Replace this treaty's content with parsed Sutra text. On any failure
    # the treaty is unchanged.
    # @param text [String]
    # @return [self]
    # @raise [TreatyError]
    def parse_sutra!(text)
      t = Call.cstr!(text, "text")
      off = Call.out(:uint32)
      fault = Call.out(:int32)
      rc = Native.lib.zxv_treaty_parse_sutra(ptr, t, off, fault)
      raise Treaty.fault_error(rc, fault, off) unless rc == Status::OK

      self
    end

    # Replace this treaty's content with a parsed copybook record. On any
    # failure the treaty is unchanged.
    # @param bytes [String] exactly {REC_LEN} bytes
    # @return [self]
    # @raise [TreatyError]
    def parse_record!(bytes)
      b = Call.bytes!(bytes, "record", size: REC_LEN)
      off = Call.out(:uint32)
      fault = Call.out(:int32)
      rc = Native.lib.zxv_treaty_parse_record(ptr, Call.mem(b), b.bytesize, off, fault)
      raise Treaty.fault_error(rc, fault, off) unless rc == Status::OK

      self
    end

    # Empty it again, as Treaty.new leaves it.
    # @return [self]
    def clear
      Call.check(Native.lib.zxv_treaty_clear(ptr))
      self
    end

    # The canonical Sutra text.
    # @return [String]
    # @raise [TreatyError] if the treaty does not validate (an empty one, say)
    def to_sutra = Call.text_out(ptr) { |*a| Native.lib.zxv_treaty_emit_sutra(*a) }

    # The canonical copybook record ({REC_LEN} bytes of ASCII, no NUL).
    # @return [String] binary
    # @raise [TreatyError] if the treaty does not validate
    def to_record = Call.bytes_out(nil, ptr, hint: REC_LEN) { |*a| Native.lib.zxv_treaty_emit_record(*a) }

    # Check R1 to R4 and the spelling rules.
    # @return [self]
    # @raise [TreatyError] with +fault+ (offset 0)
    def validate
      fault = Call.out(:int32)
      rc = Native.lib.zxv_treaty_validate(ptr, fault)
      raise Treaty.fault_error(rc, fault, nil) unless rc == Status::OK

      self
    end

    # @return [Boolean] whether #validate would pass
    def valid?
      validate
      true
    rescue TreatyError
      false
    end

    # Whether +other+ holds the same treaty (every field, every slot). A
    # treaty parsed from Sutra text equals the same treaty parsed from its
    # record. Two empty treaties are equal.
    # @return [Boolean]
    def ==(other)
      return false unless other.is_a?(Treaty)

      out = Call.out(:int32)
      Call.check(Native.lib.zxv_treaty_equal(ptr, other.ptr, out))
      out.read_int32 == 1
    end

    # The treaty name ("" when empty).
    # @return [String]
    def name = Call.text_out(ptr) { |*a| Native.lib.zxv_treaty_name(*a) }

    # The asset: three capital letters ("" when empty).
    # @return [String]
    def asset = Call.text_out(ptr) { |*a| Native.lib.zxv_treaty_asset(*a) }

    # @return [Symbol, nil] :net or :setl (nil when empty)
    def op = OPS[header[0]]

    # @return [Integer] number of parties
    def party_count = header[1]

    # @return [Integer] number of obligations
    def obligation_count = header[2]

    # Party +index+ (0-based, in treaty order).
    # @return [Party]
    # @raise [ZxvUsageError] ZXV_E_NOT_FOUND past the last party
    def party(index)
      i = Call.uint32!(index, "index")
      id = Call.text_out(ptr, i) { |*a| Native.lib.zxv_treaty_party_id(*a) }
      dep = Call.out(:int64)
      claim = Call.out(:int64)
      num = Call.out(:uint32)
      den = Call.out(:uint32)
      Call.check(Native.lib.zxv_treaty_party(ptr, i, dep, claim, num, den))
      Party.new(id: id, deposit: dep.read_int64, claim: claim.read_int64, weight_num: num.read_uint32,
                weight_den: den.read_uint32)
    end

    # @return [Array<Party>] every party, in treaty order
    def parties = Array.new(party_count) { |i| party(i) }

    # Obligation +index+ (0-based).
    # @return [Obligation]
    # @raise [ZxvUsageError] ZXV_E_NOT_FOUND past the last obligation
    def obligation(index)
      from = Call.out(:uint32)
      to = Call.out(:uint32)
      amt = Call.out(:int64)
      Call.check(Native.lib.zxv_treaty_owe(ptr, Call.uint32!(index, "index"), from, to, amt))
      Obligation.new(from: from.read_uint32, to: to.read_uint32, amount: amt.read_int64)
    end

    # @return [Array<Obligation>]
    def obligations = Array.new(obligation_count) { |i| obligation(i) }

    # Run the treaty in a fresh private engine for its asset: fund each
    # deposit, net the obligations multilaterally (at most parties - 1
    # legs), and for :setl post the legs with the 0.08889% assurance fee
    # (all or nothing), pay each claim back out and report each party's
    # weight share of the fee's V-Bill dividend bucket. Deterministic.
    # Needs no capability and changes nothing outside the call.
    # @return [Result]
    # @raise [TreatyError] the treaty does not validate
    # @raise [InsufficientFundsError] a payer cannot cover its legs plus the
    #   fee, or a claim exceeds what is left
    # @raise [ZxvUsageError] ZXV_E_RANGE (a net position leaves PIC S9(18),
    #   or an amount exceeds the engine's 2^59 per-line limit)
    def run
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_treaty_run(ptr, out))
      Result.send(:wrap, out.read_pointer)
    end

    def inspect
      return "#<Zxv::Treaty closed>" if closed?

      valid? ? "#<Zxv::Treaty #{name} #{asset} #{op} parties=#{party_count} owes=#{obligation_count}>" : "#<Zxv::Treaty (invalid)>"
    end

    private

    def header
      op = Call.out(:int32)
      np = Call.out(:uint32)
      no = Call.out(:uint32)
      Call.check(Native.lib.zxv_treaty_header(ptr, op, np, no))
      [op.read_int32, np.read_uint32, no.read_uint32]
    end

    # What running a treaty returned (zxv_treaty_result). Immutable;
    # independent of the treaty it came from.
    class Result < PureHandle
      # One party's outcome: +net+ = what it is owed minus what it owes;
      # +balance+ = its deposit for :net, else what it holds after the legs,
      # the fee and its claim; +dividend+ = its reported dividend share (0
      # for :net). All Integers in minor units.
      PartyResult = Struct.new(:net, :balance, :dividend, keyword_init: true)
      # One netted leg: party +from+ pays party +to+ +amount+ (0-based).
      Leg = Struct.new(:from, :to, :amount, keyword_init: true)

      class << self
        private

        def wrap(raw) = allocate.send(:adopt, raw, :zxv_treaty_result_destroy)
      end

      # @return [Integer] number of parties
      def party_count = summary[0]

      # @return [Integer] number of netted legs
      def leg_count = summary[1]

      # @return [Integer] the total assurance fee charged (0 for :net)
      def fees = summary[2]

      # @return [PartyResult] party +index+ (treaty order)
      def party(index)
        net, bal, div = Array.new(3) { Call.out(:int64) }
        Call.check(Native.lib.zxv_treaty_result_party(ptr, Call.uint32!(index, "index"), net, bal, div))
        PartyResult.new(net: net.read_int64, balance: bal.read_int64, dividend: div.read_int64)
      end

      # @return [Array<PartyResult>]
      def parties = Array.new(party_count) { |i| party(i) }

      # @return [Leg] netted leg +index+
      def leg(index)
        from = Call.out(:uint32)
        to = Call.out(:uint32)
        amt = Call.out(:int64)
        Call.check(Native.lib.zxv_treaty_result_leg(ptr, Call.uint32!(index, "index"), from, to, amt))
        Leg.new(from: from.read_uint32, to: to.read_uint32, amount: amt.read_int64)
      end

      # @return [Array<Leg>]
      def legs = Array.new(leg_count) { |i| leg(i) }

      # The ZXVRSLT.cpy result record ({Treaty::RESULT_LEN} bytes; party
      # numbers in it are 1-based, as the copybook says).
      # @return [String] binary
      def to_record = Call.bytes_out(nil, ptr, hint: RESULT_LEN) { |*a| Native.lib.zxv_treaty_result_record(*a) }

      def inspect = closed? ? "#<Zxv::Treaty::Result closed>" : "#<Zxv::Treaty::Result legs=#{leg_count} fees=#{fees}>"

      private

      def summary
        np = Call.out(:uint32)
        nl = Call.out(:uint32)
        fees = Call.out(:int64)
        Call.check(Native.lib.zxv_treaty_result_summary(ptr, np, nl, fees))
        [np.read_uint32, nl.read_uint32, fees.read_int64]
      end
    end

    # PIC S9(18) arithmetic, as COBOL does it with ON SIZE ERROR: exact, on
    # Integers in -{PIC18_MAX}..{PIC18_MAX}. An operand or exact result
    # outside that range, or a division by zero, raises ZxvUsageError
    # (ZXV_E_RANGE: a size error).
    module Pic
      module_function

      # @return [Integer] a + b
      def add(a, b) = op(:zxv_treaty_pic_add, a, b)

      # @return [Integer] a - b
      def sub(a, b) = op(:zxv_treaty_pic_sub, a, b)

      # @return [Integer] a * b
      def mul(a, b) = op(:zxv_treaty_pic_mul, a, b)

      # DIVIDE a BY b GIVING q REMAINDER r: q truncates toward zero and r
      # has the dividend's sign (-7 / 2 = -3 rem -1; 7 / -2 = -3 rem 1).
      # Note this differs from Ruby's Integer#divmod, which floors.
      # @return [Array(Integer, Integer)] [q, r]
      def divide(a, b)
        q = Call.out(:int64)
        r = Call.out(:int64)
        Call.check(Native.lib.zxv_treaty_pic_div(Call.int!(a, "a"), Call.int!(b, "b"), q, r))
        [q.read_int64, r.read_int64]
      end

      def op(fn, a, b)
        out = Call.out(:int64)
        Call.check(Native.lib.public_send(fn, Call.int!(a, "a"), Call.int!(b, "b"), out))
        out.read_int64
      end
      private_class_method :op
    end
  end
end
