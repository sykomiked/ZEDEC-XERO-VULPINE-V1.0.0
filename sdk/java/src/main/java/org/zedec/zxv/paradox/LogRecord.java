// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Ffi;

/**
 * An operator-log or frontier-log record ({@link #BYTES} bytes), parsed.
 * hash = SHA-256("zxv/pdx-log/v1" || bytes 0..111). op: 1 constructive,
 * 2 destructive, 3 neutral, 4 special, 5 draft, 6 review or registry add,
 * 7 graph pass.
 *
 * @param seq     the sequence number
 * @param op      the operator code
 * @param kind    the kind code
 * @param tag     the tag (0..63)
 * @param outcome the outcome code
 * @param aux     the operator's auxiliary value (uint32 bits)
 * @param a       the first 32-byte field
 * @param b       the second 32-byte field
 * @param prev    the previous record's hash
 * @param hash    this record's hash
 */
public record LogRecord(long seq, int op, int kind, int tag, int outcome, int aux, byte[] a, byte[] b, byte[] prev,
        byte[] hash) {
    /** Size ({@code ZXV_PDX_REC_BYTES}). */
    public static final int BYTES = 144;

    /** Parses a record's bytes. */
    public static LogRecord parse(byte[] r) {
        Ffi.requireLength("record", r, BYTES);
        return new LogRecord(Le.i64(r, 0), Le.u8(r, 8), Le.u8(r, 9), Le.u8(r, 10), Le.u8(r, 11), Le.i32(r, 12),
                Le.slice(r, 16, 32), Le.slice(r, 48, 32), Le.slice(r, 80, 32), Le.slice(r, 112, 32));
    }
}
