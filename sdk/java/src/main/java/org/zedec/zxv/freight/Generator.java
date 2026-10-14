// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import org.zedec.zxv.internal.Errors;

/** The built-in deterministic generators a SEED op names ({@code ZXV_FREIGHT_GEN_*}). */
public enum Generator {
    /** Params: 8 bytes, a nonzero u64 LE seed; output: xorshift64* bytes, little-endian. */
    XORSHIFT(1),
    /** Params: 1..32 bytes, the pattern, repeated. */
    REPEAT(2),
    /** Params: 2 distinct bytes a, b; output: the Fibonacci word abaababaab... */
    FIBWORD(3),
    /** Params: 3 bytes, log2 width w (1..15), on, off: byte i is on iff (x &amp; y) == 0 for x = i mod 2^w, y = (i &gt;&gt; w) mod 2^w. */
    SIERPINSKI(4);

    private final int code;

    Generator(int code) {
        this.code = code;
    }

    /** The generator's code. */
    public int code() {
        return code;
    }

    /**
     * The generator with {@code code}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for an unknown generator
     */
    public static Generator fromCode(int code) {
        for (Generator g : values()) if (g.code == code) return g;
        throw Errors.usage(-1, "unknown generator " + code);
    }
}
