// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

/** KV cache representation of a {@link Session}. */
public enum KvType {
    /** Exact Q16 cache, 4 bytes per value. */
    Q16,
    /** 8-bit cache, about 1.16 bytes per value. */
    Q8
}
