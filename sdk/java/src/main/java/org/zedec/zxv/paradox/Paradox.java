// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.HexAction;
import org.zedec.zxv.Hexagram;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The paradox engine's pure functions. A PARADOX is work whose four-valued
 * state is Both (evidence points both ways) or Neither (no evidence). The
 * engine does not decide which side is "really" true: it reads the work's
 * hexagram tag, gives the paradox one of four {@link ParadoxKind}s, and runs
 * a fixed, logged operator for that kind ({@link ParadoxBook}).
 *
 * <p>Honest limits, in plain words: automatic changes happen only inside
 * published bounds, and the constructive operator has no balance input, so
 * it cannot move money. Policies that touch money or devices need the owner,
 * and "owner ok" is the caller's assertion: inside one process this library
 * cannot check who the owner is. Detection is only by the checks the caller
 * runs (step limit, budget, repeated fingerprint, invariant result, registry
 * lookup); what they do not look at goes undetected. Destructive work is
 * contained, never answered: there is no way to reply to or act against a
 * sender. This is contradiction-tolerant bookkeeping; it never makes both
 * sides true. Integers only.
 *
 * <p>Thread-safe; no context or capability.
 */
public final class Paradox {
    private Paradox() {}

    /** Hash size ({@code ZXV_PDX_HASH_BYTES}). */
    public static final int HASH_BYTES = 32;
    /** Records per log ({@code ZXV_PDX_LOG_MAX}). */
    public static final int LOG_MAX = 65536;
    /** Registry entries ({@code ZXV_PDX_REGISTRY_MAX}). */
    public static final int REGISTRY_MAX = 65536;
    /** Parameters per policy ({@code ZXV_PDX_POLICY_MAX}). */
    public static final int POLICY_MAX = 64;
    /** Branches per hold ({@code ZXV_PDX_HOLD_MAX}). */
    public static final int HOLD_MAX = 64;
    /** Nodes per graph ({@code ZXV_PDX_GRAPH_MAX}). */
    public static final int GRAPH_MAX = 64;
    /** Dependencies per node ({@code ZXV_PDX_MAX_DEPS}). */
    public static final int MAX_DEPS = 4;
    /** Queued node updates per pass ({@code ZXV_PDX_UPDATES_MAX}). */
    public static final int UPDATES_MAX = 256;
    /** State fingerprints per detection ({@code ZXV_PDX_TRACE_MAX}). */
    public static final int TRACE_MAX = 4096;
    /** The source of a registry entry not learned from the frontier ({@code ZXV_PDX_SRC_SEED}). */
    public static final long SRC_SEED = -1L;

    /** The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data) of a canonical description. */
    public static byte[] signature(byte[] data) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(32);
            long n = data == null ? 0 : data.length;
            Ffi.check(Ffi.call(Native15.paradox_signature, Buf.in(a, data), n, out), null);
            return out.toArray(JAVA_BYTE);
        }
    }

    /** The kind a tag reads as on its own (a forbidden tag reads as NONE: forbidden is not a kind). */
    public static ParadoxKind kindOfTag(Hexagram tag) {
        Buf.nonNull("tag", tag);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment k = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_kind_of_tag, tag.tag(), k), null);
            return ParadoxKind.fromCode(k.get(JAVA_INT, 0));
        }
    }

    /**
     * What to do with a classified pair (a forbidden tag gives ROLLBACK whatever the kind).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for a pair that breaks the class rules
     */
    public static ParadoxAction action(ParadoxKind kind, Hexagram tag) {
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_action, kind.code(), tag.tag(), d), null);
            return ParadoxAction.fromCode(d.get(JAVA_INT, 0));
        }
    }

    /** The hexagram action an outcome amounts to (APPLIED is PROCEED, DEFERRED is WAIT, ...). */
    public static HexAction outcomeAction(Outcome outcome) {
        Buf.nonNull("outcome", outcome);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_outcome_action, outcome.code(), d), null);
            return HexAction.fromCode(d.get(JAVA_INT, 0));
        }
    }

    /** libzxv's name of a kind ("constructive"). */
    public static String name(ParadoxKind kind) {
        return str(Native15.paradox_kind_name, kind.code());
    }

    /** libzxv's name of an outcome ("proposal-for-owner"). */
    public static String name(Outcome outcome) {
        return str(Native15.paradox_outcome_name, outcome.code());
    }

    /** libzxv's name of a node state ("sandboxed"). */
    public static String name(NodeState state) {
        return str(Native15.paradox_node_state_name, state.code());
    }

    private static String str(java.lang.invoke.MethodHandle mh, int v) {
        try {
            MemorySegment p = (MemorySegment) mh.invoke(v);
            return p.reinterpret(64).getString(0);
        } catch (Throwable t) {
            throw new IllegalStateException(t);
        }
    }
}
