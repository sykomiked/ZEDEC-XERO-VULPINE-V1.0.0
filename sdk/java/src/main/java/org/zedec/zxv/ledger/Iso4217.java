// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * The ISO 4217 currency table compiled into libzxv (process-wide, read-only).
 * The platform rails 555/777/888 and private units such as VFV are not
 * ISO 4217 and are not found here.
 */
public final class Iso4217 {
    private Iso4217() {}

    /** Minor units value meaning "not applicable" (metals, XDR, XTS, XXX). */
    public static final int MINOR_NA = 255;

    /**
     * One ISO 4217 entry.
     *
     * @param alpha      the alpha-3 code ("NGN")
     * @param numeric    the numeric code (566)
     * @param minorUnits decimal places, or {@link #MINOR_NA}
     * @param flags      {@code ZXV_CCYF_*} bits
     */
    public record Currency(String alpha, int numeric, int minorUnits, int flags) {
        /** True if usable for a payment amount. */
        public boolean isPayable() {
            return (flags & 0x08) != 0;
        }

        /** True for a fund code (CLF, USN, ...). */
        public boolean isFund() {
            return (flags & 0x01) != 0;
        }
    }

    /** Looks up by alpha-3 code. */
    public static Optional<Currency> byAlpha(String alpha) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment num = a.allocate(JAVA_SHORT), mu = a.allocate(JAVA_BYTE), fl = a.allocate(JAVA_INT);
            int st = Ffi.call(Native.iso4217_by_alpha, Ffi.cstr(a, alpha), num, mu, fl);
            if (st == -4) return Optional.empty();
            Ffi.check(st, null);
            return Optional.of(new Currency(alpha, Short.toUnsignedInt(num.get(JAVA_SHORT, 0)),
                    Byte.toUnsignedInt(mu.get(JAVA_BYTE, 0)), fl.get(JAVA_INT, 0)));
        }
    }

    /** Looks up by numeric code (566 gives NGN; 555, 777, 888 give empty). */
    public static Optional<Currency> byNumeric(int numeric) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment mu = a.allocate(JAVA_BYTE), fl = a.allocate(JAVA_INT);
            if (numeric < 0 || numeric > 0xFFFF) return Optional.empty(); // not a uint16_t: no such code
            int n = numeric; // passed as an int 0..65535 (see Native: no sign extension)
            int st = Ffi.call(Native.iso4217_by_numeric, n, MemorySegment.NULL, 0L, a.allocate(8), mu, fl);
            if (st == -4) return Optional.empty();
            String alpha = Ffi.text(null, (b, c, len) -> Ffi.call(Native.iso4217_by_numeric, n, b, c, len, mu, fl));
            return Optional.of(new Currency(alpha, numeric, Byte.toUnsignedInt(mu.get(JAVA_BYTE, 0)), fl.get(JAVA_INT, 0)));
        }
    }

    /** The English name ("Naira"). */
    public static String name(String alpha) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment s = Ffi.cstr(a, alpha);
            return Ffi.text(null, (b, c, len) -> Ffi.call(Native.iso4217_name, s, b, c, len));
        }
    }

    /** Every alpha-3 code in the table. */
    public static List<String> codes() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.iso4217_count, n), null);
            List<String> out = new ArrayList<>();
            for (int i = 0; i < n.get(JAVA_INT, 0); i++) {
                final int idx = i;
                out.add(Ffi.text(null, (b, c, len) -> Ffi.call(Native.iso4217_at, idx, b, c, len)));
            }
            return List.copyOf(out);
        }
    }

    /** Publication date of the compiled list ("2026-01-01"). */
    public static String published() {
        return Ffi.text(null, (b, c, len) -> Ffi.call(Native.iso4217_published, b, c, len));
    }
}
