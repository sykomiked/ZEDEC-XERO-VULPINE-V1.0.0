// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

/**
 * Named octanes ({@code ZXV_OCTANE_*}). Octane is the densest token tier a
 * holding may use; like a fuel pump's grades, any octane 0..{@link #MAX} is
 * allowed and these are just names. Density buys fewer pieces and nothing
 * else: no interest, no fee, no priority.
 */
public final class Octane {
    private Octane() {}

    /** Face 1 only. */
    public static final int PLAIN = 0;
    /** Up to face 8. */
    public static final int REGULAR = 4;
    /** Up to face 89. */
    public static final int MIDGRADE = 9;
    /** Up to face 987. */
    public static final int PREMIUM = 14;
    /** Up to face 75025. */
    public static final int RACING = 23;
    /** Up to face F(65), the densest tier. */
    public static final int MAX = 63;
}
