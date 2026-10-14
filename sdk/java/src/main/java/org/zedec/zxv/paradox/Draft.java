// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import java.util.Arrays;
import org.zedec.zxv.internal.Ffi;

/**
 * A draft classification of a frontier record, for review ({@link #BYTES}
 * bytes): the record index, its hash and signature, the proposed kind, the
 * caller's reason code and {@link ParadoxFlag}s. An immutable value; a peer
 * may send one, and {@link ParadoxBook#review} checks it against this book.
 */
public final class Draft {
    /** Size ({@code ZXV_PDX_DRAFT_BYTES}). */
    public static final int BYTES = 71;

    private final byte[] bytes;

    private Draft(byte[] bytes) {
        this.bytes = bytes;
    }

    /** A draft from its bytes (copied; checked when reviewed). */
    public static Draft of(byte[] bytes) {
        Ffi.requireLength("draft", bytes, BYTES);
        return new Draft(bytes.clone());
    }

    byte[] raw() {
        return bytes;
    }

    /** The draft's bytes (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The frontier record index (uint32 bits). */
    public int record() {
        return Le.i32(bytes, 0);
    }

    /** The frontier record's hash. */
    public byte[] recordHash() {
        return Le.slice(bytes, 4, 32);
    }

    /** The record's signature. */
    public byte[] signature() {
        return Le.slice(bytes, 36, 32);
    }

    /** The proposed kind code (0..2 for a valid draft). */
    public int kindCode() {
        return Le.u8(bytes, 68);
    }

    /** The reason code. */
    public int reason() {
        return Le.u8(bytes, 69);
    }

    /** The {@link ParadoxFlag} bits. */
    public int flags() {
        return Le.u8(bytes, 70);
    }

    /** Equal when the bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof Draft d && Arrays.equals(bytes, d.bytes);
    }

    /** A hash of the bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** Record, kind, reason and flags. */
    @Override
    public String toString() {
        return "Draft[rec=" + record() + ", kind=" + kindCode() + ", reason=" + reason() + ", flags=" + flags() + "]";
    }
}
