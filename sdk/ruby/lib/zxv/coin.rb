# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: Fibonacci coins, the VFV ladder (zxv_coin_*).
  #
  # A VFV coin's face is a rung of the ladder: rung r (1..{RUNGS}) has face
  # F(r+1), so the faces are 1, 2, 3, 5, 8, 13, 21, ... up to
  # {TOP_FACE} (rung 45). Rung 0 is the ceremonial V0 note, worth nothing.
  # Any other face (4, 6, 7, 100, ...) is not a coin and raises
  # ZxvUsageError (ZXV_E_DENOM). To pay an arbitrary amount, {.split} it.
  #
  #   Zxv::Coin.split(100)   # => [10, 5, 3]  (89 + 8 + 3)
  #
  # Pure and integer-only. Honest limit: minting coins is NOT in this ABI
  # (the kernel's mint path still uses floating point); {TokenWallet} is
  # the only minting surface.
  module Coin
    # Rungs that carry value (1..45).
    RUNGS = 45
    # Face of rung 45: F(46) = 1,836,311,903.
    TOP_FACE = 1_836_311_903

    module_function

    # The face of ladder rung +rung+ (0..RUNGS; rung 0 is the V0 note, face 0).
    # @return [Integer]
    # @raise [ZxvUsageError] ZXV_E_RANGE (rung > 45)
    def face(rung)
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_coin_face(Call.uint32!(rung, "rung"), out))
      out.read_uint64
    end

    # The rung whose face is exactly +face+ (face 1 is rung 1).
    # @return [Integer] 1..RUNGS
    # @raise [ZxvUsageError] ZXV_E_DENOM (0 or any face not on the ladder)
    def rung(face)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_coin_rung(Call.uint64!(face, "face"), out))
      out.read_uint32
    end

    # @return [Boolean] whether +face+ is a coin face (1..TOP_FACE on the ladder)
    def face?(face)
      rung(face)
      true
    rescue ZxvUsageError
      false
    end

    # Default ceiling on the coins {.split} will build (16,777,216).
    MAX_SPLIT = 16_777_216

    # The coins for a whole +amount+: its Zeckendorf split into rungs,
    # largest first. Amounts at or above the top face take
    # floor(amount / TOP_FACE) coins of rung 45 first; the rest uses each
    # lower rung at most once and never two neighbouring rungs, which makes
    # the split unique and the fewest coins possible.
    # @param amount [Integer] 0..2**64-1 (0 is an empty split)
    # @param max_coins [Integer] refuse (ZXV_E_RANGE) rather than allocate a
    #   buffer for more coins than this; large amounts need billions of
    #   coins, so count them with {.split_count} instead
    # @return [Array<Integer>] rungs, largest first
    # @raise [ZxvUsageError] ZXV_E_RANGE if the split would need more than
    #   +max_coins+, or more than 4,294,967,294 coins (amounts of about
    #   7.9e18 and up)
    def split(amount, max_coins: MAX_SPLIT)
      a = Call.uint64!(amount, "amount")
      n = Call.out(:size_t)
      rc = Native.lib.zxv_coin_split(a, nil, 0, n)
      Call.check(rc) unless rc == Status::E_BUFFER || rc == Status::OK
      count = n.read(:size_t)
      return [] if count.zero?
      if count > max_coins
        raise ZxvUsageError.new(Status::E_RANGE, "split of #{amount} needs #{count} coins (max_coins: #{max_coins}); use split_count")
      end

      buf = FFI::MemoryPointer.new(:uint32, count)
      Call.check(Native.lib.zxv_coin_split(a, buf, count, n))
      buf.read_array_of_uint32(n.read(:size_t))
    end

    # @return [Integer] how many coins #split(amount) gives, without building them
    def split_count(amount)
      n = Call.out(:size_t)
      rc = Native.lib.zxv_coin_split(Call.uint64!(amount, "amount"), nil, 0, n)
      Call.check(rc) unless rc == Status::E_BUFFER || rc == Status::OK
      n.read(:size_t)
    end
  end

  # ABI 1.5: swarm AI token denominations and octane (zxv_token_*).
  #
  # Tier k (0..63) has face F(k+2): 1, 2, 3, 5, 8, ... Tier 0 is the plain
  # token; tiers {SUPER_TIER} (face 144) and up are super tokens. A
  # HOLDING is an Array of 64 Integers, the tokens held per tier; its value
  # is the sum of counts[k] * face(k). OCTANE is the densest tier a holding
  # may use (named grades like a fuel pump's: {OCTANES}; any 0..63 is
  # allowed). The canonical form of a value at an octane puts
  # floor(value / face(octane)) tokens on that tier and the rest in
  # Zeckendorf form below it, so every value has exactly one canonical form
  # per octane.
  #
  #   h = Zxv::Token.canonical(1_000_000, :racing)   # 17 pieces, not 1,000,000
  #   Zxv::Token.value(h)                            # => [1000000, 17]
  #
  # NO PREMIUM. A dense token is worth exactly its face in plain tokens.
  # Density earns no interest, costs no fee and buys no extra allotment or
  # priority; changing octane re-expresses the same value. It only means
  # fewer pieces (fewer receipts, signatures or ledger rows); it does not
  # make a model faster or an amount shorter on the wire.
  #
  # Every function here is pure and integer-only. Arrays passed in are
  # never modified: the "in place" C calls work on a copy, which is
  # returned. A value past 64 bits raises ZxvUsageError (ZXV_E_RANGE).
  module Token
    # Number of tiers.
    TIERS = 64
    # First super-token tier (face 144).
    SUPER_TIER = 10
    # The named octanes (densest tier allowed).
    OCTANES = { plain: 0, regular: 4, midgrade: 9, premium: 14, racing: 23, max: 63 }.freeze
    # Longest Fibonacci code of one value: 92 Zeckendorf bits + the closing 1.
    CODE_MAX_BITS = 93
    # Largest buffer the code functions take (bit positions are uint32).
    CODE_MAX_BYTES = 536_870_911

    module_function

    # @return [Integer] the face of +tier+ (0..63)
    # @raise [ZxvUsageError] ZXV_E_RANGE (tier >= 64)
    def face(tier)
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_token_face(Call.uint32!(tier, "tier"), out, nil))
      out.read_uint64
    end

    # @return [Boolean] whether +tier+ is a super token (face 144 and up)
    def super?(tier)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_token_face(Call.uint32!(tier, "tier"), nil, out))
      out.read_int32 == 1
    end

    # @return [Integer] the tier whose face is exactly +face+
    # @raise [ZxvUsageError] ZXV_E_DENOM (not one of the 64 faces)
    def tier(face)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_token_tier(Call.uint64!(face, "face"), out))
      out.read_uint32
    end

    # The value and piece count of a holding.
    # @param counts [Array<Integer>] 64 counts (a shorter Array is padded with zeros)
    # @return [Array(Integer, Integer)] [value, pieces]
    # @raise [ZxvUsageError] ZXV_E_RANGE (value past 64 bits)
    def value(counts)
      c = holding_mem(counts)
      v = Call.out(:uint64)
      p = Call.out(:uint64)
      Call.check(Native.lib.zxv_token_value(c, v, p))
      [v.read_uint64, p.read_uint64]
    end

    # The canonical holding of +value+ at +octane+.
    # @param octane [Symbol, Integer] a key of OCTANES or 0..63
    # @return [Array<Integer>] 64 counts
    def canonical(value, octane)
      out = FFI::MemoryPointer.new(:uint64, TIERS)
      Call.check(Native.lib.zxv_token_canonical(Call.uint64!(value, "value"), octane!(octane), out))
      out.read_array_of_uint64(TIERS)
    end

    # Whether +counts+ is the canonical form of its value at +octane+
    # (false when a tier above the octane is used).
    # @return [Boolean]
    def canonical?(counts, octane)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_token_is_canonical(holding_mem(counts), octane!(octane), out))
      out.read_int32 == 1
    end

    # +counts+ rewritten in the canonical form of its value at +octane+;
    # the value never changes.
    # @return [Array<Integer>] a new holding
    def compress(counts, octane) = in_place(counts) { |c| Native.lib.zxv_token_compress(c, octane!(octane)) }

    # Fuse one tier-k and one tier-(k+1) token into one tier-(k+2) token
    # (F(k+2) + F(k+3) = F(k+4)).
    # @return [Array<Integer>] a new holding
    # @raise [InsufficientFundsError] no tier-k or no tier-(k+1) token
    # @raise [ZxvRefusedError] ZXV_E_STATE (tier k+2 is denser than the octane allows)
    # @raise [ZxvUsageError] ZXV_E_RANGE (tier > 61, or a count is full)
    def fuse(counts, tier:, octane:)
      in_place(counts) { |c| Native.lib.zxv_token_fuse(c, octane!(octane), Call.uint32!(tier, "tier")) }
    end

    # Split one tier-k token into one tier-(k-1) and one tier-(k-2) token
    # (tier 1 becomes two plain tokens; tier 0 is atomic). Needs no octane:
    # it only makes a holding plainer.
    # @return [Array<Integer>] a new holding
    # @raise [InsufficientFundsError] no tier-k token
    # @raise [ZxvUsageError] ZXV_E_RANGE (tier 0 or >= 64)
    def split(counts, tier:) = in_place(counts) { |c| Native.lib.zxv_token_split(c, Call.uint32!(tier, "tier")) }

    # Number of bits in the Fibonacci code of +value+ (2..93).
    # @raise [ZxvUsageError] ZXV_E_RANGE (2**64-1 is not encodable)
    def code_bits(value)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_token_code_bits(Call.uint64!(value, "value"), out))
      out.read_uint32
    end

    # Encode +values+ as consecutive Fibonacci codes (Zeckendorf bits of
    # value+1, lowest first, then a closing 1, so every code ends in "11"
    # and needs no length field). Bit i is bit i%8 of byte i/8.
    # @param values [Array<Integer>] each 0..2**64-2
    # @return [Array(String, Integer)] [bytes, bit count]
    def encode_codes(values)
      vals = values.map { |v| Call.uint64!(v, "value") }
      bits = vals.sum { |v| code_bits(v) }
      nbytes = (bits + 7) / 8
      buf = Call.buf(nbytes)
      pos = Call.out(:uint32)
      pos.write_uint32(0)
      vals.each { |v| Call.check(Native.lib.zxv_token_code_put(buf, nbytes, pos, v)) }
      [buf.read_bytes(nbytes), pos.read_uint32]
    end

    # Decode Fibonacci codes from +bytes+, starting at bit +bit+.
    # @param count [Integer, nil] how many values to read (nil: until fewer
    #   than +bits+ remain, or the end of the bytes)
    # @param bits [Integer, nil] the number of meaningful bits (default all)
    # @return [Array(Array<Integer>, Integer)] [values, next bit]
    # @raise [ZxvUsageError] ZXV_E_FORMAT (no closing "11", or a value past
    #   64 bits), ZXV_E_RANGE (+bit+ past the end)
    def decode_codes(bytes, count: nil, bit: 0, bits: nil)
      b = Call.bytes!(bytes, "bytes")
      limit = bits.nil? ? b.bytesize * 8 : Call.uint32!(bits, "bits")
      mem = Call.mem(b)
      pos = Call.out(:uint32)
      pos.write_uint32(Call.uint32!(bit, "bit"))
      out = Call.out(:uint64)
      vals = []
      loop do
        break if count ? vals.size >= count : pos.read_uint32 >= limit

        Call.check(Native.lib.zxv_token_code_get(mem, b.bytesize, pos, out))
        vals << out.read_uint64
      end
      [vals, pos.read_uint32]
    end

    # An octane Symbol or Integer as 0..63.
    # @api private
    def octane!(octane)
      return OCTANES.fetch(octane) { raise ZxvUsageError.new(Status::E_ARG, "unknown octane #{octane.inspect}") } if octane.is_a?(Symbol)

      Call.uint32!(octane, "octane")
    end

    # @api private
    def holding_mem(counts)
      raise ZxvUsageError.new(Status::E_ARG, "counts must be an Array of Integers") unless counts.is_a?(Array)
      raise ZxvUsageError.new(Status::E_ARG, "counts has more than #{TIERS} tiers") if counts.size > TIERS

      c = FFI::MemoryPointer.new(:uint64, TIERS)
      c.write_array_of_uint64(counts.each_with_index.map { |n, i| Call.uint64!(n, "counts[#{i}]") } + [0] * (TIERS - counts.size))
      c
    end

    def in_place(counts)
      c = holding_mem(counts)
      Call.check(yield(c))
      c.read_array_of_uint64(TIERS)
    end
    private_class_method :in_place
  end

  # ABI 1.5: a token wallet, a holding kept canonical at its own octane
  # (zxv_token_wallet).
  #
  # Every change (#mint, #spend, #transfer, #octane=) needs the :mint
  # capability from the wallet's context; reads need none. The wallet is
  # not tied to a Swarm budget: tokens enter it only by #mint, which
  # creates them. A halted context refuses every call (ZxvFatal).
  # Thread-safe (internally serialized).
  #
  #   w = Zxv::TokenWallet.new(cap, octane: :racing)   # cap needs :mint
  #   w.mint(1_000_000)
  #   w.pieces                                         # => 17
  #
  # Density carries no premium: see {Token}.
  class TokenWallet < NativeHandle
    # The wallet's label, octane, value and piece count.
    Info = Struct.new(:model_id, :octane, :value, :pieces, keyword_init: true)

    # @return [Capability]
    attr_reader :capability

    # An empty wallet.
    # @param capability [Capability] needs :mint
    # @param octane [Symbol, Integer] a key of Token::OCTANES or 0..63
    # @param model_id [Integer] a free uint32 label (e.g. the swarm model that holds it)
    # @raise [CapabilityDeniedError] without :mint
    def initialize(capability, octane: :plain, model_id: 0)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_token_wallet_create(capability.ptr, Call.uint32!(model_id, "model_id"), Token.octane!(octane),
                                                    out), capability.context)
      super(capability.context, out.read_pointer, :zxv_token_wallet_destroy)
    end

    # Create +amount+ new tokens in the wallet.
    # @return [self]
    # @raise [ZxvUsageError] ZXV_E_RANGE (the value would pass 64 bits; nothing changes)
    def mint(amount)
      check(Native.lib.zxv_token_wallet_mint(capability.ptr, ptr, Call.uint64!(amount, "amount")))
      self
    end

    # Spend (burn) +amount+; the change stays canonical.
    # @return [self]
    # @raise [InsufficientFundsError] the wallet holds less (nothing changes)
    def spend(amount)
      check(Native.lib.zxv_token_wallet_spend(capability.ptr, ptr, Call.uint64!(amount, "amount")))
      self
    end

    # Move +amount+ to wallet +to+ (of the same context). This wallet keeps
    # its change at its octane; +to+ receives at ITS octane. The total is
    # conserved exactly; neither wallet changes on failure.
    # @param to [TokenWallet]
    # @return [self]
    # @raise [InsufficientFundsError] this wallet holds less
    # @raise [CapabilityDeniedError] +to+ belongs to another context
    def transfer(to, amount)
      raise ZxvUsageError.new(Status::E_ARG, "to must be a Zxv::TokenWallet") unless to.is_a?(TokenWallet)

      check(Native.lib.zxv_token_wallet_transfer(capability.ptr, ptr, to.ptr, Call.uint64!(amount, "amount")))
      self
    end

    # Change the octane (up = denser, down = plainer); the same value is
    # re-expressed canonically.
    # @param octane [Symbol, Integer]
    def octane=(octane)
      check(Native.lib.zxv_token_wallet_set_octane(capability.ptr, ptr, Token.octane!(octane)))
    end

    # @return [Info]
    def info
      m = Call.out(:uint32)
      o = Call.out(:uint32)
      v = Call.out(:uint64)
      p = Call.out(:uint64)
      check(Native.lib.zxv_token_wallet_info(ptr, m, o, v, p))
      Info.new(model_id: m.read_uint32, octane: o.read_uint32, value: v.read_uint64, pieces: p.read_uint64)
    end

    # @return [Integer] the octane (0..63)
    def octane = info.octane

    # @return [Integer] the value held
    def value = info.value

    # @return [Integer] the number of pieces held
    def pieces = info.pieces

    # @return [Array<Integer>] the 64 counts per tier
    def counts
      out = FFI::MemoryPointer.new(:uint64, Token::TIERS)
      check(Native.lib.zxv_token_wallet_counts(ptr, out))
      out.read_array_of_uint64(Token::TIERS)
    end

    def inspect
      return "#<Zxv::TokenWallet closed>" if closed?

      i = info
      "#<Zxv::TokenWallet value=#{i.value} octane=#{i.octane} pieces=#{i.pieces}>"
    end

    private

    def check(rc) = Call.check(rc, context)
  end
end
