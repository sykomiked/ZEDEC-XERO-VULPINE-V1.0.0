// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

/**
 * ZHP-1 capability keys ({@code ZXV_HW_CAP_*}) and the {@link #ISA} values.
 * Bit 15 marks a critical key, bits 14-13 the comparison (MIN, EXACT, MASK,
 * INFO), bits 12-0 the id; ids 0x1000-0x1FFF are private. The values are the
 * kernel's and never change.
 */
public final class HwCapKey {
    private HwCapKey() {}

    /** {@code ZXV_HW_CAP_WORD_BITS}. */
    public static final int WORD_BITS = 0x0001;
    /** {@code ZXV_HW_CAP_COUNT}. */
    public static final int COUNT = 0x0002;
    /** {@code ZXV_HW_CAP_FREQ_KHZ}. */
    public static final int FREQ_KHZ = 0x6003;
    /** {@code ZXV_HW_CAP_BYTES}. */
    public static final int BYTES = 0x0004;
    /** {@code ZXV_HW_CAP_LINE_BYTES}. */
    public static final int LINE_BYTES = 0x6005;
    /** {@code ZXV_HW_CAP_ISA}. */
    public static final int ISA = 0x2006;
    /** {@code ZXV_HW_CAP_INT_WIDTHS}. */
    public static final int INT_WIDTHS = 0x4007;
    /** {@code ZXV_HW_CAP_RADIX}. */
    public static final int RADIX = 0x2008;
    /** {@code ZXV_HW_CAP_MACS_PER_CYCLE}. */
    public static final int MACS_PER_CYCLE = 0x0009;
    /** {@code ZXV_HW_CAP_SIMD}. */
    public static final int SIMD = 0x400A;
    /** {@code ZXV_HW_CAP_TICK_NS}. */
    public static final int TICK_NS = 0x600B;
    /** {@code ZXV_HW_CAP_REGIONS}. */
    public static final int REGIONS = 0x000C;
    /** {@code ZXV_HW_CAP_BANDWIDTH_MBPS}. */
    public static final int BANDWIDTH_MBPS = 0x000D;
    /** {@code ZXV_HW_CAP_LATENCY_NS}. */
    public static final int LATENCY_NS = 0x600E;
    /** {@code ZXV_HW_CAP_PQ_ALGS}. */
    public static final int PQ_ALGS = 0x400F;
    /** {@code ZXV_HW_CAP_HASH_ALGS}. */
    public static final int HASH_ALGS = 0x4010;
    /** {@code ZXV_HW_CAP_WIRE_BITS}. */
    public static final int WIRE_BITS = 0x2011;
    /** {@code ZXV_HW_CAP_PEAK_OPS_MHZ}. */
    public static final int PEAK_OPS_MHZ = 0x6012;
    /** {@code ZXV_HW_CAP_CONTRACT_ID}: the engine contract (ZXV_HW_FN_*). */
    public static final int CONTRACT_ID = 0x2013;
    /** {@code ZXV_HW_CAP_CONTRACT_VER}: its golden-vector version (MIN). */
    public static final int CONTRACT_VER = 0x0014;
    /** {@code ZXV_HW_CAP_KIND}: ZXV_HW_KIND_* serving the node. */
    public static final int KIND = 0x6015;
    /** {@code ZXV_HW_CAP_CAPACITY_CLASS}: coarse, 0..15. */
    public static final int CAPACITY_CLASS = 0x0016;
    /** {@code ZXV_HW_CAP_PERF_BUCKET}: coarse, 0..15. */
    public static final int PERF_BUCKET = 0x0017;
    /** {@code ZXV_HW_CAP_POWER_BUCKET}: ZXV_HW_POWER_* scale. */
    public static final int POWER_BUCKET = 0x6018;
    /** {@code ZXV_HW_ISA_X86_64}: a value of {@link #ISA}. */
    public static final int ISA_X86_64 = 1;
    /** {@code ZXV_HW_ISA_ARM64}: a value of {@link #ISA}. */
    public static final int ISA_ARM64 = 2;
    /** {@code ZXV_HW_ISA_RISCV64}: a value of {@link #ISA}. */
    public static final int ISA_RISCV64 = 3;
    /** {@code ZXV_HW_ISA_WASM32}: a value of {@link #ISA}. */
    public static final int ISA_WASM32 = 4;
}
