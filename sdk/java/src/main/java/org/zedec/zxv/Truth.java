// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import java.util.function.Supplier;

/**
 * Four-valued (Belnap / K3 paraconsistent) logic: the kernel's {@code lpres}
 * states. A closed set of exactly four values, numbered as the kernel numbers
 * them.
 *
 * <p>There is deliberately no {@code toBoolean()} without a policy:
 * {@link #BOTH} is a held contradiction (not an error, not null) and
 * {@link #NEITHER} is "not yet known". Say what each means for your decision
 * with {@link #toBoolean(boolean, boolean)}, or use
 * {@link #isCertainlyTrue()} / {@link #requireTrue(Supplier)}.
 *
 * <p>{@link #and}, {@link #or} and {@link #not} follow libzxv's tables
 * (conformance key {@code lpres}). The kernel plans an orthogonal 2-bit
 * {@code mode} beside the four states; it will arrive as a separate accessor
 * or companion type, never by renumbering these constants.
 */
public enum Truth {
    /** S0: unknown, unattested, pending. Code 0. */
    NEITHER(0),
    /** S+: attested present and valid. Code 1. */
    TRUE(1),
    /** S-: attested absent or invalid. Code 2. */
    FALSE(2),
    /** Contradictory evidence, held. Code 3. */
    BOTH(3);

    // Tables copied from libzxv (zxv_lp_not/and/or); a test checks them against the C library.
    private static final int[] NOT = {0, 2, 1, 3};
    private static final int[][] AND = {{0, 0, 2, 0}, {0, 1, 2, 3}, {2, 2, 2, 2}, {0, 3, 2, 3}};
    private static final int[][] OR = {{0, 1, 0, 3}, {1, 1, 1, 1}, {0, 1, 2, 3}, {3, 1, 3, 3}};

    private final int code;

    Truth(int code) {
        this.code = code;
    }

    /** The kernel's numeric value ({@code ZXV_LP_*}). */
    public int code() {
        return code;
    }

    /**
     * The value for a kernel code.
     *
     * @throws ZxvUsageException for anything but 0..3
     */
    public static Truth fromCode(int code) {
        if (code < 0 || code > 3) throw new ZxvUsageException(-1, "ZXV_E_ARG", "not a four-valued logic state: " + code);
        return values()[code];
    }

    /** From evidence counts: both kinds give BOTH, only positive TRUE, only negative FALSE, none NEITHER. */
    public static Truth fromEvidence(long positive, long negative) {
        if (positive < 0 || negative < 0) throw new ZxvUsageException(-1, "ZXV_E_ARG", "evidence counts must be >= 0");
        return positive > 0 ? (negative > 0 ? BOTH : TRUE) : (negative > 0 ? FALSE : NEITHER);
    }

    /** Four-valued negation: swaps TRUE and FALSE, keeps NEITHER and BOTH. */
    public Truth not() {
        return values()[NOT[code]];
    }

    /** Four-valued conjunction. */
    public Truth and(Truth other) {
        return values()[AND[code][other.code]];
    }

    /** Four-valued disjunction. */
    public Truth or(Truth other) {
        return values()[OR[code][other.code]];
    }

    /** True only for {@link #TRUE}. */
    public boolean isCertainlyTrue() {
        return this == TRUE;
    }

    /** True only for {@link #FALSE}. */
    public boolean isCertainlyFalse() {
        return this == FALSE;
    }

    /** True for TRUE or FALSE (no contradiction, nothing pending). */
    public boolean isCertain() {
        return this == TRUE || this == FALSE;
    }

    /**
     * Collapses to a boolean with an explicit policy for the two non-classical states.
     *
     * @param onBoth    what a held contradiction means here
     * @param onNeither what "not yet known" means here
     */
    public boolean toBoolean(boolean onBoth, boolean onNeither) {
        return switch (this) {
            case TRUE -> true;
            case FALSE -> false;
            case BOTH -> onBoth;
            case NEITHER -> onNeither;
        };
    }

    /**
     * Returns normally only for {@link #TRUE}; otherwise throws the supplied exception.
     *
     * @param error builds the exception for FALSE, BOTH or NEITHER
     */
    public <X extends Throwable> void requireTrue(Supplier<? extends X> error) throws X {
        if (this != TRUE) throw error.get();
    }

    /** Returns normally only for {@link #TRUE}; otherwise throws {@link IllegalStateException} naming the state. */
    public void requireTrue() {
        if (this != TRUE) throw new IllegalStateException("required TRUE, was " + this);
    }
}
