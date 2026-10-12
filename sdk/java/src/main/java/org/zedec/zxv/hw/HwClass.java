// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

/**
 * ZHP-1 component classes ({@code ZXV_HW_CLASS_*}); the dotted names in
 * the comments are the profile's text names. Classes from {@link #PRIVATE}
 * up (as unsigned 32-bit values) are an implementer's own.
 */
public final class HwClass {
    private HwClass() {}

    /** {@code ZXV_HW_CLASS_MACHINE} (device). */
    public static final int MACHINE = 1;
    /** {@code ZXV_HW_CLASS_CPU_CLUSTER} (cluster). */
    public static final int CPU_CLUSTER = 2;
    /** {@code ZXV_HW_CLASS_CPU_CORE} (core). */
    public static final int CPU_CORE = 3;
    /** {@code ZXV_HW_CLASS_CACHE} (mem.cache). */
    public static final int CACHE = 4;
    /** {@code ZXV_HW_CLASS_MEMORY} (mem). */
    public static final int MEMORY = 5;
    /** {@code ZXV_HW_CLASS_INTERCONNECT} (interconnect). */
    public static final int INTERCONNECT = 6;
    /** {@code ZXV_HW_CLASS_MATRIX_UNIT} (engine.tensor.int). */
    public static final int MATRIX_UNIT = 7;
    /** {@code ZXV_HW_CLASS_VECTOR_UNIT} (engine.vector). */
    public static final int VECTOR_UNIT = 8;
    /** {@code ZXV_HW_CLASS_TERNARY_UNIT} (engine.ternary). */
    public static final int TERNARY_UNIT = 9;
    /** {@code ZXV_HW_CLASS_HASH_UNIT} (engine.hash). */
    public static final int HASH_UNIT = 10;
    /** {@code ZXV_HW_CLASS_CRYPTO_UNIT} (engine.crypto). */
    public static final int CRYPTO_UNIT = 11;
    /** {@code ZXV_HW_CLASS_CODEC_UNIT} (engine.codec). */
    public static final int CODEC_UNIT = 12;
    /** {@code ZXV_HW_CLASS_RECONF_FABRIC} (fabric.reconfig). */
    public static final int RECONF_FABRIC = 13;
    /** {@code ZXV_HW_CLASS_STORAGE} (io.storage). */
    public static final int STORAGE = 14;
    /** {@code ZXV_HW_CLASS_NETWORK} (io.network). */
    public static final int NETWORK = 15;
    /** {@code ZXV_HW_CLASS_RADIO} (radio). */
    public static final int RADIO = 16;
    /** {@code ZXV_HW_CLASS_SENSOR} (sensor). */
    public static final int SENSOR = 17;
    /** {@code ZXV_HW_CLASS_ACTUATOR} (actuator). */
    public static final int ACTUATOR = 18;
    /** {@code ZXV_HW_CLASS_CLOCK} (clock). */
    public static final int CLOCK = 19;
    /** {@code ZXV_HW_CLASS_POWER} (power). */
    public static final int POWER = 20;
    /** {@code ZXV_HW_CLASS_SECURE_ELEMENT} (secure.element). */
    public static final int SECURE_ELEMENT = 21;
    /** {@code ZXV_HW_CLASS_DISPLAY} (io.display). */
    public static final int DISPLAY = 22;
    /** {@code ZXV_HW_CLASS_AUDIO} (io.audio). */
    public static final int AUDIO = 23;
    /** {@code ZXV_HW_CLASS_WIRE} (io.wire). */
    public static final int WIRE = 24;
    /** {@code ZXV_HW_CLASS_GPU} (engine.gpu). */
    public static final int GPU = 25;
    /** {@code ZXV_HW_CLASS_PACKAGE} (package). */
    public static final int PACKAGE = 26;
    /** {@code ZXV_HW_CLASS_LANE} (lane). */
    public static final int LANE = 27;
    /** {@code ZXV_HW_CLASS_LOGIC_UNIT} (engine.logic64). */
    public static final int LOGIC_UNIT = 28;
    /** {@code ZXV_HW_CLASS_NTT_UNIT} (engine.ntt). */
    public static final int NTT_UNIT = 29;
    /** {@code ZXV_HW_CLASS_FRAME_UNIT} (engine.frame.ubh168). */
    public static final int FRAME_UNIT = 30;
    /** {@code ZXV_HW_CLASS_GCD_UNIT} (engine.gcd). */
    public static final int GCD_UNIT = 31;
    /** {@code ZXV_HW_CLASS_MEM_CHANNEL} (mem.channel). */
    public static final int MEM_CHANNEL = 32;
    /** {@code ZXV_HW_CLASS_SECURE_ISLAND} (secure.island). */
    public static final int SECURE_ISLAND = 33;
    /** {@code ZXV_HW_CLASS_IO} (io). */
    public static final int IO = 34;
    /** {@code ZXV_HW_CLASS_PRIVATE}. */
    public static final int PRIVATE = 0x80000000;
}
