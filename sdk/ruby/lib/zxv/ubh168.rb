# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # A UBH-168 harmonic wire frame: 21 octets = tag, then five 32-bit words
  # in fixed L-B-L-B-L order: W0 little-endian (S+), W1 big-endian (S-),
  # W2 LE (S+), W3 BE (S-), W4 LE (S+). Identical on every host.
  #
  # Packing and unpacking are pure Ruby (no native library needed), so a
  # service can frame without libzxv; #native_bytes cross-checks against C.
  #
  #   f = Zxv::Ubh168Frame.new(tag: Zxv::Ubh168Frame.tag(3, 2, 1), s_plus: [1, 2, 3], s_minus: [4, 5])
  #   bytes = f.to_bytes                       # 21-byte binary String
  #   Zxv::Ubh168Frame.from_bytes(bytes) == f  # => true
  class Ubh168Frame
    BYTES = 21
    # Truth values of the W5 correlation resolver.
    TRUTHS = %i[unknown true false neutral glut paradox].freeze

    # @return [Integer] 0..255
    attr_reader :tag
    # @return [Array<Integer>] three uint32 words (W0, W2, W4)
    attr_reader :s_plus
    # @return [Array<Integer>] two uint32 words (W1, W3)
    attr_reader :s_minus

    # tag = 100*sync + 10*shell + trunk (pure).
    # @param trunk [Integer] 0..9 @param shell [Integer] 0..9 @param sync [Integer] 0..1
    def self.tag(trunk, shell, sync)
      Call.int!(trunk, "trunk", min: 0, max: 9)
      Call.int!(shell, "shell", min: 0, max: 9)
      Call.int!(sync, "sync", min: 0, max: 1)
      100 * sync + 10 * shell + trunk
    end

    # Parse 21 bytes (pure).
    def self.from_bytes(bytes)
      b = Call.bytes!(bytes, "frame", size: BYTES)
      tag, w0, w1, w2, w3, w4 = b.unpack("C V N V N V")
      new(tag: tag, s_plus: [w0, w2, w4], s_minus: [w1, w3])
    end

    # The idle pilot frame of a trunk (from libzxv; never all zeros).
    def self.idle(trunk)
      out = FFI::MemoryPointer.new(:uint8, BYTES)
      Call.check(Native.lib.zxv_wire_idle(Call.int!(trunk, "trunk", min: 0, max: 9), out))
      from_bytes(out.read_bytes(BYTES))
    end

    def initialize(tag:, s_plus:, s_minus:)
      @tag = Call.int!(tag, "tag", min: 0, max: 255)
      unless s_plus.is_a?(Array) && s_plus.size == 3 && s_minus.is_a?(Array) && s_minus.size == 2
        raise ZxvUsageError.new(Status::E_ARG, "s_plus needs 3 words, s_minus 2")
      end

      @s_plus = s_plus.map { |w| Call.uint32!(w, "s_plus word") }.freeze
      @s_minus = s_minus.map { |w| Call.uint32!(w, "s_minus word") }.freeze
      freeze
    end

    # The 21 wire octets (pure Ruby), as a binary String.
    def to_bytes
      [tag, s_plus[0], s_minus[0], s_plus[1], s_minus[1], s_plus[2]].pack("C V N V N V")
    end

    # The same octets computed by libzxv's zxv_wire_pack (cross-check).
    def native_bytes
      sp = FFI::MemoryPointer.new(:uint32, 3).write_array_of_uint32(s_plus)
      sm = FFI::MemoryPointer.new(:uint32, 2).write_array_of_uint32(s_minus)
      out = FFI::MemoryPointer.new(:uint8, BYTES)
      Call.check(Native.lib.zxv_wire_pack(tag, sp, sm, out))
      out.read_bytes(BYTES)
    end

    # The W5 correlation resolver (libzxv): :unknown, :true, :false,
    # :neutral, :glut or :paradox.
    def resolve
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_wire_resolve(Call.mem(to_bytes), out))
      TRUTHS.fetch(out.read_int32)
    end

    def ==(other) = other.is_a?(Ubh168Frame) && other.to_bytes == to_bytes
    alias eql? ==
    def hash = to_bytes.hash
    def inspect = "#<Zxv::Ubh168Frame #{to_bytes.unpack1('H*')}>"
  end
end
