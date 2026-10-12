// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.serialization.json.JsonElement
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 coin, token and assurance-fee vectors, plus the token wallet. */
class CoinFeeTest {
    private val c0 = V15.sec("coin")
    private val f0 = V15.sec("fee")

    private fun holding(sparse: JsonElement): TokenHolding {
        val c = LongArray(Tokens.TIERS)
        for (p in sparse.arr()) c[p.arr()[0].i()] = V15.u64(p.arr()[1])
        return TokenHolding.of(c)
    }

    @Test
    fun ladder() {
        assertEquals(c0.obj("rungs").i(), Coins.RUNGS)
        assertEquals(V15.u64(c0.obj("top_face")), Coins.TOP_FACE)
        for (c in c0.obj("face").arr()) {
            V15.expect(c.obj("status")) { Coins.face(c.obj("rung").i()) }?.let { assertEquals(V15.u64(c.obj("face")), it) }
        }
        for (c in c0.obj("rung_of").arr()) {
            V15.expect(c.obj("status")) { Coins.rung(V15.u64(c.obj("face"))) }?.let { assertEquals(c.obj("rung").i(), it) }
        }
        assertFailsWith<NotADenominationException> { Coins.rung(4) }
    }

    @Test
    fun split() {
        for (c in c0.obj("split").arr()) {
            val amount = V15.u64(c.obj("amount"))
            val n = V15.expect(c.obj("status")) { Coins.coinCount(amount) } ?: continue
            assertEquals(V15.u64(c.obj("count")), n)
            val rungs = c.opt("rungs")
            if (rungs != null) {
                assertContentEquals(rungs.arr().map { it.i() }.toIntArray(), Coins.split(amount))
            } else if (n < 100_000) {
                assertEquals(n, Coins.split(amount).size.toLong())
            }
        }
        // 3,811,988,588 coins fit no array: counted, and split() refuses with RANGE.
        assertEquals(ZxvStatus.E_RANGE, assertFailsWith<ZxvUsageException> { Coins.split(7_000_000_000_000_000_000L) }.status)
    }

    @Test
    fun tokenFacesAndHoldings() {
        for (c in c0.obj("token_face").arr()) {
            val f = V15.expect(c.obj("status")) { Tokens.face(c.obj("tier").i()) } ?: continue
            assertEquals(V15.u64(c.obj("face")), f)
            assertEquals(V15.bool(c.obj("super")), Tokens.isSuper(c.obj("tier").i()))
        }
        for (c in c0.obj("token_tier").arr()) {
            V15.expect(c.obj("status")) { Tokens.tier(V15.u64(c.obj("face"))) }?.let { assertEquals(c.obj("tier").i(), it) }
        }
        for (c in c0.obj("token_value").arr()) {
            val h = holding(c.obj("counts"))
            val v = V15.expect(c.obj("status")) { h.value() } ?: continue
            assertEquals(V15.u64(c.obj("value")), v)
            assertEquals(V15.u64(c.obj("pieces")), h.pieces())
        }
        for (c in c0.obj("token_canonical").arr()) {
            val h = V15.expect(c.obj("status")) { TokenHolding.canonical(V15.u64(c.obj("value")), c.obj("octane").i()) } ?: continue
            assertEquals(holding(c.obj("counts")), h)
            assertEquals(V15.u64(c.obj("pieces")), h.pieces())
            assertTrue(h.isCanonical(c.obj("octane").i()))
        }
        for (c in c0.obj("token_is_canonical").arr()) {
            assertEquals(V15.bool(c.obj("canonical")), holding(c.obj("counts")).isCanonical(c.obj("octane").i()))
        }
        for (c in c0.obj("token_compress").arr()) {
            assertEquals(holding(c.obj("out")), holding(c.obj("counts")).compress(c.obj("octane").i()))
        }
        for (c in c0.obj("token_fuse_split").arr()) {
            val input = holding(c.obj("counts"))
            val tier = c.obj("tier").i()
            val out = if (c.obj("op").s() == "fuse") {
                V15.expect(c.obj("status")) { input.fuse(c.obj("octane").i(), tier) }
            } else {
                V15.expect(c.obj("status")) { input.split(tier) }
            }
            assertEquals(holding(c.obj("out")), out ?: input, "a refused change leaves the holding as it was")
        }
    }

    @Test
    fun fibonacciCode() {
        for (c in c0.obj("token_code").arr()) {
            val v = V15.u64(c.obj("value"))
            val bits = V15.expect(c.obj("status")) { FibonacciCode.bits(v) }
            if (bits == null) {
                V15.expect(c.obj("status")) { FibonacciCode.encode(v) }
                continue
            }
            assertEquals(c.obj("bits").i(), bits)
            val e = FibonacciCode.encode(v)
            assertEquals(c.obj("bytes").s(), e.bytes.toHex())
            assertEquals(bits.toLong(), e.bits)
            assertEquals(FibonacciCode.Decoded(v, bits.toLong()), FibonacciCode.get(e.bytes, 0))
        }
        val s = c0.obj("token_code_stream")
        val values = s.obj("values").arr().map { V15.u64(it) }.toLongArray()
        val e = FibonacciCode.encode(*values)
        assertEquals(V15.u64(s.obj("bits")), e.bits)
        assertEquals(s.obj("bytes").s(), e.bytes.toHex())
        assertContentEquals(values, FibonacciCode.decode(e.bytes, values.size))
        for (c in c0.obj("token_code_get_reject").arr()) {
            V15.expect(c.obj("status")) { FibonacciCode.get(V15.hexOf(c.obj("bytes")), c.obj("bit").l()) }
        }
        val tiny = ByteArray(1)
        assertEquals(ZxvStatus.E_BUFFER, assertFailsWith<ZxvException> { FibonacciCode.put(tiny, 0, 100) }.status)
        assertContentEquals(ByteArray(1), tiny, "nothing written on a refusal")
    }

    @Test
    fun wallet() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                TokenWallet.create(cap, 7, Octane.RACING).use { a ->
                    TokenWallet.create(cap, 8, Octane.PLAIN).use { b ->
                        a.mint(1_000_000)
                        assertEquals(TokenWallet.Info(7, Octane.RACING, 1_000_000, 17), a.info())
                        assertEquals(TokenHolding.canonical(1_000_000, Octane.RACING), a.holding())
                        a.transfer(b, 250)
                        assertEquals(999_750, a.value())
                        assertEquals(TokenWallet.Info(8, Octane.PLAIN, 250, 250), b.info())
                        assertFailsWith<InsufficientFundsException> { b.spend(251) }
                        assertEquals(250, b.value(), "a refused spend changes nothing")
                        b.spend(50)
                        b.setOctane(Octane.REGULAR)
                        assertEquals(TokenHolding.canonical(200, Octane.REGULAR), b.holding())
                        assertEquals(ZxvStatus.E_RANGE, assertFailsWith<ZxvException> { a.mint(-1L) }.status)
                    }
                }
            }
        }
    }

    @Test
    fun walletNeedsMintCapability() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { root ->
                V15.without(root, Permission.MINT).use { noMint ->
                    val e = assertFailsWith<CapabilityDeniedException> { TokenWallet.create(noMint, 1, Octane.PLAIN) }
                    assertEquals(ZxvStatus.E_CAP, e.status)
                }
            }
        }
    }

    @Test
    fun assuranceFee() {
        assertEquals(V15.u64(f0.obj("num")), AssuranceFee.NUM)
        assertEquals(V15.u64(f0.obj("den")), AssuranceFee.DEN)
        for (c in f0.obj("assure").arr()) assertEquals(V15.u64(c.obj("fee")), AssuranceFee.fee(V15.u64(c.obj("gross"))))
        for (c in f0.obj("carry").arr()) {
            var rem = V15.u64(c.obj("rem_in"))
            for (s in c.obj("steps").arr()) {
                val ch = AssuranceFee.charge(rem, V15.u64(s.obj("gross")))
                assertEquals(AssuranceFee.Charge(V15.u64(s.obj("fee")), V15.u64(s.obj("rem"))), ch)
                rem = ch.remainder
            }
        }
        for (c in f0.obj("carry_reject").arr()) {
            V15.expect(c.obj("status")) { AssuranceFee.charge(V15.u64(c.obj("rem_in")), V15.u64(c.obj("gross"))) }
        }
        val names = f0.obj("buckets").arr()
        for (b in FeeBucket.entries) assertEquals(names[b.ordinal].s(), b.name.lowercase())
        for (c in f0.obj("split").arr()) {
            val s = AssuranceFee.split(V15.u64(c.obj("fee")))
            val p = c.obj("parts").arr()
            for (b in FeeBucket.entries) assertEquals(V15.u64(p[b.ordinal]), s.part(b))
        }
        val d = f0.obj("policy_default")
        val p = AssuranceFee.defaultPolicy()
        d.obj("pct").arr().forEachIndexed { i, e -> assertEquals(e.i(), p.percent[i]) }
        assertEquals(d.obj("allow_below").i() != 0, p.allowBelow)
        val rates = d.obj("rate").arr()
        for (u in FeeUnit.entries) {
            val r = rates[u.ordinal].arr()
            assertEquals(FeePolicy.Ratio(V15.u64(r[0]), V15.u64(r[1])), p.creditRate(u))
        }
        val mu = d.obj("multiplier").arr()
        val cc = d.obj("commons_cap").arr()
        assertEquals(FeePolicy.Ratio(V15.u64(mu[0]), V15.u64(mu[1])), p.multiplier)
        assertEquals(FeePolicy.Ratio(V15.u64(cc[0]), V15.u64(cc[1])), p.commonsCap)
        assertFalse(p.allowBelow)
    }
}
