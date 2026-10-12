// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

/**
 * The standard engines ({@code ZXV_HW_FN_*}): engine contracts every
 * {@link HwMatrix} starts with, each with a proven SOFT implementation and
 * version-1 golden vectors, so every call works on any device. Integers in
 * their inputs and outputs are little-endian; formats are in
 * {@code sdk/docs/HW_EMULATION.md}.
 */
public final class HwFunction {
    private HwFunction() {}

    /** {@code ZXV_HW_FN_SHA256}: op 0: SHA-256. */
    public static final int SHA256 = 0x5A450001;
    /** {@code ZXV_HW_FN_KECCAK}: op 0/1 SHA3-256/512, op 2/3 SHAKE. */
    public static final int KECCAK = 0x5A450002;
    /** {@code ZXV_HW_FN_MAC}: op 0: HMAC-SHA256 (key-bearing). */
    public static final int MAC = 0x5A450003;
    /** {@code ZXV_HW_FN_GCD}: op 0: n pairs of u64 -> n gcds. */
    public static final int GCD = 0x5A450004;
    /** {@code ZXV_HW_FN_LOGIC64}: 64-lane four-valued AND / OR / NOT. */
    public static final int LOGIC64 = 0x5A450005;
    /** {@code ZXV_HW_FN_TENSOR}: op 0: one Q8_0 row dot (zt_dot_raw). */
    public static final int TENSOR = 0x5A450006;
}
