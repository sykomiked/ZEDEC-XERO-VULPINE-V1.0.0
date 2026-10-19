// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

import java.io.ByteArrayOutputStream;
import java.util.List;
import java.util.Objects;
import org.zedec.zxv.internal.Errors;

/**
 * One golden vector of an engine contract: an opcode, an input and the
 * exact output every implementation must reproduce (at most
 * {@link #MAX_OUTPUT} bytes).
 *
 * @param op     the operation
 * @param input  the input bytes
 * @param expect the expected output bytes
 */
public record HwVector(int op, byte[] input, byte[] expect) {
    /** Longest expected output. */
    public static final int MAX_OUTPUT = 512;
    /** Most vectors in one contract ({@code HWM_MAX_VECTORS}). */
    public static final int MAX_VECTORS = 16;

    /** Checks the arguments. */
    public HwVector {
        Objects.requireNonNull(input, "input");
        Objects.requireNonNull(expect, "expect");
        if (expect.length > MAX_OUTPUT) throw Errors.usage(-8, "an expected output is at most 512 bytes");
    }

    /**
     * The vector list of a {@code ZXV_HW_IMG_VECTORS} package: for each vector
     * u32 op, u32 input length, input, u32 output length, output (little-endian).
     */
    public static byte[] encodeList(List<HwVector> vectors) {
        if (vectors.isEmpty() || vectors.size() > MAX_VECTORS) {
            throw Errors.usage(-8, "a contract has 1.." + MAX_VECTORS + " vectors");
        }
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        for (HwVector v : vectors) {
            put32(b, v.op);
            put32(b, v.input.length);
            b.writeBytes(v.input);
            put32(b, v.expect.length);
            b.writeBytes(v.expect);
        }
        return b.toByteArray();
    }

    private static void put32(ByteArrayOutputStream b, int v) {
        for (int i = 0; i < 4; i++) b.write(v >>> (8 * i));
    }
}
