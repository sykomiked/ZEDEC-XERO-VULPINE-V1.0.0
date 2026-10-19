# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # A currency enabled in a context (or found in ISO 4217).
  # +flags+ are the ZXV_CCYF_* bits.
  Currency = Struct.new(:code, :minor_units, :flags, :numeric) do
    # @return [Boolean] a platform-private unit (never valid in ISO 20022)
    def private? = flags & 0x10 != 0
    # @return [Boolean] usable for a payment amount
    def payable? = flags & 0x08 != 0
    # @return [Boolean] a fund code (CLF, USN, ...)
    def fund? = flags & 0x01 != 0
  end

  # The ISO 4217 table compiled into libzxv (process-wide, read-only).
  module Iso4217
    module_function

    # Look up by alpha-3 ("NGN"); nil if not in ISO 4217 (VFV included).
    # @return [Currency, nil]
    def lookup(alpha)
      num = Call.out(:uint16)
      minor = Call.out(:uint8)
      flags = Call.out(:uint32)
      rc = Native.lib.zxv_iso4217_by_alpha(Call.text!(alpha, "alpha"), num, minor, flags)
      return nil if rc == Status::E_NOT_FOUND

      Call.check(rc)
      Currency.new(alpha, minor.read_uint8, flags.read_uint32, num.read_uint16)
    end

    # Look up by numeric (566 -> NGN); nil if unknown (555/777/888 included).
    def by_numeric(numeric)
      n = Call.int!(numeric, "numeric", min: 0, max: 65_535)
      minor = Call.out(:uint8)
      flags = Call.out(:uint32)
      rc, alpha = Call.text(n) { |num, buf, cap, len| Native.lib.zxv_iso4217_by_numeric(num, buf, cap, len, minor, flags) }
      return nil if rc == Status::E_NOT_FOUND

      Call.check(rc)
      Currency.new(alpha, minor.read_uint8, flags.read_uint32, n)
    end

    # English name ("Naira").
    def name(alpha)
      rc, s = Call.text(Call.text!(alpha, "alpha")) { |*a| Native.lib.zxv_iso4217_name(*a) }
      Call.check(rc)
      s
    end

    # Every alpha code in the table.
    # @return [Array<String>]
    def codes
      n = Call.out(:uint32)
      Call.check(Native.lib.zxv_iso4217_count(n))
      Array.new(n.read_uint32) do |i|
        rc, s = Call.text(i) { |*a| Native.lib.zxv_iso4217_at(*a) }
        Call.check(rc)
        s
      end
    end

    # Publication date of the compiled list ("2026-01-01").
    def published
      rc, s = Call.text { |*a| Native.lib.zxv_iso4217_published(*a) }
      Call.check(rc)
      s
    end

    # Interop caveat for a numeric code ("" if none; 777 = Vino CREDIT rail).
    def caveat(numeric)
      rc, s = Call.text(Call.int!(numeric, "numeric", min: 0, max: 65_535)) { |*a| Native.lib.zxv_ccy_caveat(*a) }
      Call.check(rc)
      s
    end

    # Is an ISO 3166 alpha-2 country an African Union member?
    def african_union_member?(country)
      out = Call.out(:int32)
      Call.check(Native.lib.zxv_au_is_member(Call.text!(country, "country"), out))
      out.read_int32 == 1
    end
  end
end
