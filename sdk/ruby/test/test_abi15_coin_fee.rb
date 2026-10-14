# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 conformance: the coin and fee sections of vectors.json.
class TestAbi15CoinFee < Minitest::Test
  include ZxvTest

  def cv = vectors.fetch("coin")

  # Sparse [[tier, count], ...] to a dense 64-entry holding.
  def dense(pairs)
    h = Array.new(Zxv::Token::TIERS, 0)
    pairs.each { |t, n| h[t] = u64(n) }
    h
  end

  def test_coin_faces_rungs_splits
    assert_equal cv["rungs"], Zxv::Coin::RUNGS
    assert_equal u64(cv["top_face"]), Zxv::Coin::TOP_FACE
    cv["face"].each do |v|
      got = expect_status(v["status"]) { Zxv::Coin.face(v["rung"]) }
      assert_equal u64(v["face"]), got if v["status"] == "ZXV_OK"
    end
    cv["rung_of"].each do |v|
      got = expect_status(v["status"]) { Zxv::Coin.rung(u64(v["face"])) }
      assert_equal v["rung"], got if v["status"] == "ZXV_OK"
      assert_equal v["status"] == "ZXV_OK", Zxv::Coin.face?(u64(v["face"]))
    end
    cv["split"].each do |v|
      amount = u64(v["amount"])
      n = expect_status(v["status"]) { Zxv::Coin.split_count(amount) }
      next unless v["status"] == "ZXV_OK"

      assert_equal u64(v["count"]), n
      assert_equal v["rungs"], Zxv::Coin.split(amount) if v.key?("rungs")
    end
    # 1e12 needs 558 coins: built, and they add up.
    coins = Zxv::Coin.split(1_000_000_000_000)
    assert_equal 558, coins.size
    assert_equal 1_000_000_000_000, coins.sum { |r| Zxv::Coin.face(r) }
    # 7e18 needs 3.8e9 coins: refused before allocating, counted instead.
    err = assert_raises(Zxv::ZxvUsageError) { Zxv::Coin.split(7_000_000_000_000_000_000) }
    assert_equal Zxv::Status::E_RANGE, err.status
    assert_raises(Zxv::ZxvUsageError) { Zxv::Coin.split(1000, max_coins: 1) }
  end

  def test_token_faces_and_values
    cv["token_face"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.face(v["tier"]) }
      next unless v["status"] == "ZXV_OK"

      assert_equal u64(v["face"]), got
      assert_equal v["super"], Zxv::Token.super?(v["tier"])
    end
    cv["token_tier"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.tier(u64(v["face"])) }
      assert_equal v["tier"], got if v["status"] == "ZXV_OK"
    end
    cv["token_value"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.value(dense(v["counts"])) }
      assert_equal [u64(v["value"]), u64(v["pieces"])], got if v["status"] == "ZXV_OK"
    end
  end

  def test_token_canonical_compress_fuse_split
    cv["token_canonical"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.canonical(u64(v["value"]), v["octane"]) }
      next unless v["status"] == "ZXV_OK"

      assert_equal dense(v["counts"]), got
      assert_equal [u64(v["value"]), u64(v["pieces"])], Zxv::Token.value(got)
      assert Zxv::Token.canonical?(got, v["octane"])
    end
    cv["token_is_canonical"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.canonical?(dense(v["counts"]), v["octane"]) }
      assert_equal v["canonical"], got
    end
    cv["token_compress"].each do |v|
      got = expect_status(v["status"]) { Zxv::Token.compress(dense(v["counts"]), v["octane"]) }
      assert_equal dense(v["out"]), got
    end
    cv["token_fuse_split"].each do |v|
      input = dense(v["counts"])
      got = expect_status(v["status"]) do
        if v["op"] == "fuse"
          Zxv::Token.fuse(input, tier: v["tier"], octane: v["octane"])
        else
          Zxv::Token.split(input, tier: v["tier"])
        end
      end
      if v["status"] == "ZXV_OK"
        assert_equal dense(v["out"]), got
      else
        assert_equal dense(v["out"]), input # unchanged on failure
      end
    end
    assert_equal 9, Zxv::Token::OCTANES.fetch(Zxv::Token::OCTANES.key(9))
  end

  def test_token_codes
    cv["token_code"].each do |v|
      value = u64(v["value"])
      bits = expect_status(v["status"]) { Zxv::Token.code_bits(value) }
      next unless v["status"] == "ZXV_OK"

      assert_equal v["bits"], bits
      bytes, n = Zxv::Token.encode_codes([value])
      assert_equal v["bytes"], unhex(bytes)
      assert_equal v["bits"], n
      assert_equal v["code"], Array.new(n) { |i| bytes.getbyte(i / 8)[i % 8] }.join
      assert_equal [[value], n], Zxv::Token.decode_codes(bytes, count: 1)
    end
    s = cv["token_code_stream"]
    vals = s["values"].map { |x| u64(x) }
    bytes, n = Zxv::Token.encode_codes(vals)
    assert_equal s["bits"], n
    assert_equal s["bytes"], unhex(bytes)
    assert_equal [vals, n], Zxv::Token.decode_codes(bytes, bits: n)
    cv["token_code_get_reject"].each do |v|
      expect_status(v["status"]) { Zxv::Token.decode_codes(hex(v["bytes"]), count: 1, bit: v["bit"]) }
    end
  end

  def test_token_wallet
    with_context15 do |_ctx, cap|
      a = Zxv::TokenWallet.new(cap, octane: :racing, model_id: 7)
      b = Zxv::TokenWallet.new(cap)
      a.mint(1_000_000)
      assert_equal 1_000_000, a.value
      assert_equal Zxv::Token.canonical(1_000_000, :racing), a.counts
      assert_equal 17, a.pieces
      assert_equal 7, a.info.model_id
      a.transfer(b, 1000)
      assert_equal 999_000, a.value
      assert_equal 1000, b.value
      assert_raises(Zxv::InsufficientFundsError) { b.spend(1001) }
      b.spend(1)
      assert_equal 999, b.value
      a.octane = 0
      assert_equal 999_000, a.pieces
      assert_raises(Zxv::ZxvUsageError) { a.mint(2**64 - 1) }
      assert_equal 999_000, a.value # nothing changed
    end
  end

  def fv = vectors.fetch("fee")

  def test_fee
    assert_equal fv["num"], Zxv::Fee::NUM
    assert_equal fv["den"], Zxv::Fee::DEN
    fv["assure"].each { |v| assert_equal u64(v["fee"]), Zxv::Fee.assure(u64(v["gross"])) }
    fv["carry"].each do |c|
      carry = u64(c["rem_in"])
      c["steps"].each do |s|
        fee, carry = expect_status(s["status"]) { Zxv::Fee.assure_with_carry(u64(s["gross"]), carry: carry) }
        assert_equal [u64(s["fee"]), u64(s["rem"])], [fee, carry]
      end
    end
    fv["carry_reject"].each do |v|
      expect_status(v["status"]) { Zxv::Fee.assure_with_carry(u64(v["gross"]), carry: u64(v["rem_in"])) }
    end
    assert_equal fv["buckets"].map(&:to_sym), Zxv::Fee::BUCKETS
    fv["split"].each do |v|
      parts = Zxv::Fee.split(u64(v["fee"]))
      assert_equal v["parts"].map { |x| u64(x) }, parts.values
      assert_equal u64(v["fee"]), parts.values.sum
    end
    p = fv["policy_default"]
    pol = Zxv::Fee.default_policy
    assert_equal p["pct"], pol.percent.values
    assert_equal p["allow_below"] != 0, pol.allow_below
    assert_equal(p["rate"].map { |r| r.map { |x| u64(x) } }, pol.credit_rates.values)
    assert_equal p["multiplier"].map { |x| u64(x) }, pol.multiplier
    assert_equal p["commons_cap"].map { |x| u64(x) }, pol.commons_cap
    assert_raises(Zxv::ZxvUsageError) { Zxv::Fee.assure(2**64) }
    assert_raises(Zxv::ZxvUsageError) { Zxv::Fee.assure(-1) }
  end
end
