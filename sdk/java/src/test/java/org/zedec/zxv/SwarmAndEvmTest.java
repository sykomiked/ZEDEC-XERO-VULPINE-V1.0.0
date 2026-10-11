// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.zedec.zxv.Vectors.hex;
import static org.zedec.zxv.Vectors.list;
import static org.zedec.zxv.Vectors.map;
import static org.zedec.zxv.Vectors.num;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.evm.EvmState;
import org.zedec.zxv.evm.Party;
import org.zedec.zxv.evm.StateChannel;
import org.zedec.zxv.ledger.Account;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Ledger;
import org.zedec.zxv.swarm.Swarm;

/** Swarm allotments and EVM state encoding against vectors.json. */
class SwarmAndEvmTest {
    @Test
    void swarmAllotments() {
        Map<String, Object> v = map(Vectors.root().get("swarm"));
        try (ZxvContext ctx = ZxvContext.create();
             Capability cap = ctx.rootCapability(Permission.SWARM);
             Swarm swarm = Swarm.create(cap, (int) num(v.get("levels")), num(v.get("tokens_per_cycle")))) {
            for (Object m : list(v.get("models"))) {
                int[] p = Vectors.ints(m);
                swarm.register(p[0], p[1]);
            }
            int[] mood = Vectors.ints(v.get("mood"));
            swarm.setMood(mood[0], mood[1]);
            for (Object f : list(v.get("feelings"))) {
                int[] p = Vectors.ints(f);
                swarm.setFeeling(p[0], p[1], p[2]);
            }
            swarm.beginCycle();
            long total = 0;
            for (Object a : list(v.get("allotments"))) {
                long[] p = list(a).stream().mapToLong(Vectors::num).toArray();
                Swarm.Allotment al = swarm.allotment((int) p[0]);
                assertEquals(p[1], al.allotted(), "allotted of " + p[0]);
                assertEquals(p[2], al.imaginary(), "imaginary of " + p[0]);
                total += al.allotted();
            }
            assertEquals(num(v.get("tokens_per_cycle")), total, "every token is allotted");
            Swarm.Grant g = swarm.consume(100, 10);
            assertEquals(10, g.granted());
            Swarm.Grant rest = swarm.consume(100, 1_000_000);
            assertTrue(rest.exhausted());
            assertEquals(38096 - 10, rest.granted(), "an over-ask grants what is left");
            assertTrue(swarm.endCycle() > 0);
            ZxvRefusedException full = assertThrows(ZxvRefusedException.class, () -> swarm.register(999, 0));
            assertEquals("ZXV_E_CAPACITY", full.statusName());
            assertThrows(ZxvUsageException.class, () -> swarm.setMood(0x1F600, 1), "emoji outside the palette");
        }
        int[] caps = Vectors.ints(v.get("level_capacity"));
        for (int d = 0; d < caps.length; d++) assertEquals(caps[d], Swarm.levelCapacity(d));
        Swarm.emotionProfile(0x1F914);
    }

    @Test
    void evmStateEncoding() {
        Map<String, Object> v = map(Vectors.root().get("evm_state"));
        List<Object> addrs = list(v.get("addresses"));
        int[] bal = Vectors.ints(v.get("balances"));
        List<Party> parties = new ArrayList<>();
        for (int i = 0; i < addrs.size(); i++) parties.add(new Party(hex(addrs.get(i)), bal[i]));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.all())) {
            EvmState s = StateChannel.with(cap).encodeState(hex(v.get("channel_id")), num(v.get("nonce")), parties);
            assertArrayEquals(hex(v.get("encoding")), s.encoding());
            assertArrayEquals(hex(v.get("digest")), s.digest());
            assertEquals("0x" + v.get("digest"), s.digestHex());

            // The ledger form equals the explicit form.
            ctx.enableCurrency(cap, "USD", 2);
            Ledger ledger = Ledger.of(cap);
            Account a = ledger.openAccount("a", "USD", AccountKind.ISSUER);
            Account b = ledger.openAccount("b", "USD", AccountKind.HOLDER);
            ledger.post(a, b, 250, "USD", "x");
            byte[] id = new byte[32];
            byte[] x = new byte[20], y = new byte[20];
            for (int i = 0; i < 20; i++) {
                x[i] = (byte) (i + 1);
                y[i] = (byte) (i + 21);
            }
            EvmState fromLedger = StateChannel.with(cap).ledgerState(id, 1, "USD", List.of(a, b), List.of(x, y));
            EvmState explicit = StateChannel.with(cap).encodeState(id, 1, List.of(new Party(x, -250), new Party(y, 250)));
            assertArrayEquals(explicit.digest(), fromLedger.digest());
            assertEquals(10 * 32, fromLedger.encoding().length);
            try (Capability noEvm = cap.attenuate(Permission.LEDGER_READ)) {
                assertThrows(CapabilityDeniedException.class,
                        () -> StateChannel.with(noEvm).encodeState(id, 1, List.of()));
            }
        }
    }
}
