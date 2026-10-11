// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.swarm;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * The swarm token economy: each cycle, {@code tokensPerCycle} tokens are
 * split across the swarm's Fibonacci levels (the real axis) and, by the
 * swarm's mood, an imaginary (emotional) share is split by each model's own
 * feeling. Level {@code d} (0-based) holds at most F(d+2) models. Feelings
 * are emoji code points from the kernel palette with intensity 0..5. All
 * exact integers. Mutations need {@code SWARM}.
 *
 * <pre>{@code
 * try (Swarm swarm = Swarm.create(cap, 4, 100_000)) {
 *     swarm.register(100, 0);
 *     swarm.setMood("🤔", 4);           // thinking face
 *     swarm.beginCycle();
 *     Swarm.Grant g = swarm.consume(100, 500);
 * }
 * }</pre>
 */
public final class Swarm implements AutoCloseable {
    /**
     * A model's share of the current cycle.
     *
     * @param allotted  real plus imaginary tokens
     * @param imaginary the emotional (imaginary) part
     * @param used      consumed so far this cycle
     */
    public record Allotment(long allotted, long imaginary, long used) {
        /** The real (Fibonacci) part. */
        public long real() {
            return allotted - imaginary;
        }

        /** Tokens still available this cycle. */
        public long remaining() {
            return allotted - used;
        }
    }

    /**
     * The result of a consume.
     *
     * @param requested what was asked for
     * @param granted   what was granted (all of it, or what was left)
     */
    public record Grant(long requested, long granted) {
        /** True if less than requested was granted: the allotment is now spent. */
        public boolean exhausted() {
            return granted < requested;
        }
    }

    /**
     * An emotion's behaviour profile (pure kernel data).
     *
     * @param valence          negative, zero or positive
     * @param temperatureMilli sampling temperature, in thousandths
     * @param explorePermille  exploration rate, per mille
     * @param verifyPasses     verification passes
     * @param priority         scheduling priority
     */
    public record EmotionProfile(int valence, int temperatureMilli, int explorePermille, int verifyPasses, int priority) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private Swarm(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("swarm", h, x -> Ffi.callVoid(Native.swarm_destroy, x), ctx, Caps.res(ctx));
    }

    /**
     * A new swarm.
     *
     * @param numLevels      Fibonacci levels
     * @param tokensPerCycle tokens split each cycle
     */
    public static Swarm create(Capability cap, int numLevels, long tokensPerCycle) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.swarm_create_cap, l.h(0), numLevels, tokensPerCycle, out), ctx);
            return new Swarm(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    private void gated(java.lang.invoke.MethodHandle mh, Object... rest) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Object[] args = new Object[rest.length + 2];
            args[0] = l.h(0);
            args[1] = l.h(1);
            System.arraycopy(rest, 0, args, 2, rest.length);
            Ffi.check(Ffi.call(mh, args), ctx);
        }
    }

    /**
     * Registers a model at a level.
     *
     * @throws org.zedec.zxv.ZxvRefusedException {@code ZXV_E_CAPACITY} when the level is full
     */
    public void register(int modelId, int level) {
        gated(Native.swarm_register, modelId, level);
    }

    /** Marks a model active or inactive. */
    public void setActive(int modelId, boolean active) {
        gated(Native.swarm_set_active, modelId, active ? 1 : 0);
    }

    /** Changes the tokens split per cycle. */
    public void setRate(long tokensPerCycle) {
        gated(Native.swarm_set_rate, tokensPerCycle);
    }

    /**
     * Sets the swarm's mood.
     *
     * @param codepoint an emoji of the kernel palette (e.g. 0x1F914)
     * @param intensity 0..5 (charge 0, 1, 2, 3, 5, 8)
     * @throws org.zedec.zxv.ZxvUsageException {@code ZXV_E_NOT_FOUND} for an emoji outside the palette
     */
    public void setMood(int codepoint, int intensity) {
        gated(Native.swarm_set_mood, codepoint, intensity);
    }

    /** {@link #setMood(int, int)} with the emoji as text. */
    public void setMood(String emoji, int intensity) {
        setMood(codepoint(emoji), intensity);
    }

    /** Sets one model's feeling. */
    public void setFeeling(int modelId, int codepoint, int intensity) {
        gated(Native.swarm_set_feeling, modelId, codepoint, intensity);
    }

    /** {@link #setFeeling(int, int, int)} with the emoji as text. */
    public void setFeeling(int modelId, String emoji, int intensity) {
        setFeeling(modelId, codepoint(emoji), intensity);
    }

    /** Starts a cycle and computes every allotment; returns the cycle number. */
    public long beginCycle() {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.swarm_begin_cycle, l.h(0), l.h(1), c), ctx);
            return c.get(JAVA_LONG, 0);
        }
    }

    /**
     * Grants up to {@code requested} tokens to a model. Does not throw on
     * exhaustion: libzxv grants what is left, and the result says so
     * ({@link Grant#exhausted()}).
     */
    public Grant consume(int modelId, long requested) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment g = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native.swarm_consume, l.h(0), l.h(1), modelId, requested, g);
            if (st != 0 && st != -19) Ffi.check(st, ctx);
            return new Grant(requested, g.get(JAVA_LONG, 0));
        }
    }

    /** A model's allotment in the current cycle. */
    public Allotment allotment(int modelId) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment al = a.allocate(JAVA_LONG), im = a.allocate(JAVA_LONG), u = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.swarm_allotment, l.h(0), modelId, al, im, u), ctx);
            return new Allotment(al.get(JAVA_LONG, 0), im.get(JAVA_LONG, 0), u.get(JAVA_LONG, 0));
        }
    }

    /** Closes the cycle; returns the unused tokens, which expire (they never carry over). */
    public long endCycle() {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment e = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.swarm_end_cycle, l.h(0), l.h(1), e), ctx);
            return e.get(JAVA_LONG, 0);
        }
    }

    /** How many models level {@code d} holds: F(d+2) (1, 2, 3, 5, 8, ...). */
    public static int levelCapacity(int level) {
        int[] out = new int[1];
        Ffi.check(Ffi.call(Native.swarm_level_capacity, level, MemorySegment.ofArray(out)), null);
        return out[0];
    }

    /** The behaviour profile of a palette emotion. */
    public static EmotionProfile emotionProfile(int codepoint) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_INT), t = a.allocate(JAVA_INT), x = a.allocate(JAVA_INT),
                    p = a.allocate(JAVA_INT), pr = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.swarm_emotion_profile, codepoint, v, t, x, p, pr), null);
            return new EmotionProfile(v.get(JAVA_INT, 0), t.get(JAVA_INT, 0), x.get(JAVA_INT, 0),
                    p.get(JAVA_INT, 0), pr.get(JAVA_INT, 0));
        }
    }

    private static int codepoint(String emoji) {
        if (emoji == null || emoji.isEmpty()) throw Errors.usage(-1, "empty emoji");
        return emoji.codePointAt(0);
    }

    /** Frees the swarm. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
