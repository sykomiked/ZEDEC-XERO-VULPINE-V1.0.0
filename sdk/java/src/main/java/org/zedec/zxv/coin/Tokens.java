// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * Swarm AI token denominations: tier k (0..{@link #TIERS}-1) has face
 * F(k+2), so 1, 2, 3, 5, 8, ... F(65). Tier 0 is the plain token; tiers
 * {@link #SUPER_TIER} (face 144) and up are super tokens. A dense token is
 * worth exactly its face in plain tokens: there is no premium.
 *
 * <p>Faces are unsigned 64-bit values in a Java {@code long}. Pure and
 * thread-safe; no capability needed.
 */
public final class Tokens {
    private Tokens() {}

    /** Number of tiers ({@code ZXV_TOKEN_TIERS}). */
    public static final int TIERS = 64;
    /** First super-token tier ({@code ZXV_TOKEN_SUPER_TIER}, face 144). */
    public static final int SUPER_TIER = 10;

    /**
     * The face of {@code tier}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for tier 64 and up
     */
    public static long face(int tier) {
        Buf.index("tier", tier);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment f = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.token_face, tier, f, MemorySegment.NULL), null);
            return f.get(JAVA_LONG, 0);
        }
    }

    /**
     * Whether {@code tier} is a super token (face 144 and up).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for tier 64 and up
     */
    public static boolean isSuper(int tier) {
        Buf.index("tier", tier);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.token_face, tier, MemorySegment.NULL, s), null);
            return s.get(JAVA_INT, 0) != 0;
        }
    }

    /**
     * The tier whose face is exactly {@code face}.
     *
     * @throws NotADenominationException when {@code face} is not one of the 64 faces
     */
    public static int tier(long face) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment t = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.token_tier, face, t), null);
            return t.get(JAVA_INT, 0);
        }
    }
}
