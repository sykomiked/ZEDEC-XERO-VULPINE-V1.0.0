// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 V-Bill hub vectors: one scenario on one hub, in order. */
class VHubTest {
    private val v = V15.sec("vhub")

    @Test
    fun pegMessages() {
        for (c in v.obj("peg_msg").arr()) {
            val msg = VHub.pegMessage(c.obj("slot").i(), V15.u64(c.obj("num")), V15.u64(c.obj("den")), V15.u64(c.obj("epoch")))
            assertEquals(c.obj("msg").s(), msg.toHex())
        }
    }

    @Test
    fun scenario() {
        val s = v.obj("scenario")
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                VHub.create(cap, V15.hexOf(s.obj("hub_seed"))).use { hub ->
                    val dsa = MlDsa87(cap)
                    val keys = s.obj("keys").arr().map { ks ->
                        val k = SecretKey.fromBytes(V15.hexOf(ks.obj("seed"))).use { seed -> dsa.generate(seed) }
                        assertEquals(ks.obj("pk_sha3_256").s(), Hashes.sha3_256(k.publicKey).toHex())
                        k
                    }
                    try {
                        for (c in s.obj("currencies").arr()) {
                            val slot = c.obj("slot").i()
                            val pk = keys[c.opt("issuer_key")?.i() ?: 0].publicKey
                            val pool = c.opt("pool_funding")?.let { V15.u64(it) } ?: 1
                            val backing = c.opt("vbill_backing")?.let { V15.u64(it) } ?: 1
                            val mu = c.opt("minor_units")?.i() ?: 2
                            val ci = V15.expect(c.obj("status")) { hub.addCurrency(slot, c.obj("code").s(), mu, pk, pool, backing) } ?: continue
                            assertEquals(c.obj("issuer_account").l(), ci.issuerAccount)
                            assertEquals(c.obj("pool_account").l(), ci.poolAccount)
                            assertEquals(c.obj("vfloat_account").l(), ci.vfloatAccount)
                            assertFalse(ci.hasPeg)
                        }
                        for (c in s.obj("pegs").arr()) {
                            val slot = c.obj("slot").i()
                            val num = V15.u64(c.obj("num"))
                            val den = V15.u64(c.obj("den"))
                            val epoch = V15.u64(c.obj("epoch"))
                            val sig = dsa.signDeterministic(keys[c.obj("signer").i()].secretKey, VHub.pegMessage(slot, num, den, epoch), ByteArray(0))
                            assertEquals(c.obj("sig_sha3_256").s(), Hashes.sha3_256(sig).toHex())
                            V15.expect(c.obj("status")) { hub.setPeg(slot, num, den, epoch, sig) }
                        }
                        val zta = hub.currency(0)
                        assertEquals(350, zta.pegNum)
                        assertEquals(1, zta.pegEpoch, "refused pegs changed nothing")
                        for (c in s.obj("accounts").arr()) {
                            val acct = hub.openAccount(c.obj("slot").i(), c.obj("owner").l())
                            assertEquals(c.obj("account").l(), acct)
                            c.opt("issued")?.let { hub.issue(acct, V15.u64(it)) }
                        }
                        for (c in s.obj("quotes").arr()) {
                            V15.expect(c.obj("status")) { hub.quote(c.obj("from").i(), c.obj("to").i(), V15.u64(c.obj("want"))) }?.let {
                                assertEquals(VHub.Quote(V15.u64(c.obj("amount")), V15.u64(c.obj("vbills")), V15.u64(c.obj("received"))), it)
                            }
                        }
                        for (c in s.obj("swaps").arr()) {
                            c.opt("add_limit")?.let { al -> hub.addLimit(al.obj("slot").i(), V15.u64(al.obj("limit"))) }
                            V15.expect(c.obj("status")) {
                                hub.swap(
                                    c.obj("from").i(), c.obj("to").i(), c.obj("sender").l(), c.obj("receiver").l(),
                                    V15.u64(c.obj("amount")), V15.u64(c.obj("tick")),
                                )
                            }?.let { assertEquals(VHub.Swap(V15.u64(c.obj("vbills")), V15.u64(c.obj("received"))), it) }
                        }
                        assertEquals(V15.u64(s.obj("postings")), hub.check())
                        for (c in s.obj("balances").arr()) {
                            assertEquals(
                                VHub.Balance(V15.u64(c.obj("debit")), V15.u64(c.obj("credit")), V15.i64(c.obj("equity"))),
                                hub.balance(c.obj("account").l()),
                            )
                        }
                        // Typed refusals.
                        assertFailsWith<LimitException> { hub.swap(0, 1, 13, 14, 105_000, 20) }
                        assertFailsWith<PegException> { hub.quote(0, 3, 1) }
                        assertEquals(1, hub.nextEpoch())
                        assertContentEquals(VHub.pegMessage(0, 350, 1, 1), VHub.pegMessage(0, 350, 1, 1))
                        assertTrue(hub.check() >= 14)
                    } finally {
                        keys.forEach { it.close() }
                    }
                }
            }
        }
    }
}
