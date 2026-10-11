// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.phase;

import java.util.Set;

/** Properties of a step submitted for admission ({@code ZXV_STEP_*}). */
public enum StepFlag {
    /** The step requires its phase to be healthy. */
    REQUIRES_HEALTH(0x1),
    /** The step touches hardware. */
    HARDWARE(0x2),
    /** The step depends on an S0 (pending) fact; it is deferred. */
    S0_PENDING(0x4);

    private final int bit;

    StepFlag(int bit) {
        this.bit = bit;
    }

    /** The flag bit. */
    public int bit() {
        return bit;
    }

    static int mask(Set<StepFlag> flags) {
        int m = 0;
        for (StepFlag f : flags) m |= f.bit;
        return m;
    }
}
