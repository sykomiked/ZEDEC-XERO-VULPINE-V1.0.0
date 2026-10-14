// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Ffi;

/**
 * A quarantine-log record ({@link #BYTES} bytes), parsed.
 * hash = SHA-256("zxv/lpx-qlog/v1" || bytes 0..77).
 *
 * @param seq       the sequence number
 * @param tag       the (forbidden) tag
 * @param reason    the {@link Trip} bits
 * @param submitter the submitter (uint32 bits)
 * @param subject   the RepairProof hash
 * @param prev      the previous record's hash
 * @param hash      this record's hash
 */
public record QuarantineRecord(long seq, int tag, int reason, int submitter, byte[] subject, byte[] prev,
        byte[] hash) {
    /** Size ({@code ZXV_PDX_QREC_BYTES}). */
    public static final int BYTES = 110;

    /** Parses a record's bytes. */
    public static QuarantineRecord parse(byte[] r) {
        Ffi.requireLength("record", r, BYTES);
        return new QuarantineRecord(Le.i64(r, 0), Le.u8(r, 8), Le.u8(r, 9), Le.i32(r, 10), Le.slice(r, 14, 32),
                Le.slice(r, 46, 32), Le.slice(r, 78, 32));
    }
}
