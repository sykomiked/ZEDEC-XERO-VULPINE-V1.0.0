// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import java.util.Arrays;

/** Little-endian readers for record bytes. */
final class Le {
    private Le() {}

    static int u8(byte[] b, int o) {
        return b[o] & 0xFF;
    }

    static int i32(byte[] b, int o) {
        return (b[o] & 0xFF) | (b[o + 1] & 0xFF) << 8 | (b[o + 2] & 0xFF) << 16 | (b[o + 3] & 0xFF) << 24;
    }

    static long i64(byte[] b, int o) {
        return (i32(b, o) & 0xFFFFFFFFL) | (long) i32(b, o + 4) << 32;
    }

    static byte[] slice(byte[] b, int o, int n) {
        return Arrays.copyOfRange(b, o, o + n);
    }
}
