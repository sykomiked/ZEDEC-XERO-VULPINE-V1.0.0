# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestRational < Minitest::Test
  include ZxvTest
  R = Zxv::Rational

  def r(pair) = R.of(*pair)

  def test_operations_match_vectors
    vectors["rational_ops"].each do |v|
      a = r(v["a"])
      b = r(v["b"])
      { "add" => :+, "sub" => :-, "mul" => :*, "div" => :/ }.each do |k, op|
        res = a.public_send(op, b)
        assert_equal v[k], [res.numerator, res.denominator], "#{v['a']} #{op} #{v['b']}"
      end
      assert_equal v["cmp"], a <=> b
    end
  end

  def test_parse_matches_vectors
    vectors["rational_parse"].each do |v|
      if v["status"].zero?
        x = R.parse(v["text"])
        assert_equal v["value"], [x.numerator, x.denominator], v["text"]
        assert_equal v["format"], x.to_s
      else
        err = assert_raises(Zxv::ZxvUsageError, v["text"]) { R.parse(v["text"]) }
        assert_equal v["status"], err.status, v["text"]
      end
    end
  end

  def test_ieee754_boundary_matches_vectors
    vectors["ieee754"].each do |v|
      bits = v["bits"].to_i(16)
      if v["status"].zero?
        x = R.from_ieee754_bits(bits)
        assert_equal v["value"], [x.numerator, x.denominator], v["bits"]
        f = [bits].pack("Q>").unpack1("G")
        assert_equal x, R.from_double_exact(f)
      else
        err = assert_raises(Zxv::ZxvUsageError) { R.from_ieee754_bits(bits) }
        assert_equal v["status"], err.status, v["bits"]
        assert_kind_of Zxv::FloatRejectedError, err if v["status"] == Zxv::Status::E_FLOAT
      end
    end
    assert_equal R.of(3_602_879_701_896_397, 36_028_797_018_963_968), R.from_double_exact(0.1)
    refute_equal R.of(1, 10), R.from_double_exact(0.1)
  end

  def test_q16_matches_vectors
    vectors["q16"].each do |v|
      if v["status"].zero?
        assert_equal v["q16"], r(v["value"]).to_q16
      else
        assert_raises(Zxv::ZxvUsageError) { r(v["value"]).to_q16 }
      end
    end
  end

  def test_floats_are_rejected_everywhere
    assert_raises(Zxv::FloatRejectedError) { R.of(0.5) }
    assert_raises(Zxv::FloatRejectedError) { R.of(1, 2.0) }
    assert_raises(Zxv::FloatRejectedError) { R.from(0.1) }
    assert_raises(Zxv::FloatRejectedError) { R.of(1) + 0.5 }
    assert_raises(Zxv::FloatRejectedError) { R.of(1) < 0.5 }
    assert_raises(Zxv::FloatRejectedError) { Zxv::Rational.from_double_exact(Float::NAN) }
    assert_raises(Zxv::FloatRejectedError) { Zxv::Rational.from_double_exact(Float::INFINITY) }
    refute_respond_to R.of(1), :to_f
  end

  def test_exact_host_types
    require "bigdecimal"
    assert_equal R.of(3, 10), R.from(Kernel.Rational(3, 10))
    assert_equal R.of(51, 4), R.from(BigDecimal("12.75"))
    assert_equal Kernel.Rational(-51, 4), R.parse("-12.75").to_r
    assert_equal R.of(1, 2), R.of(2, 4)
    assert_equal R.of(1, 2).hash, R.of(2, 4).hash
  end

  def test_overflow_is_detected
    err = assert_raises(Zxv::ZxvUsageError) { R.of(2**63 - 1) * 2 }
    assert_equal Zxv::Status::E_RANGE, err.status
    assert_raises(Zxv::ZxvUsageError) { R.of(1, 2) / 0 }
    assert_raises(Zxv::ZxvUsageError) { R.of(1, 0) }
  end
end
