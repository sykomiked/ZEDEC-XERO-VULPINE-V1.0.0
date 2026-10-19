# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "zxv/version"
require_relative "zxv/native"
require_relative "zxv/errors"
require_relative "zxv/call"
require_relative "zxv/rational"
require_relative "zxv/truth"
require_relative "zxv/context"
require_relative "zxv/currency"
require_relative "zxv/ledger"
require_relative "zxv/message"
require_relative "zxv/future"
require_relative "zxv/attestations"
require_relative "zxv/budgets"
require_relative "zxv/phase_clock"
require_relative "zxv/ubh168"
require_relative "zxv/swarm"
require_relative "zxv/crypto"
require_relative "zxv/engine"
require_relative "zxv/state_channel"
require_relative "zxv/hexagram"
require_relative "zxv/hw"
require_relative "zxv/pure_handle"
require_relative "zxv/treaty"
require_relative "zxv/cid"
require_relative "zxv/aipi"
require_relative "zxv/coin"
require_relative "zxv/fee"
require_relative "zxv/vhub"
require_relative "zxv/freight"
require_relative "zxv/web4"
require_relative "zxv/paradox"
require_relative "zxv/vino"

# Ruby SDK for ZXV: a thin, idiomatic wrapper of libzxv (zxv_api.h).
#
#   require "zxv"
#   Zxv::Context.open do |ctx|
#     cap = ctx.root_capability
#     ...
#   end
#
# See README.md for concepts (exactness, four-valued logic, capabilities,
# fail-closed errors) and the API overview.
module Zxv
  # Feature bits (zxv_features), by name.
  FEATURES = {
    ledger: 0x1, mx_pacs008: 0x2, mx_camt053: 0x4, iso4217: 0x8, mx_pacs002: 0x10, mx_pacs004: 0x20,
    mx_pacs009: 0x40, mx_camt056: 0x80, mx_camt029: 0x100, netting: 0x200, cards: 0x400, card_check: 0x800,
    vss: 0x1000, rational: 0x2000, lpres: 0x4000, caps: 0x8000, rmag: 0x10000, phase: 0x20000, wire: 0x40000,
    swarm: 0x80000, hash: 0x100000, mlkem1024: 0x200000, mldsa87: 0x400000, ed25519: 0x800000,
    tensor: 0x1000000, lora: 0x2000000, evm: 0x4000000, hex: 0x8000000,
    hw: 0x10000000
  }.freeze

  # ABI 1.5+ feature bits (zxv_features2), by name (+:vino+ since 1.6). A group whose bit is
  # clear is not built into the loaded library: every call in it raises
  # Zxv::NotImplementedError (ZXV_E_NOTIMPL).
  FEATURES2 = {
    treaty: 0x01, aipi: 0x02, coin: 0x04, fee: 0x08, vhub: 0x10, freight: 0x20, web4: 0x40, paradox: 0x80,
    vino: 0x100
  }.freeze

  @fatal_policy = :raise

  class << self
    # How ZxvFatal is delivered: :raise (default) raises ZxvFatal (an
    # Exception outside StandardError); :abort prints the error and ends the
    # process at once with exit status 70 (Process.exit!: no ensure blocks,
    # no at_exit handlers; the only truly uncatchable form).
    attr_reader :fatal_policy

    # @param policy [Symbol] :raise or :abort
    def fatal_policy=(policy)
      raise ZxvUsageError.new(Status::E_ARG, "fatal_policy must be :raise or :abort") unless %i[raise abort].include?(policy)

      @fatal_policy = policy
    end

    # Load libzxv from an explicit path (otherwise it loads on first use
    # from ZXV_LIBRARY_PATH, the bundled copy, or the system path).
    # @return [String] what was loaded
    def load_library(path = nil) = Native.load!(path)

    # @return [Array(Integer, Integer, Integer)] the loaded library's ABI version
    def abi_version
      ptrs = Array.new(3) { Call.out(:uint32) }
      Native.lib.zxv_version(*ptrs)
      ptrs.map(&:read_uint32)
    end

    # @return [Array<Symbol>] feature groups live in this build
    def features
      m = Call.out(:uint32)
      Native.lib.zxv_features(m)
      mask = m.read_uint32
      FEATURES.select { |_, bit| mask & bit != 0 }.keys
    end

    # The ABI 1.5 groups live in this build (zxv_features2). Empty against
    # a library older than 1.5.
    # @return [Array<Symbol>] keys of {FEATURES2}
    def features2
      m = Call.out(:uint32)
      return [] unless Native.lib.zxv_features2(m) == Status::OK

      mask = m.read_uint32
      FEATURES2.select { |_, bit| mask & bit != 0 }.keys
    end

    # Whether feature group +name+ is live: a key of {FEATURES} or of
    # {FEATURES2} (e.g. :hex, :treaty, :web4).
    # @return [Boolean]
    def feature?(name) = features.include?(name) || features2.include?(name)

    # Bytes from the OS CSPRNG.
    # @return [String]
    def random_bytes(n)
      len = Call.int!(n, "n", min: 0, max: 1 << 24)
      buf = FFI::MemoryPointer.new(:uint8, [len, 1].max)
      Call.check(Native.lib.zxv_random(buf, len))
      buf.read_bytes(len)
    end

    private

    def on_fatal(err)
      return unless @fatal_policy == :abort

      $stderr.puts("zxv: FATAL #{err.message}; aborting (Zxv.fatal_policy = :abort)")
      $stderr.flush
      Process.exit!(70)
    end
  end
end
