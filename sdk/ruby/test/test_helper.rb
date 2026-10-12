# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "zxv"
require "json"
require "minitest/autorun"

module ZxvTest
  SDK = File.expand_path("../..", __dir__)
  VECTORS = JSON.parse(File.read(File.join(SDK, "conformance/vectors.json")))
  FIXTURES = File.join(SDK, "conformance/fixtures")

  def vectors = VECTORS
  def hex(s) = [s].pack("H*")
  def unhex(b) = b.unpack1("H*")

  def with_context
    Zxv::Context.open { |ctx| yield ctx, ctx.root_capability }
  end

  # ABI 1.5: a context with a root capability holding every 1.5 bit.
  def with_context15
    Zxv::Context.open { |ctx| yield ctx, ctx.root_capability(:all_1_5) }
  end

  # Run the block. For "ZXV_OK" return its value; otherwise assert it
  # raises a ZXV error whose status name is +name+ and return the error.
  def expect_status(name, &block)
    return yield if name == "ZXV_OK"

    err = assert_raises(Zxv::StatusError, Zxv::ZxvFatal, &block)
    assert_equal name, Zxv::Status.name_of(err.status), err.message
    err
  end

  # assert_equal that also accepts an expected nil.
  def assert_eq(expected, actual, msg = nil)
    expected.nil? ? assert_nil(actual, msg) : assert_equal(expected, actual, msg)
  end

  # 64-bit vector numbers are decimal strings.
  def u64(text) = Integer(text, 10)

  def sha3(bytes) = Zxv::Hashes.sha3_256(bytes)
end
