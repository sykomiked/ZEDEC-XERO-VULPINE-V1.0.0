// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.treaty.Pic18;
import org.zedec.zxv.treaty.Treaty;
import org.zedec.zxv.treaty.TreatyException;
import org.zedec.zxv.treaty.TreatyResult;

/** ABI 1.5 treaty vectors. */
class TreatyTest {
    private static final Map<String, Object> V = V15.sec("treaty");

    @Test
    void sizes() {
        assertEquals(V15.i(V.get("rec_len")), Treaty.RECORD_BYTES);
        assertEquals(V15.i(V.get("result_len")), Treaty.RESULT_RECORD_BYTES);
    }

    @Test
    void sutraAndRecordRoundTrip() {
        for (Object o : V15.l(V.get("sutra"))) {
            Map<String, Object> c = V15.m(o);
            try (Treaty t = Treaty.parse((String) c.get("sutra"))) {
                byte[] rec = t.toRecord();
                assertEquals(c.get("record"), V15.hex(rec));
                assertEquals(c.get("reemit"), t.toSutra());
                try (Treaty r = Treaty.parseRecord(rec)) {
                    assertTrue(t.contentEquals(r));
                }
                assertEquals(c.get("name"), t.name());
                assertEquals(c.get("asset"), t.asset());
                assertEquals(V15.i(c.get("op")), t.op().code());
                assertEquals(V15.i(c.get("party_count")), t.partyCount());
                assertEquals(V15.i(c.get("owe_count")), t.obligationCount());
                List<Treaty.Party> ps = t.parties();
                List<Object> want = V15.l(c.get("parties"));
                for (int i = 0; i < want.size(); i++) {
                    Map<String, Object> p = V15.m(want.get(i));
                    List<Object> w = V15.l(p.get("weight"));
                    assertEquals(new Treaty.Party((String) p.get("id"), V15.u64(p.get("deposit")),
                            V15.u64(p.get("claim")), V15.i(w.get(0)), V15.i(w.get(1))), ps.get(i));
                }
                List<Object> owes = V15.l(c.get("owes"));
                for (int i = 0; i < owes.size(); i++) {
                    Map<String, Object> w = V15.m(owes.get(i));
                    assertEquals(new Treaty.Obligation(V15.i(w.get("from")), V15.i(w.get("to")), V15.u64(w.get("amount"))),
                            t.obligation(i));
                }
            }
        }
    }

    @Test
    void sutraRefusals() {
        for (Object o : V15.l(V.get("reject"))) {
            Map<String, Object> c = V15.m(o);
            TreatyException e = assertThrows(TreatyException.class, () -> Treaty.parse((String) c.get("sutra")));
            assertEquals(ZxvStatus.E_TREATY, e.status());
            assertEquals(V15.i(c.get("fault")), e.fault().code(), (String) c.get("sutra"));
            assertEquals(V15.u64(c.get("offset")), e.offset());
        }
    }

    @Test
    void recordRefusals() {
        byte[] base = V15.hex(V15.m(V15.l(V.get("sutra")).get(0)).get("record"));
        for (Object o : V15.l(V.get("reject_record"))) {
            Map<String, Object> c = V15.m(o);
            byte[] r = base.clone();
            r[V15.i(c.get("patch_offset"))] = V15.hex(c.get("patch_byte"))[0];
            TreatyException e = assertThrows(TreatyException.class, () -> Treaty.parseRecord(r));
            assertEquals(V15.i(c.get("fault")), e.fault().code());
            assertEquals(V15.u64(c.get("offset")), e.offset());
        }
    }

    @Test
    void pic18() {
        for (Object o : V15.l(V.get("pic"))) {
            Map<String, Object> c = V15.m(o);
            long a = Long.parseLong((String) c.get("a")), b = Long.parseLong((String) c.get("b"));
            switch ((String) c.get("op")) {
                case "add" -> check(c, V15.expect(c.get("status"), () -> Pic18.add(a, b)));
                case "sub" -> check(c, V15.expect(c.get("status"), () -> Pic18.subtract(a, b)));
                case "mul" -> check(c, V15.expect(c.get("status"), () -> Pic18.multiply(a, b)));
                default -> {
                    Pic18.Division d = V15.expect(c.get("status"), () -> Pic18.divide(a, b));
                    if (d != null) {
                        assertEquals(Long.parseLong((String) c.get("out")), d.quotient());
                        assertEquals(Long.parseLong((String) c.get("rem")), d.remainder());
                    }
                }
            }
        }
    }

    private static void check(Map<String, Object> c, Long got) {
        if (got != null) assertEquals(Long.parseLong((String) c.get("out")), got);
    }

    @Test
    void run() {
        for (Object o : V15.l(V.get("run"))) {
            Map<String, Object> c = V15.m(o);
            try (Treaty t = Treaty.parse((String) c.get("sutra"))) {
                TreatyResult r = V15.expect(c.get("status"), t::run);
                if (r == null) continue;
                assertEquals(V15.i(c.get("parties")), r.partyCount());
                assertEquals(V15.i(c.get("legs")), r.legCount());
                assertEquals(V15.u64(c.get("fees")), r.fees());
                List<Object> ps = V15.l(c.get("party"));
                for (int i = 0; i < ps.size(); i++) {
                    Map<String, Object> p = V15.m(ps.get(i));
                    assertEquals(new TreatyResult.PartyResult(Long.parseLong((String) p.get("net")),
                            V15.u64(p.get("balance")), V15.u64(p.get("dividend"))), r.party(i));
                }
                List<Object> legs = V15.l(c.get("leg"));
                for (int i = 0; i < legs.size(); i++) {
                    Map<String, Object> g = V15.m(legs.get(i));
                    assertEquals(new TreatyResult.Leg(V15.i(g.get("from")), V15.i(g.get("to")), V15.u64(g.get("amount"))),
                            r.leg(i));
                }
                assertArrayEquals(V15.hex(c.get("result_record")), r.toRecord());
            }
        }
        try (Treaty poor = Treaty.parse("TREATY POOR USD SETL\nPARTY A 0 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n")) {
            assertThrows(InsufficientFundsException.class, poor::run);
        }
    }
}
