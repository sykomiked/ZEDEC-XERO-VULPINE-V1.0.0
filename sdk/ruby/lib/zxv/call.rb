# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Internal helpers shared by every wrapper: status mapping, out
  # parameters, the R4 two-call text pattern and argument guards.
  module Call
    INT64_MIN = -(2**63)
    INT64_MAX = 2**63 - 1
    UINT32_MAX = 2**32 - 1
    UINT64_MAX = 2**64 - 1

    REFUSED_TYPES = {
      Status::E_CAP => CapabilityDeniedError,
      Status::E_BUDGET => BudgetExhaustedError,
      Status::E_FUNDS => InsufficientFundsError,
      Status::E_VERIFY => VerificationFailedError,
      Status::E_NOTIMPL => NotImplementedError,
      Status::E_CONSENT => ConsentRequiredError,
      Status::E_PARADOX_OWNER => OwnerApprovalRequiredError
    }.freeze

    module_function

    # The calling thread's last error detail (R7).
    def last_error
      buf = FFI::MemoryPointer.new(:char, 512)
      len = FFI::MemoryPointer.new(:size_t)
      Native.lib.zxv_last_error(buf, 512, len)
      buf.read_string([len.read(:size_t), 511].min).force_encoding(Encoding::UTF_8)
    end

    # Build (but do not raise) the exception for a negative status.
    def error_for(status)
      detail = last_error
      case Status.class_of(status)
      when Status::CLASS_USAGE
        case status
        when Status::E_FLOAT then FloatRejectedError.new(detail)
        when Status::E_TREATY then TreatyError.new(status, detail)
        else ZxvUsageError.new(status, detail)
        end
      when Status::CLASS_REFUSED then (REFUSED_TYPES[status] || ZxvRefusedError).new(status, detail)
      when Status::CLASS_FATAL then ZxvFatal.new(status, detail)
      else ZxvSystemError.new(status, detail)
      end
    end

    # Raise unless +status+ is ZXV_OK. +context+ (a Context or nil) is
    # marked dead on a FATAL status, and Zxv.fatal_policy applies.
    def check(status, context = nil)
      return status if status == Status::OK

      err = error_for(status)
      if err.is_a?(ZxvFatal)
        context&.send(:mark_dead!)
        Zxv.send(:on_fatal, err)
      end
      raise err
    end

    # Call +fn+ with (buf, cap, out_len) appended, following R4.
    def text(*args, &fn)
      len = FFI::MemoryPointer.new(:size_t)
      cap = 64
      loop do
        buf = FFI::MemoryPointer.new(:char, cap)
        rc = fn.call(*args, buf, cap, len)
        need = len.read(:size_t)
        if rc == Status::E_BUFFER && need + 1 > cap
          cap = need + 1
          next
        end
        return [rc, rc == Status::OK ? buf.read_string(need).force_encoding(Encoding::UTF_8) : nil]
      end
    end

    # Call +fn+ with (buf, cap, out_len) appended, following "R4 for
    # bytes": the result has no NUL, and a call that needs more room
    # returns ZXV_E_BUFFER with *out_len set, so allocate that and call
    # again. +hint+ is the first buffer size (enough for most results, so
    # most calls make one trip). Raises on any other status.
    # @return [String] binary
    def bytes_out(context = nil, *args, hint: 256, &fn)
      len = FFI::MemoryPointer.new(:size_t)
      cap = [hint, 1].max
      3.times do
        buf = FFI::MemoryPointer.new(:uint8, cap)
        rc = fn.call(*args, buf, cap, len)
        need = len.read(:size_t)
        if rc == Status::E_BUFFER && need > cap
          cap = need
          next
        end
        check(rc, context)
        return buf.read_bytes(need)
      end
      check(Status::E_BUFFER, context)
    end

    # Like #text, but raises on any status other than OK.
    # @return [String] UTF-8
    def text_out(*args, context: nil, &fn)
      rc, s = text(*args, &fn)
      check(rc, context)
      s
    end

    # --- argument guards -------------------------------------------------

    # Reject floats loudly; require an Integer in [min, max].
    def int!(value, name, min: INT64_MIN, max: INT64_MAX)
      raise FloatRejectedError, "#{name}: Float #{value.inspect} is not exact; pass an Integer" if value.is_a?(Float)
      unless value.is_a?(Integer)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be an Integer, got #{value.class}")
      end
      unless value.between?(min, max)
        raise ZxvUsageError.new(Status::E_RANGE, "#{name} #{value} is outside #{min}..#{max}")
      end

      value
    end

    def uint32!(value, name) = int!(value, name, min: 0, max: UINT32_MAX)
    def uint64!(value, name) = int!(value, name, min: 0, max: UINT64_MAX)
    def uint8!(value, name) = int!(value, name, min: 0, max: 255)
    def uint16!(value, name) = int!(value, name, min: 0, max: 0xFFFF)
    def int32!(value, name) = int!(value, name, min: -(2**31), max: 2**31 - 1)

    # A text argument passed to C as a NUL-terminated string: UTF-8, with
    # no NUL inside (C would silently stop reading there).
    def cstr!(value, name)
      s = text!(value, name)
      raise ZxvUsageError.new(Status::E_ARG, "#{name} must not contain a NUL byte") if s.include?("\0")

      s
    end

    # true/false (and only those) as the C int 1/0.
    def bool!(value, name)
      return 1 if value == true
      return 0 if value == false

      raise ZxvUsageError.new(Status::E_ARG, "#{name} must be true or false, got #{value.inspect}")
    end

    # Bytes in native memory, or nil when empty: for (pointer, length) pairs
    # whose pointer may be NULL when the length is 0.
    def mem0(bytes) = mem(bytes, allow_null: true)

    # A fresh zero-filled native byte buffer.
    def buf(n) = FFI::MemoryPointer.new(:uint8, [n, 1].max)

    # A String (any encoding) as bytes, or raise.
    def bytes!(value, name, size: nil)
      raise FloatRejectedError, "#{name}: Float given" if value.is_a?(Float)
      unless value.is_a?(String)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be a String of bytes, got #{value.class}")
      end

      b = value.b
      if size && b.bytesize != size
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be #{size} bytes, got #{b.bytesize}")
      end

      b
    end

    # A UTF-8 text argument.
    def text!(value, name)
      raise FloatRejectedError, "#{name}: Float given" if value.is_a?(Float)
      unless value.is_a?(String)
        raise ZxvUsageError.new(Status::E_ARG, "#{name} must be a String, got #{value.class}")
      end

      value.encode(Encoding::UTF_8)
    rescue EncodingError => e
      raise ZxvUsageError.new(Status::E_UTF8, "#{name}: #{e.message}")
    end

    # Copy bytes into native memory (nil for an empty string when +allow_null+).
    def mem(bytes, allow_null: false)
      return nil if allow_null && bytes.empty?

      p = FFI::MemoryPointer.new(:uint8, [bytes.bytesize, 1].max)
      p.put_bytes(0, bytes) unless bytes.empty?
      p
    end

    def out(type) = FFI::MemoryPointer.new(type)
  end
end
