# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # The swarm token economy: each cycle, tokens_per_cycle tokens are split
  # across Fibonacci levels (the real axis), plus an imaginary (emotional)
  # share by mood and per-model feelings. All exact integers.
  #
  #   swarm = Zxv::Swarm.new(cap, levels: 3, tokens_per_cycle: 1000)
  #   swarm.register(1, 0)
  #   swarm.begin_cycle
  #   swarm.allotment(1).allotted   # => 1000
  class Swarm < NativeHandle
    # This cycle's allotment of a model.
    Allotment = Struct.new(:allotted, :imaginary, :used, keyword_init: true) do
      # @return [Integer] real (Fibonacci) part
      def real = allotted - imaginary
    end
    # Behaviour profile of an emotion.
    EmotionProfile = Struct.new(:valence, :temperature_milli, :explore_permille, :verify_passes, :priority,
                                keyword_init: true)

    # Capacity of level +level+ (0-based): F(level + 2).
    def self.level_capacity(level)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_swarm_level_capacity(Call.uint32!(level, "level"), out))
      out.read_uint32
    end

    # Behaviour profile of an emoji code point of the kernel palette.
    # @return [EmotionProfile]
    def self.emotion_profile(codepoint)
      v = Call.out(:int32)
      t = Call.out(:uint32)
      x = Call.out(:uint32)
      p = Call.out(:uint32)
      pr = Call.out(:uint32)
      Call.check(Native.lib.zxv_swarm_emotion_profile(Call.uint32!(codepoint, "codepoint"), v, t, x, p, pr))
      EmotionProfile.new(valence: v.read_int32, temperature_milli: t.read_uint32, explore_permille: x.read_uint32,
                         verify_passes: p.read_uint32, priority: pr.read_uint32)
    end

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :swarm
    def initialize(capability, levels:, tokens_per_cycle:)
      @capability = capability
      out = Call.out(:pointer)
      Call.check(Native.lib.zxv_swarm_create_cap(capability.ptr, Call.uint32!(levels, "levels"),
                                                 Call.uint64!(tokens_per_cycle, "tokens_per_cycle"), out), capability.context)
      super(capability.context, out.read_pointer, :zxv_swarm_destroy)
    end

    # Register a model at a level. ZXV_E_CAPACITY (refused) when the level is full.
    def register(model_id, level)
      Call.check(Native.lib.zxv_swarm_register(capability.ptr, ptr, Call.uint32!(model_id, "model_id"),
                                               Call.uint32!(level, "level")), context)
      self
    end

    # Activate or pause a model.
    def set_active(model_id, active)
      Call.check(Native.lib.zxv_swarm_set_active(capability.ptr, ptr, Call.uint32!(model_id, "model_id"), active ? 1 : 0), context)
      self
    end

    # Change the tokens per cycle.
    def tokens_per_cycle=(n)
      Call.check(Native.lib.zxv_swarm_set_rate(capability.ptr, ptr, Call.uint64!(n, "tokens_per_cycle")), context)
    end

    # Set the swarm's mood (emoji code point of the palette, intensity 0..5).
    def set_mood(codepoint, intensity)
      Call.check(Native.lib.zxv_swarm_set_mood(capability.ptr, ptr, Call.uint32!(codepoint, "codepoint"),
                                               Call.int!(intensity, "intensity", min: 0, max: 5)), context)
      self
    end

    # Set one model's feeling.
    def set_feeling(model_id, codepoint, intensity)
      Call.check(Native.lib.zxv_swarm_set_feeling(capability.ptr, ptr, Call.uint32!(model_id, "model_id"),
                                                  Call.uint32!(codepoint, "codepoint"),
                                                  Call.int!(intensity, "intensity", min: 0, max: 5)), context)
      self
    end

    # Start a cycle; returns its number.
    def begin_cycle
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_swarm_begin_cycle(capability.ptr, ptr, out), context)
      out.read_uint64
    end

    # Take +requested+ tokens for a model; returns the number granted.
    # @raise [BudgetExhaustedError] when fewer were left; #granted holds
    #   what was granted (and is consumed)
    def consume(model_id, requested)
      out = Call.out(:uint64)
      rc = Native.lib.zxv_swarm_consume(capability.ptr, ptr, Call.uint32!(model_id, "model_id"),
                                        Call.uint64!(requested, "requested"), out)
      if rc == Status::E_BUDGET
        err = Call.error_for(rc)
        err.granted = out.read_uint64
        raise err
      end
      Call.check(rc, context)
      out.read_uint64
    end

    # @return [Allotment]
    def allotment(model_id)
      a = Call.out(:uint64)
      i = Call.out(:uint64)
      u = Call.out(:uint64)
      Call.check(Native.lib.zxv_swarm_allotment(ptr, Call.uint32!(model_id, "model_id"), a, i, u), context)
      Allotment.new(allotted: a.read_uint64, imaginary: i.read_uint64, used: u.read_uint64)
    end

    # Close the cycle; returns the unused tokens (they never carry over).
    def end_cycle
      out = Call.out(:uint64)
      Call.check(Native.lib.zxv_swarm_end_cycle(capability.ptr, ptr, out), context)
      out.read_uint64
    end
  end
end
