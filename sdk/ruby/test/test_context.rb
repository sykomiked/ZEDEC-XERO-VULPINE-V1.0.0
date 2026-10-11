# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"
require "open3"
require "rbconfig"

class TestContext < Minitest::Test
  include ZxvTest

  def ledger_setup(ctx, cap)
    ctx.enable_currency("NGN", minor_units: 2, cap: cap)
    ledger = Zxv::Ledger.new(cap)
    bank = ledger.open_account("bank", "NGN", kind: :issuer)
    alice = ledger.open_account("alice", "NGN")
    [ledger, bank, alice]
  end

  def test_ledger_round_trip
    with_context do |ctx, cap|
      ledger, bank, alice = ledger_setup(ctx, cap)
      bob = ledger.open_account("bob", "NGN")
      ledger.post(bank, alice, 50_000, "NGN", "E2E-1")
      ledger.post(alice, bob, 12_345, "NGN", "E2E-2")
      assert_equal Zxv::Ledger::Balance.new(debit: 50_000, credit: 12_345, equity: 37_655), ledger.balance(alice)
      assert_equal(-50_000, ledger.balance(bank).equity)
      assert ledger.check
      assert_equal 3, ledger.account_count
      assert_equal :issuer, ledger.account(bank).kind
      assert_equal %w[E2E-1 E2E-2], ledger.journal(alice).map(&:reference)
      assert_equal [50_000, -12_345], ledger.journal(alice).map(&:amount)
      assert_equal ["NGN"], ctx.currencies.map(&:code)
      err = assert_raises(Zxv::InsufficientFundsError) { ledger.post(bob, alice, 1_000_000, "NGN", "E2E-3") }
      assert_equal "ZXV_E_FUNDS", err.status_name
      assert_raises(Zxv::FloatRejectedError) { ledger.post(bank, alice, 1.5, "NGN", "E2E-4") }
    end
  end

  def test_private_currency
    with_context do |ctx, cap|
      vfv = ctx.register_private_currency("VFV", 2, cap: cap)
      assert vfv.private?
      assert_nil ctx.currency("XOF")
    end
  end

  def test_capability_denial_attenuation_and_revocation
    with_context do |ctx, cap|
      ledger, bank, alice = ledger_setup(ctx, cap)
      ledger.post(bank, alice, 500, "NGN", "r1")
      ro = cap.attenuate(:ledger_read)
      assert_equal [:ledger_read], ro.info.names
      read_only = Zxv::Ledger.new(ro)
      assert_equal 500, read_only.balance(alice).equity
      err = assert_raises(Zxv::CapabilityDeniedError) { read_only.post(bank, alice, 1, "NGN", "r2") }
      assert_equal Zxv::Status::E_CAP, err.status
      refute ro.attenuate(:all).allows?(:ledger_post), "attenuation never widens"
      assert_raises(Zxv::CapabilityDeniedError) { ro.check(:ledger_post) }

      cap.attestation = Zxv::Truth::BOTH
      err = assert_raises(Zxv::CapabilityDeniedError) { ledger.balance(alice) }
      assert_match(/contradictory/, err.detail)
      cap.attestation = Zxv::Truth::TRUE

      cap.revoke
      assert cap.info.revoked
      assert_raises(Zxv::CapabilityDeniedError) { read_only.balance(alice) }
      assert_raises(Zxv::ZxvUsageError) { ctx.root_capability(1 << 40) }
    end
  end

  def test_fatal_path_via_halt
    ctx = Zxv::Context.new
    cap = ctx.root_capability
    ledger, bank, alice = ledger_setup(ctx, cap)
    ctx.halt("test halt")
    assert ctx.poisoned?

    # A bare rescue does NOT catch ZxvFatal.
    caught_by_bare_rescue = false
    fatal = assert_raises(Zxv::ZxvFatal) do
      begin
        ledger.post(bank, alice, 1, "NGN", "r3")
      rescue StandardError
        caught_by_bare_rescue = true
      end
    end
    refute caught_by_bare_rescue
    assert_equal Zxv::Status::E_POISONED, fatal.status
    assert_equal "ZXV_E_POISONED", fatal.status_name
    # Every later call fails the same way: never retry.
    assert_raises(Zxv::ZxvFatal) { ledger.balance(alice) }
    assert_raises(Zxv::ZxvFatal) { Zxv::Budgets.new(cap) }
    ctx.close
    assert ctx.closed?
  end

  def test_fatal_status_from_c_marks_the_context_dead
    with_context do |ctx, cap|
      ledger, _bank, alice = ledger_setup(ctx, cap)
      Zxv::Native.lib.zxv_ctx_halt(ctx.raw, "halted behind the SDK's back")
      err = assert_raises(Zxv::ZxvFatal) { ledger.balance(alice) } # ZXV_E_POISONED from C
      assert_equal Zxv::Status::E_POISONED, err.status
      assert ctx.poisoned?
      # From now on the SDK refuses even calls C would let through.
      assert_raises(Zxv::ZxvFatal) { Zxv::PhaseClock.new(cap) }
    end
  end

  def test_fatal_policy_abort_ends_the_process
    script = <<~RUBY
      $LOAD_PATH.unshift #{File.expand_path('../lib', __dir__).inspect}
      require "zxv"
      Zxv.fatal_policy = :abort
      ctx = Zxv::Context.new
      cap = ctx.root_capability
      ctx.halt("x")
      begin
        Zxv::Ledger.new(cap).balance(0)
      rescue Exception
        puts "caught"
      end
      puts "survived"
    RUBY
    out, err, status = Open3.capture3(RbConfig.ruby, "-e", script)
    assert_equal 70, status.exitstatus
    assert_equal "", out
    assert_match(/FATAL/, err)
  end

  def test_message_pacs008
    with_context do |_ctx, cap|
      msg = Zxv::Message.new(cap, :pacs008)
      { msg_id: "M1", cre_dt_tm: "2026-10-11T12:00:00Z", debtor_name: "Ada & Co", debtor_acct: "D1",
        creditor_name: "Kofi", creditor_acct: "C1", end_to_end_id: "E2E", ccy: "NGN" }.each { |k, v| msg[k] = v }
      msg.amount(:settlement, 150_075, 2)
      xml = msg.render
      assert_includes xml, "Ada &amp; Co"
      assert_includes xml, "1500.75"
      msg.close
      assert_raises(Zxv::NotImplementedError) { Zxv::Message.new(cap, :pacs002) }
    end
  end
end
