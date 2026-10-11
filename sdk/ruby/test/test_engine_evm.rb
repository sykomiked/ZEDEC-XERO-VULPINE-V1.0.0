# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestEngineEvm < Minitest::Test
  include ZxvTest

  def gen = vectors["generate"]
  def prompt = File.binread(File.join(FIXTURES, gen["prompt"])).force_encoding(Encoding::UTF_8)

  def open_model(cap, &blk)
    Zxv::Model.open(cap, File.join(FIXTURES, gen["model"]), File.join(FIXTURES, gen["tokenizer"]), &blk)
  end

  def test_tokenize_and_greedy_generation
    with_context do |_ctx, cap|
      assert_raises(Zxv::CapabilityDeniedError) { open_model(cap.attenuate(:ledger_read)) }
      err = assert_raises(Zxv::ZxvSystemError) { Zxv::Model.open(cap, "/nonexistent.gguf") }
      assert_equal "ZXV_E_IO", err.status_name
      open_model(cap) do |model|
        assert_equal 1200, model.info(:n_vocab)
        assert_equal :qwen2, model.info(:arch)
        ids = model.tokenize(prompt)
        assert_equal gen["prompt_tokens"], ids
        assert_equal prompt, model.detokenize(ids)

        s = model.new_session(n_ctx: gen["n_ctx"])
        streamed = []
        res = s.generate(prompt, Zxv::GenerateOptions.new(max_tokens: gen["greedy"]["max_tokens"])) { |_p, t| streamed << t }
        assert_equal gen["greedy"]["tokens"], res.tokens
        assert_equal res.tokens, streamed
        assert_equal :max_tokens, res.stop_reason
        assert_equal gen["prompt_tokens"].size, res.prompt_tokens
        assert_equal model.detokenize(res.tokens), res.text
        assert_raises(Zxv::NotImplementedError) { model.load_lora("x.gguf") }
      end
    end
  end

  def test_seeded_sampling_and_callback_stop
    sv = gen["sampled"]
    with_context do |_ctx, cap|
      open_model(cap) do |model|
        s = model.new_session(n_ctx: gen["n_ctx"])
        opts = Zxv::GenerateOptions.from_q16(max_tokens: sv["max_tokens"], temperature_q16: sv["temperature_q16"],
                                             top_k: sv["top_k"], top_p_q16: sv["top_p_q16"],
                                             repeat_penalty_q16: sv["repeat_penalty_q16"],
                                             repeat_last_n: sv["repeat_last_n"], seed: sv["seed"])
        assert_equal sv["tokens"], s.generate(prompt, opts).tokens
        s.reset
        assert_equal sv["tokens"], s.generate_async(prompt, opts).value(60).tokens
        s.reset
        n = 0
        res = s.generate(prompt, Zxv::GenerateOptions.new(max_tokens: 16)) { (n += 1) == 3 ? :stop : nil }
        assert_equal :callback, res.stop_reason
        assert_equal 3, res.tokens.size
        assert_raises(Zxv::FloatRejectedError) { Zxv::GenerateOptions.new(temperature: 0.7) }
      end
    end
  end

  def test_budget_bound_generation
    with_context do |_ctx, cap|
      open_model(cap) do |model|
        s = model.new_session(n_ctx: gen["n_ctx"])
        np = gen["prompt_tokens"].size
        budget = Zxv::Budgets.new(cap).budget("gen", resource: :tokens, limit: np + 8)
        s.bind_budget(budget)
        res = s.generate(prompt, Zxv::GenerateOptions.new(max_tokens: 16))
        assert_equal :budget, res.stop_reason
        assert_equal 8, res.tokens.size
        s.reset
        assert_raises(Zxv::BudgetExhaustedError) { s.generate(prompt, Zxv::GenerateOptions.new(max_tokens: 16)) }
        s.bind_budget(nil)
      end
    end
  end

  def test_evm_state_matches_vectors
    v = vectors["evm_state"]
    with_context do |ctx, cap|
      ch = Zxv::StateChannel.new(cap)
      st = ch.encode_state(channel_id: hex(v["channel_id"]), nonce: v["nonce"],
                           parties: v["addresses"].map { |a| hex(a) }, balances: v["balances"])
      assert_equal v["encoding"], unhex(st.encoding)
      assert_equal v["digest"], unhex(st.digest)
      st2 = ch.encode_state(channel_id: "0x#{v['channel_id']}", nonce: v["nonce"],
                            parties: v["addresses"].map { |a| "0x#{a}" }, balances: v["balances"])
      assert_equal st, st2

      ctx.enable_currency("USD", minor_units: 2, cap: cap)
      ledger = Zxv::Ledger.new(cap)
      a = ledger.open_account("a", "USD", kind: :issuer)
      b = ledger.open_account("b", "USD")
      ledger.post(a, b, 250, "USD", "x")
      parties = v["addresses"][0, 2].map { |x| hex(x) }
      from_ledger = ch.ledger_state(channel_id: hex(v["channel_id"]), nonce: 1, currency: "USD", accounts: [a, b], parties: parties)
      explicit = ch.encode_state(channel_id: hex(v["channel_id"]), nonce: 1, parties: parties, balances: [-250, 250])
      assert_equal explicit, from_ledger
      assert_equal 10 * 32, from_ledger.encoding.bytesize
      assert_raises(Zxv::CapabilityDeniedError) do
        Zxv::StateChannel.new(cap.attenuate(:ledger_read)).encode_state(channel_id: hex(v["channel_id"]), nonce: 1,
                                                                        parties: parties, balances: [1, 2])
      end
    end
  end
end
