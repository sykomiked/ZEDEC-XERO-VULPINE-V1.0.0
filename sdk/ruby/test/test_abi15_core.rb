# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.5 plumbing: status codes, capability bits, feature bits, handle
# lifetimes, the R4 buffer-growth path, error mapping, capability
# refusals and a poisoned context.
class TestAbi15Core < Minitest::Test
  include ZxvTest

  ERROR_TYPES = {
    "ZXV_E_DENOM" => Zxv::ZxvUsageError, "ZXV_E_TREATY" => Zxv::TreatyError, "ZXV_E_CID" => Zxv::ZxvRefusedError,
    "ZXV_E_CONSENT" => Zxv::ConsentRequiredError, "ZXV_E_ERASURE" => Zxv::ZxvRefusedError,
    "ZXV_E_PEG" => Zxv::ZxvRefusedError, "ZXV_E_LIMIT" => Zxv::ZxvRefusedError,
    "ZXV_E_PARADOX_STEP" => Zxv::ZxvRefusedError, "ZXV_E_PARADOX_OWNER" => Zxv::OwnerApprovalRequiredError
  }.freeze

  def test_status_abi15_vectors_and_mapping
    vectors.fetch("status_abi15").each do |v|
      code = v["code"]
      assert_equal code, Zxv::Status.const_get(v["name"].delete_prefix("ZXV_"))
      assert_equal v["name"], Zxv::Status.name_of(code)
      assert_equal v["class"], Zxv::Status.class_of(code)
      err = Zxv::Call.error_for(code)
      assert_instance_of ERROR_TYPES.fetch(v["name"]), err
      assert_kind_of(v["class"] == Zxv::Status::CLASS_USAGE ? Zxv::ZxvUsageError : Zxv::ZxvRefusedError, err)
      assert_equal code, err.status
      assert_equal v["name"], err.status_name
      raised = assert_raises(Zxv::StatusError) { Zxv::Call.check(code) }
      assert_equal code, raised.status
    end
  end

  def test_error_mapping_from_real_calls
    expect_status("ZXV_E_DENOM") { Zxv::Coin.rung(4) }
    err = expect_status("ZXV_E_TREATY") { Zxv::Treaty.parse("TREATY") }
    assert_equal :syntax, err.fault
    assert_equal 0, err.offset
    expect_status("ZXV_E_CID") { Zxv::Cid.parse("z") }
    expect_status("ZXV_E_ERASURE") do
      payload = "x" * 50
      h = Zxv::Freight::Header.for_payload(payload, freight_id: 1, k: 10)
      Zxv::Freight.decode(h, Zxv::Freight.rows(Zxv::Freight.encode(h, payload)).first(9))
    end
    with_context15 do |_ctx, cap|
      book = Zxv::Paradox::Book.new(cap, log_capacity: 8, registry_capacity: 4)
      err = assert_raises(Zxv::OwnerApprovalRequiredError) do
        book.registry_add(Zxv::Paradox.signature("m"), kind: :neutral, flags: :money)
      end
      assert_equal "ZXV_E_PARADOX_OWNER", err.status_name
      log = Zxv::Web4::ConsentLog.new(capacity: 4)
      human = Zxv::Web4::Agent.new(cap, name: "human", now_ms: 1, seed: "\x44".b * 32)
      err = assert_raises(Zxv::ConsentRequiredError) do
        log.check!(nil, human_public_key: human.public_key, agent_id: "\x01".b * 32, action: "\x02".b * 32,
                        need: :money, amount: 1, now_ms: 2)
      end
      assert_equal "ZXV_E_CONSENT", err.status_name
    end
  end

  def test_capability_bits_and_features2
    assert_equal 1 << 12, Zxv::Capability::BITS[:aipi]
    assert_equal 1 << 13, Zxv::Capability::BITS[:mint]
    assert_equal 1 << 14, Zxv::Capability::BITS[:paradox]
    assert_equal 0x7FFF, Zxv::Capability::ALL_1_5
    assert_equal 0x3FF, Zxv::Capability::ALL # :all never silently grows
    f2 = Zxv.features2
    %i[treaty aipi coin fee vhub freight web4 paradox].each do |g|
      assert f2.include?(g), "feature #{g} missing"
      assert Zxv.feature?(g)
    end
    with_context15 do |ctx, cap|
      assert_equal Zxv::Capability::ALL_1_5, cap.info.bits
      assert_includes cap.info.names, :paradox
      refute_includes ctx.root_capability.info.names, :mint
    end
  end

  # A capability without :aipi, :mint or :paradox gets the capability error.
  def test_capability_refusals
    with_context15 do |ctx, cap|
      plain = ctx.root_capability(:all_1_4)
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Aipi::Node.new(plain) }
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Paradox::Book.new(plain) }
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Paradox::Policy.new(plain, lo: [0], hi: [1], initial: [0]) }
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Paradox::Hold.new(plain, capacity: 2) }
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Paradox::Graph.new(plain, step_limit: 2) }
      err = assert_raises(Zxv::CapabilityDeniedError) { Zxv::TokenWallet.new(plain) }
      assert_equal "ZXV_E_CAP", err.status_name
      minted = Zxv::TokenWallet.new(cap.attenuate(:mint))
      minted.mint(10)
      assert_equal 10, minted.value
      # A wallet's changes check the capability it was made with.
      revocable = cap.attenuate(:mint)
      w = Zxv::TokenWallet.new(revocable)
      revocable.revoke
      assert_raises(Zxv::CapabilityDeniedError) { w.mint(1) }
      # A revoked capability is refused too.
      sub = cap.attenuate(:paradox)
      sub.revoke
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Paradox::Book.new(sub) }
    end
  end

  def test_pure_handle_lifetimes
    t = Zxv::Treaty.parse("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nEND\n")
    assert_equal "N", t.name
    t.close
    assert t.closed?
    t.close # idempotent
    err = assert_raises(Zxv::ZxvUsageError) { t.name }
    assert_equal Zxv::Status::E_STATE, err.status
    j = Zxv::Freight::Join.new(capacity: 2)
    j.close
    assert_raises(Zxv::ZxvUsageError) { j.status }
    w = Zxv::Freight::Ops::Writer.new(declared_total: 1)
    w.close
    assert_raises(Zxv::ZxvUsageError) { w.literal("a") }
    m = Zxv::Web4::Manifest.new(version: 1, issued_ms: 1, expires_ms: 2)
    m.close
    assert_raises(Zxv::ZxvUsageError) { m.info }
  end

  def test_context_handle_lifetimes
    ctx = Zxv::Context.new
    cap = ctx.root_capability(:all_1_5)
    w = Zxv::TokenWallet.new(cap)
    book = Zxv::Paradox::Book.new(cap)
    hub = Zxv::VHub.new(cap)
    node = Zxv::Aipi::Node.new(cap)
    agent = Zxv::Web4::Agent.new(cap, name: "a", now_ms: 1, seed: "\x11".b * 32)
    w.close
    assert_raises(Zxv::ZxvUsageError) { w.mint(1) }
    # Closing the context releases the rest first (R6), then refuses use.
    ctx.close
    [book, hub, node, agent].each do |h|
      assert_raises(Zxv::ZxvUsageError, h.class.name) { h.send(:ptr) }
    end
  end

  # The R4 bytes pattern: a too-small first buffer is regrown to fit.
  def test_buffer_growth
    t = Zxv::Treaty.parse(vectors["treaty"]["sutra"][1]["sutra"])
    full = t.to_record
    grown = Zxv::Call.bytes_out(nil, t.ptr, hint: 1) { |*a| Zxv::Native.lib.zxv_treaty_emit_record(*a) }
    assert_equal full, grown
    assert_equal Zxv::Treaty::REC_LEN, grown.bytesize
    # A buffer one byte short gets ZXV_E_BUFFER and the needed length.
    len = Zxv::Call.out(:size_t)
    buf = Zxv::Call.buf(Zxv::Treaty::REC_LEN - 1)
    assert_equal Zxv::Status::E_BUFFER, Zxv::Native.lib.zxv_treaty_emit_record(t.ptr, buf, Zxv::Treaty::REC_LEN - 1, len)
    assert_equal Zxv::Treaty::REC_LEN, len.read(:size_t)
    # Text R4: the canonical Sutra re-emitted through a growing buffer.
    assert_equal vectors["treaty"]["sutra"][1]["reemit"], t.to_sutra
    big = "x".b * 5000
    assert_equal Zxv::Freight::Ops.encode(big, with_sha: true).bytesize,
                 Zxv::Call.bytes_out(nil, Zxv::Call.mem(big), big.bytesize, 1, hint: 1) { |*a|
                   Zxv::Native.lib.zxv_freight_ops_encode(*a)
                 }.bytesize
  end

  def test_poisoned_context
    ctx = Zxv::Context.new
    cap = ctx.root_capability(:all_1_5)
    w = Zxv::TokenWallet.new(cap)
    book = Zxv::Paradox::Book.new(cap)
    w.mint(5)
    ctx.halt("abi15 test")
    assert ctx.poisoned?
    assert_raises(Zxv::ZxvFatal) { w.mint(1) }
    assert_raises(Zxv::ZxvFatal) { book.registry_add(Zxv::Paradox.signature("x"), kind: :destructive) }
    assert_raises(Zxv::ZxvFatal) { Zxv::VHub.new(cap) }
    # Pure functions need no context and still work.
    assert_equal 1, Zxv::Fee.assure(1125)
    ctx.close
  end

  def test_floats_refused
    assert_raises(Zxv::FloatRejectedError) { Zxv::Fee.assure(1.5) }
    assert_raises(Zxv::FloatRejectedError) { Zxv::Coin.split(2.0) }
    assert_raises(Zxv::FloatRejectedError) { Zxv::Treaty::Pic.add(1.0, 1) }
  end
end
