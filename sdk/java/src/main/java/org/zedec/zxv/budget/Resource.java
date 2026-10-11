// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.budget;

/** What a budget measures (the kernel's {@code rmag_resource_t}). */
public enum Resource {
    /** Compute cycles. */
    COMPUTE_CYCLES(0),
    /** Memory. */
    MEMORY(1),
    /** Bandwidth. */
    BANDWIDTH(2),
    /** Energy. */
    ENERGY(3),
    /** Storage. */
    STORAGE(4),
    /** Model tokens: one prompt or generated token is one unit. */
    TOKENS(12);

    private final int code;

    Resource(int code) {
        this.code = code;
    }

    /** The kernel code ({@code ZXV_RES_*}). */
    public int code() {
        return code;
    }
}
