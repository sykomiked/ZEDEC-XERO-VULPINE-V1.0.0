# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: an IPFS content identifier (zxv_cid_*), a frozen value.
  #
  # A CID names bytes by WHAT they are (a hash), not where they live. It
  # holds the binary form (1 to {MAX} bytes: version, codec, multihash);
  # a Cid can only be made from bytes that are exactly one canonical CIDv0
  # or CIDv1 with a sha2-256 or identity multihash.
  #
  # The CIDs are byte for byte the ones Kubo (go-ipfs) makes:
  # {.compute} is <tt>ipfs block put</tt> (CIDv1, sha2-256 of the bytes as
  # one block), {.unixfs} is <tt>ipfs add --cid-version=1</tt>, and
  # <tt>to_s</tt> prints what Kubo prints ("b..." base32 for CIDv1, "Qm..."
  # for CIDv0); <tt>to_s(:base58btc)</tt> prints the "z..." AIPI form.
  #
  #   cid = Zxv::Cid.compute("hello")       # raw codec
  #   cid.to_s                              # => "bafkrei..."
  #   cid.verify("hello")                   # => true
  #   Zxv::Cid.parse(cid.to_s) == cid       # => true
  #
  # Pure: no context, no capability. Honest limits: a CID proves which
  # bytes are meant; it does not make them available (a CID resolves only
  # while someone holds the bytes) and does not prove that a claimed
  # function OUTPUT is right. libzxv has no network: nothing is fetched.
  class Cid
    # Longest binary CID accepted or produced (ZXV_CID_MAX).
    MAX = 80
    # Longest CID text (ZXV_CID_TEXT_MAX).
    TEXT_MAX = 139
    # Longest multihash digest (an identity hash; ZXV_CID_DIGEST_MAX).
    DIGEST_MAX = 64
    # Multicodec: raw bytes.
    CODEC_RAW = 0x55
    # Multicodec: dag-pb (UnixFS directories and large files).
    CODEC_DAG_PB = 0x70
    # Multihash: identity (the bytes themselves are the "digest").
    HASH_IDENTITY = 0x00
    # Multihash: sha2-256.
    HASH_SHA2_256 = 0x12
    # Text bases for #to_s: :default is Kubo's form, :base58btc the "z..."
    # form (CIDv1 only).
    BASES = { default: 0, base58btc: 1 }.freeze

    # @return [String] the binary CID (frozen)
    attr_reader :bytes
    # @return [Integer] 0 or 1
    attr_reader :version
    # @return [Integer] the multicodec (CODEC_RAW, CODEC_DAG_PB, an AIPI codec, ...)
    attr_reader :codec
    # @return [Integer] the multihash code (HASH_SHA2_256 or HASH_IDENTITY)
    attr_reader :hash_code
    # @return [String] the multihash digest (frozen)
    attr_reader :digest

    class << self
      # The CIDv1 of +data+ as ONE block: sha2-256 under +codec+ (Kubo
      # <tt>ipfs block put</tt>).
      # @param data [String]
      # @param codec [Integer] CODEC_RAW for plain bytes
      # @return [Cid]
      def compute(data, codec: CODEC_RAW)
        d = Call.bytes!(data, "data")
        from_out { |b, l| Native.lib.zxv_cid_compute(Call.uint32!(codec, "codec"), Call.mem0(d), d.bytesize, b, l) }
      end

      # The root CID Kubo gives +data+ as a file (<tt>ipfs add
      # --cid-version=1</tt>, default chunking): a raw CID up to 262144
      # bytes, a dag-pb root above. The blocks are not kept.
      # @return [Cid]
      def unixfs(data)
        d = Call.bytes!(data, "data")
        from_out { |b, l| Native.lib.zxv_cid_unixfs(Call.mem0(d), d.bytesize, b, l) }
      end

      # Parse canonical CID text: "b..." (CIDv1 base32 lower), "Qm..."
      # (CIDv0) or "z..." (CIDv1 base58btc). No upper case, padding or
      # other multibases.
      # @return [Cid]
      # @raise [ZxvRefusedError] ZXV_E_CID for anything else
      def parse(text)
        t = Call.cstr!(text, "text")
        from_out { |b, l| Native.lib.zxv_cid_from_text(t, b, l) }
      end

      # Coerce a Cid or a binary String to a Cid.
      # @api private
      def coerce(value, name = "cid")
        return value if value.is_a?(Cid)

        new(Call.bytes!(value, name))
      end

      # @api private
      def from_out
        buf = FFI::MemoryPointer.new(:uint8, MAX)
        len = Call.out(:size_t)
        Call.check(yield(buf, len))
        new(buf.read_bytes(len.read(:size_t)))
      end
    end

    # Wrap binary CID bytes.
    # @param bytes [String] exactly one canonical CID
    # @raise [ZxvRefusedError] ZXV_E_CID if the bytes are not one
    def initialize(bytes)
      b = Call.bytes!(bytes, "cid")
      ver, codec, hash = Array.new(3) { Call.out(:uint32) }
      dig = FFI::MemoryPointer.new(:uint8, DIGEST_MAX)
      dlen = Call.out(:size_t)
      Call.check(Native.lib.zxv_cid_info(Call.mem(b), b.bytesize, ver, codec, hash, dig, dlen))
      @bytes = b.freeze
      @version = ver.read_uint32
      @codec = codec.read_uint32
      @hash_code = hash.read_uint32
      @digest = dig.read_bytes(dlen.read(:size_t)).freeze
      freeze
    end

    # CID text.
    # @param base [Symbol] :default (Kubo's form) or :base58btc ("z...", CIDv1 only)
    # @return [String]
    # @raise [ZxvRefusedError] ZXV_E_CID for a CIDv0 in base58btc (it has no multibase form)
    def to_s(base = :default)
      b = BASES.fetch(base) { raise ZxvUsageError.new(Status::E_ARG, "base must be :default or :base58btc") }
      Call.text_out(Call.mem(bytes), bytes.bytesize, b) { |*a| Native.lib.zxv_cid_to_text(*a) }
    end

    # This CID as CIDv1 (a CIDv0 becomes dag-pb CIDv1 with the same hash;
    # a CIDv1 is returned unchanged; Kubo <tt>ipfs cid base32</tt>).
    # @return [Cid]
    def to_v1 = Cid.from_out { |b, l| Native.lib.zxv_cid_to_v1(Call.mem(bytes), bytes.bytesize, b, l) }

    # Whether +data+ is exactly the block this CID names (re-hashed;
    # identity CIDs compare the bytes). A UnixFS root (dag-pb) names its
    # root BLOCK, not the file: check a file with Cid.unixfs(data) == cid.
    # @return [Boolean]
    def verify(data)
      d = Call.bytes!(data, "data")
      rc = Native.lib.zxv_cid_verify(Call.mem(bytes), bytes.bytesize, Call.mem0(d), d.bytesize)
      return true if rc == Status::OK
      return false if rc == Status::E_CID

      Call.check(rc)
    end

    # Like #verify, but raises.
    # @return [self]
    # @raise [ZxvRefusedError] ZXV_E_CID when the data does not match
    def verify!(data)
      d = Call.bytes!(data, "data")
      Call.check(Native.lib.zxv_cid_verify(Call.mem(bytes), bytes.bytesize, Call.mem0(d), d.bytesize))
      self
    end

    # The Kademlia key under which providers of this CID are announced on
    # Vinea: SHA-256("zxv/aipi/v1/provider" || binary CID).
    # @return [String] 32 bytes
    def provider_key
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.zxv_aipi_provider_key(Call.mem(bytes), bytes.bytesize, out))
      out.read_bytes(32)
    end

    # @return [Integer] length of the binary form
    def bytesize = bytes.bytesize

    def ==(other) = other.is_a?(Cid) && other.bytes == bytes
    alias eql? ==
    def hash = [Cid, bytes].hash

    def inspect = "#<Zxv::Cid #{to_s(version.zero? ? :default : :base58btc)}>"
  end
end
