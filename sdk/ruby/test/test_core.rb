# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestCore < Minitest::Test
  include ZxvTest

  def test_abi_version_matches_vectors
    assert_equal vectors["abi"][0..1], Zxv.abi_version[0..1]
    assert Zxv.feature?(:rational)
    refute Zxv.feature?(:lora), "LoRA is not built"
  end

  def test_status_names_and_classes
    vectors["status"].each do |s|
      assert_equal s["name"], Zxv::Status.name_of(s["code"])
      assert_equal s["class"], Zxv::Status.class_of(s["code"])
    end
  end

  def test_error_hierarchy_follows_status_class
    assert_operator Zxv::ZxvUsageError, :<, ArgumentError
    assert_operator Zxv::FloatRejectedError, :<, Zxv::ZxvUsageError
    [Zxv::CapabilityDeniedError, Zxv::BudgetExhaustedError, Zxv::InsufficientFundsError,
     Zxv::VerificationFailedError, Zxv::NotImplementedError].each { |k| assert_operator k, :<, Zxv::ZxvRefusedError }
    assert_operator Zxv::ZxvFatal, :<, Exception
    refute_operator Zxv::ZxvFatal, :<, StandardError
    refute_operator Zxv::ZxvFatal, :<, Zxv::StatusError
    vectors["status"].each do |s|
      next if s["code"].zero?

      err = Zxv::Call.error_for(s["code"])
      expected = { 1 => Zxv::ZxvUsageError, 2 => Zxv::ZxvRefusedError, 3 => Zxv::ZxvFatal, 4 => Zxv::ZxvSystemError }
      assert_kind_of expected.fetch(s["class"]), err, s["name"]
      assert_equal s["code"], err.status
      assert_equal s["name"], err.status_name
    end
  end

  def test_random_bytes
    a = Zxv.random_bytes(32)
    assert_equal 32, a.bytesize
    refute_equal a, Zxv.random_bytes(32)
  end

  def test_iso4217
    ngn = Zxv::Iso4217.lookup("NGN")
    assert_equal 566, ngn.numeric
    assert_equal 2, ngn.minor_units
    assert ngn.payable?
    assert_nil Zxv::Iso4217.lookup("VFV")
    assert_equal "NGN", Zxv::Iso4217.by_numeric(566).code
    assert_nil Zxv::Iso4217.by_numeric(777)
    assert_equal "Naira", Zxv::Iso4217.name("NGN")
    assert_includes Zxv::Iso4217.codes, "GHS"
    assert_match(/\A\d{4}-\d{2}-\d{2}\z/, Zxv::Iso4217.published)
    assert Zxv::Iso4217.african_union_member?("NG")
  end

  def test_card_check_digits
    assert Zxv::CardCheck.valid?(:dragon, "4111111111111111")
    refute Zxv::CardCheck.valid?(:dragon, "4111111111111112")
  end
end
