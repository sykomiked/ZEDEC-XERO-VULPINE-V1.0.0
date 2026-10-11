// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * SHA3-256, SHAKE256, Ethereum's Keccak-256, and the OS random generator.
 * Pure functions: no capability. The {@code MemorySegment} forms read heap or
 * native memory in place, without copying.
 */
public final class Hashes {
    private Hashes() {}

    /** FIPS 202 SHA3-256 (32 bytes). */
    public static byte[] sha3_256(byte[] data) {
        return sha3_256(MemorySegment.ofArray(data));
    }

    /** SHA3-256 of a segment, read in place. */
    public static byte[] sha3_256(MemorySegment data) {
        byte[] out = new byte[32];
        Ffi.check(Ffi.call(Native.sha3_256, data, data.byteSize(), MemorySegment.ofArray(out)), null);
        return out;
    }

    /** Keccak-256 with the pre-FIPS 0x01 padding, as Ethereum uses it (32 bytes). */
    public static byte[] keccak256(byte[] data) {
        return keccak256(MemorySegment.ofArray(data));
    }

    /** Keccak-256 of a segment, read in place. */
    public static byte[] keccak256(MemorySegment data) {
        byte[] out = new byte[32];
        Ffi.check(Ffi.call(Native.keccak256, data, data.byteSize(), MemorySegment.ofArray(out)), null);
        return out;
    }

    /** FIPS 202 SHAKE256 with {@code outLength} bytes of output. */
    public static byte[] shake256(byte[] data, int outLength) {
        return shake256(MemorySegment.ofArray(data), outLength);
    }

    /** SHAKE256 of a segment, read in place. */
    public static byte[] shake256(MemorySegment data, int outLength) {
        if (outLength < 0) throw Errors.usage(-1, "outLength must be >= 0");
        byte[] out = new byte[outLength];
        Ffi.check(Ffi.call(Native.shake256, data, data.byteSize(), MemorySegment.ofArray(out), (long) outLength), null);
        return out;
    }

    /** {@code n} bytes from the OS CSPRNG (getrandom / arc4random). */
    public static byte[] random(int n) {
        byte[] out = new byte[n];
        try (var a = java.lang.foreign.Arena.ofConfined()) {
            MemorySegment s = a.allocate(Math.max(1, n));
            Ffi.check(Ffi.call(Native.random, s, (long) n), null);
            MemorySegment.copy(s, 0, MemorySegment.ofArray(out), 0, n);
            Ffi.call(Native.secure_zero, s, (long) n);
        }
        return out;
    }

    /** Fills a native segment from the OS CSPRNG, in place. */
    public static void random(MemorySegment out) {
        if (!out.isNative()) throw Errors.usage(-1, "random(MemorySegment) needs a native segment; use random(int)");
        Ffi.check(Ffi.call(Native.random, out, out.byteSize()), null);
    }
}
