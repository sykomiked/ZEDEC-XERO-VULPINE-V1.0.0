// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.zedec.zxv.Vectors.hex;
import static org.zedec.zxv.Vectors.ints;
import static org.zedec.zxv.Vectors.list;
import static org.zedec.zxv.Vectors.map;
import static org.zedec.zxv.Vectors.num;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.EnumSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.trust.QuarantineLog;

/** ABI 1.3: the 64-state hexagram tag and quarantine log against vectors.json "hex". */
class HexagramTest {
    private static final Map<String, Object> HX = map(Vectors.root().get("hex"));
    private static final Hexagram[] ALL = new Hexagram[64];

    static {
        for (int t = 0; t < 64; t++) ALL[t] = Hexagram.fromTag(t);
    }

    private static <E extends Enum<E>> List<String> lower(E[] values) {
        return java.util.Arrays.stream(values).map(v -> v.name().toLowerCase(Locale.ROOT)).toList();
    }

    @Test
    void tagsMatchVectors() {
        assertTrue(Zxv.features().contains(Feature.HEX));
        assertEquals(list(HX.get("base_names")), lower(HexBase.values()));
        assertEquals(list(HX.get("polarity_names")), lower(HexPolarity.values()));
        assertEquals(list(HX.get("phase_names")), lower(HexPhase.values()));
        assertEquals(list(HX.get("action_names")), lower(HexAction.values()));
        for (Object o : list(HX.get("tags"))) {
            Map<String, Object> v = map(o);
            Hexagram h = ALL[(int) num(v.get("tag"))];
            assertEquals(v.get("name"), h.name());
            assertEquals(num(v.get("base")), h.base().code());
            assertEquals(num(v.get("polarity")), h.polarity().code());
            assertEquals(num(v.get("phase")), h.phase().code());
            assertEquals(num(v.get("action")), h.action().code());
            assertEquals(h, new Hexagram(h.base(), h.polarity(), h.phase()));
        }
        int[] next = ints(HX.get("phase_next"));
        for (HexPhase p : HexPhase.values()) assertEquals(next[p.code()], p.next().code());
    }

    @Test
    void logicTables() {
        int[] not = ints(HX.get("not"));
        List<Object> and = list(HX.get("and")), or = list(HX.get("or"));
        for (Hexagram a : ALL) {
            assertEquals(not[a.tag()], a.not().tag());
            int[] ar = ints(and.get(a.tag())), or2 = ints(or.get(a.tag()));
            for (Hexagram b : ALL) {
                assertEquals(ar[b.tag()], a.and(b).tag());
                assertEquals(or2[b.tag()], a.or(b).tag());
            }
        }
    }

    @Test
    void stepAndSet() {
        List<Object> step = list(HX.get("step")), set = list(HX.get("set"));
        for (Hexagram h : ALL) {
            int[] st = ints(step.get(h.tag())), se = ints(set.get(h.tag()));
            for (HexPhase p : HexPhase.values()) {
                if (st[p.code()] < 0) {
                    ZxvRefusedException e = assertThrows(ZxvRefusedException.class, () -> h.step(p));
                    assertEquals("ZXV_E_STATE", e.statusName());
                } else {
                    assertEquals(st[p.code()], h.step(p).tag());
                }
            }
            for (HexBase b : HexBase.values()) {
                for (HexPolarity pol : HexPolarity.values()) {
                    int want = se[b.code() * 4 + pol.code()];
                    if (want < 0) assertThrows(ZxvRefusedException.class, () -> h.withState(b, pol));
                    else assertEquals(want, h.withState(b, pol).tag());
                }
            }
            assertEquals(new Hexagram(h.base(), HexPolarity.FORBIDDEN, h.phase()), h.forbid());
            assertSame(h.phase().next(), h.advance().phase());
        }
    }

    @Test
    void polarityAndTruthBridge() {
        for (Object o : list(HX.get("polarity"))) {
            Map<String, Object> c = map(o);
            assertEquals(num(c.get("polarity")), Hexagram.polarityFromEvidence(num(c.get("pos0")), num(c.get("neg0")),
                    num(c.get("pos1")), num(c.get("neg1")), (Boolean) c.get("stale")).code());
        }
        assertThrows(ZxvUsageException.class, () -> Hexagram.polarityFromEvidence(-1, 0, 0, 0, false));
        int[] lpToBase = ints(HX.get("lp_to_base")), baseToLp = ints(HX.get("base_to_lp"));
        for (Truth t : Truth.values()) {
            Hexagram h = Hexagram.fromTruth(t, HexPolarity.NEUTRAL, HexPhase.POTENTIAL);
            assertEquals(lpToBase[t.code()], h.base().code());
            assertSame(t, h.toTruth());
        }
        for (HexBase b : HexBase.values()) {
            assertEquals(baseToLp[b.code()], new Hexagram(b, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).toTruth().code());
        }
        // the numbering differs on purpose
        assertEquals(1, Truth.TRUE.code());
        assertEquals(3, Hexagram.fromTruth(Truth.TRUE, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).base().code());
    }

    @Test
    void refusalsAndValueSemantics() {
        for (int bad : new int[] {64, 100, 255, 256, -1}) {
            assertThrows(ZxvUsageException.class, () -> Hexagram.fromTag(bad));
        }
        Hexagram h = new Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.MANIFEST);
        assertEquals(63, h.tag());
        assertEquals("Hexagram(true/negentropic/manifest, tag=63)", h.toString());
        assertEquals(ALL[63].hashCode(), h.hashCode());
        assertNotEquals(ALL[62], h);
        Hexagram f = new Hexagram(HexBase.TRUE, HexPolarity.FORBIDDEN, HexPhase.POTENTIAL);
        assertSame(HexAction.ROLLBACK, f.action());
        for (int i = 0; i < 4; i++) {
            f = f.advance();
            assertTrue(f.isForbidden());
        }
        assertTrue(f.and(h).isForbidden() && f.or(h).isForbidden() && f.not().isForbidden());
        Hexagram ff = f;
        assertThrows(ZxvRefusedException.class, () -> ff.withState(HexBase.TRUE, HexPolarity.NEUTRAL));
        assertSame(HexBase.FALSE, f.withState(HexBase.FALSE, HexPolarity.FORBIDDEN).base());
    }

    private static byte[] link(long seq, int tag, int reason, long who, byte[] subj, byte[] prev) throws Exception {
        ByteBuffer b = ByteBuffer.allocate(15 + 8 + 1 + 1 + 4 + 32 + 32).order(ByteOrder.LITTLE_ENDIAN);
        b.put("zxv/lpx-qlog/v1".getBytes(StandardCharsets.US_ASCII)).putLong(seq).put((byte) tag).put((byte) reason)
                .putInt((int) who).put(subj).put(prev);
        return MessageDigest.getInstance("SHA-256").digest(b.array());
    }

    @Test
    void quarantineChainMatchesVectors() throws Exception {
        Map<String, Object> q = map(HX.get("qlog"));
        try (ZxvContext ctx = ZxvContext.create();
             Capability cap = ctx.rootCapability(Permission.all());
             QuarantineLog log = QuarantineLog.create(cap, 3)) {
            assertEquals(0, log.count());
            assertArrayEquals(new byte[32], log.head());
            byte[] prev = new byte[32];
            for (Object o : list(q.get("records"))) {
                Map<String, Object> r = map(o);
                byte[] subj = hex(r.get("subject"));
                int tag = (int) num(r.get("tag")), reason = (int) num(r.get("reason"));
                long who = num(r.get("submitter"));
                long seq = log.append(Hexagram.fromTag(tag), reason, (int) who, subj);
                assertEquals(num(r.get("seq")), seq);
                byte[] want = link(seq, tag, reason, who, subj, prev);
                assertArrayEquals(hex(r.get("hash")), want);
                QuarantineLog.Record rec = log.record((int) seq);
                assertArrayEquals(want, rec.hash());
                assertArrayEquals(prev, rec.prev());
                assertArrayEquals(subj, rec.subject());
                assertEquals(tag, rec.tag().tag());
                assertEquals(reason, rec.reason());
                assertEquals(who, Integer.toUnsignedLong(rec.submitter()));
                prev = want;
            }
            assertArrayEquals(hex(q.get("head")), log.head());
            assertEquals(3, log.count());
            log.verify();
            ZxvRefusedException full = assertThrows(ZxvRefusedException.class,
                    () -> log.append(ALL[0x3B], 0, 0, new byte[32]));
            assertEquals("ZXV_E_CAPACITY", full.statusName());
        }
    }

    @Test
    void quarantineRefusals() {
        Hexagram forb = new Hexagram(HexBase.BOTH, HexPolarity.FORBIDDEN, HexPhase.EMERGING);
        try (ZxvContext ctx = ZxvContext.create();
             Capability cap = ctx.rootCapability(Permission.all());
             Capability noAttest = cap.attenuate(EnumSet.complementOf(EnumSet.of(Permission.ATTEST)))) {
            assertThrows(CapabilityDeniedException.class, () -> QuarantineLog.create(noAttest, 4));
            assertThrows(ZxvUsageException.class, () -> QuarantineLog.create(cap, 0));
            assertThrows(ZxvUsageException.class, () -> QuarantineLog.create(cap, QuarantineLog.MAX_RECORDS + 1));
            QuarantineLog log = QuarantineLog.create(cap, 4);
            assertThrows(ZxvUsageException.class,
                    () -> log.append(new Hexagram(HexBase.TRUE, HexPolarity.NEUTRAL, HexPhase.EMERGING), 1, 1, new byte[32]));
            assertThrows(ZxvUsageException.class, () -> log.append(forb, 1, 1, new byte[31]));
            assertThrows(ZxvUsageException.class, () -> log.append(forb, 256, 1, new byte[32]));
            assertEquals(0, log.append(forb, 9, -1, new byte[32]));
            assertEquals(0xFFFFFFFFL, Integer.toUnsignedLong(log.record(0).submitter()));
            ctx.halt("test");
            assertThrows(ZxvFatalError.class, log::count);
            log.close();
        }
    }
}
