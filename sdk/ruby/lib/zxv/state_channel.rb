# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # The ledger's exact balances as an EVM state-channel state for
  # sdk/solidity/ZxvStateChannel.sol:
  #   abi.encode(bytes32 channelId, uint256 nonce, address[] parties, int256[] balances)
  # with digest = keccak256(encoding). The parties sign the digest (EIP-191)
  # in their own wallets: ZXV has no secp256k1.
  class StateChannel
    # An encoded state: +encoding+ and +digest+ are binary Strings.
    State = Struct.new(:encoding, :digest, keyword_init: true) do
      # @return [String] 0x-prefixed hex digest
      def digest_hex = "0x#{digest.unpack1('H*')}"
      # @return [String] 0x-prefixed hex encoding
      def encoding_hex = "0x#{encoding.unpack1('H*')}"
    end

    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :evm (and :ledger_read for #ledger_state)
    def initialize(capability)
      @capability = capability
    end

    # Encode explicit balances.
    # @param channel_id [String] 32 bytes
    # @param parties [Array<String>] 20-byte addresses (binary or "0x" hex)
    # @param balances [Array<Integer>] int64 each
    # @return [State]
    def encode_state(channel_id:, nonce:, parties:, balances:)
      raise ZxvUsageError.new(Status::E_ARG, "parties and balances differ in length") if parties.size != balances.size

      bal = balances.map { |b| Call.int!(b, "balance") }
      bmem = FFI::MemoryPointer.new(:int64, [bal.size, 1].max).write_array_of_int64(bal)
      call(parties.size) do |buf, cap, len, dg|
        Native.lib.zxv_evm_encode_state(capability.ptr, id_mem(channel_id), Call.uint64!(nonce, "nonce"),
                                        addr_mem(parties), bmem, parties.size, buf, cap, len, dg)
      end
    end

    # Encode each party's EQUITY (888) balance read from ledger +accounts+.
    # @return [State]
    def ledger_state(channel_id:, nonce:, currency:, accounts:, parties:)
      raise ZxvUsageError.new(Status::E_ARG, "parties and accounts differ in length") if parties.size != accounts.size

      acc = FFI::MemoryPointer.new(:uint32, [accounts.size, 1].max)
                              .write_array_of_uint32(accounts.map { |a| Call.uint32!(a, "account") })
      call(parties.size) do |buf, cap, len, dg|
        Native.lib.zxv_evm_ledger_state(capability.ptr, id_mem(channel_id), Call.uint64!(nonce, "nonce"),
                                        Call.text!(currency, "currency"), acc, addr_mem(parties), parties.size,
                                        buf, cap, len, dg)
      end
    end

    private

    def call(n)
      need = (6 + 2 * n) * 32
      buf = FFI::MemoryPointer.new(:uint8, [need, 1].max)
      len = Call.out(:size_t)
      dg = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(yield(buf, need, len, dg), capability.context)
      State.new(encoding: buf.read_bytes(len.read(:size_t)), digest: dg.read_bytes(32))
    end

    def id_mem(channel_id) = Call.mem(bytes20or32(channel_id, 32, "channel_id"))

    def addr_mem(parties)
      Call.mem(parties.map { |a| bytes20or32(a, 20, "party address") }.join.b, allow_null: true)
    end

    def bytes20or32(v, size, name)
      s = Call.bytes!(v, name)
      s = [s[2..]].pack("H*") if s.start_with?("0x") && s.bytesize == 2 + 2 * size
      Call.bytes!(s, name, size: size)
    end
  end
end
