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
import org.zedec.zxv.coin.Coins;
import org.zedec.zxv.coin.FibonacciCode;
import org.zedec.zxv.coin.NotADenominationException;
import org.zedec.zxv.coin.Octane;
import org.zedec.zxv.coin.TokenHolding;
import org.zedec.zxv.coin.TokenWallet;
import org.zedec.zxv.coin.Tokens;
import org.zedec.zxv.fee.AssuranceFee;
import org.zedec.zxv.fee.FeeBucket;
import org.zedec.zxv.fee.FeePolicy;
import org.zedec.zxv.fee.FeeUnit;

/** ABI 1.5 coin, token and fee vectors, plus the token wallet. */
class CoinFeeTest {
    private static final Map<String, Object> C = V15.sec("coin");
    private static final Map<String, Object> F = V15.sec("fee");

    private static TokenHolding holding(Object sparse) {
        long[] c = new long[Tokens.TIERS];
        for (Object o : V15.l(sparse)) {
            List<Object> p = V15.l(o);
            c[V15.i(p.get(0))] = V15.u64(p.get(1));
        }
        return TokenHolding.of(c);
    }

    @Test
    void ladder() {
        assertEquals(V15.i(C.get("rungs")), Coins.RUNGS);
        assertEquals(V15.u64(C.get("top_face")), Coins.TOP_FACE);
        for (Object o : V15.l(C.get("face"))) {
            Map<String, Object> c = V15.m(o);
            Long f = V15.expect(c.get("status"), () -> Coins.face(V15.i(c.get("rung"))));
            if (f != null) assertEquals(V15.u64(c.get("face")), f);
        }
        for (Object o : V15.l(C.get("rung_of"))) {
            Map<String, Object> c = V15.m(o);
            Integer r = V15.expect(c.get("status"), () -> Coins.rung(V15.u64(c.get("face"))));
            if (r != null) assertEquals(V15.i(c.get("rung")), r);
        }
        assertThrows(NotADenominationException.class, () -> Coins.rung(4));
    }

    @Test
    void split() {
        for (Object o : V15.l(C.get("split"))) {
            Map<String, Object> c = V15.m(o);
            long amount = V15.u64(c.get("amount"));
            Long n = V15.expect(c.get("status"), () -> Coins.coinCount(amount));
            if (n == null) continue;
            assertEquals(V15.u64(c.get("count")), n);
            if (c.containsKey("rungs")) assertArrayEquals(Vectors.ints(c.get("rungs")), Coins.split(amount));
            else if (n < 100_000) assertEquals(n, Coins.split(amount).length);
        }
        // 3,811,988,588 coins fit no Java array: counted, and split() refuses with RANGE.
        assertEquals(ZxvStatus.E_RANGE, V15.status(assertThrows(ZxvUsageException.class,
                () -> Coins.split(7_000_000_000_000_000_000L))));
    }

    @Test
    void tokenFacesAndHoldings() {
        for (Object o : V15.l(C.get("token_face"))) {
            Map<String, Object> c = V15.m(o);
            Long f = V15.expect(c.get("status"), () -> Tokens.face(V15.i(c.get("tier"))));
            if (f == null) continue;
            assertEquals(V15.u64(c.get("face")), f);
            assertEquals(c.get("super"), Tokens.isSuper(V15.i(c.get("tier"))));
        }
        for (Object o : V15.l(C.get("token_tier"))) {
            Map<String, Object> c = V15.m(o);
            Integer t = V15.expect(c.get("status"), () -> Tokens.tier(V15.u64(c.get("face"))));
            if (t != null) assertEquals(V15.i(c.get("tier")), t);
        }
        for (Object o : V15.l(C.get("token_value"))) {
            Map<String, Object> c = V15.m(o);
            TokenHolding h = holding(c.get("counts"));
            Long v = V15.expect(c.get("status"), h::value);
            if (v == null) continue;
            assertEquals(V15.u64(c.get("value")), v);
            assertEquals(V15.u64(c.get("pieces")), h.pieces());
        }
        for (Object o : V15.l(C.get("token_canonical"))) {
            Map<String, Object> c = V15.m(o);
            TokenHolding h = V15.expect(c.get("status"),
                    () -> TokenHolding.canonical(V15.u64(c.get("value")), V15.i(c.get("octane"))));
            if (h == null) continue;
            assertEquals(holding(c.get("counts")), h);
            assertEquals(V15.u64(c.get("pieces")), h.pieces());
            assertTrue(h.isCanonical(V15.i(c.get("octane"))));
        }
        for (Object o : V15.l(C.get("token_is_canonical"))) {
            Map<String, Object> c = V15.m(o);
            assertEquals(c.get("canonical"), holding(c.get("counts")).isCanonical(V15.i(c.get("octane"))));
        }
        for (Object o : V15.l(C.get("token_compress"))) {
            Map<String, Object> c = V15.m(o);
            assertEquals(holding(c.get("out")), holding(c.get("counts")).compress(V15.i(c.get("octane"))));
        }
        for (Object o : V15.l(C.get("token_fuse_split"))) {
            Map<String, Object> c = V15.m(o);
            TokenHolding in = holding(c.get("counts"));
            int tier = V15.i(c.get("tier"));
            TokenHolding out = "fuse".equals(c.get("op"))
                    ? V15.expect(c.get("status"), () -> in.fuse(V15.i(c.get("octane")), tier))
                    : V15.expect(c.get("status"), () -> in.split(tier));
            assertEquals(holding(c.get("out")), out == null ? in : out, "a refused change leaves the holding as it was");
        }
    }

    @Test
    void fibonacciCode() {
        for (Object o : V15.l(C.get("token_code"))) {
            Map<String, Object> c = V15.m(o);
            long v = V15.u64(c.get("value"));
            Integer bits = V15.expect(c.get("status"), () -> FibonacciCode.bits(v));
            if (bits == null) {
                V15.expect(c.get("status"), () -> FibonacciCode.encode(v));
                continue;
            }
            assertEquals(V15.i(c.get("bits")), bits);
            FibonacciCode.Encoded e = FibonacciCode.encode(v);
            assertEquals(c.get("bytes"), V15.hex(e.bytes()));
            assertEquals(bits.longValue(), e.bits());
            assertEquals(new FibonacciCode.Decoded(v, bits), FibonacciCode.get(e.bytes(), 0));
        }
        Map<String, Object> s = V15.m(C.get("token_code_stream"));
        List<Object> vs = V15.l(s.get("values"));
        long[] values = new long[vs.size()];
        for (int i = 0; i < values.length; i++) values[i] = V15.u64(vs.get(i));
        FibonacciCode.Encoded e = FibonacciCode.encode(values);
        assertEquals(V15.u64(s.get("bits")), e.bits());
        assertEquals(s.get("bytes"), V15.hex(e.bytes()));
        assertArrayEquals(values, FibonacciCode.decode(e.bytes(), values.length));
        for (Object o : V15.l(C.get("token_code_get_reject"))) {
            Map<String, Object> c = V15.m(o);
            V15.expect(c.get("status"), () -> FibonacciCode.get(V15.hex(c.get("bytes")), V15.i(c.get("bit"))));
        }
        byte[] tiny = new byte[1];
        assertEquals(ZxvStatus.E_BUFFER, V15.status(assertThrows(ZxvException.class, () -> FibonacciCode.put(tiny, 0, 100))));
        assertArrayEquals(new byte[1], tiny, "nothing written on a refusal");
    }

    @Test
    void wallet() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                TokenWallet a = TokenWallet.create(cap, 7, Octane.RACING);
                TokenWallet b = TokenWallet.create(cap, 8, Octane.PLAIN)) {
            a.mint(1_000_000);
            assertEquals(new TokenWallet.Info(7, Octane.RACING, 1_000_000, 17), a.info());
            assertEquals(TokenHolding.canonical(1_000_000, Octane.RACING), a.holding());
            a.transfer(b, 250);
            assertEquals(999_750, a.value());
            assertEquals(new TokenWallet.Info(8, Octane.PLAIN, 250, 250), b.info());
            assertThrows(InsufficientFundsException.class, () -> b.spend(251));
            assertEquals(250, b.value(), "a refused spend changes nothing");
            b.spend(50);
            b.setOctane(Octane.REGULAR);
            assertEquals(TokenHolding.canonical(200, Octane.REGULAR), b.holding());
            assertEquals(ZxvStatus.E_RANGE, V15.status(assertThrows(ZxvException.class, () -> a.mint(-1L))));
        }
    }

    @Test
    void walletNeedsMintCapability() {
        try (ZxvContext ctx = ZxvContext.create(); Capability root = V15.root(ctx);
                Capability noMint = V15.without(root, Permission.MINT)) {
            assertEquals(ZxvStatus.E_CAP, V15.status(assertThrows(CapabilityDeniedException.class,
                    () -> TokenWallet.create(noMint, 1, Octane.PLAIN))));
        }
    }

    @Test
    void assuranceFee() {
        assertEquals(V15.u64(F.get("num")), AssuranceFee.NUM);
        assertEquals(V15.u64(F.get("den")), AssuranceFee.DEN);
        for (Object o : V15.l(F.get("assure"))) {
            Map<String, Object> c = V15.m(o);
            assertEquals(V15.u64(c.get("fee")), AssuranceFee.fee(V15.u64(c.get("gross"))));
        }
        for (Object o : V15.l(F.get("carry"))) {
            Map<String, Object> c = V15.m(o);
            long rem = V15.u64(c.get("rem_in"));
            for (Object so : V15.l(c.get("steps"))) {
                Map<String, Object> s = V15.m(so);
                AssuranceFee.Charge ch = AssuranceFee.charge(rem, V15.u64(s.get("gross")));
                assertEquals(new AssuranceFee.Charge(V15.u64(s.get("fee")), V15.u64(s.get("rem"))), ch);
                rem = ch.remainder();
            }
        }
        for (Object o : V15.l(F.get("carry_reject"))) {
            Map<String, Object> c = V15.m(o);
            V15.expect(c.get("status"), () -> AssuranceFee.charge(V15.u64(c.get("rem_in")), V15.u64(c.get("gross"))));
        }
        List<Object> names = V15.l(F.get("buckets"));
        for (FeeBucket b : FeeBucket.values()) assertEquals(names.get(b.index()), b.name().toLowerCase());
        for (Object o : V15.l(F.get("split"))) {
            Map<String, Object> c = V15.m(o);
            AssuranceFee.FeeSplit s = AssuranceFee.split(V15.u64(c.get("fee")));
            List<Object> p = V15.l(c.get("parts"));
            for (FeeBucket b : FeeBucket.values()) assertEquals(V15.u64(p.get(b.index())), s.part(b));
        }
        Map<String, Object> d = V15.m(F.get("policy_default"));
        FeePolicy p = AssuranceFee.defaultPolicy();
        List<Object> pct = V15.l(d.get("pct"));
        for (int i = 0; i < 4; i++) assertEquals(V15.i(pct.get(i)), p.percent().get(i));
        assertEquals(V15.i(d.get("allow_below")) != 0, p.allowBelow());
        List<Object> rates = V15.l(d.get("rate"));
        for (FeeUnit u : FeeUnit.values()) {
            List<Object> r = V15.l(rates.get(u.index()));
            assertEquals(new FeePolicy.Ratio(V15.u64(r.get(0)), V15.u64(r.get(1))), p.creditRate(u));
        }
        List<Object> mu = V15.l(d.get("multiplier")), cc = V15.l(d.get("commons_cap"));
        assertEquals(new FeePolicy.Ratio(V15.u64(mu.get(0)), V15.u64(mu.get(1))), p.multiplier());
        assertEquals(new FeePolicy.Ratio(V15.u64(cc.get(0)), V15.u64(cc.get(1))), p.commonsCap());
        assertFalse(p.allowBelow());
    }
}
