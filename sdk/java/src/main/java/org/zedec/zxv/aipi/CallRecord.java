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
 * An AIPI call record (ABI 1.5): the bytes naming one invocation, a function
 * CID plus its input (at most {@value #INPUT_MAX} bytes). Its CID
 * ({@link #cid()}, codec {@code zxv-aipi-call}) is how executors and claims
 * refer to the call. Immutable and thread-safe; pure.
 *
 * <pre>{@code
 * CallRecord call = CallRecord.encode(fn.cid(), "hello".getBytes(UTF_8));
 * Cid callCid = call.cid();
 * }</pre>
 */
public final class CallRecord {
    /** Largest call input in bytes ({@code ZXV_AIPI_IO_MAX}). */
    public static final int INPUT_MAX = 1024;
    /** Longest call record in bytes ({@code ZXV_AIPI_CALL_MAX}). */
    public static final int MAX_BYTES = 1112;

    private final byte[] bytes;
    private final Cid function;
    private final byte[] input;
    private final Cid cid;

    private CallRecord(byte[] bytes, Cid function, byte[] input, Cid cid) {
        this.bytes = bytes;
        this.function = function;
        this.input = input;
        this.cid = cid;
    }

    /**
     * The call record for {@code function} on {@code input}.
     *
     * @param function a function CID (CIDv1 sha2-256 with an AIPI function codec)
     * @param input    at most {@link #INPUT_MAX} bytes (may be empty)
     * @throws org.zedec.zxv.cid.CidException if {@code function} is not a function CID
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG if the input is too long
     */
    public static CallRecord encode(Cid function, byte[] input) {
        Buf.nonNull("function", function);
        Buf.nonNull("input", input);
        byte[] f = function.bytes();
        byte[] rec;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment fs = Buf.in(a, f), is = Buf.in(a, input);
            rec = Buf.r4(null, MAX_BYTES, (b, c, n) -> Ffi.call(Native15.aipi_call_encode, fs, (long) f.length, is,
                    (long) input.length, b, c, n));
        }
        return decode(rec);
    }

    /**
     * The call CID of (function, input) without keeping the record: the same
     * value as {@code encode(function, input).cid()}.
     */
    public static Cid cidOf(Cid function, byte[] input) {
        Buf.nonNull("function", function);
        Buf.nonNull("input", input);
        byte[] f = function.bytes();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(Cid.MAX_BYTES), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_call_cid, Buf.in(a, f), (long) f.length, Buf.in(a, input),
                    (long) input.length, c, n), null);
            return Cid.of(Buf.read(c, n.get(JAVA_LONG, 0)));
        }
    }

    /**
     * Checks a call record strictly and reads its parts.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT if it is not a valid call record
     */
    public static CallRecord decode(byte[] record) {
        Buf.nonNull("record", record);
        byte[] rec = record.clone();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment fo = a.allocate(JAVA_LONG), fl = a.allocate(JAVA_LONG), io = a.allocate(JAVA_LONG),
                    il = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_call_decode, Buf.in(a, rec), (long) rec.length, fo, fl, io, il), null);
            Cid fn = Cid.of(slice(rec, fo.get(JAVA_LONG, 0), fl.get(JAVA_LONG, 0)));
            byte[] in = slice(rec, io.get(JAVA_LONG, 0), il.get(JAVA_LONG, 0));
            return new CallRecord(rec, fn, in, cidOf(fn, in));
        }
    }

    static byte[] slice(byte[] b, long off, long len) {
        return Arrays.copyOfRange(b, (int) off, (int) (off + len));
    }

    /** The record bytes (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The function this call invokes. */
    public Cid function() {
        return function;
    }

    /** The call's input (a copy). */
    public byte[] input() {
        return input.clone();
    }

    /** The call CID (codec {@code zxv-aipi-call}, sha2-256 of the record). */
    public Cid cid() {
        return cid;
    }

    /** Records are equal when their bytes are. */
    @Override
    public boolean equals(Object o) {
        return o instanceof CallRecord c && Arrays.equals(bytes, c.bytes);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    @Override
    public String toString() {
        return "CallRecord[" + function + ", " + input.length + " input bytes]";
    }
}
