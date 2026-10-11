// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.wire;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.HexFormat;
import org.zedec.zxv.ZxvUsageException;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * One UBH-168 harmonic wire frame: 21 octets (168 bits). A tag octet, then
 * five 32-bit words in the fixed L-B-L-B-L byte order:
 * <pre>
 *   octet 0      tag = 100*sync + 10*shell + trunk
 *   octets 1-4   W0 little-endian  S+[0]
 *   octets 5-8   W1 big-endian     S-[0]
 *   octets 9-12  W2 little-endian  S+[1]
 *   octets 13-16 W3 big-endian     S-[1]
 *   octets 17-20 W4 little-endian  S+[2]
 * </pre>
 * The octets are identical on every host. Packing, unpacking and the idle
 * pilot are pure Java (a service can frame without the native library);
 * {@link #resolve()} calls libzxv's W5 correlation resolver. Words are raw
 * 32-bit values ({@code int}; read them unsigned with
 * {@link Integer#toUnsignedLong(int)}). Immutable.
 */
public final class Ubh168Frame {
    /** Frame length in octets. */
    public static final int BYTES = 21;
    /** The word every idle pilot carries in all five positions ("1VXZ"). */
    static final int IDLE_WORD = 0x5A585631;

    private static final ValueLayout.OfInt LE = ValueLayout.JAVA_INT_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);
    private static final ValueLayout.OfInt BE = ValueLayout.JAVA_INT_UNALIGNED.withOrder(ByteOrder.BIG_ENDIAN);

    private final int tag;
    private final int[] sPlus;
    private final int[] sMinus;

    private Ubh168Frame(int tag, int[] sPlus, int[] sMinus) {
        this.tag = tag;
        this.sPlus = sPlus;
        this.sMinus = sMinus;
    }

    /**
     * The tag octet for a trunk, shell and sync bit.
     *
     * @param trunk 0..9
     * @param shell 0..9
     * @param sync  0 or 1
     */
    public static int tag(int trunk, int shell, int sync) {
        if (trunk < 0 || trunk > 9 || shell < 0 || shell > 9 || sync < 0 || sync > 1) {
            throw Errors.usage(-1, "trunk and shell are 0..9, sync is 0..1");
        }
        return 100 * sync + 10 * shell + trunk;
    }

    /**
     * A frame from its parts.
     *
     * @param tag    0..199 (see {@link #tag(int, int, int)})
     * @param sPlus  three raw 32-bit S+ words
     * @param sMinus two raw 32-bit S- words
     */
    public static Ubh168Frame of(int tag, int[] sPlus, int[] sMinus) {
        if (tag < 0 || tag > 199) throw Errors.usage(-1, "tag must be 0..199");
        if (sPlus.length != 3 || sMinus.length != 2) throw Errors.usage(-1, "S+ has 3 words and S- has 2");
        return new Ubh168Frame(tag, sPlus.clone(), sMinus.clone());
    }

    /** The idle pilot frame of a trunk (never all zeros). */
    public static Ubh168Frame idle(int trunk) {
        if (trunk < 0 || trunk > 9) throw Errors.usage(-1, "trunk must be 0..9");
        int w = IDLE_WORD;
        return new Ubh168Frame(100 + trunk, new int[] {w, w, w}, new int[] {w, w});
    }

    /** Reads a frame from exactly 21 octets. */
    public static Ubh168Frame parse(byte[] octets) {
        Ffi.requireLength("frame", octets, BYTES);
        return parse(MemorySegment.ofArray(octets));
    }

    /** Reads a frame from the first 21 octets of a segment (heap or native; no copy). */
    public static Ubh168Frame parse(MemorySegment seg) {
        if (seg.byteSize() < BYTES) throw Errors.usage(-1, "a UBH-168 frame is 21 octets");
        int tag = Byte.toUnsignedInt(seg.get(ValueLayout.JAVA_BYTE, 0));
        if (tag > 199) throw new ZxvUsageException(-22, "ZXV_E_FORMAT", "tag octet " + tag + " is above 199");
        return new Ubh168Frame(tag,
                new int[] {seg.get(LE, 1), seg.get(LE, 9), seg.get(LE, 17)},
                new int[] {seg.get(BE, 5), seg.get(BE, 13)});
    }

    /** The tag octet. */
    public int tag() {
        return tag;
    }

    /** The trunk digit (tag mod 10). */
    public int trunk() {
        return tag % 10;
    }

    /** The shell digit. */
    public int shell() {
        return (tag / 10) % 10;
    }

    /** The sync bit. */
    public int sync() {
        return tag / 100;
    }

    /** A copy of the three S+ words. */
    public int[] sPlus() {
        return sPlus.clone();
    }

    /** A copy of the two S- words. */
    public int[] sMinus() {
        return sMinus.clone();
    }

    /** The 21 octets. */
    public byte[] toBytes() {
        byte[] out = new byte[BYTES];
        writeTo(MemorySegment.ofArray(out), 0);
        return out;
    }

    /** Writes the 21 octets into {@code seg} at {@code offset} (zero-copy into native or heap memory). */
    public void writeTo(MemorySegment seg, long offset) {
        seg.set(ValueLayout.JAVA_BYTE, offset, (byte) tag);
        seg.set(LE, offset + 1, sPlus[0]);
        seg.set(BE, offset + 5, sMinus[0]);
        seg.set(LE, offset + 9, sPlus[1]);
        seg.set(BE, offset + 13, sMinus[1]);
        seg.set(LE, offset + 17, sPlus[2]);
    }

    /** The lower-case hex of the 21 octets. */
    public String toHex() {
        return HexFormat.of().formatHex(toBytes());
    }

    /**
     * Runs libzxv's W5 correlation resolver (words read as signed Q16.16)
     * over this frame. Needs the native library.
     */
    public WireTruth resolve() {
        int[] out = new int[1];
        Ffi.check(Ffi.call(Native.wire_resolve, MemorySegment.ofArray(toBytes()), MemorySegment.ofArray(out)), null);
        return WireTruth.fromCode(out[0]);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Ubh168Frame f && f.tag == tag && Arrays.equals(f.sPlus, sPlus) && Arrays.equals(f.sMinus, sMinus);
    }

    @Override
    public int hashCode() {
        return 31 * (31 * tag + Arrays.hashCode(sPlus)) + Arrays.hashCode(sMinus);
    }

    @Override
    public String toString() {
        return "Ubh168Frame[" + toHex() + "]";
    }
}
