# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Status codes of zxv_api.h (stable; never renumbered).
  module Status
    OK = 0
    E_ARG = -1
    E_BUFFER = -2
    E_NOTIMPL = -3
    E_NOT_FOUND = -4
    E_CAPACITY = -5
    E_FUNDS = -6
    E_CURRENCY = -7
    E_RANGE = -8
    E_STATE = -9
    E_INVARIANT = -10
    E_UTF8 = -11
    E_TOO_LONG = -12
    E_DUPLICATE = -13
    E_NOMEM = -14
    E_DECLINED = -15
    E_FIELD = -16
    E_CAP = -17
    E_POISONED = -18
    E_BUDGET = -19
    E_RMAG_INVALID = -20
    E_VERIFY = -21
    E_FORMAT = -22
    E_IO = -23
    E_FLOAT = -24
    # ABI 1.4: hardware emulation and hardware updates.
    E_NOPROVIDER = -25  # no proven implementation may serve the call
    E_DISPUTE = -26     # a shadow check found two implementations disagree
    E_QUARANTINED = -27 # image failed in hardware: rolled back, logged
    E_ROLLBACK = -28    # image or contract not newer / below the floor
    E_BLOCKED = -29     # image already quarantined; never reloaded
    E_TARGET = -30      # image is for other hardware
    E_POLICY = -31      # refused by the function's placement policy
    # ABI 1.5: the newer kernel features.
    E_DENOM = -32         # not a Fibonacci ladder face (coins, token tiers); USAGE
    E_TREATY = -33        # a treaty fails validation; see TreatyError#fault; USAGE
    E_CID = -34           # content does not match its CID, or bad CID bytes; REFUSED
    E_CONSENT = -35       # no valid consent for this agent action; REFUSED
    E_ERASURE = -36       # too few freight rows to rebuild the packet; REFUSED
    E_PEG = -37           # no valid signed peg for this currency pair; REFUSED
    E_LIMIT = -38         # refused by a hub limit rule (not a fault); REFUSED
    E_PARADOX_STEP = -39  # a paradox graph reached its step limit: goes to the owner; REFUSED
    E_PARADOX_OWNER = -40 # needs the owner's approval (money or device policy); REFUSED
    # ABI 1.6: Vino Embed (-51 .. -59 are reserved for the vino group).
    E_RAIL = -46     # not the voucher's active rail (bearer/ghost); REFUSED
    E_COVERAGE = -47 # would take a play issuer below 1.8x coverage; REFUSED
    E_LANE = -48     # wrong lane for this call (play vs live); USAGE
    E_EXPIRED = -49  # the payment request expired, or was approved too late; REFUSED
    E_CHANCE = -50   # a paid chance outcome in a live payment request; REFUSED
    E_INTERNAL = -99

    # Status classes (zxv_status_class).
    CLASS_OK = 0
    CLASS_USAGE = 1
    CLASS_REFUSED = 2
    CLASS_FATAL = 3
    CLASS_SYSTEM = 4

    # Symbolic name of a status ("ZXV_E_FUNDS"), from the library.
    def self.name_of(status)
      Native.lib.zxv_status_name(status)
    end

    # ZXV_CLASS_* of a status, from the library (unknown negatives: SYSTEM).
    def self.class_of(status)
      Native.lib.zxv_status_class(status)
    end
  end

  # Mixed into every ZXV error that is not fatal, so
  # <tt>rescue Zxv::StatusError</tt> catches usage, refused and system
  # errors (and never {ZxvFatal}).
  module StatusError
    # @return [Integer] the negative ZXV_E_* status
    attr_reader :status
    # @return [String] the detail text from zxv_last_error ("" if none)
    attr_reader :detail

    # @return [String] the symbolic status name, e.g. "ZXV_E_FUNDS"
    def status_name
      Status.name_of(status)
    rescue StandardError
      "ZXV_E_UNKNOWN"
    end

    private

    def init_status(status, detail)
      @status = status
      @detail = detail.to_s
    end
  end

  # USAGE class: a caller bug. Fix the arguments; retrying fails again.
  # Subclass of ArgumentError, so it reads as one.
  class ZxvUsageError < ArgumentError
    include StatusError

    # @param status [Integer] @param detail [String]
    def initialize(status = Status::E_ARG, detail = "")
      init_status(status, detail)
      super("#{status_name} (#{status})#{detail.to_s.empty? ? '' : ": #{detail}"}")
    end
  end

  # A host float reached an API that only takes exact numbers. Use
  # {Zxv::Rational.from_double_exact} when you really mean the float's exact
  # binary value.
  class FloatRejectedError < ZxvUsageError
    def initialize(detail = "a Float is not exact; pass an Integer or Zxv::Rational, " \
                            "or call Zxv::Rational.from_double_exact(x) on purpose")
      super(Status::E_FLOAT, detail)
    end
  end

  # REFUSED class: a well-formed request the platform declined. Handle it;
  # a retry may succeed once the situation changes.
  class ZxvRefusedError < StandardError
    include StatusError

    # @param status [Integer] @param detail [String]
    def initialize(status, detail = "")
      init_status(status, detail)
      super("#{status_name} (#{status})#{detail.to_s.empty? ? '' : ": #{detail}"}")
    end
  end

  # The capability lacks a bit, is revoked, or its attestation is not TRUE.
  class CapabilityDeniedError < ZxvRefusedError; end

  # An rmag or swarm budget is exhausted. For swarm consumes, {#granted}
  # holds what was granted before the budget ran out.
  class BudgetExhaustedError < ZxvRefusedError
    # @return [Integer, nil] tokens granted by a partial swarm consume
    attr_accessor :granted
  end

  # A holder account would go below zero.
  class InsufficientFundsError < ZxvRefusedError; end

  # A signature or tag did not verify.
  class VerificationFailedError < ZxvRefusedError; end

  # The feature is not built into this library (ZXV_E_NOTIMPL), e.g. LoRA.
  # Note: this is Zxv::NotImplementedError, unrelated to Ruby's
  # ::NotImplementedError (a ScriptError).
  class NotImplementedError < ZxvRefusedError; end

  # ABI 1.5: the treaty failed validation (ZXV_E_TREATY, class USAGE).
  # {#fault} names the rule that failed and {#offset} the byte offset of the
  # first fault in the Sutra text or record (0 when not known, e.g. after
  # {Treaty#validate}).
  class TreatyError < ZxvUsageError
    # @return [Symbol] a key of {Treaty::FAULTS} (:syntax, :weight, ...), or
    #   the raw Integer for a code this SDK does not know
    attr_accessor :fault
    # @return [Integer] byte offset of the first fault (0 when not known)
    attr_accessor :offset
  end

  # ABI 1.5: a Web 4 action has no valid consent (ZXV_E_CONSENT): no token,
  # a mismatch, expired, a bad signature, or a token already used or
  # revoked. {#detail} says which (diagnostic text only).
  class ConsentRequiredError < ZxvRefusedError; end

  # ABI 1.5: a paradox registry entry, draft or policy touches money or
  # devices and needs the owner's approval (ZXV_E_PARADOX_OWNER).
  class OwnerApprovalRequiredError < ZxvRefusedError; end

  # ABI 1.6: a voucher was spent or switched on a rail that is not its
  # active one (ZXV_E_RAIL, class REFUSED). A voucher lives on exactly one
  # rail at a time (BEARER or GHOST), so it can never be spent on both.
  class WrongRailError < ZxvRefusedError; end

  # ABI 1.6: the play issuer would fall below 1.8x coverage
  # (ZXV_E_COVERAGE, class REFUSED): it may only issue, or withdraw
  # backing, while 10 * backing >= 18 * outstanding. Nothing changed.
  class CoverageError < ZxvRefusedError; end

  # ABI 1.6: the payment request has expired, or the receipt says it was
  # approved outside the request's window (ZXV_E_EXPIRED, class REFUSED).
  class ExpiredError < ZxvRefusedError; end

  # ABI 1.6: a LIVE payment request marked as having a paid chance outcome
  # (loot boxes, paid draws) is refused (ZXV_E_CHANCE, class REFUSED).
  class ChanceRefusedError < ZxvRefusedError; end

  # ABI 1.6: the wrong lane for the call, or a lane number that is neither
  # PLAY (1) nor LIVE (2) (ZXV_E_LANE, class USAGE: a caller bug).
  class LaneError < ZxvUsageError; end

  # ABI 1.6: {Vino::SpendMeter#admit} refused an amount that would pass the
  # device's daily cap (ZXV_E_LIMIT, class REFUSED). Nothing was counted.
  # {#left} is what may still be approved on that day.
  class DailyCapExceededError < ZxvRefusedError
    # @return [Integer, nil] what is left of the day's cap (nil if unknown)
    attr_accessor :left
  end

  # SYSTEM class: out of memory, I/O, internal: environmental.
  class ZxvSystemError < StandardError
    include StatusError

    # @param status [Integer] @param detail [String]
    def initialize(status, detail = "")
      init_status(status, detail)
      super("#{status_name} (#{status})#{detail.to_s.empty? ? '' : ": #{detail}"}")
    end
  end

  # FATAL class: the context halted after a broken invariant (or an
  # operator halt). The context refuses every further call; never retry.
  #
  # It derives from Exception, NOT StandardError, so a bare +rescue+ or
  # <tt>rescue => e</tt> does not swallow it. Only
  # <tt>rescue Zxv::ZxvFatal</tt> (or <tt>rescue Exception</tt>) does. Set
  # <tt>Zxv.fatal_policy = :abort</tt> to end the process instead.
  class ZxvFatal < Exception # rubocop:disable Lint/InheritException
    # @return [Integer] ZXV_E_POISONED or ZXV_E_INVARIANT
    attr_reader :status
    # @return [String] detail text from zxv_last_error
    attr_reader :detail

    def initialize(status, detail = "")
      @status = status
      @detail = detail.to_s
      super("#{status_name} (#{status}): context halted#{@detail.empty? ? '' : ": #{@detail}"}; never retry")
    end

    # @return [String]
    def status_name
      Status.name_of(status)
    rescue StandardError
      "ZXV_E_UNKNOWN"
    end
  end

  # Raised by {Future#value} after {Future#cancel}.
  class CancelledError < StandardError; end

  # Raised when a {Future} times out.
  class TimeoutError < StandardError; end

  # Raised by Truth's +!+ and the require_* helpers: a Truth has no implicit
  # boolean meaning.
  class TruthConversionError < TypeError; end
end
