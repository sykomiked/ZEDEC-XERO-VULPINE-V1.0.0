// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import org.zedec.zxv.cid.Cid;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * An AIPI claim (ABI 1.5): the bytes an executor signs to answer a call, its
 * output (and, for CERT functions, a certificate) to a call CID, with its
 * 32-byte node id. A claim is not a CID. Immutable and thread-safe; pure.
 *
 * <p>Honest limits: signing a claim and checking its signature are the
 * caller's job (for example with {@link org.zedec.zxv.crypto.MlDsa87}); a
 * claim says what an executor answered, not that the answer is right.
 */
public final class ClaimRecord {
    /** Largest claim output in bytes ({@code ZXV_AIPI_IO_MAX}). */
    public static final int OUTPUT_MAX = 1024;
    /** Largest claim certificate in bytes ({@code ZXV_AIPI_CERT_MAX}). */
    public static final int CERT_MAX = 1024;
    /** Longest claim record in bytes ({@code ZXV_AIPI_CLAIM_MAX}). */
    public static final int MAX_BYTES = 2170;

    private final byte[] bytes;
    private final Cid call;
    private final byte[] executor, output, certificate;

    private ClaimRecord(byte[] bytes, Cid call, byte[] executor, byte[] output, byte[] certificate) {
        this.bytes = bytes;
        this.call = call;
        this.executor = executor;
        this.output = output;
        this.certificate = certificate;
    }

    /**
     * The claim record an executor signs.
     *
     * @param call        the call CID (a CIDv1 sha2-256 call CID)
     * @param executor    the executor's 32-byte node id
     * @param output      its answer, at most {@link #OUTPUT_MAX} bytes (may be empty)
     * @param certificate CERT functions: the certificate, at most {@link #CERT_MAX} bytes; else empty
     * @throws org.zedec.zxv.cid.CidException if {@code call} is not a call CID
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG for an output or certificate over 1024 bytes
     */
    public static ClaimRecord encode(Cid call, byte[] executor, byte[] output, byte[] certificate) {
        Buf.nonNull("call", call);
        Buf.nonNull("output", output);
        Buf.nonNull("certificate", certificate);
        byte[] c = call.bytes();
        byte[] rec;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment cs = Buf.in(a, c), ex = Buf.fixed(a, "executor", executor, 32), os = Buf.in(a, output),
                    ce = Buf.in(a, certificate);
            rec = Buf.r4(null, MAX_BYTES, (b, cap, n) -> Ffi.call(Native15.aipi_claim_encode, cs, (long) c.length, ex, os,
                    (long) output.length, ce, (long) certificate.length, b, cap, n));
        }
        return decode(rec);
    }

    /**
     * Checks a claim record strictly and reads its parts.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT if it is not a valid claim record
     */
    public static ClaimRecord decode(byte[] record) {
        Buf.nonNull("record", record);
        byte[] rec = record.clone();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment co = a.allocate(JAVA_LONG), cl = a.allocate(JAVA_LONG), eo = a.allocate(JAVA_LONG),
                    oo = a.allocate(JAVA_LONG), ol = a.allocate(JAVA_LONG), ko = a.allocate(JAVA_LONG),
                    kl = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_claim_decode, Buf.in(a, rec), (long) rec.length, co, cl, eo, oo, ol, ko, kl),
                    null);
            return new ClaimRecord(rec, Cid.of(CallRecord.slice(rec, co.get(JAVA_LONG, 0), cl.get(JAVA_LONG, 0))),
                    CallRecord.slice(rec, eo.get(JAVA_LONG, 0), 32),
                    CallRecord.slice(rec, oo.get(JAVA_LONG, 0), ol.get(JAVA_LONG, 0)),
                    CallRecord.slice(rec, ko.get(JAVA_LONG, 0), kl.get(JAVA_LONG, 0)));
        }
    }

    /** The record bytes to sign (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The call this claim answers. */
    public Cid call() {
        return call;
    }

    /** The executor's 32-byte node id (a copy). */
    public byte[] executor() {
        return executor.clone();
    }

    /** The claimed output (a copy). */
    public byte[] output() {
        return output.clone();
    }

    /** The certificate (a copy; empty unless the function is CERT). */
    public byte[] certificate() {
        return certificate.clone();
    }

    /** Records are equal when their bytes are. */
    @Override
    public boolean equals(Object o) {
        return o instanceof ClaimRecord c && Arrays.equals(bytes, c.bytes);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    @Override
    public String toString() {
        return "ClaimRecord[" + call + ", " + output.length + " output bytes]";
    }
}
