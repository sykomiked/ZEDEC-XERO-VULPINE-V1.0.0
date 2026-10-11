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
end
