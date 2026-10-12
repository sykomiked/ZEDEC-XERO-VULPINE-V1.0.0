# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # ABI 1.5: freight packets (zxv_freight_*).
  #
  # A FREIGHT carries up to {MAX_PAYLOAD} payload bytes as {ROWS} smart
  # packets of {PACKET_BYTES} bytes (one UBH-168 frame each): byte 0 is the
  # row index, bytes 1..20 row data. Rows 0..k-1 hold the payload
  # verbatim; rows k..167 are Reed-Solomon parity. ANY k distinct rows
  # rebuild the payload, so up to 168 - k rows may be lost. Choosing k is
  # the trade: k * 20 payload bytes per freight against 168 - k rows of
  # loss tolerance.
  #
  # The 64-byte {Header} travels separately (in the outer message). A
  # larger payload is {.split} into a set of freights that share a
  # freight id and is reassembled by a {Join}.
  #
  #   h = Zxv::Freight::Header.for_payload(data, freight_id: 42, k: 10)
  #   packets = Zxv::Freight.encode(h, data)             # 3528 bytes
  #   rows = Zxv::Freight.rows(packets).sample(10)        # lose 158 rows
  #   Zxv::Freight.decode(h, rows) == data                # => true
  #
  # The payload may be an op stream ({Ops}) that the receiver expands.
  #
  # Pure: no context, no capability.
  #
  # Honest limits: erasure coding only. A lost row is recovered; a
  # CORRUPTED row is detected (the payload SHA-256 fails:
  # VerificationFailedError) but not located or repaired, so drop rows you
  # distrust and decode again. The header is neither protected by the code
  # nor authenticated: sign it (or the outer message) if the sender
  # matters. A freight is larger than its payload (3528 wire bytes for at
  # most 3360). No compression is promised: see {Ops}.
  module Freight
    # Bytes per packet (row index + 20 data bytes).
    PACKET_BYTES = 21
    # Payload bytes per row.
    ROW_PAYLOAD = 20
    # Rows (packets) per freight.
    ROWS = 168
    # Bytes of one encoded freight (168 * 21).
    BYTES = 3528
    # Most payload bytes in one freight (at k = 168, no loss tolerated).
    MAX_PAYLOAD = 3360
    # Bytes of a serialized header.
    HEADER_BYTES = 64
    # The header version this library writes.
    VERSION = 1
    # Most freights in one set.
    MAX_SET = 16_384
    # Largest set payload (MAX_SET * MAX_PAYLOAD).
    SET_MAX_BYTES = 55_050_240
    # Header flag: the (set's) payload is an op stream.
    F_OPSTREAM = 0x01

    # A freight header, a frozen value: the serialized 64 bytes and their
    # fields. +payload_sha256+ is 32 bytes.
    class Header
      # @return [String] the 64 serialized bytes (frozen)
      attr_reader :bytes
      # @return [Integer] 1
      attr_reader :version
      # @return [Integer] data rows, 1..168
      attr_reader :k
      # @return [Integer] 0 or F_OPSTREAM
      attr_reader :flags
      # @return [Integer] payload bytes in this freight
      attr_reader :payload_len
      # @return [Integer] this member's index in its set
      attr_reader :seq
      # @return [Integer] members in the set (1 for a single freight)
      attr_reader :seq_count
      # @return [Integer] the freight (set) id, uint64
      attr_reader :freight_id
      # @return [Integer] the receiver's expansion limit (op streams only; else 0)
      attr_reader :expand_limit
      # @return [String] SHA-256 of the payload (32 bytes)
      attr_reader :payload_sha256

      # The header of a single freight (seq 0 of 1) carrying +payload+ with
      # +k+ data rows; it hashes the payload.
      # @param payload [String] at most k * 20 bytes
      # @param k [Integer] 1..168
      # @param opstream [Boolean] the payload is an op stream
      # @param expand_limit [Integer] op streams only (else must be 0)
      # @return [Header]
      # @raise [ZxvUsageError] ZXV_E_ARG (bad k, expand_limit without
      #   opstream), ZXV_E_TOO_LONG (payload over k * 20)
      def self.for_payload(payload, freight_id:, k:, opstream: false, expand_limit: 0)
        p = Call.bytes!(payload, "payload")
        out = FFI::MemoryPointer.new(:uint8, HEADER_BYTES)
        Call.check(Native.lib.zxv_freight_header_init(Call.uint64!(freight_id, "freight_id"), Call.uint8!(k, "k"),
                                                      opstream ? F_OPSTREAM : 0, Call.uint64!(expand_limit, "expand_limit"),
                                                      Call.mem0(p), p.bytesize, out))
        new(out.read_bytes(HEADER_BYTES))
      end

      # Serialize a header from its fields (for a set member, or to
      # re-create a header received field by field). The version is always
      # {VERSION}; any rule violation is ZxvUsageError (ZXV_E_ARG).
      # @return [Header]
      def self.build(k:, payload_len:, seq:, seq_count:, freight_id:, payload_sha256:, flags: 0, expand_limit: 0)
        sha = Call.bytes!(payload_sha256, "payload_sha256", size: 32)
        out = FFI::MemoryPointer.new(:uint8, HEADER_BYTES)
        Call.check(Native.lib.zxv_freight_header_serialize(Call.uint8!(k, "k"), Call.uint8!(flags, "flags"),
                                                           Call.uint16!(payload_len, "payload_len"), Call.uint16!(seq, "seq"),
                                                           Call.uint16!(seq_count, "seq_count"),
                                                           Call.uint64!(freight_id, "freight_id"),
                                                           Call.uint64!(expand_limit, "expand_limit"), Call.mem(sha), out))
        new(out.read_bytes(HEADER_BYTES))
      end

      # Check serialized header bytes without decoding them.
      # @param bytes [String] 64 bytes
      # @return [true]
      # @raise [ZxvUsageError] ZXV_E_FORMAT (bad magic, reserved bytes or fields)
      def self.check!(bytes)
        Call.check(Native.lib.zxv_freight_header_check(Call.mem(Call.bytes!(bytes, "header", size: HEADER_BYTES))))
        true
      end

      # Coerce a Header or its 64 bytes.
      # @api private
      def self.coerce(value) = value.is_a?(Header) ? value : new(value)

      # Parse and check serialized header bytes.
      # @param bytes [String] 64 bytes
      # @raise [ZxvUsageError] ZXV_E_FORMAT: bad magic, nonzero reserved
      #   bytes, or a field that fails the check
      def initialize(bytes)
        b = Call.bytes!(bytes, "header", size: HEADER_BYTES)
        u8 = Array.new(3) { Call.out(:uint8) }
        u16 = Array.new(3) { Call.out(:uint16) }
        u64 = Array.new(2) { Call.out(:uint64) }
        sha = FFI::MemoryPointer.new(:uint8, 32)
        Call.check(Native.lib.zxv_freight_header_parse(Call.mem(b), *u8, *u16, *u64, sha))
        @bytes = b.freeze
        @version, @k, @flags = u8.map(&:read_uint8)
        @payload_len, @seq, @seq_count = u16.map(&:read_uint16)
        @freight_id, @expand_limit = u64.map(&:read_uint64)
        @payload_sha256 = sha.read_bytes(32).freeze
        freeze
      end

      # @return [Boolean] whether the payload is an op stream
      def opstream? = flags.anybits?(F_OPSTREAM)

      # @return [String] the 64 bytes
      def to_bytes = bytes

      def ==(other) = other.is_a?(Header) && other.bytes == bytes
      alias eql? ==
      def hash = [Header, bytes].hash

      def inspect = "#<Zxv::Freight::Header id=#{freight_id} k=#{k} seq=#{seq}/#{seq_count} len=#{payload_len}>"
    end

    module_function

    # Encode the 168 packets (rows in order) of the freight +header+
    # describes. +payload+ must be exactly what the header was built for.
    # @return [String] {BYTES} bytes
    # @raise [ZxvUsageError] ZXV_E_ARG (length mismatch), ZXV_E_FORMAT (bad header)
    # @raise [VerificationFailedError] the payload does not match the header's hash
    def encode(header, payload)
      h = Header.coerce(header)
      p = Call.bytes!(payload, "payload")
      out = FFI::MemoryPointer.new(:uint8, BYTES)
      Call.check(Native.lib.zxv_freight_encode(Call.mem(h.bytes), Call.mem0(p), p.bytesize, out))
      out.read_bytes(BYTES)
    end

    # Split encoded packets into their 21-byte rows.
    # @return [Array<String>]
    def rows(packets)
      b = Call.bytes!(packets, "packets")
      raise ZxvUsageError.new(Status::E_ARG, "packets must be a multiple of #{PACKET_BYTES} bytes") unless (b.bytesize % PACKET_BYTES).zero?

      Array.new(b.bytesize / PACKET_BYTES) { |i| b.byteslice(i * PACKET_BYTES, PACKET_BYTES) }
    end

    # Rebuild the payload from whichever packets arrived, in any order;
    # duplicates and row indices above 167 are ignored, and the first k
    # distinct rows are used.
    # @param packets [String, Array<String>] concatenated 21-byte packets, or an Array of them
    # @return [String] the payload
    # @raise [ZxvRefusedError] ZXV_E_ERASURE (fewer than k distinct rows)
    # @raise [VerificationFailedError] the rebuilt payload fails the header's
    #   SHA-256 (a corrupted row, or a header from another freight)
    def decode(header, packets)
      h = Header.coerce(header)
      pk = packets_bytes(packets)
      Call.bytes_out(nil, Call.mem(h.bytes), Call.mem0(pk), pk.bytesize, hint: h.payload_len) do |*a|
        Native.lib.zxv_freight_decode(*a)
      end
    end

    # Freights needed to carry +len+ bytes at +k+ (0 bytes still needs one).
    # @return [Integer]
    # @raise [ZxvUsageError] ZXV_E_TOO_LONG (more than MAX_SET would be needed)
    def set_count(len, k:)
      out = Call.out(:uint32)
      Call.check(Native.lib.zxv_freight_set_count(Call.uint64!(len, "len"), Call.uint8!(k, "k"), out))
      out.read_uint32
    end

    # Member +seq+ of the set carrying +data+: its header and packets.
    # @return [Array(Header, String)]
    # @raise [ZxvUsageError] ZXV_E_RANGE (seq not below the set count)
    def split_member(data, seq:, freight_id:, k:, opstream: false, expand_limit: 0)
      d = Call.bytes!(data, "data")
      member(Call.mem0(d), d.bytesize, Call.uint32!(seq, "seq"), freight_id, k, opstream, expand_limit)
    end

    # Every member of the set carrying +data+, in order.
    # @return [Array<Array(Header, String)>] [header, packets] per member
    def split(data, freight_id:, k:, opstream: false, expand_limit: 0)
      d = Call.bytes!(data, "data")
      mem = Call.mem0(d)
      Array.new(set_count(d.bytesize, k: k)) { |s| member(mem, d.bytesize, s, freight_id, k, opstream, expand_limit) }
    end

    # @api private
    def packets_bytes(packets)
      return Call.bytes!(packets, "packets") unless packets.is_a?(Array)

      packets.each_with_index.map { |p, i| Call.bytes!(p, "packets[#{i}]", size: PACKET_BYTES) }.join.b
    end

    def member(mem, len, seq, freight_id, k, opstream, expand_limit)
      h = FFI::MemoryPointer.new(:uint8, HEADER_BYTES)
      p = FFI::MemoryPointer.new(:uint8, BYTES)
      Call.check(Native.lib.zxv_freight_split(Call.uint64!(freight_id, "freight_id"), Call.uint8!(k, "k"),
                                              opstream ? F_OPSTREAM : 0, Call.uint64!(expand_limit, "expand_limit"),
                                              mem, len, seq, h, p))
      [Header.new(h.read_bytes(HEADER_BYTES)), p.read_bytes(BYTES)]
    end
    private_class_method :member

    # Reassembles one freight set, member by member and in any order, into
    # a buffer it allocates. The first accepted member fixes the set (id,
    # k, flags, count, expand_limit). Pure; NOT thread-safe.
    #
    #   join = Zxv::Freight::Join.new(capacity: data.bytesize)
    #   members.shuffle.each { |h, p| join.add(h, p) }
    #   join.result == data   # => true
    class Join < PureHandle
      # Progress: members received, members in the set (0 until the first
      # is accepted), whether all arrived, and the joined length once complete.
      Progress = Struct.new(:received, :count, :complete, :total, keyword_init: true)

      # @param capacity [Integer] 0..SET_MAX_BYTES
      # @raise [ZxvUsageError] ZXV_E_RANGE (capacity too large)
      def initialize(capacity:)
        super()
        create_handle(:zxv_freight_join_destroy) do |o|
          Native.lib.zxv_freight_join_create(Call.uint64!(capacity, "capacity"), o)
        end
      end

      # Decode one member into its place. A member already received is a
      # no-op. A failed member is not recorded: add it again when more rows arrive.
      # @return [self]
      # @raise [ZxvRefusedError] ZXV_E_STATE (another set, or a non-last
      #   member not full), ZXV_E_CAPACITY (the set cannot fit), ZXV_E_ERASURE
      # @raise [VerificationFailedError]
      def add(header, packets)
        h = Header.coerce(header)
        pk = Freight.packets_bytes(packets)
        Call.check(Native.lib.zxv_freight_join_add(ptr, Call.mem(h.bytes), Call.mem0(pk), pk.bytesize))
        self
      end

      # @return [Progress]
      def status
        r = Call.out(:uint32)
        c = Call.out(:uint32)
        done = Call.out(:int32)
        t = Call.out(:uint64)
        Call.check(Native.lib.zxv_freight_join_status(ptr, r, c, done, t))
        Progress.new(received: r.read_uint32, count: c.read_uint32, complete: done.read_int32 != 0, total: t.read_uint64)
      end

      # @return [Boolean] whether every member arrived
      def complete? = status.complete

      # The joined bytes.
      # @return [String]
      # @raise [ZxvRefusedError] ZXV_E_STATE until every member arrived
      def result = Call.bytes_out(nil, ptr, hint: [status.total, 1].max) { |*a| Native.lib.zxv_freight_join_result(*a) }
    end

    # Op streams: a payload of ops that the receiver expands.
    #
    # Format: "ZXOP", version, flags, the declared total (u64), an optional
    # SHA-256 of the output, then ops: LITERAL bytes; COPY (an LZ77-style
    # back reference, overlap allowed); CIDREF (bytes the receiver resolves
    # by content id, through a block); SEED (a built-in deterministic
    # generator, {GENERATORS}). Expansion checks the declared total against
    # the receiver's limit before writing a byte, so a stream cannot expand
    # past what the receiver allowed.
    #
    # Honest limits: no size guarantee. Random data encodes slightly
    # LARGER than itself; COPY helps only data that actually repeats; CIDREF
    # only names bytes the receiver must already hold or fetch; SEED only
    # reproduces output of the built-in generators (no encoder searches for
    # seeds). No information is created: an expanded stream holds what its
    # ops carry plus what the receiver supplies.
    module Ops
      # Longest CIDREF content id.
      CID_MAX = 128
      # Longest SEED parameter string.
      GEN_PARAM_MAX = 32
      # Largest op stream or expansion handled here (64 MiB).
      MAX = 67_108_864
      # SEED generators: :xorshift (8 bytes: u64 LE seed, nonzero;
      # xorshift64* bytes LE), :repeat (1..32 bytes repeated), :fibword (2
      # distinct bytes a, b: the Fibonacci word abaababaab...), :sierpinski
      # (3 bytes: log2 width w 1..15, on, off).
      GENERATORS = { xorshift: 1, repeat: 2, fibword: 3, sierpinski: 4 }.freeze

      module_function

      # Read a stream's declared total and whether it carries an output
      # SHA-256, without expanding.
      # @return [Array(Integer, Boolean)] [total, has_sha]
      # @raise [ZxvUsageError] ZXV_E_FORMAT (bad header)
      def peek(ops)
        o = Call.bytes!(ops, "ops")
        total = Call.out(:uint64)
        sha = Call.out(:int32)
        Call.check(Native.lib.zxv_freight_ops_peek(Call.mem0(o), o.bytesize, total, sha))
        [total.read_uint64, sha.read_int32 != 0]
      end

      # Expand an op stream. The optional block resolves CIDREF ops: it
      # receives (cid_bytes, length) and returns a String of exactly
      # +length+ bytes, or nil if it cannot. It runs on the calling thread,
      # must not expand another stream, and any exception it raises is
      # caught and treated as "cannot resolve" (never unwound through C).
      # @param limit [Integer] the most bytes you accept (<= MAX)
      # @return [String] the expanded bytes
      # @raise [ZxvUsageError] ZXV_E_TOO_LONG (declared total above +limit+,
      #   checked before anything is written), ZXV_E_FORMAT (malformed),
      #   ZXV_E_RANGE (limit above MAX)
      # @raise [ZxvUsageError] ZXV_E_NOT_FOUND (a CIDREF was not resolved)
      # @raise [VerificationFailedError] the output fails the stream's SHA-256
      def expand(ops, limit:, &resolver)
        o = Call.bytes!(ops, "ops")
        lim = Call.uint64!(limit, "limit")
        total, = peek(o)
        cb = resolver && FFI::Function.new(:int32, %i[pointer pointer size_t pointer uint64]) do |_u, cid, cid_len, out, len|
          resolve_guard(resolver, cid, cid_len, out, len)
        end
        size = total > lim ? 0 : total
        buf = size.zero? ? nil : FFI::MemoryPointer.new(:uint8, size)
        n = Call.out(:size_t)
        rc = Native.lib.zxv_freight_ops_expand(Call.mem0(o), o.bytesize, lim, cb, nil, buf, size, n)
        Call.check(rc)
        # +cb+ (a local of this frame) keeps the callback alive until here.
        size.zero? ? "".b : buf.read_bytes(n.read(:size_t))
      end

      # Run one SEED generator directly.
      # @param gen [Symbol, Integer] a key of GENERATORS
      # @param params [String] at most GEN_PARAM_MAX bytes
      # @return [String] +length+ bytes
      # @raise [ZxvUsageError] ZXV_E_ARG (unknown generator or params it refuses)
      def generate(gen, params, length)
        p = Call.bytes!(params, "params")
        n = Call.int!(length, "length", min: 0, max: MAX)
        out = Call.buf(n)
        Call.check(Native.lib.zxv_freight_gen_run(gen!(gen), Call.mem0(p), p.bytesize, n.zero? ? nil : out, n))
        out.read_bytes(n)
      end

      # Greedy LZ77 encoding of +data+ into a LITERAL/COPY op stream.
      # Deterministic. The result may be LARGER than the input.
      # @param with_sha [Boolean] include the output SHA-256 (expansion verifies it)
      # @return [String]
      def encode(data, with_sha: true)
        d = Call.bytes!(data, "data")
        Call.bytes_out(nil, Call.mem0(d), d.bytesize, Call.bool!(with_sha, "with_sha"),
                       hint: bound(d.bytesize, with_sha: with_sha)) { |*a| Native.lib.zxv_freight_ops_encode(*a) }
      end

      # Worst-case encoded size of +n+ input bytes (all literals).
      # @return [Integer]
      def bound(n, with_sha: true)
        out = Call.out(:uint64)
        Call.check(Native.lib.zxv_freight_ops_bound(Call.uint64!(n, "n"), Call.bool!(with_sha, "with_sha"), out))
        out.read_uint64
      end

      # @api private
      def gen!(gen)
        return GENERATORS.fetch(gen) { raise ZxvUsageError.new(Status::E_ARG, "unknown generator #{gen.inspect}") } if gen.is_a?(Symbol)

        Call.uint8!(gen, "gen")
      end

      def resolve_guard(resolver, cid, cid_len, out, len)
        bytes = resolver.call(cid.read_bytes(cid_len), len)
        return 1 unless bytes.is_a?(String) && bytes.bytesize == len

        out.put_bytes(0, bytes.b) if len.positive?
        Status::OK
      rescue Exception # rubocop:disable Lint/RescueException
        1
      end
      private_class_method :resolve_guard

      # Builds an op stream op by op into a buffer of +max_bytes+. Each op
      # returns self; the first error raises, and is sticky (every later op
      # and #finish raise it again). Pure; NOT thread-safe.
      #
      #   w = Zxv::Freight::Ops::Writer.new(declared_total: 12)
      #   w.literal("ab").copy(distance: 2, length: 10)
      #   Zxv::Freight::Ops.expand(w.finish, limit: 12)   # => "abababababab"
      class Writer < PureHandle
        # @param declared_total [Integer] what the ops must produce in all
        # @param sha256 [String, nil] 32-byte SHA-256 of that output (expansion verifies it)
        # @param max_bytes [Integer] the stream buffer size (14..MAX; 46 with sha256)
        def initialize(declared_total:, sha256: nil, max_bytes: 65_536)
          super()
          sha = sha256 && Call.mem(Call.bytes!(sha256, "sha256", size: 32))
          create_handle(:zxv_freight_ops_writer_destroy) do |o|
            Native.lib.zxv_freight_ops_writer_create(Call.uint64!(declared_total, "declared_total"), sha,
                                                     Call.uint64!(max_bytes, "max_bytes"), o)
          end
        end

        # A LITERAL op: these bytes.
        # @return [self]
        def literal(bytes)
          b = Call.bytes!(bytes, "bytes")
          Call.check(Native.lib.zxv_freight_ops_put_literal(ptr, Call.mem0(b), b.bytesize))
          self
        end

        # A COPY op: +length+ bytes starting +distance+ bytes back (overlap allowed).
        # @return [self]
        def copy(distance:, length:)
          Call.check(Native.lib.zxv_freight_ops_put_copy(ptr, Call.uint64!(distance, "distance"), Call.uint64!(length, "length")))
          self
        end

        # A CIDREF op: +length+ bytes the receiver resolves by content id.
        # @param cid [Cid, String] a Cid or 1..128 raw id bytes
        # @return [self]
        def cidref(cid, length:)
          c = cid.is_a?(Cid) ? cid.bytes : Call.bytes!(cid, "cid")
          Call.check(Native.lib.zxv_freight_ops_put_cidref(ptr, Call.mem0(c), c.bytesize, Call.uint64!(length, "length")))
          self
        end

        # A SEED op: +length+ bytes of generator +gen+ with +params+.
        # @return [self]
        def seed(gen, params, length:)
          p = Call.bytes!(params, "params")
          Call.check(Native.lib.zxv_freight_ops_put_seed(ptr, Ops.gen!(gen), Call.mem0(p), p.bytesize,
                                                         Call.uint64!(length, "length")))
          self
        end

        # The finished stream. The writer stays usable.
        # @return [String]
        # @raise [ZxvRefusedError] ZXV_E_STATE (fewer bytes than declared), or the sticky error
        def finish = Call.bytes_out(nil, ptr, hint: 1024) { |*a| Native.lib.zxv_freight_ops_writer_finish(*a) }
      end
    end
  end
end
