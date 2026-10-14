# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: the 0.08889% assurance fee and its split (zxv_fee_*).
  #
  #   fee(g) = floor(g * 8889 / 10,000,000)
  #
  # +g+ is the gross in the unit's own minor measure (money minor units,
  # compute token-cycles, bandwidth bytes, storage byte-hours), exact for
  # every g up to 2**64-1: fee(1124) = 0 and fee(1125) = 1. The fee is
  # charged ONCE per transaction, on its gross: never on a fee, never
  # compounded.
  #
  # CARRY. The sub-unit remainder of an account (0 <= carry < {DEN}) is
  # carried to its next charge, so many small charges pay exactly what one
  # charge of their sum pays. Store the carry per account:
  #
  #   fee, carry = Zxv::Fee.assure_with_carry(100, carry: carry)
  #
  # THE FOUR BUCKETS ({.split}): 50% reserve floor (backs Vino), 25% V-Bill
  # dividend pool, 15% infrastructure / node bounties, 10% regenerative
  # capital. The last three get floor(fee * pct / 100); the reserve floor gets
  # the rest, so the parts always sum to the fee exactly.
  #
  # Pure: no context, no capability. Honest limits: these are the
  # arithmetic rules only. How a fee, a reserve or a dividend pool is
  # treated for tax, securities or consumer law is a question for counsel;
  # nothing here is a licence or a certification.
  module Fee
    # Fee numerator: 0.08889% = 8889 / 10^7.
    NUM = 8889
    # Fee denominator, 10^7; a carry is always below this.
    DEN = 10_000_000
    # The four buckets, in ZXV_FEE_* order.
    BUCKETS = %i[reserve_floor vbill_dividend infra_bounty regen_capital].freeze
    # The unit kinds, in ZXV_FEE_UNIT_* order.
    UNITS = %i[money compute bandwidth storage].freeze

    # The default fee policy (zxv_fee_policy_default).
    # +percent+:: bucket shares in percent, by BUCKETS key (50, 25, 15, 10);
    # +allow_below+:: false: a contribution below the fee is raised to it;
    # +credit_rates+:: posted VFV store-credit rate per unit kind, as
    #                  [num, den] by UNITS key, for a voluntary contribution
    #                  above the fee (money 1/1, the others 0/1: the excess
    #                  earns no credit until a rate is posted);
    # +multiplier+:: the operator's credit multiplier [num, den], 1/1;
    # +commons_cap+:: the no-monopoly cap [num, den] on one recipient's
    #                 share of the non-money commons pool, 8/21.
    Policy = Struct.new(:percent, :allow_below, :credit_rates, :multiplier, :commons_cap, keyword_init: true)

    module_function

    # The fee on +gross+, with no carry.
    # @param gross [Integer] 0..2**64-1
    # @return [Integer]
    def assure(gross)
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_fee_assure(Call.uint64!(gross, "gross"), out))
      out.read_uint64
    end

    # The fee on +gross+ given the account's incoming carry; returns the
    # fee and the carry to store for the account's next charge.
    # @param carry [Integer] 0...DEN
    # @return [Array(Integer, Integer)] [fee, new carry]
    # @raise [ZxvUsageError] ZXV_E_RANGE (carry >= DEN: a corrupt carry)
    def assure_with_carry(gross, carry: 0)
      fee = Call.out(:uint64)
      rem = Call.out(:uint64)
      Call.check(Native.lib.zxv_fee_assure_carry(Call.uint64!(carry, "carry"), Call.uint64!(gross, "gross"), fee, rem))
      [fee.read_uint64, rem.read_uint64]
    end

    # Split +fee+ into the four buckets (the reserve floor takes every
    # remainder unit; the parts sum to +fee+ exactly).
    # @return [Hash{Symbol=>Integer}] keyed by BUCKETS
    def split(fee)
      out = FFI::MemoryPointer.new(:uint64, 4)
      Call.check(Native.lib.zxv_fee_split(Call.uint64!(fee, "fee"), out))
      BUCKETS.zip(out.read_array_of_uint64(4)).to_h
    end

    # @return [Policy] the default policy
    def default_policy
      pct = FFI::MemoryPointer.new(:uint32, 4)
      allow = Call.out(:int32)
      rn = FFI::MemoryPointer.new(:uint64, 4)
      rd = FFI::MemoryPointer.new(:uint64, 4)
      mn, md, cn, cd = Array.new(4) { Call.out(:uint64) }
      Call.check(Native.lib.zxv_fee_policy_default(pct, allow, rn, rd, mn, md, cn, cd))
      Policy.new(percent: BUCKETS.zip(pct.read_array_of_uint32(4)).to_h, allow_below: allow.read_int32 != 0,
                 credit_rates: UNITS.zip(rn.read_array_of_uint64(4).zip(rd.read_array_of_uint64(4))).to_h,
                 multiplier: [mn.read_uint64, md.read_uint64], commons_cap: [cn.read_uint64, cd.read_uint64])
    end
  end
end
