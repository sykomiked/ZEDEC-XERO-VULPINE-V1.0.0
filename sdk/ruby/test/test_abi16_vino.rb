# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.6 (Vino Embed): the status_abi16 and vino sections of
# vectors.json, plus handle lifetimes, the vouchers buffer path, error
# mapping, capability refusals and a poisoned context.
class TestAbi16Vino < Minitest::Test
  include ZxvTest

  V = Zxv::Vino

  ERROR_TYPES = {
    "ZXV_E_RAIL" => Zxv::WrongRailError, "ZXV_E_COVERAGE" => Zxv::CoverageError, "ZXV_E_LANE" => Zxv::LaneError,
    "ZXV_E_EXPIRED" => Zxv::ExpiredError, "ZXV_E_CHANCE" => Zxv::ChanceRefusedError
  }.freeze

  # The header's numbers (zxv_api.h), written out independently of the SDK
  # tables so a wrong table is caught. Vector integers go through these.
  RAIL_NAMES = { 0 => :bearer, 1 => :ghost }.freeze
  LANE_NAMES = { 1 => :play, 2 => :live }.freeze
  STATE_NAMES = { 1 => :active, 2 => :redeemed }.freeze

  def vv = vectors.fetch("vino")

  # A known number as its symbol; an unknown one stays an Integer.
  def rail(n) = RAIL_NAMES.fetch(n, n)

  def request_from(v)
    V::PaymentRequest.new(lane: LANE_NAMES.fetch(v["lane"], v["lane"]), flags: v["flags"], merchant: hex(v["merchant"]),
                          nonce: hex(v["nonce"]),
                          amount: u64(v["amount"]), created_at: u64(v["created_at"]),
                          expires_at: u64(v["expires_at"]), memo: v["memo"])
  end

  def base_request_bytes = hex(vv["requests"][0]["bytes"])

  # --- status codes, bits, features ------------------------------------

  def test_status_abi16_vectors_and_mapping
    vectors.fetch("status_abi16").each do |v|
      code = v["code"]
      assert_equal code, Zxv::Status.const_get(v["name"].delete_prefix("ZXV_"))
      assert_equal v["name"], Zxv::Status.name_of(code)
      assert_equal v["class"], Zxv::Status.class_of(code)
      err = Zxv::Call.error_for(code)
      assert_instance_of ERROR_TYPES.fetch(v["name"]), err
      assert_kind_of(v["class"] == Zxv::Status::CLASS_USAGE ? Zxv::ZxvUsageError : Zxv::ZxvRefusedError, err)
      assert_equal v["name"], err.status_name
    end
  end

  def test_capability_bits_and_features2
    assert_equal 1 << 15, Zxv::Capability::BITS[:vino]
    assert_equal 0xFFFF, Zxv::Capability::ALL_1_6
    assert_equal 0x7FFF, Zxv::Capability::ALL_1_5 # 1.5 constant kept
    assert_equal 0x100, Zxv::FEATURES2[:vino]
    assert Zxv.feature?(:vino)
    assert_equal [1, 6], Zxv.abi_version.first(2)
    with_context16 do |ctx, cap|
      assert_equal Zxv::Capability::ALL_1_6, cap.info.bits
      assert_includes cap.info.names, :vino
      assert_equal [:vino], ctx.root_capability(:vino).info.names
      # 1 << 15 is now known; the first unknown bit is 1 << 16.
      err = assert_raises(Zxv::StatusError) { ctx.root_capability(Zxv::Capability::ALL_1_6 + 1) }
      assert_equal "ZXV_E_ARG", err.status_name
    end
  end

  # --- payment requests --------------------------------------------------

  def test_request_vectors
    vv["requests"].each do |v|
      req = request_from(v)
      got = expect_status(v["status"]) { req.encode_with_id }
      next unless v["status"] == "ZXV_OK"

      bytes, id = got
      assert_equal v["bytes"], unhex(bytes), v["memo"]
      assert_equal v["id"], unhex(id)
      assert_equal bytes, req.encode
      assert_equal id, req.id
      back = V::PaymentRequest.decode(bytes)
      assert_equal req, back
      assert_equal v["memo"], back.memo
      assert_equal u64(v["amount"]), back.amount
      assert_equal v["flags"].allbits?(1), back.chance?
      assert_equal id, V::PaymentRequest.id_of(bytes)
    end
  end

  def test_request_decode_refusals
    vv["decode"].each do |v|
      bad = base_request_bytes.dup
      bad.setbyte(v["offset"], v["value"])
      expect_status(v["status"]) { V::PaymentRequest.decode(bad) }
      expect_status(v["status"]) { V::PaymentRequest.id_of(bad) }
    end
  end

  def test_request_value_type
    v = vv["requests"][0]
    req = request_from(v)
    assert_equal :live, req.lane
    assert_equal :play, request_from(vv["requests"][1]).lane
    assert req.frozen?
    assert_equal req.hash, request_from(v).hash
    assert_raises(Zxv::FloatRejectedError) { V::PaymentRequest.new(**req.to_h, amount: 1.0) }
    assert_raises(Zxv::ZxvUsageError) { V::PaymentRequest.new(**req.to_h, merchant: "short") }
    assert_raises(Zxv::ZxvUsageError) { V::PaymentRequest.new(**req.to_h, memo: "a\0b") }
    assert_raises(Zxv::ZxvUsageError) { V::PaymentRequest.new(**req.to_h, lane: :moon) }
    assert_raises(Zxv::ZxvUsageError) { V::PaymentRequest.decode("x" * 159) }
    # Non-ASCII memo text is the library's call: ZXV_E_FORMAT.
    expect_status("ZXV_E_FORMAT") { V::PaymentRequest.new(**req.to_h, memo: "café").encode }
  end

  # --- receipts ----------------------------------------------------------

  def test_receipt_vectors
    rv = vv["receipt"]
    req_bytes = base_request_bytes
    req_id = hex(vv["requests"][0]["id"])
    approved = u64(rv["approved_at"])
    with_context16 do |_ctx, cap|
      kp = Zxv::MlDsa87.new(cap).generate(seed: hex(rv["seed"]))
      assert_equal rv["pk_sha3_256"], unhex(sha3(kp.public_key))
      msg = V::Receipt.message(request_id: req_id, wallet_public_key: kp.public_key, approved_at: approved)
      assert_equal rv["msg"], unhex(msg)
      assert_equal V::RECEIPT_MSG_BYTES, msg.bytesize
      # The vector signature is the deterministic ML-DSA-87 signature over msg.
      det = Zxv::MlDsa87.new(cap).sign(kp.secret_key, msg, deterministic: true)
      assert_equal rv["sig"], unhex(det)
      rv["checks"].each do |c|
        sig = det.dup
        sig.setbyte(c["flip_sig"], sig.getbyte(c["flip_sig"]) ^ 1) if c["flip_sig"] >= 0
        r = V::Receipt.new(request: req_bytes, wallet_public_key: kp.public_key, approved_at: u64(c["approved_at"]),
                           signature: sig)
        expect_status(c["status"]) { r.verify!(now: u64(c["now"])) }
        assert_equal c["status"] == "ZXV_OK", r.valid?(now: u64(c["now"]))
      end
      # receipt_sign (hedged) gives a receipt that verifies the same way.
      signed = V::Receipt.sign(cap, request: req_bytes, wallet_secret_key: kp.secret_key,
                                    wallet_public_key: kp.public_key, approved_at: approved)
      assert signed.verify!(now: approved + 100)
      assert_equal req_id, signed.request_id
      assert_equal request_from(vv["requests"][0]), signed.payment_request
      refute signed.valid?(now: u64(vv["requests"][0]["expires_at"]) + 1)
      # A PaymentRequest may stand in for its bytes.
      assert V::Receipt.new(request: request_from(vv["requests"][0]), wallet_public_key: kp.public_key,
                            approved_at: approved, signature: det).verify!(now: approved)
      # The wallet refuses to sign outside the request's window.
      assert_raises(Zxv::ExpiredError) do
        V::Receipt.sign(cap, request: req_bytes, wallet_secret_key: kp.secret_key, wallet_public_key: kp.public_key,
                             approved_at: u64(vv["requests"][0]["expires_at"]) + 1)
      end
      # Signing needs a SecretKey, never a heap String.
      assert_raises(Zxv::ZxvUsageError) do
        V::Receipt.sign(cap, request: req_bytes, wallet_secret_key: kp.secret_key.export_bytes_unsafe,
                             wallet_public_key: kp.public_key, approved_at: approved)
      end
      # Signing is the wallet's call: it needs :crypto_sign.
      err = assert_raises(Zxv::CapabilityDeniedError) do
        V::Receipt.sign(cap.attenuate(:vino), request: req_bytes, wallet_secret_key: kp.secret_key,
                                              wallet_public_key: kp.public_key, approved_at: approved)
      end
      assert_equal "ZXV_E_CAP", err.status_name
      # A receipt over corrupted request bytes is refused by decode rules.
      bad = req_bytes.dup
      bad.setbyte(0, 0)
      expect_status("ZXV_E_FORMAT") do
        V::Receipt.new(request: bad, wallet_public_key: kp.public_key, approved_at: approved, signature: det)
                  .verify!(now: approved)
      end
      kp.secret_key.close
    end
  end

  # --- play lane -----------------------------------------------------------

  def run_play_step(play, s)
    case s["op"]
    when "open"
      # 0/1 map to false/true; anything else is passed as is (refused: ZXV_E_ARG).
      play.open_account(owner: s["owner"], issuer: { 0 => false, 1 => true }.fetch(s["is_issuer"], s["is_issuer"]))
    when "back" then play.back(s["issuer"], u64(s["amount"]))
    when "unback" then play.unback(s["issuer"], u64(s["amount"]))
    when "issue"
      play.issue(issuer: s["issuer"], holder: s["holder"], face: u64(s["face"]), rail: rail(s["rail"]))
    when "transfer" then play.transfer(u64(s["voucher"]), from: s["from"], to: s["to"], rail: rail(s["rail"]))
    when "set_rail" then play.set_rail(u64(s["voucher"]), holder: s["holder"], rail: rail(s["rail"]))
    when "redeem" then play.redeem(u64(s["voucher"]), holder: s["holder"])
    else flunk "unknown op #{s['op']}"
    end
  end

  def test_play_scenario
    pv = vv["play"]
    with_context16 do |_ctx, cap|
      play = V::PlaySandbox.new(cap)
      pv["steps"].each_with_index do |s, i|
        got = expect_status(s["status"]) { run_play_step(play, s) }
        next unless s["status"] == "ZXV_OK"

        assert_equal s["account"], got, "step #{i}" if s.key?("account")
        assert_equal u64(s["id"]), got, "step #{i}" if s.key?("id")
      end
      assert_same play, play.check!
      pv["accounts"].each do |a|
        got = play.account(a["account"])
        assert_equal [a["owner"], a["is_issuer"] == 1, u64(a["backing"]), u64(a["faces"]), a["count"]],
                     [got.owner, got.issuer?, got.backing, got.faces, got.voucher_count], "account #{a['account']}"
        assert_equal a["vouchers"].map { |x| u64(x) }, play.vouchers(a["account"])
      end
      pv["vouchers"].each do |v|
        got = play.voucher(u64(v["id"]))
        assert_equal [u64(v["face"]), v["issuer"], v["holder"], RAIL_NAMES.fetch(v["rail"]), STATE_NAMES.fetch(v["state"])],
                     [got.face, got.issuer, got.holder, got.rail, got.state]
      end
      assert play.voucher(1).redeemed?
      assert play.voucher(2).active?
      expect_status("ZXV_E_NOT_FOUND") { play.account(99) }
      expect_status("ZXV_E_NOT_FOUND") { play.vouchers(99) }
      expect_status("ZXV_E_NOT_FOUND") { play.voucher(99) }
      play.close
    end
  end

  # The two-call vouchers path: count first, then fetch; growth and order.
  def test_vouchers_buffer_path
    with_context16 do |_ctx, cap|
      play = V::PlaySandbox.new(cap)
      shop = play.open_issuer(1)
      alice = play.open_holder(2)
      bob = play.open_holder(3)
      assert_equal [], play.vouchers(alice)
      play.back(shop, 10_000)
      ids = Array.new(40) do |i|
        play.issue(issuer: shop, holder: alice, face: [1, 2, 3, 5, 8][i % 5], rail: i.even? ? :bearer : :ghost)
      end
      assert_equal ids, play.vouchers(alice)
      assert_equal ids, play.vouchers(shop)
      play.transfer(ids[3], from: alice, to: bob, rail: :ghost)
      play.redeem(ids[0], holder: alice)
      assert_equal ids - [ids[0], ids[3]], play.vouchers(alice)
      assert_equal [ids[3]], play.vouchers(bob)
      assert_equal 38, play.account(alice).voucher_count
      # The rail numbers reaching C are the header's (BEARER 0, GHOST 1):
      # read them back raw, not through the SDK's own table.
      raw_rail = Zxv::Call.out(:int32)
      [[ids[1], 1], [ids[2], 0]].each do |vid, want|
        assert_equal Zxv::Status::OK, Zxv::Native.lib.zxv_vino_voucher_info(cap.ptr, play.ptr, vid, nil, nil, nil, raw_rail, nil)
        assert_equal want, raw_rail.read_int32
      end
      # Raw: a short buffer gets ZXV_E_BUFFER with the count and is untouched.
      count = Zxv::Call.out(:size_t)
      buf = FFI::MemoryPointer.new(:uint64, 2)
      buf.write_array_of_uint64([7, 7])
      assert_equal Zxv::Status::E_BUFFER, Zxv::Native.lib.zxv_vino_vouchers(cap.ptr, play.ptr, bob, nil, 0, count)
      assert_equal 1, count.read(:size_t)
      assert_equal Zxv::Status::E_BUFFER, Zxv::Native.lib.zxv_vino_vouchers(cap.ptr, play.ptr, alice, buf, 2, count)
      assert_equal 38, count.read(:size_t)
      assert_equal [7, 7], buf.read_array_of_uint64(2)
      assert_same play, play.check!
    end
  end

  def test_error_mapping_from_real_calls
    with_context16 do |_ctx, cap|
      play = V::PlaySandbox.new(cap)
      shop = play.open_issuer(1)
      a = play.open_holder(2)
      b = play.open_holder(3)
      play.back(shop, 18)
      err = assert_raises(Zxv::CoverageError) { play.issue(issuer: shop, holder: a, face: 13) }
      assert_equal "ZXV_E_COVERAGE", err.status_name
      id = play.issue(issuer: shop, holder: a, face: 8, rail: :ghost)
      err = assert_raises(Zxv::WrongRailError) { play.transfer(id, from: a, to: b, rail: :bearer) }
      assert_equal "ZXV_E_RAIL", err.status_name
      assert_raises(Zxv::WrongRailError) { play.set_rail(id, holder: a, rail: :ghost) }
      assert_raises(Zxv::CoverageError) { play.unback(shop, 4) }
      expect_status("ZXV_E_ARG") { play.issue(issuer: shop, holder: a, face: 1, rail: 7) }
      expect_status("ZXV_E_DENOM") { play.issue(issuer: shop, holder: a, face: 4) }
      assert_raises(Zxv::ZxvUsageError) { play.issue(issuer: shop, holder: a, face: 1, rail: :paper) }
      assert_raises(Zxv::FloatRejectedError) { play.back(shop, 1.0) }
      # A refusal changed nothing.
      shop_now = play.account(shop)
      assert_equal [18, 8, 1], [shop_now.backing, shop_now.faces, shop_now.voucher_count]
    end
    req = request_from(vv["requests"][0])
    err = assert_raises(Zxv::LaneError) { V::PaymentRequest.new(**req.to_h, lane: 3).encode }
    assert_equal "ZXV_E_LANE", err.status_name
    err = assert_raises(Zxv::ChanceRefusedError) { V::PaymentRequest.new(**req.to_h, flags: V::FLAG_CHANCE).encode }
    assert_equal "ZXV_E_CHANCE", err.status_name
    # The same flag on the PLAY lane is allowed.
    assert_equal 160, V::PaymentRequest.new(**req.to_h, lane: :play, flags: V::FLAG_CHANCE).encode.bytesize
  end

  # --- the daily meter -----------------------------------------------------

  def test_meter_scenario
    mv = vv["meter"]
    with_context16 do |_ctx, cap|
      meter = V::SpendMeter.new(cap, daily_cap: u64(mv["daily_cap"]))
      assert_equal 100, meter.daily_cap
      mv["steps"].each_with_index do |s, i|
        got = expect_status(s["status"]) { meter.admit(day: u64(s["day"]), amount: u64(s["amount"])) }
        case s["status"]
        when "ZXV_OK" then assert_equal u64(s["left"]), got, "step #{i}"
        when "ZXV_E_LIMIT"
          assert_instance_of Zxv::DailyCapExceededError, got
          assert_kind_of Zxv::ZxvRefusedError, got
          assert_equal u64(s["left"]), got.left, "step #{i}"
        end
      end
      expect_status("ZXV_E_RANGE") { V::SpendMeter.new(cap, daily_cap: 0) }
      expect_status("ZXV_E_RANGE") { V::SpendMeter.new(cap, daily_cap: V::AMOUNT_MAX + 1) }
      meter.close
    end
  end

  # --- lifetimes, capabilities, poisoning ----------------------------------

  def test_handle_lifetimes
    ctx = Zxv::Context.new
    cap = ctx.root_capability(:all_1_6)
    play = V::PlaySandbox.new(cap)
    meter = V::SpendMeter.new(cap, daily_cap: 10)
    play.close
    assert play.closed?
    play.close # idempotent
    err = assert_raises(Zxv::ZxvUsageError) { play.open_holder(1) }
    assert_equal Zxv::Status::E_STATE, err.status
    assert_raises(Zxv::ZxvUsageError) { play.vouchers(0) }
    play2 = V::PlaySandbox.new(cap)
    play2.open_holder(1)
    # Closing the context releases the rest first (R6), then refuses use.
    ctx.close
    assert_raises(Zxv::ZxvUsageError) { play2.open_holder(2) }
    assert_raises(Zxv::ZxvUsageError) { meter.admit(day: 1, amount: 1) }
    assert_match(/closed/, play.inspect)
  end

  def test_capability_refusals
    with_context16 do |ctx, cap|
      plain = ctx.root_capability(:all_1_5) # everything but :vino
      err = assert_raises(Zxv::CapabilityDeniedError) { V::PlaySandbox.new(plain) }
      assert_equal "ZXV_E_CAP", err.status_name
      assert_raises(Zxv::CapabilityDeniedError) { V::SpendMeter.new(plain, daily_cap: 5) }
      # A sandbox checks the capability it was made with on every call.
      sub = cap.attenuate(:vino)
      play = V::PlaySandbox.new(sub)
      meter = V::SpendMeter.new(sub, daily_cap: 5)
      holder = play.open_holder(1)
      sub.revoke
      assert_raises(Zxv::CapabilityDeniedError) { play.open_holder(2) }
      assert_raises(Zxv::CapabilityDeniedError) { play.vouchers(holder) }
      assert_raises(Zxv::CapabilityDeniedError) { meter.admit(day: 1, amount: 1) }
      # Pure calls need no capability at all.
      assert_equal 160, request_from(vv["requests"][0]).encode.bytesize
    end
  end

  def test_poisoned_context
    ctx = Zxv::Context.new
    cap = ctx.root_capability(:all_1_6)
    play = V::PlaySandbox.new(cap)
    shop = play.open_issuer(1)
    meter = V::SpendMeter.new(cap, daily_cap: 10)
    Zxv::Native.lib.zxv_ctx_halt(ctx.raw, "vino test") # halted behind the SDK's back
    err = assert_raises(Zxv::ZxvFatal) { play.back(shop, 5) }
    assert_equal "ZXV_E_POISONED", err.status_name
    assert ctx.poisoned?
    assert_raises(Zxv::ZxvFatal) { play.account(shop) }
    assert_raises(Zxv::ZxvFatal) { meter.admit(day: 1, amount: 1) }
    assert_raises(Zxv::ZxvFatal) { V::PlaySandbox.new(cap) }
    # Pure calls still work.
    assert_equal 32, request_from(vv["requests"][0]).id.bytesize
    ctx.close
  end
end
