# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "lib/zxv/version"

Gem::Specification.new do |s|
  s.name = "zxv"
  s.version = Zxv::VERSION
  s.summary = "Ruby SDK for the ZXV platform core (libzxv)"
  s.description = "Exact rationals, four-valued logic, capabilities, the triple ledger, budgets, phase ticks, " \
                  "UBH-168 frames, the swarm economy, post-quantum crypto, the integer inference engine and " \
                  "EVM state channels, wrapping libzxv's stable C ABI."
  s.authors = ["Michael Laurence Curzi", "36N9 Genetics, LLC"]
  s.license = "Apache-2.0"
  s.required_ruby_version = ">= 3.1"
  s.files = Dir["lib/**/*.rb", "lib/zxv/native/*", "README.md", "examples/*.rb"]
  s.require_paths = ["lib"]
  s.add_dependency "ffi", "~> 1.15"
  s.add_development_dependency "minitest", "~> 5.0"
  s.add_development_dependency "rake", ">= 13"
  s.metadata["rubygems_mfa_required"] = "true"
end
