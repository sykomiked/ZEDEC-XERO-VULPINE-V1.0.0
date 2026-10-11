# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestTruth < Minitest::Test
  include ZxvTest
  T = Zxv::Truth

  def test_values_and_names_match_vectors
    lp = vectors["lpres"]
    assert_equal lp["names"], T::ALL.map(&:name)
    assert_equal [0, 1, 2, 3], T::ALL.map(&:value)
    assert_equal 0, T::TRUE.mode
  end

  def test_tables_match_vectors
    lp = vectors["lpres"]
    T::ALL.each do |a|
      assert_equal lp["not"][a.value], a.not.value
      assert_equal lp["not"][a.value], (~a).value
      T::ALL.each do |b|
        assert_equal lp["and"][a.value][b.value], a.and(b).value, "#{a} and #{b}"
        assert_equal lp["or"][a.value][b.value], (a | b).value, "#{a} or #{b}"
      end
    end
  end

  def test_no_boolean_shortcut
    assert_raises(Zxv::TruthConversionError) { !T::FALSE }
    assert_raises(Zxv::TruthConversionError) { T::BOTH.require_true! }
    assert_same T::TRUE, T::TRUE.require_true!
    assert T::TRUE.certainly_true?
    assert_equal false, T::BOTH.certainly_true?
    assert_equal :held, T::BOTH.to_boolean(on_both: :held, on_neither: :gap)
    assert_equal :gap, T::NEITHER.to_boolean(on_both: :held, on_neither: :gap)
    assert_equal false, T::FALSE.to_boolean(on_both: true, on_neither: true)
  end

  def test_from_evidence
    assert_equal T::BOTH, T.from_evidence(2, 1)
    assert_equal T::TRUE, T.from_evidence(1, 0)
    assert_equal T::FALSE, T.from_evidence(0, 3)
    assert_equal T::NEITHER, T.from_evidence(0, 0)
    assert_raises(Zxv::ZxvUsageError) { T.of(7) }
  end
end
