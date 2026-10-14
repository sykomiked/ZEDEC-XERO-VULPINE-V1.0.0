// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.freight.ErasureException;
import org.zedec.zxv.freight.Freight;
import org.zedec.zxv.freight.FreightHeader;
import org.zedec.zxv.freight.FreightJoin;
import org.zedec.zxv.freight.Generator;
import org.zedec.zxv.freight.OpStream;
import org.zedec.zxv.freight.OpStreamWriter;

/** ABI 1.5 freight vectors: headers, erasure coding, sets, op streams. */
class FreightTest {
    private static final Map<String, Object> V = V15.sec("freight");

    @Test
    void headers() {
        for (Object o : V15.l(V.get("header_serialize"))) {
            Map<String, Object> c = V15.m(o);
            FreightHeader h = V15.expect(c.get("status"), () -> FreightHeader.of(V15.i(c.get("k")), V15.i(c.get("flags")),
                    V15.i(c.get("payload_len")), V15.i(c.get("seq")), V15.i(c.get("seq_count")),
                    V15.u64(c.get("freight_id")), V15.u64(c.get("expand_limit")), V15.hex(c.get("payload_sha256"))));
            if (h == null) continue;
            assertEquals(c.get("header"), V15.hex(h.toBytes()));
            FreightHeader p = FreightHeader.parse(h.toBytes());
            assertEquals(h, p);
            assertEquals(V15.u64(c.get("freight_id")), p.freightId());
            assertEquals(V15.i(c.get("seq_count")), p.seqCount());
            assertTrue(FreightHeader.isValid(h.toBytes()));
        }
        assertFalse(FreightHeader.isValid(new byte[64]));
        for (Object o : V15.l(V.get("header_init_reject"))) {
            Map<String, Object> c = V15.m(o);
            int flags = c.containsKey("flags") ? V15.i(c.get("flags")) : 0;
            long limit = c.containsKey("expand_limit") ? V15.u64(c.get("expand_limit")) : 0;
            V15.expect(c.get("status"), () -> FreightHeader.forPayload(0, V15.i(c.get("k")), flags, limit,
                    new byte[V15.i(c.get("len"))]));
        }
    }

    @Test
    void codec() {
        for (Object o : V15.l(V.get("codec"))) {
            Map<String, Object> c = V15.m(o);
            byte[] payload = V15.hex(c.get("payload"));
            FreightHeader h = FreightHeader.forPayload(V15.u64(c.get("freight_id")), V15.i(c.get("k")), 0, 0, payload);
            assertEquals(c.get("header"), V15.hex(h.toBytes()));
            byte[] packets = Freight.encode(h, payload);
            assertEquals(c.get("packets"), V15.hex(packets));
            for (Object d : V15.l(c.get("decode"))) {
                Map<String, Object> dc = V15.m(d);
                int[] rows = Vectors.ints(dc.get("rows"));
                byte[][] pk = new byte[rows.length][];
                for (int i = 0; i < rows.length; i++) pk[i] = Freight.packet(packets, rows[i]);
                if (dc.containsKey("flip")) {
                    Map<String, Object> f = V15.m(dc.get("flip"));
                    pk[V15.i(f.get("index_in_rows"))][V15.i(f.get("byte"))] ^= V15.hex(f.get("xor"))[0];
                }
                byte[] got = V15.expect(dc.get("status"), () -> Freight.decode(h, Freight.packets(pk)));
                if (got != null) assertArrayEquals(payload, got);
            }
        }
        FreightHeader h = FreightHeader.forPayload(1, 10, 0, 0, V15.ramp(50));
        byte[] few = Freight.packets(Freight.packet(Freight.encode(h, V15.ramp(50)), 3));
        ErasureException e = assertThrows(ErasureException.class, () -> Freight.decode(h, few));
        assertSame(StatusClass.REFUSED, e.statusClass());
    }

    @Test
    void setsAndJoin() {
        for (Object o : V15.l(V.get("set_count"))) {
            Map<String, Object> c = V15.m(o);
            Integer n = V15.expect(c.get("status"), () -> Freight.setCount(V15.u64(c.get("len")), V15.i(c.get("k"))));
            if (n != null) assertEquals(V15.i(c.get("count")), n);
        }
        Map<String, Object> s = V15.m(V.get("split"));
        byte[] data = V15.ramp(V15.i(s.get("len")));
        int k = V15.i(s.get("k"));
        long id = V15.u64(s.get("freight_id"));
        try (FreightJoin join = FreightJoin.create(data.length)) {
            List<Object> members = V15.l(s.get("members"));
            for (int i = members.size() - 1; i >= 0; i--) { // any order
                Map<String, Object> m = V15.m(members.get(i));
                Freight.Member mem = V15.expect(m.get("status"), () -> Freight.split(id, k, 0, 0, data, V15.i(m.get("seq"))));
                if (mem == null) continue;
                assertEquals(m.get("header"), V15.hex(mem.header().toBytes()));
                assertEquals(m.get("packets_sha3_256"), V15.hex(Hashes.sha3_256(mem.packets())));
                join.add(mem);
            }
            assertEquals(new FreightJoin.Status(3, 3, true, data.length), join.status());
            assertArrayEquals(data, join.result());
        }
        // Buffer growth: a joined result larger than the first 4096-byte buffer.
        byte[] big = V15.ramp(10_000);
        int count = Freight.setCount(big.length, 168);
        try (FreightJoin join = FreightJoin.create(big.length)) {
            assertEquals(ZxvStatus.E_STATE, V15.status(assertThrows(ZxvException.class, join::result)));
            for (int q = 0; q < count; q++) join.add(Freight.split(9, 168, 0, 0, big, q));
            assertArrayEquals(big, join.result());
        }
    }

    @Test
    void opStreams() {
        for (Object o : V15.l(V.get("ops_encode"))) {
            Map<String, Object> c = V15.m(o);
            byte[] in = V15.utf8(c.get("utf8"));
            boolean sha = (Boolean) c.get("with_sha");
            byte[] ops = OpStream.encode(in, sha);
            assertEquals(c.get("ops"), V15.hex(ops));
            assertEquals(V15.u64(c.get("bound")), OpStream.bound(in.length, sha));
            assertArrayEquals(in, OpStream.expand(ops, OpStream.OPS_MAX));
            assertEquals(new OpStream.Peek(in.length, sha), OpStream.peek(ops));
        }
        List<Object> ws = V15.l(V.get("ops_writer"));
        Map<String, Object> w0 = V15.m(ws.get(0));
        try (OpStreamWriter w = OpStreamWriter.create(V15.u64(w0.get("declared_total")), null, 4096)) {
            for (Object o : V15.l(w0.get("ops_list"))) {
                Map<String, Object> op = V15.m(o);
                switch ((String) op.get("op")) {
                    case "literal" -> w.literal(V15.utf8(op.get("utf8")));
                    case "copy" -> w.copy(V15.u64(op.get("distance")), V15.u64(op.get("len")));
                    default -> w.seed(Generator.fromCode(V15.i(op.get("gen"))), V15.hex(op.get("params")),
                            V15.u64(op.get("len")));
                }
            }
            byte[] ops = w.finish();
            assertEquals(w0.get("ops"), V15.hex(ops));
            byte[] out = OpStream.expand(ops, 1 << 20);
            assertEquals(w0.get("expanded"), V15.hex(out));
            assertEquals(new OpStream.Peek(V15.u64(w0.get("peek_total")), (Boolean) w0.get("peek_has_sha")),
                    OpStream.peek(ops));
            V15.expect(w0.get("expand_limit_too_small"), () -> OpStream.expand(ops, out.length - 1));
            byte[] cut = java.util.Arrays.copyOf(ops, ops.length - 1);
            V15.expect(w0.get("expand_truncated"), () -> OpStream.expand(cut, 1 << 20));
        }
        Map<String, Object> w1 = V15.m(ws.get(1));
        byte[] ops = V15.hex(w1.get("ops"));
        byte[] cid = null;
        try (OpStreamWriter w = OpStreamWriter.create(V15.u64(w1.get("declared_total")), null, 1024)) {
            for (Object o : V15.l(w1.get("ops_list"))) {
                Map<String, Object> op = V15.m(o);
                if ("literal".equals(op.get("op"))) {
                    w.literal(V15.utf8(op.get("utf8")));
                } else {
                    cid = V15.hex(op.get("cid"));
                    w.cidRef(cid, V15.u64(op.get("len")));
                }
            }
            assertArrayEquals(ops, w.finish());
        }
        V15.expect(w1.get("expand_without_resolver"), () -> OpStream.expand(ops, 100));
        byte[] want = cid;
        byte[] out = OpStream.expand(ops, 100,
                (id, len) -> java.util.Arrays.equals(id, want) ? "abc".getBytes(StandardCharsets.UTF_8) : null);
        assertEquals(w1.get("expanded_with_resolver_utf8"), new String(out, StandardCharsets.UTF_8));
        // A resolver that throws: the exception never crosses C, it is rethrown here.
        IllegalStateException boom = new IllegalStateException("resolver failed");
        assertSame(boom, assertThrows(IllegalStateException.class, () -> OpStream.expand(ops, 100, (id, len) -> {
            throw boom;
        })));
        // A resolver that answers the wrong length fails the expansion.
        assertThrows(ZxvUsageException.class, () -> OpStream.expand(ops, 100, (id, len) -> new byte[1]));
        assertEquals("<abc>", new String(OpStream.expand(ops, 100, (id, len) -> V15.utf8("abc")), StandardCharsets.UTF_8));
    }

    @Test
    void generators() {
        for (Object o : V15.l(V.get("gen_run"))) {
            Map<String, Object> c = V15.m(o);
            byte[] out = V15.expect(c.get("status"), () -> OpStream.generate(Generator.fromCode(V15.i(c.get("gen"))),
                    V15.hex(c.get("params")), V15.i(c.get("len"))));
            if (out != null) assertEquals(c.get("out"), V15.hex(out));
        }
    }
}
