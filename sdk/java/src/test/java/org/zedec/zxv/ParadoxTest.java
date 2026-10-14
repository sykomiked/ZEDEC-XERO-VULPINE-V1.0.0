// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.paradox.Classification;
import org.zedec.zxv.paradox.Draft;
import org.zedec.zxv.paradox.Evidence;
import org.zedec.zxv.paradox.LogRecord;
import org.zedec.zxv.paradox.NodeState;
import org.zedec.zxv.paradox.Outcome;
import org.zedec.zxv.paradox.OwnerApprovalRequiredException;
import org.zedec.zxv.paradox.Paradox;
import org.zedec.zxv.paradox.ParadoxAction;
import org.zedec.zxv.paradox.ParadoxBook;
import org.zedec.zxv.paradox.ParadoxFlag;
import org.zedec.zxv.paradox.ParadoxGraph;
import org.zedec.zxv.paradox.ParadoxHold;
import org.zedec.zxv.paradox.ParadoxKind;
import org.zedec.zxv.paradox.ParadoxLog;
import org.zedec.zxv.paradox.ParadoxPolicy;
import org.zedec.zxv.paradox.QuarantineRecord;
import org.zedec.zxv.paradox.RepairProof;
import org.zedec.zxv.paradox.StepLimitException;

/** ABI 1.5 paradox vectors: every step runs in order on ONE book (log capacity 64, registry 16). */
class ParadoxTest {
    private static final Map<String, Object> V = V15.sec("paradox");

    private static Hexagram tag(Object o) {
        return Hexagram.fromTag(V15.i(o));
    }

    private static ParadoxKind kind(Object o) {
        return ParadoxKind.fromCode(V15.i(o));
    }

    private static long[] longs(Object o) {
        List<Object> l = V15.l(o);
        long[] r = new long[l.size()];
        for (int i = 0; i < r.length; i++) r[i] = Long.parseLong((String) l.get(i));
        return r;
    }

    private static void heads(ParadoxBook b, Object o) {
        Map<String, Object> h = V15.m(o);
        for (ParadoxLog log : ParadoxLog.values()) {
            Map<String, Object> w = V15.m(h.get(log.name().toLowerCase()));
            assertEquals(V15.i(w.get("count")), b.logCount(log), log.name());
            assertEquals(w.get("head"), V15.hex(b.logHead(log)), log.name());
        }
    }

    @Test
    void pureFunctions() {
        List<Object> sigs = V15.l(V.get("signature"));
        for (Object o : sigs) {
            Map<String, Object> c = V15.m(o);
            assertEquals(c.get("sig"), V15.hex(Paradox.signature(V15.utf8(c.get("utf8")))));
        }
        List<Object> kn = V15.l(V.get("kind_names"));
        for (ParadoxKind k : ParadoxKind.values()) assertEquals(kn.get(k.code()), Paradox.name(k));
        List<Object> on = V15.l(V.get("outcome_names"));
        for (Outcome k : Outcome.values()) assertEquals(on.get(k.code()), Paradox.name(k));
        List<Object> sn = V15.l(V.get("node_state_names"));
        for (NodeState k : NodeState.values()) assertEquals(sn.get(k.code()), Paradox.name(k));
        int[] kot = Vectors.ints(V.get("kind_of_tag"));
        for (int t = 0; t < 64; t++) assertEquals(kot[t], Paradox.kindOfTag(Hexagram.fromTag(t)).code(), "tag " + t);
        List<Object> act = V15.l(V.get("action"));
        for (ParadoxKind k : ParadoxKind.values()) {
            int[] row = Vectors.ints(act.get(k.code()));
            for (int t = 0; t < 64; t++) {
                Hexagram h = Hexagram.fromTag(t);
                if (row[t] < 0) {
                    assertEquals(ZxvStatus.E_ARG, V15.status(assertThrows(ZxvUsageException.class, () -> Paradox.action(k, h))));
                } else {
                    assertEquals(ParadoxAction.fromCode(row[t]), Paradox.action(k, h));
                }
            }
        }
        int[] oa = Vectors.ints(V.get("outcome_action"));
        for (Outcome o : Outcome.values()) assertEquals(oa[o.code()], Paradox.outcomeAction(o).code());
    }

    @Test
    void oneBook() {
        byte[] sigL = Paradox.signature(V15.utf8("loop:A->B->A"));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                ParadoxBook b = ParadoxBook.create(cap, 64, 16)) {
            heads(b, V.get("heads_fresh"));
            for (Object o : V15.l(V.get("classify"))) {
                Map<String, Object> c = V15.m(o);
                if (c.containsKey("registry_add")) {
                    Map<String, Object> r = V15.m(c.get("registry_add"));
                    assertEquals(V15.i(r.get("index")), b.registryAdd(sigL, kind(r.get("kind")), V15.i(r.get("reason")),
                            V15.i(r.get("flags")), (Boolean) r.get("owner_ok")));
                    continue;
                }
                Evidence ev = null;
                if (c.get("evidence") != null) {
                    int[] e = Vectors.ints(c.get("evidence"));
                    ev = new Evidence(e[0], e[1], e[2], e[3], (Boolean) c.get("stale"));
                }
                Classification k = b.classify(tag(c.get("tag")), ev, c.get("sig") == null ? null : sigL);
                assertEquals(new Classification(kind(c.get("kind")), tag(c.get("out_tag")), V15.i(c.get("reg_index"))), k);
            }
            for (Object o : V15.l(V.get("registry"))) {
                Map<String, Object> c = V15.m(o);
                Integer idx = V15.expect(c.get("status"), () -> b.registryAdd(Paradox.signature(V15.utf8(c.get("utf8"))),
                        kind(c.get("kind")), 0, V15.i(c.get("flags")), (Boolean) c.get("owner_ok")));
                if (idx != null) assertEquals(V15.i(c.get("index")), idx);
            }
            assertThrows(OwnerApprovalRequiredException.class,
                    () -> b.registryAdd(new byte[32], ParadoxKind.NEUTRAL, 0, ParadoxFlag.DEVICE, false));
            heads(b, V.get("heads_after_classify"));
            assertEquals(0, b.registryFind(sigL).orElseThrow());
            assertEquals(Paradox.SRC_SEED, b.registryEntry(0).source());

            constructive(cap, b);
            destructive(b);
            neutral(cap, b);
            special(b);
            for (Object g : V15.l(V.get("graphs"))) graph(cap, b, V15.m(g));

            heads(b, V.get("heads_final"));
            Map<String, Object> logs = V15.m(V.get("logs"));
            for (ParadoxLog log : ParadoxLog.values()) {
                List<Object> recs = V15.l(logs.get(log.name().toLowerCase()));
                for (int i = 0; i < recs.size(); i++) {
                    byte[] r = b.logRecord(log, i);
                    assertEquals(recs.get(i), V15.hex(r), log + " " + i);
                    if (log == ParadoxLog.QUARANTINE) assertEquals(i, QuarantineRecord.parse(r).seq());
                    else assertEquals(i, LogRecord.parse(r).seq());
                }
                assertEquals(ZxvStatus.E_NOT_FOUND, V15.status(assertThrows(ZxvException.class,
                        () -> b.logRecord(log, recs.size()))));
            }
            List<Object> lv = V15.l(V.get("log_verify"));
            for (ParadoxLog log : ParadoxLog.values()) V15.expectRun(lv.get(log.code()), () -> b.logVerify(log));
        }
    }

    private static void constructive(Capability cap, ParadoxBook b) {
        Map<String, Object> c = V15.m(V.get("constructive"));
        long[] lo = longs(c.get("lo")), hi = longs(c.get("hi")), init = longs(c.get("initial"));
        try (ParadoxPolicy plain = ParadoxPolicy.create(cap, lo, hi, init, 0);
                ParadoxPolicy money = ParadoxPolicy.create(cap, lo, hi, init, ParadoxFlag.MONEY)) {
            assertArrayEquals(hi, plain.bounds().hi());
            assertEquals(ParadoxFlag.MONEY, money.bounds().flags());
            for (Object o : V15.l(c.get("steps"))) {
                Map<String, Object> s = V15.m(o);
                ParadoxPolicy p = "money".equals(s.get("policy")) ? money : plain;
                long[] synth = s.get("synth") == null ? null : longs(s.get("synth"));
                Outcome out = V15.expect(s.get("status"), () -> b.constructive(p, kind(s.get("kind")), tag(s.get("tag")),
                        synth, V15.i(s.get("tension"))));
                if (out != null) assertEquals(Outcome.fromCode(V15.i(s.get("outcome"))), out);
                ParadoxPolicy.Values v = p.values();
                assertArrayEquals(longs(s.get("values")), v.values());
                assertArrayEquals(longs(s.get("prev")), v.previous());
                assertEquals(s.get("armed"), v.armed());
                assertEquals(V15.i(s.get("tension_at")), v.tensionAt());
                if (s.containsKey("proposal")) assertEquals(s.get("proposal"), V15.hex(p.proposal(synth)));
            }
        }
    }

    private static void destructive(ParadoxBook b) {
        for (Object o : V15.l(V.get("destructive"))) {
            Map<String, Object> c = V15.m(o);
            List<Object> tr = V15.l(c.get("trace"));
            long[] trace = new long[tr.size()];
            for (int i = 0; i < trace.length; i++) trace[i] = V15.u64(tr.get(i));
            ParadoxBook.Detection d = new ParadoxBook.Detection(V15.i(c.get("steps")), V15.i(c.get("step_limit")),
                    V15.i(c.get("spent")), V15.i(c.get("budget")), trace, (Boolean) c.get("invariant_ok"));
            byte[] pattern = ((String) c.get("pattern_utf8")).isEmpty() ? null : V15.utf8(c.get("pattern_utf8"));
            byte[] state = V15.utf8(c.get("state_utf8"));
            ParadoxBook.Containment r = b.destructive(kind(c.get("kind")), tag(c.get("tag")), d, state,
                    V15.utf8(c.get("checkpoint_utf8")), pattern, V15.i(c.get("submitter")));
            assertEquals(Outcome.fromCode(V15.i(c.get("outcome"))), r.outcome());
            assertEquals(c.get("state_after"), V15.hex(r.state()));
            assertArrayEquals(V15.utf8(c.get("state_utf8")), state, "the caller's array is not modified");
            if (c.containsKey("repair_proof")) {
                RepairProof p = r.proof();
                assertEquals(c.get("repair_proof"), V15.hex(p.bytes()));
                V15.expectRun(c.get("repair_verify"), p::verify);
                byte[] bad = p.bytes();
                bad[0] ^= 1;
                V15.expectRun(c.get("repair_verify_flipped_byte0"), () -> RepairProof.of(bad).verify());
                assertFalse(RepairProof.of(bad).isValid());
                assertArrayEquals(Paradox.signature(pattern), p.signature());
                assertEquals(0, p.quarantineIndex());
            } else {
                assertNull(r.proof());
            }
        }
    }

    private static void neutral(Capability cap, ParadoxBook b) {
        Map<String, Object> c = V15.m(V.get("neutral"));
        List<Object> br = V15.l(c.get("branches"));
        byte[] b0 = V15.hex(br.get(0)), b1 = V15.hex(br.get(1));
        try (ParadoxHold h = ParadoxHold.create(cap, V15.i(c.get("capacity")), V15.i(c.get("decisive")))) {
            h.add(b0);
            h.add(b1);
            h.evidence(b0, true);
            h.evidence(b1, true);
            for (Object o : V15.l(c.get("steps"))) {
                Map<String, Object> s = V15.m(o);
                if (!s.containsKey("kind")) continue;
                for (int k = 0; k < V15.i(s.get("add_for_branch0")); k++) h.evidence(b0, true);
                ParadoxBook.Hold r = V15.expect(s.get("status"),
                        () -> b.neutral(h, kind(s.get("kind")), tag(s.get("tag")), V15.i(s.get("owner_pick"))));
                assertEquals(new ParadoxBook.Hold(Outcome.fromCode(V15.i(s.get("outcome"))), V15.i(s.get("settled"))), r);
            }
            assertEquals(0, h.info().settled());
            assertArrayEquals(b0, h.branch(0).id());
            assertEquals(3, h.branch(0).positive());
        }
    }

    private static void special(ParadoxBook b) {
        Map<String, Object> c = V15.m(V.get("special"));
        byte[] sg = Paradox.signature(V15.utf8(c.get("sig_utf8")));
        assertEquals(c.get("sig"), V15.hex(sg));
        ParadoxBook.Frontier f = b.special(kind(c.get("kind")), tag(c.get("tag")), sg, V15.hex(c.get("subject")));
        assertEquals(new ParadoxBook.Frontier(Outcome.fromCode(V15.i(c.get("outcome"))), V15.i(c.get("rec"))), f);
        Map<String, Object> dc = V15.m(c.get("draft")), dm = V15.m(c.get("draft_money"));
        Draft draft = b.draft(f.record(), kind(dc.get("kind")), V15.i(dc.get("reason")), V15.i(dc.get("flags")));
        assertEquals(dc.get("bytes"), V15.hex(draft.bytes()));
        Draft money = b.draft(f.record(), kind(dm.get("kind")), V15.i(dm.get("reason")), V15.i(dm.get("flags")));
        assertEquals(dm.get("bytes"), V15.hex(money.bytes()));
        assertEquals(ParadoxFlag.MONEY, money.flags());
        for (Object o : V15.l(c.get("review"))) {
            Map<String, Object> r = V15.m(o);
            Draft d = "draft_money".equals(r.get("draft")) ? money : draft;
            Outcome out = V15.expect(r.get("status"), () -> b.review(d, (Boolean) r.get("owner_ok"), (Boolean) r.get("consensus_ok")));
            assertEquals(Outcome.fromCode(V15.i(r.get("outcome"))), out);
        }
        assertEquals(V15.i(c.get("registry_count_after")), b.registryCount());
    }

    private static byte[] id(Object letter) {
        return V15.fill(((String) letter).charAt(0), 32);
    }

    private static void graph(Capability cap, ParadoxBook b, Map<String, Object> g) {
        try (ParadoxGraph graph = ParadoxGraph.create(cap, V15.i(g.get("step_limit")))) {
            for (Object o : V15.l(g.get("nodes"))) {
                Map<String, Object> n = V15.m(o);
                V15.expectRun(n.get("status"), () -> graph.addNode(id(n.get("id")), kind(n.get("kind")), tag(n.get("tag")),
                        V15.i(n.get("mag")), V15.i(n.get("dir"))));
            }
            for (Object o : V15.l(g.get("deps"))) {
                Map<String, Object> d = V15.m(o);
                V15.expectRun(d.get("status"), () -> graph.addDependency(id(d.get("id")), id(d.get("dep"))));
            }
            for (Object o : V15.l(g.get("passes"))) {
                Map<String, Object> p = V15.m(o);
                if ("ZXV_E_PARADOX_STEP".equals(p.get("status"))) {
                    StepLimitException e = assertThrows(StepLimitException.class, () -> b.pass(graph));
                    assertEquals(ZxvStatus.E_PARADOX_STEP, e.status());
                } else {
                    Outcome out = V15.expect(p.get("status"), () -> b.pass(graph));
                    assertEquals(Outcome.fromCode(V15.i(p.get("outcome"))), out);
                }
                ParadoxGraph.Info info = graph.info();
                assertEquals(V15.i(p.get("pass")), info.passes());
                assertEquals(V15.i(p.get("total_mag")), info.totalMag());
                assertEquals(V15.i(p.get("total_dir")), info.totalDir());
                assertEquals(p.get("escalated"), info.escalated());
                assertEquals(p.get("chain"), V15.hex(info.chain()));
                for (Map.Entry<String, Object> e : V15.m(p.get("states")).entrySet()) {
                    assertEquals(NodeState.fromCode(V15.i(e.getValue())), graph.state(id(e.getKey())), e.getKey());
                }
            }
            ParadoxGraph.Node first = graph.node(0);
            assertTrue(first.state() != NodeState.OPEN);
        }
    }
}
