# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # An exact rational number: the kernel's +rat_t+ (orbital_compat IR
  # numeric normal form). Immutable; always reduced with a positive
  # denominator; numerator and denominator fit in int64. Arithmetic runs in
  # libzxv, so overflow raises ZxvUsageError (ZXV_E_RANGE), never wraps.
  #
  #   Zxv::Rational.of(1, 10) + Zxv::Rational.parse("0.2")  # => 0.3, exactly
  #
  # Note: inside +module Zxv+ the name +Rational+ means this class; Ruby's
  # own is +::Rational+. Convert with {#to_r} / {.from}.
  class Rational
    include Comparable

    # @return [Integer] numerator (sign carrier)
    attr_reader :numerator
    # @return [Integer] denominator (> 0)
    attr_reader :denominator

    class << self
      # Build from integers (reduced). +of(3, 4)+, +of(5)+.
      # @raise [FloatRejectedError] for a Float argument
      def of(numerator, denominator = 1)
        n = Call.int!(numerator, "numerator")
        d = Call.int!(denominator, "denominator")
        on = Call.out(:int64)
        od = Call.out(:int64)
        Call.check(Native.lib.zxv_rat_normalize(n, d, on, od))
        new_raw(on.read_int64, od.read_int64)
      end

      # Parse exact text: "12.75", "-0.1", "3/4", "42". No exponents, no
      # spaces, no "NaN".
      # @raise [ZxvUsageError] ZXV_E_FORMAT / ZXV_E_RANGE / ZXV_E_ARG
      def parse(text)
        s = Call.text!(text, "text")
        on = Call.out(:int64)
        od = Call.out(:int64)
        Call.check(Native.lib.zxv_rat_parse(s, on, od))
        new_raw(on.read_int64, od.read_int64)
      end

      # Convert an exact host number: Integer, ::Rational, BigDecimal (or
      # another Zxv::Rational). Floats are refused.
      def from(value)
        case value
        when Rational then value
        when Float then raise FloatRejectedError
        when Integer then of(value, 1)
        when ::Rational then of(value.numerator, value.denominator)
        else
          if defined?(BigDecimal) && value.is_a?(BigDecimal)
            raise ZxvUsageError.new(Status::E_ARG, "BigDecimal is not finite") unless value.finite?

            r = value.to_r
            of(r.numerator, r.denominator)
          else
            raise ZxvUsageError.new(Status::E_ARG, "cannot make a Zxv::Rational from #{value.class}")
          end
        end
      end

      # THE FLOAT BOUNDARY: the exact value of a binary64 float. 0.1 becomes
      # 3602879701896397/36028797018963968, not 1/10: the drift is visible.
      # @raise [FloatRejectedError] for NaN and infinities
      # @raise [ZxvUsageError] ZXV_E_RANGE if the exact value needs > 63 bits
      def from_double_exact(x)
        raise ZxvUsageError.new(Status::E_ARG, "from_double_exact takes a Float") unless x.is_a?(Float)

        from_ieee754_bits([x].pack("G").unpack1("Q>"))
      end

      # The same from raw IEEE-754 bits (an Integer 0..2**64-1).
      def from_ieee754_bits(bits)
        b = Call.uint64!(bits, "bits")
        on = Call.out(:int64)
        od = Call.out(:int64)
        Call.check(Native.lib.zxv_rat_from_ieee754(b, on, od))
        new_raw(on.read_int64, od.read_int64)
      end

      # Coerce an argument of an API that takes an exact amount.
      # @api private
      def coerce_arg(value)
        from(value)
      end

      private

      def new_raw(n, d)
        r = allocate
        r.send(:init_raw, n, d)
        r
      end
    end

    # @return [Rational] self + other
    def +(other) = binop(:zxv_rat_add, other)
    # @return [Rational] self - other
    def -(other) = binop(:zxv_rat_sub, other)
    # @return [Rational] self * other
    def *(other) = binop(:zxv_rat_mul, other)

    # @return [Rational] self / other
    # @raise [ZxvUsageError] ZXV_E_ARG on division by zero
    def /(other) = binop(:zxv_rat_div, other)

    # @return [Rational]
    def -@ = Rational.of(-numerator, denominator)

    # Exact comparison (no epsilon). Floats are refused.
    def <=>(other)
      o = other.is_a?(Float) ? (raise FloatRejectedError) : (Rational.from(other) rescue (return nil))
      c = Call.out(:int32)
      Call.check(Native.lib.zxv_rat_cmp(numerator, denominator, o.numerator, o.denominator, c))
      c.read_int32
    end

    # Equal value (1/2 == 2/4 because both are stored reduced).
    def ==(other)
      return false if other.is_a?(Float)

      o = other.is_a?(Rational) ? other : (Rational.from(other) rescue (return false))
      numerator == o.numerator && denominator == o.denominator
    end
    alias eql? ==

    def hash = [numerator, denominator].hash

    # @return [Boolean]
    def zero? = numerator.zero?
    # @return [Boolean]
    def negative? = numerator.negative?
    # @return [Boolean]
    def integer? = denominator == 1

    # Exact text: "12.75" when a terminating decimal exists, else "1/3".
    def to_s
      rc, s = Call.text(numerator, denominator) { |*a| Native.lib.zxv_rat_format(*a) }
      Call.check(rc)
      s
    end

    def inspect = "#<Zxv::Rational #{numerator}/#{denominator}>"

    # Ruby's exact ::Rational.
    def to_r = Kernel.Rational(numerator, denominator)

    # Q16.16 fixed point (round half away from zero), for engine parameters.
    # @raise [ZxvUsageError] ZXV_E_RANGE outside int32
    def to_q16
      q = Call.out(:int32)
      Call.check(Native.lib.zxv_rat_to_q16(numerator, denominator, q))
      q.read_int32
    end

    # There is deliberately no to_f.

    private

    def init_raw(n, d)
      @numerator = n
      @denominator = d
      freeze
    end

    def binop(fn, other)
      o = Rational.from(other)
      on = Call.out(:int64)
      od = Call.out(:int64)
      Call.check(Native.lib.public_send(fn, numerator, denominator, o.numerator, o.denominator, on, od))
      Rational.send(:new_raw, on.read_int64, od.read_int64)
    end
  end
end
