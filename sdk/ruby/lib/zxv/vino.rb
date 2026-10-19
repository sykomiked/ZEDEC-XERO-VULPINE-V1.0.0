# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.6: Vino Embed (zxv_vino_*). What an app or game needs to take
  # part in Vino without ever holding anyone's value or keys. There are two
  # lanes, and they never meet:
  #
  # PLAY:: a sandbox inside the app ({PlaySandbox}). It has its own play
  #        units (never live Vino), its own issuers, holders and vouchers,
  #        and lives in process memory only. Play units are not money: no
  #        call takes a play sandbox and anything live, so a play unit can
  #        never be converted to, redeemed for, or moved into live value.
  #        They are lost when the sandbox is closed.
  # LIVE:: real Vino, held in the user's own ZXV wallet, never in the app.
  #        The app builds a {PaymentRequest}; the user approves it in their
  #        wallet, which signs a {Receipt} with its ML-DSA-87 key; the app
  #        checks the receipt offline ({Receipt#verify!}). The app never
  #        sees a balance, a key or a custody account.
  #
  # == Vouchers (play lane)
  # A voucher is one coin whose face is a Fibonacci ladder face
  # ({Coin.face?}), held by one holder on exactly ONE active rail at a time:
  # +:bearer+ (a note in the holder's hand) or +:ghost+ (its twin on the
  # play ledger). Spending must name the active rail, so one voucher can
  # never be spent on both ({WrongRailError}). Switching rails moves nothing.
  #
  # == Coverage
  # An issuer posts play backing and may only issue while its backing stays
  # at least 1.8 times everything it has outstanding, in exact integers:
  # <tt>10 * backing >= 18 * outstanding</tt>. An issue or a withdrawal of
  # backing that would break this is refused ({CoverageError}). Redeeming a
  # voucher returns it to its issuer and retires it: the issuer's
  # outstanding falls and nothing is paid out to anyone. There is no call
  # that turns a voucher into backing, play units or anything else, and no
  # interest, yield or return of any kind.
  #
  # == Requests and receipts
  # A payment request is a fixed 160-byte canonical record; its id is the
  # SHA-256 of those bytes. A LIVE request marked as having a paid chance
  # outcome (loot boxes, paid draws) is refused ({ChanceRefusedError}); the
  # allotment is a separate module, not a payment request. A receipt is the
  # wallet's ML-DSA-87 signature over 88 bytes naming the request id, the
  # wallet key and the approval time. A receipt for an expired request, or
  # one approved outside the request's window, is {ExpiredError}.
  #
  # == Daily cap
  # A deployment may set a per-device daily cap. {SpendMeter} counts what
  # one device approved per day and refuses an amount that would pass the
  # cap. It is a counter, not a legal check, and makes no exemption claim.
  #
  # == Capabilities
  # Every play-lane call and the meter need +:vino+ from a capability of the
  # handle's context ({CapabilityDeniedError} without it; {ZxvFatal} once
  # the context is poisoned). Encoding and decoding a request, building a
  # receipt message and verifying a receipt are pure and need no capability.
  # Signing a receipt needs +:crypto_sign+: it is the wallet's call, not the
  # app's.
  #
  # == Not here, by design
  # Minting live Vino, setting pegs, any conversion between Vino and other
  # currencies, V-Bill auctions or bids, V-Bonds and derivatives, and every
  # operator or steward action. They are never app-facing.
  #
  # == Honest limits
  # The LIVE flow here is the app's half only: approving a request happens
  # in the user's wallet (zxvd), which posts the payment on its own ledger.
  # ML-DSA-87 here is the reference implementation, not FIPS 140-3
  # validated. Whether a given deployment of Vino is store credit, a prepaid
  # access product or something else is a legal question pending counsel;
  # nothing in this SDK decides it.
  module Vino
    # Rails a voucher can be on, by name (ZXV_VINO_RAIL_*).
    RAILS = { bearer: 0, ghost: 1 }.freeze
    # Lanes, by name (ZXV_VINO_LANE_*).
    LANES = { play: 1, live: 2 }.freeze
    # Voucher states, by name (ZXV_VINO_V_*).
    VOUCHER_STATES = { active: 1, redeemed: 2 }.freeze

    # Most accounts (issuers plus holders) one play sandbox may open.
    MAX_ACCOUNTS = 256
    # Most active (unredeemed) vouchers in one play sandbox.
    MAX_VOUCHERS = 4096
    # Largest single amount, face, backing step or daily cap (2**59).
    AMOUNT_MAX = 1 << 59

    # Size of an encoded payment request.
    REQUEST_BYTES = 160
    # Size of a request id (SHA-256 of the encoded request).
    REQUEST_ID_BYTES = 32
    # Longest memo, in bytes (printable ASCII 0x20..0x7e).
    MEMO_MAX = 64
    # Size of the message a wallet signs to approve a request.
    RECEIPT_MSG_BYTES = 88
    # Request flag: the purchase has a paid chance outcome (refused on LIVE).
    FLAG_CHANCE = 0x1
    # Every request flag this ABI knows; any other bit is ZXV_E_FORMAT.
    FLAGS_KNOWN = 0x1
    # ML-DSA-87 sizes: a wallet public key, secret key and receipt signature.
    PUBLIC_KEY_BYTES = 2592
    SECRET_KEY_BYTES = 4896
    SIGNATURE_BYTES = 4627

    class << self
      # @api private
      # A rail as the C int: a key of {RAILS}, or an Integer passed through
      # unchanged so the library can refuse it (ZXV_E_ARG).
      def rail!(rail) = enum!(RAILS, rail, "rail")

      # @api private
      # A lane as the C int: a key of {LANES}, or an Integer passed through
      # unchanged so the library can refuse it (ZXV_E_LANE).
      def lane!(lane) = enum!(LANES, lane, "lane")

      # @api private
      # The symbol for a C enum value, or the Integer itself if unknown.
      def sym(table, value) = table.key(value) || value

      private

      def enum!(table, value, name)
        return Call.int32!(value, name) if value.is_a?(Integer) || value.is_a?(Float)

        table.fetch(value) do
          raise ZxvUsageError.new(Status::E_ARG, "#{name} must be one of #{table.keys.inspect}, got #{value.inspect}")
        end
      end
    end

    # One voucher as {PlaySandbox#voucher} reports it.
    # +id+:: the voucher id (> 0, never reused in its sandbox)
    # +face+:: its Fibonacci face, in play units
    # +issuer+, +holder+:: account ids
    # +rail+:: +:bearer+ or +:ghost+, the one rail it can be spent on
    # +state+:: +:active+ or +:redeemed+ (retired; nothing was paid out)
    Voucher = Struct.new(:id, :face, :issuer, :holder, :rail, :state, keyword_init: true) do
      # @return [Boolean] true while it can still be spent or redeemed
      def active? = state == :active

      # @return [Boolean] true once redeemed (retired)
      def redeemed? = state == :redeemed
    end

    # One play account as {PlaySandbox#account} reports it.
    # +id+:: the account id (counts up from 0)
    # +owner+:: the owner id given when it was opened
    # +issuer+:: true for an issuer, false for a holder
    # +backing+:: an issuer's play backing (0 for a holder)
    # +faces+:: an issuer's outstanding faces, or the faces a holder holds
    # +voucher_count+:: active vouchers it issued (issuer) or holds (holder)
    Account = Struct.new(:id, :owner, :issuer, :backing, :faces, :voucher_count, keyword_init: true) do
      # @return [Boolean] true for an issuer account
      def issuer? = issuer

      # @return [Boolean] true for a holder account
      def holder? = !issuer
    end

    # A play-lane sandbox (zxv_vino_play): issuers post play backing and
    # issue Fibonacci-face vouchers to holders, who pass them on and redeem
    # them. Play units come from nowhere, are worth nothing, can never
    # become live value, and are lost on {#close}.
    #
    #   play = Zxv::Vino::PlaySandbox.new(cap)            # cap needs :vino
    #   shop = play.open_issuer(700)
    #   alice = play.open_holder(801)
    #   play.back(shop, 100)                              # play units, not money
    #   id = play.issue(issuer: shop, holder: alice, face: 34)
    #   play.redeem(id, holder: alice)                    # retired; pays nothing out
    #
    # Every call uses the capability the sandbox was created with, which
    # must hold +:vino+. A refused call changes nothing. Thread-safe
    # (internally serialized). Close it before its context; closing the
    # context closes it too, and any later use raises {ZxvUsageError}
    # (ZXV_E_STATE).
    class PlaySandbox < NativeHandle
      # @return [Capability] the capability every call is checked against
      attr_reader :capability

      # Create an empty play sandbox on the capability's context.
      # @param capability [Capability] needs :vino
      # @raise [CapabilityDeniedError] without :vino (or revoked)
      # @raise [ZxvFatal] the context is poisoned
      def initialize(capability)
        @capability = capability
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_vino_play_create(capability.ptr, out), capability.context)
        super(capability.context, out.read_pointer, :zxv_vino_play_destroy)
      end

      # Open an account for +owner+ (an id of your choosing; owners need not
      # be distinct).
      # @param owner [Integer] 0..2**32-1
      # @param issuer [Boolean] true opens an issuer, false a holder
      # @return [Integer] the new account id (ids count up from 0)
      # @raise [ZxvRefusedError] ZXV_E_CAPACITY ({MAX_ACCOUNTS} already open)
      def open_account(owner:, issuer:)
        out = Call.out(:uint32)
        check(Native.lib.zxv_vino_play_open(capability.ptr, ptr, Call.uint32!(owner, "owner"),
                                            Call.bool!(issuer, "issuer"), out))
        out.read_uint32
      end

      # Open an issuer account (see {#open_account}).
      # @return [Integer] the account id
      def open_issuer(owner) = open_account(owner: owner, issuer: true)

      # Open a holder account (see {#open_account}).
      # @return [Integer] the account id
      def open_holder(owner) = open_account(owner: owner, issuer: false)

      # Post +amount+ play units of backing to an issuer. This is the play
      # faucet: play units come from nowhere and are worth nothing.
      # @param issuer [Integer] an issuer account id
      # @param amount [Integer] 1..{AMOUNT_MAX}
      # @return [self]
      # @raise [ZxvUsageError] ZXV_E_ARG (not an issuer), ZXV_E_NOT_FOUND (no
      #   such account), ZXV_E_RANGE (0, too large, or backing would pass 2**62)
      def back(issuer, amount)
        check(Native.lib.zxv_vino_play_back(capability.ptr, ptr, Call.uint32!(issuer, "issuer"),
                                            Call.uint64!(amount, "amount")))
        self
      end

      # Withdraw +amount+ of an issuer's backing, only while it stays at
      # 1.8x coverage of what it has outstanding. A refusal changes nothing.
      # @return [self]
      # @raise [CoverageError] the issuer would fall below 1.8x coverage
      # @raise [InsufficientFundsError] more than the issuer's backing
      # @raise [ZxvUsageError] as {#back}
      def unback(issuer, amount)
        check(Native.lib.zxv_vino_play_unback(capability.ptr, ptr, Call.uint32!(issuer, "issuer"),
                                              Call.uint64!(amount, "amount")))
        self
      end

      # Issue one voucher of Fibonacci face +face+ from +issuer+ to +holder+.
      # @param issuer [Integer] an issuer account id
      # @param holder [Integer] a holder account id
      # @param face [Integer] a Fibonacci ladder face (1, 2, 3, 5, 8, ...)
      # @param rail [Symbol, Integer] the rail it starts on, :bearer or :ghost
      # @return [Integer] the voucher id (> 0, never reused in this sandbox)
      # @raise [CoverageError] the issuer would fall below 1.8x coverage
      # @raise [ZxvUsageError] ZXV_E_DENOM (not a ladder face), ZXV_E_ARG
      #   (issuer is not an issuer, holder not a holder, bad rail),
      #   ZXV_E_NOT_FOUND
      # @raise [ZxvRefusedError] ZXV_E_CAPACITY ({MAX_VOUCHERS} active)
      def issue(issuer:, holder:, face:, rail: :bearer)
        out = Call.out(:uint64)
        check(Native.lib.zxv_vino_issue(capability.ptr, ptr, Call.uint32!(issuer, "issuer"), Call.uint32!(holder, "holder"),
                                        Call.uint64!(face, "face"), Vino.rail!(rail), out))
        out.read_uint64
      end

      # Move an active voucher from holder +from+ to holder +to+, spending
      # it on +rail+, which must be its active rail. The rail does not change.
      # @return [self]
      # @raise [WrongRailError] +rail+ is not the voucher's active rail
      # @raise [ZxvUsageError] ZXV_E_STATE (already redeemed), ZXV_E_ARG
      #   (+from+ is not its holder, +to+ not a holder, from == to, bad
      #   rail), ZXV_E_NOT_FOUND (no such voucher or account)
      def transfer(voucher, from:, to:, rail:)
        check(Native.lib.zxv_vino_transfer(capability.ptr, ptr, Call.uint64!(voucher, "voucher"), Call.uint32!(from, "from"),
                                           Call.uint32!(to, "to"), Vino.rail!(rail)))
        self
      end

      # Switch an active voucher, by its holder, to +rail+ (bearer <-> ghost).
      # Nothing moves; the voucher just changes which rail it can be spent on.
      # @return [self]
      # @raise [WrongRailError] it is already on +rail+
      # @raise [ZxvUsageError] ZXV_E_STATE (redeemed), ZXV_E_ARG (not its
      #   holder, bad rail), ZXV_E_NOT_FOUND
      def set_rail(voucher, holder:, rail:)
        check(Native.lib.zxv_vino_set_rail(capability.ptr, ptr, Call.uint64!(voucher, "voucher"),
                                           Call.uint32!(holder, "holder"), Vino.rail!(rail)))
        self
      end

      # Redeem an active voucher, by its holder, to its issuer: the voucher
      # is retired and the issuer's outstanding falls by its face. Nothing
      # is paid out to anyone.
      # @return [self]
      # @raise [ZxvUsageError] ZXV_E_STATE (already redeemed), ZXV_E_ARG (not
      #   its holder), ZXV_E_NOT_FOUND
      def redeem(voucher, holder:)
        check(Native.lib.zxv_vino_redeem(capability.ptr, ptr, Call.uint64!(voucher, "voucher"),
                                         Call.uint32!(holder, "holder")))
        self
      end

      # One voucher's face, issuer, holder, rail and state. Redeemed
      # vouchers stay listed by id.
      # @return [Voucher]
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (no such voucher)
      def voucher(id)
        face = Call.out(:uint64)
        issuer, holder = Array.new(2) { Call.out(:uint32) }
        rail, state = Array.new(2) { Call.out(:int32) }
        check(Native.lib.zxv_vino_voucher_info(capability.ptr, ptr, Call.uint64!(id, "voucher"), face, issuer, holder,
                                               rail, state))
        Voucher.new(id: id, face: face.read_uint64, issuer: issuer.read_uint32, holder: holder.read_uint32,
                    rail: Vino.sym(RAILS, rail.read_int32), state: Vino.sym(VOUCHER_STATES, state.read_int32))
      end

      # One account's owner, kind and totals.
      # @return [Account]
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (no such account)
      def account(id)
        owner, count = Array.new(2) { Call.out(:uint32) }
        is_issuer = Call.out(:int32)
        backing, faces = Array.new(2) { Call.out(:uint64) }
        check(Native.lib.zxv_vino_account_info(capability.ptr, ptr, Call.uint32!(id, "account"), owner, is_issuer,
                                               backing, faces, count))
        Account.new(id: id, owner: owner.read_uint32, issuer: is_issuer.read_int32 == 1, backing: backing.read_uint64,
                    faces: faces.read_uint64, voucher_count: count.read_uint32)
      end

      # The ids of the active vouchers an account issued (issuer) or holds
      # (holder), ascending. Redeemed vouchers are not listed.
      # @return [Array<Integer>]
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (no such account)
      def vouchers(account)
        acct = Call.uint32!(account, "account")
        count = Call.out(:size_t)
        cap_len = 0
        # Two-call pattern: ask for the count, then fetch. Another thread
        # may issue in between, so regrow a few times before giving up.
        4.times do
          ids = cap_len.zero? ? nil : FFI::MemoryPointer.new(:uint64, cap_len)
          rc = Native.lib.zxv_vino_vouchers(capability.ptr, ptr, acct, ids, cap_len, count)
          need = count.read(:size_t)
          if rc == Status::E_BUFFER
            cap_len = need
            next
          end
          check(rc)
          return need.zero? ? [] : ids.read_array_of_uint64(need)
        end
        check(Status::E_BUFFER)
      end

      # Re-check every invariant of the sandbox: each active voucher has a
      # ladder face, one rail, an issuer and a holder; each issuer's
      # outstanding equals the sum of its active faces and keeps 1.8x
      # coverage; each holder's total equals what it holds.
      # @return [self]
      # @raise [ZxvFatal] ZXV_E_INVARIANT (the context is poisoned)
      def check!
        check(Native.lib.zxv_vino_play_check(capability.ptr, ptr))
        self
      end

      def inspect = closed? ? "#<Zxv::Vino::PlaySandbox closed>" : "#<Zxv::Vino::PlaySandbox>"

      private

      def check(rc) = Call.check(rc, context)
    end

    # A per-device daily spend counter (zxv_vino_meter). It counts what one
    # device approved per day index and refuses an amount that would pass
    # the deployment's daily cap. It is a counter, not a legal check, and
    # makes no exemption claim.
    #
    #   meter = Zxv::Vino::SpendMeter.new(cap, daily_cap: 10_000)  # cap needs :vino
    #   left = meter.admit(day: Time.now.to_i / 86_400, amount: 2_500)
    #
    # Thread-safe (internally serialized). Close it before its context.
    class SpendMeter < NativeHandle
      # @return [Capability] the capability every call is checked against
      attr_reader :capability
      # @return [Integer] the cap per day
      attr_reader :daily_cap

      # @param capability [Capability] needs :vino
      # @param daily_cap [Integer] 1..{AMOUNT_MAX}
      # @raise [CapabilityDeniedError] without :vino
      # @raise [ZxvUsageError] ZXV_E_RANGE (cap 0 or too large)
      def initialize(capability, daily_cap:)
        @capability = capability
        @daily_cap = Call.uint64!(daily_cap, "daily_cap")
        out = Call.out(:pointer)
        Call.check(Native.lib.zxv_vino_meter_create(capability.ptr, @daily_cap, out), capability.context)
        super(capability.context, out.read_pointer, :zxv_vino_meter_destroy)
      end

      # Count +amount+ against day +day+. A day above the last one counted
      # starts a fresh count; a day below it is refused (the clock went back).
      # @param day [Integer] a day index of your clock, e.g. Unix time / 86400
      # @param amount [Integer] 1..{AMOUNT_MAX}
      # @return [Integer] what is left of the day's cap after this amount
      # @raise [DailyCapExceededError] the day's total would pass the cap;
      #   nothing was counted, and the error's +left+ says what is left
      # @raise [ZxvUsageError] ZXV_E_RANGE (amount 0 or too large, or a day
      #   below the last one)
      def admit(day:, amount:)
        left = Call.out(:uint64)
        rc = Native.lib.zxv_vino_meter_admit(capability.ptr, ptr, Call.uint64!(day, "day"),
                                             Call.uint64!(amount, "amount"), left)
        if rc == Status::E_LIMIT
          err = DailyCapExceededError.new(rc, Call.last_error)
          err.left = left.read_uint64
          raise err
        end
        Call.check(rc, context)
        left.read_uint64
      end

      def inspect = closed? ? "#<Zxv::Vino::SpendMeter closed>" : "#<Zxv::Vino::SpendMeter daily_cap=#{daily_cap}>"
    end

    # A payment request: a plain, immutable value. The app builds one, sends
    # its {#encode}d bytes to the user's wallet, and the wallet approves it
    # with a {Receipt}. Encoding and decoding are pure: no capability needed.
    #
    #   req = Zxv::Vino::PaymentRequest.new(
    #     lane: :live, merchant: shop_id, nonce: Zxv.random_bytes(16),
    #     amount: 1_500, created_at: now, expires_at: now + 600, memo: "Sword of Dawn")
    #   bytes = req.encode     # 160 bytes, canonical
    #   req.id                 # SHA-256 of those bytes (32 bytes)
    #   Zxv::Vino::PaymentRequest.decode(bytes) == req  # => true
    #
    # The library checks every field when encoding and decoding (nothing is
    # checked when the value is built):
    # +lane+:: +:play+ (play units) or +:live+ (VFV minor units, approved
    #          in the user's wallet); any other value is {LaneError}
    # +flags+:: request flags ({FLAG_CHANCE}); an unknown bit is
    #           ZXV_E_FORMAT, and a LIVE request with {FLAG_CHANCE} is
    #           {ChanceRefusedError} (paid chance outcomes are not payment
    #           requests)
    # +merchant+:: the payee's 32-byte id (e.g. SHA-256 of its key)
    # +nonce+:: 16 bytes, unique per request
    # +amount+:: 1..{AMOUNT_MAX} (ZXV_E_RANGE otherwise)
    # +created_at+, +expires_at+:: seconds of the caller's clock (Unix time
    #                               by convention); expires_at > created_at
    # +memo+:: 0..64 bytes of printable ASCII (ZXV_E_TOO_LONG if longer,
    #          ZXV_E_FORMAT if not printable)
    class PaymentRequest
      # @return [Symbol, Integer] :play or :live (an Integer only if unknown)
      attr_reader :lane
      # @return [Integer] request flags
      attr_reader :flags
      # @return [String] 32 bytes
      attr_reader :merchant
      # @return [String] 16 bytes
      attr_reader :nonce
      # @return [Integer]
      attr_reader :amount
      # @return [Integer]
      attr_reader :created_at
      # @return [Integer]
      attr_reader :expires_at
      # @return [String] printable ASCII, possibly empty
      attr_reader :memo

      # Decode and validate 160 request bytes (the rules of {#encode}, plus
      # the magic, version and zero padding: ZXV_E_FORMAT).
      # @param bytes [String] 160 bytes
      # @return [PaymentRequest]
      # @raise [LaneError], [ChanceRefusedError], [ZxvUsageError] (ZXV_E_FORMAT,
      #   ZXV_E_RANGE, ZXV_E_TOO_LONG) when the bytes are not a valid request
      def self.decode(bytes)
        lane = Call.out(:int32)
        flags = Call.out(:uint32)
        merchant = Call.buf(32)
        nonce = Call.buf(16)
        amount, created, expires = Array.new(3) { Call.out(:uint64) }
        memo = Call.buf(MEMO_MAX + 1)
        Call.check(Native.lib.zxv_vino_request_decode(Call.mem(Call.bytes!(bytes, "bytes", size: REQUEST_BYTES)), lane, flags,
                                                      merchant, nonce, amount, created, expires, memo, nil))
        new(lane: Vino.sym(LANES, lane.read_int32), flags: flags.read_uint32, merchant: merchant.read_bytes(32),
            nonce: nonce.read_bytes(16), amount: amount.read_uint64, created_at: created.read_uint64,
            expires_at: expires.read_uint64, memo: memo.read_string.force_encoding(Encoding::US_ASCII))
      end

      # The id of encoded request bytes (SHA-256 of them), after checking
      # that they decode.
      # @param bytes [String] 160 bytes
      # @return [String] 32 bytes
      # @raise as {.decode}
      def self.id_of(bytes)
        id = Call.buf(REQUEST_ID_BYTES)
        Call.check(Native.lib.zxv_vino_request_decode(Call.mem(Call.bytes!(bytes, "bytes", size: REQUEST_BYTES)),
                                                      nil, nil, nil, nil, nil, nil, nil, nil, id))
        id.read_bytes(REQUEST_ID_BYTES)
      end

      # Build a request value. Only types and sizes are checked here; the
      # field rules are checked by {#encode}.
      # @param lane [Symbol, Integer] :play or :live
      # @param merchant [String] 32 bytes
      # @param nonce [String] 16 bytes
      # @param amount [Integer]
      # @param created_at [Integer]
      # @param expires_at [Integer]
      # @param memo [String] printable ASCII, up to 64 bytes
      # @param flags [Integer] 0 or {FLAG_CHANCE}
      # @raise [FloatRejectedError] a Float for a number
      # @raise [ZxvUsageError] ZXV_E_ARG (wrong type or byte size, a NUL in the memo)
      def initialize(lane:, merchant:, nonce:, amount:, created_at:, expires_at:, memo: "", flags: 0)
        Vino.lane!(lane)
        @lane = lane.is_a?(Integer) ? Vino.sym(LANES, lane) : lane
        @flags = Call.uint32!(flags, "flags")
        @merchant = Call.bytes!(merchant, "merchant", size: 32).freeze
        @nonce = Call.bytes!(nonce, "nonce", size: 16).freeze
        @amount = Call.uint64!(amount, "amount")
        @created_at = Call.uint64!(created_at, "created_at")
        @expires_at = Call.uint64!(expires_at, "expires_at")
        @memo = Call.cstr!(memo, "memo").freeze
        freeze
      end

      # @return [Boolean] whether {FLAG_CHANCE} is set
      def chance? = flags.anybits?(FLAG_CHANCE)

      # The canonical 160 bytes of this request.
      # @return [String] 160 bytes
      # @raise [LaneError] lane not :play or :live
      # @raise [ChanceRefusedError] a LIVE request with {FLAG_CHANCE}
      # @raise [ZxvUsageError] ZXV_E_FORMAT (unknown flag, memo not printable
      #   ASCII), ZXV_E_RANGE (amount 0 or above {AMOUNT_MAX}, expires_at <=
      #   created_at), ZXV_E_TOO_LONG (memo above 64 bytes)
      def encode = encode_with_id.first

      # The request id: SHA-256 of {#encode}. A receipt names it.
      # @return [String] 32 bytes
      # @raise as {#encode}
      def id = encode_with_id.last

      # Both at once, in one library call.
      # @return [Array(String, String)] [160 bytes, 32-byte id]
      # @raise as {#encode}
      def encode_with_id
        out = Call.buf(REQUEST_BYTES)
        id = Call.buf(REQUEST_ID_BYTES)
        Call.check(Native.lib.zxv_vino_request_encode(Vino.lane!(lane), flags, Call.mem(merchant), Call.mem(nonce), amount,
                                                      created_at, expires_at, memo, out, id))
        [out.read_bytes(REQUEST_BYTES), id.read_bytes(REQUEST_ID_BYTES)]
      end

      # @return [Hash] the fields
      def to_h
        { lane: lane, flags: flags, merchant: merchant, nonce: nonce, amount: amount, created_at: created_at,
          expires_at: expires_at, memo: memo }
      end

      def ==(other) = other.is_a?(PaymentRequest) && to_h == other.to_h
      alias eql? ==

      def hash = [self.class, to_h].hash

      def inspect
        "#<Zxv::Vino::PaymentRequest lane=#{lane.inspect} amount=#{amount} created_at=#{created_at} " \
          "expires_at=#{expires_at} memo=#{memo.inspect}>"
      end
    end

    # A wallet's approval of one payment request: the wallet's ML-DSA-87
    # signature (empty FIPS 204 context) over {.message}. The app receives
    # it from the user's wallet and checks it offline with {#verify!}; no
    # capability and no network are needed.
    #
    #   receipt = Zxv::Vino::Receipt.new(request: bytes, wallet_public_key: pk,
    #                                    approved_at: t, signature: sig)
    #   receipt.verify!(now: Time.now.to_i)   # raises unless genuine and in time
    #
    # ML-DSA-87 here is the reference implementation, not FIPS 140-3 validated.
    class Receipt
      # @return [String] the 160 request bytes this receipt approves
      attr_reader :request
      # @return [String] the wallet's ML-DSA-87 public key (2592 bytes)
      attr_reader :wallet_public_key
      # @return [Integer] when the wallet approved it (the request's clock)
      attr_reader :approved_at
      # @return [String] the 4627-byte ML-DSA-87 signature
      attr_reader :signature

      # The 88 bytes a wallet signs to approve request +request_id+ with key
      # +wallet_public_key+ at +approved_at+: the tag "zxv/vino-rcpt/v1",
      # the request id, SHA-256 of the wallet key, approved_at (8 bytes LE).
      # @param request_id [String] 32 bytes ({PaymentRequest#id})
      # @param wallet_public_key [String] 2592 bytes
      # @param approved_at [Integer] seconds of the request's clock
      # @return [String] 88 bytes
      def self.message(request_id:, wallet_public_key:, approved_at:)
        out = Call.buf(RECEIPT_MSG_BYTES)
        Call.check(Native.lib.zxv_vino_receipt_msg(Call.mem(Call.bytes!(request_id, "request_id", size: REQUEST_ID_BYTES)),
                                                   Call.mem(Call.bytes!(wallet_public_key, "wallet_public_key",
                                                                        size: PUBLIC_KEY_BYTES)),
                                                   Call.uint64!(approved_at, "approved_at"), out))
        out.read_bytes(RECEIPT_MSG_BYTES)
      end

      # Wallet side: approve +request+ at +approved_at+ (which must lie in
      # the request's window) and sign with the wallet's key. This is the
      # wallet's call, not the app's: an app never holds a wallet key.
      #
      # The secret key stays in its {SecretKey} (off the Ruby heap); it is
      # used for this one call and not kept by the returned receipt.
      # @param capability [Capability] needs :crypto_sign
      # @param request [String, PaymentRequest] the encoded request (160 bytes)
      # @param wallet_secret_key [SecretKey] 4896 bytes ({MlDsa87#generate})
      # @param wallet_public_key [String] 2592 bytes
      # @param approved_at [Integer]
      # @return [Receipt]
      # @raise [ExpiredError] approved_at is outside [created_at, expires_at]
      # @raise [CapabilityDeniedError] without :crypto_sign
      # @raise [NotImplementedError] the library was built without PQC
      # @raise [ZxvUsageError], [ChanceRefusedError] the request bytes do not decode
      def self.sign(capability, request:, wallet_secret_key:, wallet_public_key:, approved_at:)
        unless wallet_secret_key.is_a?(SecretKey) && wallet_secret_key.bytesize == SECRET_KEY_BYTES
          raise ZxvUsageError.new(Status::E_ARG, "wallet_secret_key must be a #{SECRET_KEY_BYTES}-byte Zxv::SecretKey")
        end

        req = request_bytes!(request)
        pk = Call.bytes!(wallet_public_key, "wallet_public_key", size: PUBLIC_KEY_BYTES)
        at = Call.uint64!(approved_at, "approved_at")
        sig = Call.buf(SIGNATURE_BYTES)
        Call.check(Native.lib.zxv_vino_receipt_sign(capability.ptr, Call.mem(req), wallet_secret_key.pointer, Call.mem(pk),
                                                    at, sig), capability.context)
        new(request: req, wallet_public_key: pk, approved_at: at, signature: sig.read_bytes(SIGNATURE_BYTES))
      end

      # @api private
      def self.request_bytes!(request)
        return request.encode if request.is_a?(PaymentRequest)

        Call.bytes!(request, "request", size: REQUEST_BYTES)
      end

      # A receipt as received from a wallet. Nothing is checked until
      # {#verify!} or {#valid?}.
      # @param request [String, PaymentRequest] the encoded request (160 bytes)
      # @param wallet_public_key [String] 2592 bytes
      # @param approved_at [Integer]
      # @param signature [String] 4627 bytes
      def initialize(request:, wallet_public_key:, approved_at:, signature:)
        @request = self.class.request_bytes!(request).freeze
        @wallet_public_key = Call.bytes!(wallet_public_key, "wallet_public_key", size: PUBLIC_KEY_BYTES).freeze
        @approved_at = Call.uint64!(approved_at, "approved_at")
        @signature = Call.bytes!(signature, "signature", size: SIGNATURE_BYTES).freeze
        freeze
      end

      # The id of the request this receipt approves (SHA-256 of {#request}).
      # @return [String] 32 bytes
      # @raise as {PaymentRequest.decode} when the request bytes are not valid
      def request_id = PaymentRequest.id_of(request)

      # The decoded request this receipt approves.
      # @return [PaymentRequest]
      def payment_request = PaymentRequest.decode(request)

      # App side, offline: check that this is the wallet's signature over
      # the request and approval time, that approved_at lies within the
      # request's window, and that +now+ is not past its expiry.
      # @param now [Integer] the current time on the request's clock
      # @return [self]
      # @raise [VerificationFailedError] not that wallet's signature over
      #   this request and time
      # @raise [ExpiredError] expired, or approved outside the window
      # @raise [ZxvUsageError], [ChanceRefusedError] the request bytes do not decode
      # @raise [NotImplementedError] the library was built without PQC
      def verify!(now:)
        Call.check(Native.lib.zxv_vino_receipt_verify(Call.mem(request), Call.mem(wallet_public_key), approved_at,
                                                      Call.mem(signature), Call.uint64!(now, "now")))
        self
      end

      # Like {#verify!}, but answers false (instead of raising) when the
      # signature does not verify or the request has expired. Other errors
      # (malformed request bytes, no PQC) still raise.
      # @return [Boolean]
      def valid?(now:)
        verify!(now: now)
        true
      rescue VerificationFailedError, ExpiredError
        false
      end

      # @return [Array] the four fields, in order
      def to_a = [request, wallet_public_key, approved_at, signature]

      def ==(other) = other.is_a?(Receipt) && to_a == other.to_a
      alias eql? ==

      def hash = [self.class, *to_a].hash

      def inspect = "#<Zxv::Vino::Receipt approved_at=#{approved_at}>"
    end
  end
end
