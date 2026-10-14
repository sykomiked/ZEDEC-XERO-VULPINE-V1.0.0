# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: the V-Bill hub (zxv_vhub_*), a DEMONSTRATION of cross-currency
  # clearing through V-Bills, with test currencies only.
  #
  # Each test currency has ONE exact peg against the V-Bill, set only by
  # its issuer with an ML-DSA-87 signature. A swap from currency A to
  # currency B goes A -> V-Bill -> B as ONE balanced, all-or-nothing
  # posting on the hub's own in-process ledger:
  #
  #   sender -a, A pool +a;  A V-Bill float -v, B V-Bill float +v;
  #   B pool -b, receiver +b;   v = a*denA/numA,  b = v*numB/denB
  #
  # EXACT. A peg is num/den: num minor units per den V-Bills. Both
  # divisions must be exact or the swap is refused (ZXV_E_RANGE); nothing
  # is rounded and no rate is ever a float. #quote gives the largest exact
  # amount not above a requested one.
  #
  # RULES. Every rule of the sending AND the receiving currency must allow
  # a swap, and a currency with no rule refuses every swap through it
  # (fail closed, ZXV_E_STATE): add a limit (#add_limit) to each currency
  # before swapping. A limit refusal is {ZxvRefusedError} ZXV_E_LIMIT.
  #
  # CAPABILITIES. Changing anything needs :ledger_post; #quote, #balance,
  # #currency, #balances and #check! need :ledger_read. The hub uses the capability it
  # was created with, which must belong to the hub's context.
  #
  #   hub = Zxv::VHub.new(cap)
  #   hub.add_currency(0, code: "ZTA", minor_units: 2, issuer_public_key: pk)
  #   msg = Zxv::VHub.peg_message(0, num: 350, den: 1, epoch: 1)
  #   hub.set_peg(0, num: 350, den: 1, epoch: 1,
  #               signature: Zxv::MlDsa87.new(cap).sign(sk, msg, deterministic: true))
  #
  # Thread-safe (internally serialized). Destroy (#close) before the context.
  #
  # Honest limits: test currencies only (ISO 4217 codes are refused) and
  # no real money: the ledger lives in process memory, separate from the
  # context's ledger, and is lost on close. No network: designed to support
  # settlement rails such as PAPSS but connected to none, nor to any
  # central bank or issuer. Peg signatures use ML-DSA-87 (not FIPS 140-3
  # validated). V-Bills are likely securities in many jurisdictions; real
  # use needs legal review and a participating issuer. A rule reports what
  # it decided; it is not itself a legal check.
  class VHub < NativeHandle
    # Currency slots 0..7.
    MAX_CCY = 8
    # Limit rules per currency.
    MAX_RULES = 4
    # Senders one limit tracks per epoch (a 17th is refused).
    LIMIT_SENDERS = 16
    # Size of a peg message (#peg_message).
    PEG_MSG_BYTES = 40
    # Longest test currency code.
    CODE_MAX = 12
    # Largest single amount (2**59); balances stay below 2**62.
    AMOUNT_MAX = 1 << 59
    # The V-Bill issuer's account: always account 0.
    VBILL_ISSUER = 0
    # ML-DSA-87 public key size (an issuer key).
    ISSUER_KEY_BYTES = 2592
    # ML-DSA-87 signature size (a peg signature).
    SIGNATURE_BYTES = 4627

    # A currency's accounts and current peg (num = den = epoch = 0 while unset).
    CurrencyInfo = Struct.new(:issuer_account, :pool_account, :vfloat_account, :peg_num, :peg_den, :peg_epoch,
                              keyword_init: true) do
      # @return [Boolean] whether the peg has been set
      def pegged? = peg_den != 0
    end
    # One account's rails: DEBIT (held), CREDIT (owed, issuers only) and
    # EQUITY = DEBIT - CREDIT (may be negative).
    Balance = Struct.new(:debit, :credit, :equity, keyword_init: true)
    # A quote: the largest exact +amount+ of the source currency not above
    # the request (0 if none), the +vbills+ it would carry and the units
    # +received+ in the target currency.
    Quote = Struct.new(:amount, :vbills, :received, keyword_init: true)
    # A completed swap: the V-Bills moved and the units received.
    Swap = Struct.new(:vbills, :received, keyword_init: true)

    # @return [Capability]
    attr_reader :capability

    # The exact 40 bytes an issuer signs to set a peg: "zxv/vhub-peg/v1"
    # || slot || num || den || epoch (each u64 little-endian). Sign them
    # with ML-DSA-87 and an empty context ({MlDsa87#sign}).
    # @return [String] 40 bytes
    def self.peg_message(slot, num:, den:, epoch:)
      out = FFI::MemoryPointer.new(:uint8, PEG_MSG_BYTES)
      Call.check(Native.lib.zxv_vhub_peg_msg(Call.uint8!(slot, "slot"), Call.uint64!(num, "num"), Call.uint64!(den, "den"),
                                             Call.uint64!(epoch, "epoch"), out))
      out.read_bytes(PEG_MSG_BYTES)
    end

    # A new hub on the capability's context. Its ledger starts with the
    # V-Bill asset and the V-Bill issuer's account; the rule epoch is 0.
    # @param capability [Capability] needs :ledger_post (and :ledger_read for reads)
    # @param seed [String, nil] 32 bytes from which every posting's
    #   idempotency key and UETR are derived; nil draws a random seed
    # @raise [CapabilityDeniedError] without :ledger_post
    def initialize(capability, seed: nil)
      @capability = capability
      s = seed && Call.mem(Call.bytes!(seed, "seed", size: 32))
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_vhub_create(capability.ptr, s, out), capability.context)
      super(capability.context, out.read_pointer, :zxv_vhub_destroy)
    end

    # Register test currency +slot+, opening its issuer, pool and V-Bill
    # float accounts and funding the pool and float. The peg starts unset.
    # A failure registers nothing.
    # @param slot [Integer] 0..7
    # @param code [String] 3..12 of A-Z and 0-9; not ISO 4217, "VBL" or "VFV"
    # @param minor_units [Integer] 0..4
    # @param issuer_public_key [String] the issuer's ML-DSA-87 public key
    #   ({ISSUER_KEY_BYTES}); it alone may set the peg
    # @param pool_funding [Integer] issued into the pool by the currency's issuer
    # @param vbill_backing [Integer] V-Bills issued into the float
    # @return [CurrencyInfo]
    # @raise [ZxvUsageError] ZXV_E_CURRENCY (an ISO 4217 code), ZXV_E_DUPLICATE
    def add_currency(slot, code:, minor_units:, issuer_public_key:, pool_funding: 0, vbill_backing: 0)
      pk = Call.bytes!(issuer_public_key, "issuer_public_key", size: ISSUER_KEY_BYTES)
      check(Native.lib.zxv_vhub_add_ccy(capability.ptr, ptr, Call.uint8!(slot, "slot"), Call.cstr!(code, "code"),
                                        Call.uint8!(minor_units, "minor_units"), Call.mem(pk),
                                        Call.uint64!(pool_funding, "pool_funding"), Call.uint64!(vbill_backing, "vbill_backing")))
      currency(slot)
    end

    # A currency's accounts and current peg (needs :ledger_read).
    # @return [CurrencyInfo]
    # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (slot not registered)
    def currency(slot)
      ia, pa, va = Array.new(3) { Call.out(:uint32) }
      num, den, ep = Array.new(3) { Call.out(:uint64) }
      check(Native.lib.zxv_vhub_ccy_info(capability.ptr, ptr, Call.uint8!(slot, "slot"), ia, pa, va, num, den, ep))
      CurrencyInfo.new(issuer_account: ia.read_uint32, pool_account: pa.read_uint32, vfloat_account: va.read_uint32,
                       peg_num: num.read_uint64, peg_den: den.read_uint64, peg_epoch: ep.read_uint64)
    end

    # Set a currency's peg to num/den at +epoch+ (above the current one),
    # given the issuer's ML-DSA-87 signature (empty context) over
    # {.peg_message}. Applies to the next quote and swap. A refused peg
    # changes nothing.
    # @return [self]
    # @raise [ZxvRefusedError] ZXV_E_PEG (a stale or replayed epoch)
    # @raise [VerificationFailedError] not the issuer's signature over these numbers
    # @raise [ZxvUsageError] ZXV_E_RANGE (num or den 0)
    def set_peg(slot, num:, den:, epoch:, signature:)
      sig = Call.bytes!(signature, "signature", size: SIGNATURE_BYTES)
      check(Native.lib.zxv_vhub_set_peg(capability.ptr, ptr, Call.uint8!(slot, "slot"), Call.uint64!(num, "num"),
                                        Call.uint64!(den, "den"), Call.uint64!(epoch, "epoch"), Call.mem(sig)))
      self
    end

    # Open a holder account in currency +slot+ for +owner+ (an id of your choosing).
    # @return [Integer] the account number
    # @raise [ZxvRefusedError] ZXV_E_CAPACITY (512 accounts)
    def open_account(slot, owner:)
      out = Call.out(:uint32)
      check(Native.lib.zxv_vhub_open_account(capability.ptr, ptr, Call.uint8!(slot, "slot"), Call.uint32!(owner, "owner"), out))
      out.read_uint32
    end

    # Test faucet: issue +amount+ to a holder account or a currency's pool
    # (by that currency's issuer), or V-Bills to a V-Bill float (by the
    # V-Bill issuer). The issuer's CREDIT rises by the same amount.
    # @return [self]
    def issue(account, amount)
      check(Native.lib.zxv_vhub_issue(capability.ptr, ptr, Call.uint32!(account, "account"), Call.uint64!(amount, "amount")))
      self
    end

    # @return [Balance] one account's rails (needs :ledger_read)
    def balance(account)
      d = Call.out(:uint64)
      c = Call.out(:uint64)
      e = Call.out(:int64)
      check(Native.lib.zxv_vhub_balance(capability.ptr, ptr, Call.uint32!(account, "account"), d, c, e))
      Balance.new(debit: d.read_uint64, credit: c.read_uint64, equity: e.read_int64)
    end

    # Every account's rails, account 0 first.
    # @return [Array<Balance>]
    def balances
      list = []
      d = Call.out(:uint64)
      c = Call.out(:uint64)
      e = Call.out(:int64)
      loop do
        rc = Native.lib.zxv_vhub_balance(capability.ptr, ptr, list.size, d, c, e)
        break if rc == Status::E_NOT_FOUND

        check(rc)
        list << Balance.new(debit: d.read_uint64, credit: c.read_uint64, equity: e.read_int64)
      end
      list
    end

    # Add the built-in limit rule to currency +slot+: at most +limit+ minor
    # units out per sender per rule epoch (inflows are not counted). One
    # limit tracks 16 senders per epoch and refuses a 17th (fail closed).
    # Several limits on one currency all apply.
    # @return [self]
    # @raise [ZxvRefusedError] ZXV_E_CAPACITY (4 rules already)
    def add_limit(slot, limit)
      check(Native.lib.zxv_vhub_add_limit(capability.ptr, ptr, Call.uint8!(slot, "slot"), Call.uint64!(limit, "limit")))
      self
    end

    # Start the next rule epoch: every limit starts every sender at zero.
    # @return [Integer] the new epoch
    def next_epoch
      out = Call.out(:uint64)
      check(Native.lib.zxv_vhub_next_epoch(capability.ptr, ptr, out))
      out.read_uint64
    end

    # The largest exact amount of +from+ not above +want+ and what it would
    # carry. Runs no rules and checks no balances, so a quoted swap may
    # still be refused.
    # @return [Quote]
    # @raise [ZxvRefusedError] ZXV_E_PEG (either peg unset)
    # @raise [ZxvUsageError] ZXV_E_ARG (from == to)
    def quote(from:, to:, want:)
      a, v, r = Array.new(3) { Call.out(:uint64) }
      check(Native.lib.zxv_vhub_quote(capability.ptr, ptr, Call.uint8!(from, "from"), Call.uint8!(to, "to"),
                                      Call.uint64!(want, "want"), a, v, r))
      Quote.new(amount: a.read_uint64, vbills: v.read_uint64, received: r.read_uint64)
    end

    # Swap +amount+ minor units of currency +from+ from holder account
    # +sender+ to holder account +receiver+ in currency +to+, as one
    # six-line posting at +tick+ (a caller-supplied time, never the wall
    # clock). A refused swap changes nothing.
    # @return [Swap]
    # @raise [ZxvRefusedError] ZXV_E_LIMIT (a limit refused), ZXV_E_STATE (a
    #   currency has no rule), ZXV_E_PEG
    # @raise [InsufficientFundsError] the sender, A's float or B's pool would go below zero
    # @raise [ZxvUsageError] ZXV_E_RANGE (0, too large, or not exact under the pegs), ZXV_E_CURRENCY
    # @raise [ZxvFatal] ZXV_E_INVARIANT (the ledger broke; the context halts)
    def swap(from:, to:, sender:, receiver:, amount:, tick:)
      v = Call.out(:uint64)
      r = Call.out(:uint64)
      check(Native.lib.zxv_vhub_swap(capability.ptr, ptr, Call.uint8!(from, "from"), Call.uint8!(to, "to"),
                                     Call.uint32!(sender, "sender"), Call.uint32!(receiver, "receiver"),
                                     Call.uint64!(amount, "amount"), Call.uint64!(tick, "tick"), v, r))
      Swap.new(vbills: v.read_uint64, received: r.read_uint64)
    end

    # Verify the whole hub ledger: per account A = L + E with no negative
    # rail and no CREDIT outside an issuer, per asset sum DEBIT == sum
    # CREDIT, and the posting hash chain.
    # @return [Integer] the postings made so far (issues and swaps)
    # @raise [ZxvFatal] ZXV_E_INVARIANT on a failure (the context halts)
    def check!
      out = Call.out(:uint64)
      check(Native.lib.zxv_vhub_check(capability.ptr, ptr, out))
      out.read_uint64
    end

    def inspect = closed? ? "#<Zxv::VHub closed>" : "#<Zxv::VHub>"

    private

    def check(rc) = Call.check(rc, context)
  end
end
