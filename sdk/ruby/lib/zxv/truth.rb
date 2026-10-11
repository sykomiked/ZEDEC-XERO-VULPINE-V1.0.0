# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Four-valued (Belnap / K3 paraconsistent) logic: the kernel's
  # +lpres_state_t+. Exactly four values, numbered as the kernel numbers
  # them: NEITHER = 0, TRUE = 1, FALSE = 2, BOTH = 3. BOTH is a held
  # contradiction, not an error and not nil.
  #
  # == No truthiness shortcut
  #
  # Ruby makes EVERY object except +nil+ and +false+ truthy, and that cannot
  # be overridden: <tt>if truth</tt> is ALWAYS taken, even for
  # Truth::FALSE. Never write it. Use an explicit policy instead:
  #
  #   truth.certainly_true?                      # TRUE only
  #   truth.to_boolean(on_both: false, on_neither: false)
  #   truth.require_true!                         # raises unless TRUE
  #
  # As a guard, +!truth+ and +not truth+ raise TruthConversionError.
  #
  # Logic: +truth.and(o)+, +truth.or(o)+, +truth.not+ (or +&+, +|+, +~+),
  # computed by libzxv's tables.
  #
  # Instances are frozen value objects. The kernel is adding an orthogonal
  # 2-bit +mode+ beside the four states; {#mode} exists now (always 0) so
  # code that reads it keeps working.
  class Truth
    # @return [Integer] 0..3
    attr_reader :value
    # @return [Integer] reserved, always 0 in ABI 1.1
    attr_reader :mode

    NAMES = %w[NEITHER TRUE FALSE BOTH].freeze
    private_constant :NAMES

    def initialize(value, mode = 0)
      @value = value
      @mode = mode
      freeze
    end
    private_class_method :new

    NEITHER = new(0)
    TRUE = new(1)
    FALSE = new(2)
    BOTH = new(3)
    ALL = [NEITHER, TRUE, FALSE, BOTH].freeze

    # The Truth for a kernel value 0..3.
    # @raise [ZxvUsageError] for anything else
    def self.of(value)
      Call.int!(value, "truth value", min: 0, max: 3)
      ALL[value]
    end

    # From evidence counts: both -> BOTH, only positive -> TRUE, only
    # negative -> FALSE, none -> NEITHER.
    def self.from_evidence(positive, negative)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_lp_from_evidence(Call.uint32!(positive, "positive"),
                                                 Call.uint32!(negative, "negative"), out))
      of(out.read_int32)
    end

    # @return [String] "NEITHER", "TRUE", "FALSE" or "BOTH"
    def name = NAMES[value]
    def to_s = name
    def inspect = "Zxv::Truth::#{name}"
    def to_i = value

    # Four-valued conjunction.
    def and(other) = Truth.of(lp2(:zxv_lp_and, other))
    # Four-valued disjunction.
    def or(other) = Truth.of(lp2(:zxv_lp_or, other))

    # Four-valued negation (TRUE <-> FALSE; BOTH and NEITHER stay).
    def not
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_lp_not(value, out))
      Truth.of(out.read_int32)
    end

    alias & and
    alias | or
    alias ~ not

    # Refuses: a Truth has no boolean meaning without a policy.
    # @raise [TruthConversionError]
    def !
      raise TruthConversionError,
            "Zxv::Truth has no boolean value; use certainly_true?, to_boolean(on_both:, on_neither:) or require_true!"
    end

    # @return [Boolean] true only for TRUE
    def certainly_true? = equal?(TRUE)
    # @return [Boolean] true only for FALSE
    def certainly_false? = equal?(FALSE)
    # @return [Boolean] TRUE or FALSE (no contradiction, no gap)
    def certain? = equal?(TRUE) || equal?(FALSE)
    # @return [Boolean]
    def both? = equal?(BOTH)
    # @return [Boolean]
    def neither? = equal?(NEITHER)

    # Collapse to a boolean with an explicit policy for the two non-classical values.
    def to_boolean(on_both:, on_neither:)
      case value
      when 1 then true
      when 2 then false
      when 3 then on_both
      else on_neither
      end
    end

    # @return [self] if TRUE
    # @raise [TruthConversionError] otherwise
    def require_true!
      return self if certainly_true?

      raise TruthConversionError, "expected TRUE, got #{name}"
    end

    def ==(other) = other.is_a?(Truth) && other.value == value && other.mode == mode
    alias eql? ==
    def hash = [Truth, value, mode].hash

    private

    def lp2(fn, other)
      raise ZxvUsageError.new(Status::E_ARG, "expected a Zxv::Truth, got #{other.class}") unless other.is_a?(Truth)

      out = Call.out(:int32)
      Call.check(Native.lib.public_send(fn, value, other.value, out))
      out.read_int32
    end
  end
end
