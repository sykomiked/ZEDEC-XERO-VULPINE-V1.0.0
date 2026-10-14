// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.vhub.LimitException;
import org.zedec.zxv.vhub.PegException;
import org.zedec.zxv.vhub.VHub;

/** ABI 1.5 V-Bill hub vectors: one scenario on one hub, in order. */
class VHubTest {
    private static final Map<String, Object> V = V15.sec("vhub");

    @Test
    void pegMessages() {
        for (Object o : V15.l(V.get("peg_msg"))) {
            Map<String, Object> c = V15.m(o);
            assertEquals(c.get("msg"), V15.hex(VHub.pegMessage(V15.i(c.get("slot")), V15.u64(c.get("num")),
                    V15.u64(c.get("den")), V15.u64(c.get("epoch")))));
        }
    }

    @Test
    void scenario() {
        Map<String, Object> s = V15.m(V.get("scenario"));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                VHub hub = VHub.create(cap, V15.hex(s.get("hub_seed")))) {
            MlDsa87 dsa = MlDsa87.with(cap);
            List<Object> keySpecs = V15.l(s.get("keys"));
            MlDsa87.KeyPair[] keys = new MlDsa87.KeyPair[keySpecs.size()];
            for (int k = 0; k < keys.length; k++) {
                Map<String, Object> ks = V15.m(keySpecs.get(k));
                keys[k] = dsa.generateKeyPair(V15.hex(ks.get("seed")));
                assertEquals(ks.get("pk_sha3_256"), V15.hex(Hashes.sha3_256(keys[k].publicKey())));
            }
            try {
                for (Object o : V15.l(s.get("currencies"))) {
                    Map<String, Object> c = V15.m(o);
                    int slot = V15.i(c.get("slot"));
                    byte[] pk = c.containsKey("issuer_key") ? keys[V15.i(c.get("issuer_key"))].publicKey()
                            : keys[0].publicKey();
                    long pool = c.containsKey("pool_funding") ? V15.u64(c.get("pool_funding")) : 1;
                    long backing = c.containsKey("vbill_backing") ? V15.u64(c.get("vbill_backing")) : 1;
                    int mu = c.containsKey("minor_units") ? V15.i(c.get("minor_units")) : 2;
                    VHub.CurrencyInfo ci = V15.expect(c.get("status"),
                            () -> hub.addCurrency(slot, (String) c.get("code"), mu, pk, pool, backing));
                    if (ci == null) continue;
                    assertEquals(V15.i(c.get("issuer_account")), ci.issuerAccount());
                    assertEquals(V15.i(c.get("pool_account")), ci.poolAccount());
                    assertEquals(V15.i(c.get("vfloat_account")), ci.vfloatAccount());
                    assertFalse(ci.hasPeg());
                }
                for (Object o : V15.l(s.get("pegs"))) {
                    Map<String, Object> c = V15.m(o);
                    int slot = V15.i(c.get("slot"));
                    long num = V15.u64(c.get("num")), den = V15.u64(c.get("den")), epoch = V15.u64(c.get("epoch"));
                    byte[] sig = dsa.signDeterministic(keys[V15.i(c.get("signer"))].secretKey(),
                            VHub.pegMessage(slot, num, den, epoch), new byte[0]);
                    assertEquals(c.get("sig_sha3_256"), V15.hex(Hashes.sha3_256(sig)));
                    V15.expectRun(c.get("status"), () -> hub.setPeg(slot, num, den, epoch, sig));
                }
                VHub.CurrencyInfo zta = hub.currency(0);
                assertEquals(350, zta.pegNum());
                assertEquals(1, zta.pegEpoch(), "refused pegs changed nothing");
                for (Object o : V15.l(s.get("accounts"))) {
                    Map<String, Object> c = V15.m(o);
                    int acct = hub.openAccount(V15.i(c.get("slot")), V15.i(c.get("owner")));
                    assertEquals(V15.i(c.get("account")), acct);
                    if (c.containsKey("issued")) hub.issue(acct, V15.u64(c.get("issued")));
                }
                for (Object o : V15.l(s.get("quotes"))) {
                    Map<String, Object> c = V15.m(o);
                    VHub.Quote q = V15.expect(c.get("status"),
                            () -> hub.quote(V15.i(c.get("from")), V15.i(c.get("to")), V15.u64(c.get("want"))));
                    if (q != null) {
                        assertEquals(new VHub.Quote(V15.u64(c.get("amount")), V15.u64(c.get("vbills")),
                                V15.u64(c.get("received"))), q);
                    }
                }
                for (Object o : V15.l(s.get("swaps"))) {
                    Map<String, Object> c = V15.m(o);
                    if (c.containsKey("add_limit")) {
                        Map<String, Object> al = V15.m(c.get("add_limit"));
                        hub.addLimit(V15.i(al.get("slot")), V15.u64(al.get("limit")));
                    }
                    VHub.Swap w = V15.expect(c.get("status"), () -> hub.swap(V15.i(c.get("from")), V15.i(c.get("to")),
                            V15.i(c.get("sender")), V15.i(c.get("receiver")), V15.u64(c.get("amount")),
                            V15.u64(c.get("tick"))));
                    if (w != null) assertEquals(new VHub.Swap(V15.u64(c.get("vbills")), V15.u64(c.get("received"))), w);
                }
                assertEquals(V15.u64(s.get("postings")), hub.check());
                for (Object o : V15.l(s.get("balances"))) {
                    Map<String, Object> c = V15.m(o);
                    assertEquals(new VHub.Balance(V15.u64(c.get("debit")), V15.u64(c.get("credit")),
                            Long.parseLong((String) c.get("equity"))), hub.balance(V15.i(c.get("account"))));
                }
                // Typed refusals.
                assertThrows(LimitException.class, () -> hub.swap(0, 1, 13, 14, 105_000, 20));
                assertThrows(PegException.class, () -> hub.quote(0, 3, 1));
                long epoch = hub.nextEpoch();
                assertEquals(1, epoch);
                assertArrayEquals(VHub.pegMessage(0, 350, 1, 1), VHub.pegMessage(0, 350, 1, 1));
                assertTrue(hub.check() >= 14);
            } finally {
                for (MlDsa87.KeyPair k : keys) if (k != null) k.close();
            }
        }
    }
}
