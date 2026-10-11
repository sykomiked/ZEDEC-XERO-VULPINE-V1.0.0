# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestWireSwarm < Minitest::Test
  include ZxvTest

  def test_ubh168_frames_byte_for_byte
    vectors["ubh168"].each do |v|
      tag = Zxv::Ubh168Frame.tag(v["trunk"], v["shell"], v["sync"])
      assert_equal v["tag"], tag
      f = Zxv::Ubh168Frame.new(tag: tag, s_plus: v["s_plus"], s_minus: v["s_minus"])
      assert_equal v["frame"], unhex(f.to_bytes), "pure Ruby pack"
      assert_equal v["frame"], unhex(f.native_bytes), "libzxv pack"
      back = Zxv::Ubh168Frame.from_bytes(hex(v["frame"]))
      assert_equal v["s_plus"], back.s_plus
      assert_equal v["s_minus"], back.s_minus
      assert_equal v["truth"], Zxv::Ubh168Frame::TRUTHS.index(f.resolve)
    end
  end

  def test_ubh168_idle_frames
    vectors["ubh168_idle"].each_with_index do |h, trunk|
      idle = Zxv::Ubh168Frame.idle(trunk)
      assert_equal h, unhex(idle.to_bytes)
    end
    assert_equal :neutral, Zxv::Ubh168Frame.idle(0).resolve
    assert_raises(Zxv::ZxvUsageError) { Zxv::Ubh168Frame.tag(10, 0, 0) }
    assert_raises(Zxv::ZxvUsageError) { Zxv::Ubh168Frame.from_bytes("\x00" * 20) }
  end

  def test_swarm_allotments_match_vectors
    sv = vectors["swarm"]
    with_context do |_ctx, cap|
      swarm = Zxv::Swarm.new(cap, levels: sv["levels"], tokens_per_cycle: sv["tokens_per_cycle"])
      sv["models"].each { |id, level| swarm.register(id, level) }
      swarm.set_mood(*sv["mood"])
      sv["feelings"].each { |id, cp, intensity| swarm.set_feeling(id, cp, intensity) }
      swarm.begin_cycle
      sv["allotments"].each do |id, allotted, imaginary|
        a = swarm.allotment(id)
        assert_equal [allotted, imaginary], [a.allotted, a.imaginary], "model #{id}"
      end
      total = sv["allotments"].sum { |_, a, _| a }
      assert_equal sv["tokens_per_cycle"], total

      first = sv["allotments"][0]
      assert_equal 10, swarm.consume(first[0], 10)
      err = assert_raises(Zxv::BudgetExhaustedError) { swarm.consume(first[0], first[1]) }
      assert_equal first[1] - 10, err.granted
      assert_operator swarm.end_cycle, :>, 0
      assert_raises(Zxv::ZxvRefusedError) { swarm.register(999, 0) } # level 0 is full (CAPACITY)
      assert_raises(Zxv::ZxvUsageError) { swarm.set_mood(0x1F600, 1) } # not in the palette
    end
    sv["level_capacity"].each_with_index { |c, d| assert_equal c, Zxv::Swarm.level_capacity(d) }
    prof = Zxv::Swarm.emotion_profile(0x1F914)
    assert_kind_of Integer, prof.temperature_milli
  end
end
