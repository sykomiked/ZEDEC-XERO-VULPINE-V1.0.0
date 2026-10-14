// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import java.lang.foreign.Arena;
import java.util.Arrays;
import org.zedec.zxv.ZxvStatus;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * A RepairProof ({@link #BYTES} bytes, little-endian): what was contained,
 * the state hashes before and after rollback, the signature to share with
 * peers, and the proof hash that binds it all. An immutable value. Signing
 * it is the caller's job; {@link #verify()} checks integrity only.
 */
public final class RepairProof {
    /** Size ({@code ZXV_PDX_REPAIR_BYTES}). */
    public static final int BYTES = 160;

    private final byte[] bytes;

    private RepairProof(byte[] bytes) {
        this.bytes = bytes;
    }

    /** A proof from its bytes (copied; not verified). */
    public static RepairProof of(byte[] bytes) {
        Ffi.requireLength("proof", bytes, BYTES);
        return new RepairProof(bytes.clone());
    }

    /** The proof's bytes (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The {@link Trip} bits that fired. */
    public int trips() {
        return Le.i32(bytes, 0);
    }

    /** The steps reported. */
    public int steps() {
        return Le.i32(bytes, 4);
    }

    /** The budget spent reported. */
    public int spent() {
        return Le.i32(bytes, 8);
    }

    /** The number of trace fingerprints. */
    public int traceLength() {
        return Le.i32(bytes, 12);
    }

    /** The submitter (uint32 bits). */
    public int submitter() {
        return Le.i32(bytes, 16);
    }

    /** The state length. */
    public int stateLength() {
        return Le.i32(bytes, 20);
    }

    /** SHA-256 of the state as found. */
    public byte[] stateFoundHash() {
        return Le.slice(bytes, 24, 32);
    }

    /** SHA-256 of the state after rollback. */
    public byte[] stateRestoredHash() {
        return Le.slice(bytes, 56, 32);
    }

    /** The signature to share with peers ({@link Paradox#signature} of the trap's pattern). */
    public byte[] signature() {
        return Le.slice(bytes, 88, 32);
    }

    /** The proof hash, SHA-256("zxv/pdx-repair/v1" || bytes 0..119). */
    public byte[] hash() {
        return Le.slice(bytes, 120, 32);
    }

    /** The quarantine record's index. */
    public long quarantineIndex() {
        return Le.i64(bytes, 152);
    }

    /**
     * Recomputes the binding hash.
     *
     * @throws org.zedec.zxv.VerificationFailedException when it does not match
     */
    public void verify() {
        try (Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.paradox_repair_verify, Buf.in(a, bytes)), null);
        }
    }

    /** Whether {@link #verify()} would succeed. */
    public boolean isValid() {
        try (Arena a = Arena.ofConfined()) {
            int st = Ffi.call(Native15.paradox_repair_verify, Buf.in(a, bytes));
            if (st != Ffi.OK && st != ZxvStatus.E_VERIFY) Ffi.check(st, null);
            return st == Ffi.OK;
        }
    }

    /** Equal when the bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof RepairProof p && Arrays.equals(bytes, p.bytes);
    }

    /** A hash of the bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** Trips and quarantine index. */
    @Override
    public String toString() {
        return "RepairProof[trips=" + trips() + ", quarantine=" + quarantineIndex() + "]";
    }
}
