// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * The 64-state hexagram tag of ABI 1.3 (the kernel's {@code lpres_hex}): an
 * immutable value that tags one unit of work with a four-valued
 * {@link HexBase}, the {@link HexPolarity} of its evidence and the
 * {@link HexPhase} of its lifecycle, packed as
 * {@code tag = base << 4 | polarity << 2 | phase} (0..63).
 *
 * <p>What happens next is {@link #action()}. A phase moves one step at a
 * time ({@link #step}), a forbidden tag stays forbidden, and
 * {@link #and}/{@link #or}/{@link #not} combine tags with libzxv's tables.
 * Every rule is computed by libzxv.
 *
 * <p>The base is numbered by its line pattern, not like {@link Truth}:
 * convert only with {@link #toTruth()} and {@link #fromTruth}.
 *
 * <pre>{@code
 * Hexagram h = new Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
 * h.action();                         // PROCEED
 * h = h.advance();                    // EMERGING
 * h.step(HexPhase.DISSOLVING);        // ZxvRefusedException (ZXV_E_STATE): a jump
 * }</pre>
 */
public final class Hexagram {
    private final int tag;

    private Hexagram(int tag) {
        this.tag = tag;
    }

    /** A tag from its three fields. */
    public Hexagram(HexBase base, HexPolarity polarity, HexPhase phase) {
        this(out8(Native.hex_pack, base.code(), polarity.code(), phase.code()));
    }

    /**
     * The tag with this byte value.
     *
     * @throws ZxvUsageException (ZXV_E_ARG) outside 0..63
     */
    public static Hexagram fromTag(int tag) {
        if (tag < 0 || tag > 255) throw new ZxvUsageException(-1, "ZXV_E_ARG", "tag " + tag + " is not 0..63");
        Ffi.check(Ffi.call(Native.hex_unpack, tag, MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL), null);
        return new Hexagram(tag);
    }

    /** A tag whose base is {@code truth} (mapped by {@code zxv_hex_from_lp}). */
    public static Hexagram fromTruth(Truth truth, HexPolarity polarity, HexPhase phase) {
        return new Hexagram(HexBase.fromCode(out8(Native.hex_from_lp, truth.code())), polarity, phase);
    }

    /**
     * Polarity from evidence counts before and after: stale gives ENTROPIC;
     * otherwise the margin |pos - neg| growing gives NEGENTROPIC, shrinking
     * ENTROPIC, unchanged NEUTRAL. Counts are uint32 (0..4294967295).
     */
    public static HexPolarity polarityFromEvidence(long pos0, long neg0, long pos1, long neg1, boolean stale) {
        for (long v : new long[] {pos0, neg0, pos1, neg1}) {
            if (v < 0 || v > 0xFFFFFFFFL) throw new ZxvUsageException(-8, "ZXV_E_RANGE", "evidence count " + v + " is not a uint32");
        }
        return HexPolarity.fromCode(out8(Native.hex_polarity, (int) pos0, (int) neg0, (int) pos1, (int) neg1, stale ? 1 : 0));
    }

    static int nextPhaseCode(int phase) {
        return out8(Native.hex_phase_next, phase);
    }

    /** The byte value, 0..63. */
    public int tag() {
        return tag;
    }

    /** Bits 5-4. */
    public HexBase base() {
        return HexBase.fromCode(tag >> 4);
    }

    /** Bits 3-2. */
    public HexPolarity polarity() {
        return HexPolarity.fromCode((tag >> 2) & 3);
    }

    /** Bits 1-0. */
    public HexPhase phase() {
        return HexPhase.fromCode(tag & 3);
    }

    /** True if the polarity is FORBIDDEN (absorbing). */
    public boolean isForbidden() {
        return polarity() == HexPolarity.FORBIDDEN;
    }

    /** What happens next: one lookup in the kernel's 64-entry table. */
    public HexAction action() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hex_action, tag, out), null);
            return HexAction.fromCode(out.get(JAVA_INT, 0));
        }
    }

    /** "base/polarity/phase", e.g. "true/negentropic/manifest". */
    public String name() {
        try {
            MemorySegment p = (MemorySegment) Native.hex_name.invokeExact(tag);
            return p.reinterpret(64).getString(0);
        } catch (Throwable t) {
            throw new IllegalStateException("zxv_hex_name failed", t);
        }
    }

    /** The base as a {@link Truth} ({@code zxv_hex_to_lp}); an explicit conversion. */
    public Truth toTruth() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hex_to_lp, tag >> 4, out), null);
            return Truth.fromCode(out.get(JAVA_INT, 0));
        }
    }

    /**
     * The same work in {@code phase}: the same phase or the next one only.
     *
     * @throws ZxvRefusedException (ZXV_E_STATE) for a jump
     */
    public Hexagram step(HexPhase phase) {
        return new Hexagram(out8(Native.hex_step, tag, phase.code()));
    }

    /** One step along the cycle. */
    public Hexagram advance() {
        return step(phase().next());
    }

    /**
     * New base and polarity, same phase.
     *
     * @throws ZxvRefusedException (ZXV_E_STATE) if this tag is forbidden and
     *     {@code polarity} is not FORBIDDEN (forbidden is absorbing)
     */
    public Hexagram withState(HexBase base, HexPolarity polarity) {
        return new Hexagram(out8(Native.hex_set, tag, base.code(), polarity.code()));
    }

    /** The same tag with polarity FORBIDDEN. */
    public Hexagram forbid() {
        return new Hexagram(out8(Native.hex_forbid, tag));
    }

    /** Conjunction: four-valued AND on the base, worst polarity, least advanced phase. */
    public Hexagram and(Hexagram other) {
        return new Hexagram(out8(Native.hex_and, tag, other.tag));
    }

    /** Disjunction: four-valued OR on the base, worst polarity, least advanced phase. */
    public Hexagram or(Hexagram other) {
        return new Hexagram(out8(Native.hex_or, tag, other.tag));
    }

    /** Negation of the base; polarity and phase are kept. */
    public Hexagram not() {
        return new Hexagram(out8(Native.hex_not, tag));
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Hexagram h && h.tag == tag;
    }

    @Override
    public int hashCode() {
        return tag;
    }

    @Override
    public String toString() {
        return "Hexagram(" + name() + ", tag=" + tag + ")";
    }

    /** Calls a pure hex function whose last argument is a uint8_t out pointer. */
    private static int out8(MethodHandle mh, Object... args) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_BYTE);
            Object[] all = java.util.Arrays.copyOf(args, args.length + 1);
            all[args.length] = out;
            Ffi.check(Ffi.call(mh, all), null);
            return Byte.toUnsignedInt(out.get(JAVA_BYTE, 0));
        }
    }
}
