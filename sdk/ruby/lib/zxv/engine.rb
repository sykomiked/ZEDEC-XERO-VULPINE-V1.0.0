# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # A GGUF model on the integer inference engine (bit-identical on every
  # machine). Read-only after loading; sessions on any threads may share it.
  #
  #   Zxv::Model.open(cap, "model.gguf") do |model|
  #     session = model.new_session(n_ctx: 512)
  #     result = session.generate("Hello", Zxv::GenerateOptions.new(max_tokens: 32))
  #     puts result.text
  #   end
  class Model < NativeHandle
    INFO = { arch: 1, n_vocab: 2, n_ctx_train: 3, n_layer: 4, n_embd: 5, n_head: 6, n_head_kv: 7, eos: 8 }.freeze
    ARCHS = { 1 => :qwen2, 2 => :qwen3, 3 => :llama }.freeze

    # Open a GGUF file (+tokenizer_path+ nil = the model's own tokenizer).
    # With a block, yields the model and closes it afterwards.
    # @param capability [Capability] needs :ai_inference
    def self.open(capability, path, tokenizer_path = nil)
      model = new(capability, path, tokenizer_path)
      return model unless block_given?

      begin
        yield model
      ensure
        model.close
      end
    end

    # @return [Capability]
    attr_reader :capability

    # @api private (use Model.open)
    def initialize(capability, path, tokenizer_path)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_model_open(capability.ptr, Call.text!(path.to_s, "path"),
                                           tokenizer_path && Call.text!(tokenizer_path.to_s, "tokenizer_path"), out),
                 capability.context)
      super(capability.context, out.read_pointer, :zxv_model_destroy)
    end

    # One metadata value: :arch, :n_vocab, :n_ctx_train, :n_layer, :n_embd,
    # :n_head, :n_head_kv or :eos.
    def info(key)
      out = Call.out(:int64)
      Call.check(Native.lib.zxv_model_info(ptr, INFO.fetch(key), out), context)
      v = out.read_int64
      key == :arch ? ARCHS.fetch(v, v) : v
    end

    # Token ids of UTF-8 text.
    # @return [Array<Integer>]
    def tokenize(text, parse_special: true)
      t = Call.text!(text, "text")
      n = Call.out(:size_t)
      mem = Call.mem(t.b, allow_null: true)
      rc = Native.lib.zxv_tokenize(ptr, mem, t.bytesize, parse_special ? 1 : 0, nil, 0, n)
      Call.check(rc, context) unless rc == Status::E_BUFFER
      count = n.read(:size_t)
      ids = FFI::MemoryPointer.new(:int32, [count, 1].max)
      Call.check(Native.lib.zxv_tokenize(ptr, mem, t.bytesize, parse_special ? 1 : 0, ids, count, n), context)
      ids.read_array_of_int32(n.read(:size_t))
    end

    # Text of token ids.
    # @return [String]
    def detokenize(ids)
      arr = ids.map { |i| Call.int!(i, "token", min: -(2**31), max: 2**31 - 1) }
      mem = FFI::MemoryPointer.new(:int32, [arr.size, 1].max).write_array_of_int32(arr)
      rc, s = Call.text(ptr, mem, arr.size) { |*a| Native.lib.zxv_detokenize(*a) }
      Call.check(rc, context)
      s
    end

    # A new session (KV cache). +n_ctx+ 0 = min(n_ctx_train, 4096);
    # +kv+ :q16 (exact) or :q8.
    # @return [Session]
    def new_session(n_ctx: 0, kv: :q16)
      Session.new(self, n_ctx, kv)
    end

    # LoRA adapters are declared in the ABI but not built yet.
    # @raise [Zxv::NotImplementedError] while ZXV_FEAT_LORA is off
    def load_lora(path, scale: Rational.of(1))
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_lora_load(capability.ptr, ptr, Call.text!(path.to_s, "path"),
                                          Rational.coerce_arg(scale).to_q16, out), context)
      raise Zxv::NotImplementedError.new(Status::E_NOTIMPL, "LoRA handles are not wrapped by this SDK version")
    end
  end

  # Sampling options. Exact values only: +temperature+, +top_p+ and
  # +repeat_penalty+ are Zxv::Rational (or Integer / ::Rational), converted
  # to Q16.16. temperature 0 = greedy; top_p 0 = off; repeat_penalty 0 = off.
  class GenerateOptions
    attr_reader :max_tokens, :temperature, :top_k, :top_p, :repeat_penalty, :repeat_last_n, :seed

    def initialize(max_tokens: 64, temperature: 0, top_k: 0, top_p: 0, repeat_penalty: 0, repeat_last_n: 64, seed: 0)
      @max_tokens = Call.uint32!(max_tokens, "max_tokens")
      @temperature = Rational.coerce_arg(temperature)
      @top_k = Call.uint32!(top_k, "top_k")
      @top_p = Rational.coerce_arg(top_p)
      @repeat_penalty = Rational.coerce_arg(repeat_penalty)
      @repeat_last_n = Call.uint32!(repeat_last_n, "repeat_last_n")
      @seed = Call.uint64!(seed, "seed")
      freeze
    end

    # Build from raw Q16.16 integers (as vectors.json stores them).
    def self.from_q16(temperature_q16: 0, top_p_q16: 0, repeat_penalty_q16: 0, **rest)
      new(temperature: Rational.of(temperature_q16, 65_536), top_p: Rational.of(top_p_q16, 65_536),
          repeat_penalty: Rational.of(repeat_penalty_q16, 65_536), **rest)
    end
  end

  # What a generation produced. +stop_reason+ is :eos, :max_tokens,
  # :callback, :context_full or :budget.
  GenerateResult = Struct.new(:text, :tokens, :stop_reason, :prompt_tokens, keyword_init: true)

  # A conversation (KV cache) on a model. One thread at a time; generation
  # releases the Ruby GVL while the engine runs.
  class Session < NativeHandle
    STOPS = { 1 => :eos, 2 => :max_tokens, 3 => :stop_string, 4 => :callback, 5 => :context_full, 6 => :budget }.freeze
    KV = { q16: 0, q8: 1 }.freeze

    # @return [Model]
    attr_reader :model

    # @api private (use Model#new_session)
    def initialize(model, n_ctx, kv)
      @model = model
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_session_create(model.ptr, Call.uint32!(n_ctx, "n_ctx"), KV.fetch(kv), out), model.context)
      super(model.context, out.read_pointer, :zxv_session_destroy)
      @lock = Mutex.new
    end

    # Generate, appending to the conversation. With a block, streams each
    # token: the block receives (piece_bytes, token_id) and may return
    # +:stop+ to end generation early (stop_reason :callback).
    # @return [GenerateResult]
    # @raise [BudgetExhaustedError] a bound budget cannot cover the prompt
    def generate(prompt, options = GenerateOptions.new, &on_token)
      @lock.synchronize { run(prompt, options, on_token) }
    end

    # #generate on a background thread. @return [Future] of GenerateResult
    def generate_async(prompt, options = GenerateOptions.new, &on_token)
      Future.new { generate(prompt, options, &on_token) }
    end

    # Forget the conversation.
    def reset
      Call.check(Native.lib.zxv_session_reset(ptr), context)
      self
    end

    # @return [Integer] tokens in the KV cache
    def position
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_session_position(ptr, out), context)
      out.read_uint32
    end

    # Charge a budget one unit per prompt and generated token (nil unbinds).
    def bind_budget(budget)
      rc = if budget.nil?
             Native.lib.zxv_session_bind_budget(ptr, nil, 0)
           else
             Native.lib.zxv_session_bind_budget(ptr, budget.budgets.ptr, budget.id)
           end
      Call.check(rc, context)
      @budget = budget # keep the Budgets alive while bound
      self
    end

    private

    def run(prompt, opt, on_token)
      p = Call.text!(prompt, "prompt").b
      tokens = []
      text = +"".b
      error = nil
      cb = FFI::Function.new(:int32, %i[pointer int32 pointer size_t]) do |_u, tok, piece, len|
        bytes = len.zero? ? "".b : piece.read_bytes(len)
        tokens << tok
        text << bytes
        if on_token
          begin
            on_token.call(bytes.dup.force_encoding(Encoding::UTF_8), tok) == :stop ? 0 : 1
          rescue Exception => e # rubocop:disable Lint/RescueException
            error = e
            0
          end
        else
          1
        end
      end
      np = Call.out(:uint32)
      ng = Call.out(:uint32)
      stop = Call.out(:int32)
      rc = Native.lib.zxv_generate(model.capability.ptr, ptr, Call.mem(p, allow_null: true), p.bytesize, opt.max_tokens,
                                   opt.temperature.to_q16, opt.top_k, opt.top_p.to_q16, opt.repeat_penalty.to_q16,
                                   opt.repeat_last_n, opt.seed, cb, nil, np, ng, stop)
      raise error if error

      Call.check(rc, context)
      GenerateResult.new(text: text.force_encoding(Encoding::UTF_8), tokens: tokens,
                         stop_reason: STOPS.fetch(stop.read_int32, stop.read_int32), prompt_tokens: np.read_uint32)
    end
  end
end
