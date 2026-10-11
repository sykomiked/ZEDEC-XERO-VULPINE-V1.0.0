# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.2: every gated call goes through a capability (the zxv_*_cap forms).
class TestCapForms < Minitest::Test
  include ZxvTest

  def denied(&block)
    err = assert_raises(Zxv::CapabilityDeniedError, &block)
    assert_equal Zxv::Status::E_CAP, err.status
    err
  end

  def test_cap_forms_need_the_right_bit
    with_context do |ctx, root|
      post = root.attenuate(:ledger_post)
      read = root.attenuate(:ledger_read)
      ctx.enable_currency("NGN", cap: post)
      ctx.enable_currency("GHS", minor_units: 2, cap: post)
      ctx.register_private_currency("VFV", 2, cap: post)
      assert_equal %w[NGN GHS VFV], ctx.currencies.map(&:code)
      denied { ctx.enable_currency("KES", cap: read) }
      denied { ctx.enable_currency("KES", minor_units: 2, cap: read) }
      denied { ctx.register_private_currency("VFX", 2, cap: read) }
      denied { Zxv::Ledger.new(read).open_account("x", "NGN") }

      writer = Zxv::Ledger.new(post)
      bank = writer.open_account("bank", "NGN", kind: :issuer)
      ada = writer.open_account("ada", "NGN")
      writer.post(bank, ada, 7, "NGN", "R-1")
      reader = Zxv::Ledger.new(read)
      assert_equal 2, reader.account_count
      assert_equal "ada", reader.account(ada).name
      assert_equal ["R-1"], reader.journal(ada).map(&:reference)
      denied { writer.account_count }
      denied { writer.account(ada) }
      denied { writer.journal(ada) }

      denied { Zxv::Message.new(post, :pacs008) }
      msg = Zxv::Message.new(read, :pacs008)
      msg[:msg_id] = "M-1"
      read.attestation = Zxv::Truth::NEITHER
      denied { msg[:msg_id] = "M-2" }

      denied { Zxv::Budgets.new(post) }
      denied { Zxv::PhaseClock.new(post) }
      denied { Zxv::Attestations.new(post) }
      denied { Zxv::Swarm.new(post, levels: 2, tokens_per_cycle: 10) }
      Zxv::Budgets.new(root.attenuate(:budget)).close
      Zxv::PhaseClock.new(root.attenuate(:phase)).close
      Zxv::Attestations.new(root.attenuate(:attest)).close
      Zxv::Swarm.new(root.attenuate(:swarm), levels: 2, tokens_per_cycle: 10).close
    end
  end

  def test_cap_from_another_context_is_refused
    with_context do |ctx, _cap|
      Zxv::Context.open do |other|
        foreign = other.root_capability
        [-> { ctx.enable_currency("NGN", cap: foreign) },
         -> { ctx.enable_currency("NGN", minor_units: 2, cap: foreign) },
         -> { ctx.register_private_currency("VFV", 2, cap: foreign) }].each do |call|
          err = denied(&call)
          assert_match(/different context/, err.detail)
        end
        assert_empty ctx.currencies
        assert_empty other.currencies
      end
    end
  end

  def test_no_cap_forms_still_work_and_warn
    with_context do |ctx, _cap|
      previous = Warning[:deprecated]
      Warning[:deprecated] = true
      _out, err = capture_io do
        ctx.enable_currency("NGN")
        ctx.register_private_currency("VFV", 2)
        Zxv::Message.new(ctx, :pacs008).close
      end
      Warning[:deprecated] = previous
      assert_match(/enable_currency without cap: is deprecated/, err)
      assert_match(/register_private_currency without cap: is deprecated/, err)
      assert_match(/Message.new\(context, kind\) is deprecated/, err)
      assert_equal %w[NGN VFV], ctx.currencies.map(&:code)
    end
  end
end
