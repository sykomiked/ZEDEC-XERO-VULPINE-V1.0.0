// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;

/** Small helpers for this package. */
final class W4 {
    private W4() {}

    /** A NUL-terminated name from a fixed buffer of {@code n} bytes. */
    static String name(MemorySegment s, int n) {
        int len = 0;
        while (len < n && s.get(java.lang.foreign.ValueLayout.JAVA_BYTE, len) != 0) len++;
        return new String(s.asSlice(0, len).toArray(java.lang.foreign.ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
    }
}
