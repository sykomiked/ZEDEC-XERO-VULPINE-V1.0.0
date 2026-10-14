// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/** ABI 1.5 treaty vectors: Sutra text, copybook records, PIC S9(18), runs. */
class TreatyTest {
    private val v = V15.sec("treaty")

    @Test
    fun sizes() {
        assertEquals(v.obj("rec_len").i(), Treaty.RECORD_BYTES)
        assertEquals(v.obj("result_len").i(), Treaty.RESULT_RECORD_BYTES)
    }

    @Test
    fun sutraAndRecordRoundTrip() {
        for (c in v.obj("sutra").arr()) {
            Treaty.parse(c.obj("sutra").s()).use { t ->
                val rec = t.toRecord()
                assertEquals(c.obj("record").s(), rec.toHex())
                assertEquals(c.obj("reemit").s(), t.toSutra())
                Treaty.parseRecord(rec).use { r -> assertTrue(t.contentEquals(r)) }
                assertEquals(c.obj("name").s(), t.name())
                assertEquals(c.obj("asset").s(), t.asset())
                assertEquals(c.obj("op").i(), t.op().code)
                assertEquals(c.obj("party_count").i(), t.partyCount())
                assertEquals(c.obj("owe_count").i(), t.obligationCount())
                val ps = t.parties()
                c.obj("parties").arr().forEachIndexed { i, p ->
                    val w = p.obj("weight").arr()
                    assertEquals(
                        Treaty.Party(p.obj("id").s(), V15.u64(p.obj("deposit")), V15.u64(p.obj("claim")), w[0].l(), w[1].l()),
                        ps[i],
                    )
                }
                c.obj("owes").arr().forEachIndexed { i, w ->
                    assertEquals(Treaty.Obligation(w.obj("from").i(), w.obj("to").i(), V15.u64(w.obj("amount"))), t.obligation(i))
                }
            }
        }
    }

    @Test
    fun sutraRefusals() {
        for (c in v.obj("reject").arr()) {
            val e = assertFailsWith<TreatyException> { Treaty.parse(c.obj("sutra").s()) }
            assertEquals(ZxvStatus.E_TREATY, e.status)
            assertEquals(c.obj("fault").i(), e.fault.code, c.obj("sutra").s())
            assertEquals(V15.u64(c.obj("offset")), e.offset)
        }
    }

    @Test
    fun recordRefusals() {
        val base = V15.hexOf(v.obj("sutra").arr()[0].obj("record"))
        for (c in v.obj("reject_record").arr()) {
            val r = base.copyOf()
            r[c.obj("patch_offset").i()] = V15.hexOf(c.obj("patch_byte"))[0]
            val e = assertFailsWith<TreatyException> { Treaty.parseRecord(r) }
            assertEquals(c.obj("fault").i(), e.fault.code)
            assertEquals(V15.u64(c.obj("offset")), e.offset)
        }
    }

    @Test
    fun pic18() {
        for (c in v.obj("pic").arr()) {
            val a = V15.i64(c.obj("a"))
            val b = V15.i64(c.obj("b"))
            val st = c.obj("status")
            when (c.obj("op").s()) {
                "add" -> V15.expect(st) { Pic18.add(a, b) }?.let { assertEquals(V15.i64(c.obj("out")), it) }
                "sub" -> V15.expect(st) { Pic18.subtract(a, b) }?.let { assertEquals(V15.i64(c.obj("out")), it) }
                "mul" -> V15.expect(st) { Pic18.multiply(a, b) }?.let { assertEquals(V15.i64(c.obj("out")), it) }
                else -> V15.expect(st) { Pic18.divide(a, b) }?.let {
                    assertEquals(V15.i64(c.obj("out")), it.quotient)
                    assertEquals(V15.i64(c.obj("rem")), it.remainder)
                }
            }
        }
    }

    @Test
    fun run() {
        for (c in v.obj("run").arr()) {
            Treaty.parse(c.obj("sutra").s()).use { t ->
                val r = V15.expect(c.obj("status")) { t.run() } ?: return@use
                assertEquals(c.obj("parties").i(), r.partyCount)
                assertEquals(c.obj("legs").i(), r.legCount)
                assertEquals(V15.u64(c.obj("fees")), r.fees)
                c.obj("party").arr().forEachIndexed { i, p ->
                    assertEquals(
                        TreatyResult.PartyResult(V15.i64(p.obj("net")), V15.u64(p.obj("balance")), V15.u64(p.obj("dividend"))),
                        r.party(i),
                    )
                }
                c.obj("leg").arr().forEachIndexed { i, g ->
                    assertEquals(TreatyResult.Leg(g.obj("from").i(), g.obj("to").i(), V15.u64(g.obj("amount"))), r.leg(i))
                }
                assertContentEquals(V15.hexOf(c.obj("result_record")), r.toRecord())
            }
        }
        Treaty.parse("TREATY POOR USD SETL\nPARTY A 0 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n").use { poor ->
            assertFailsWith<InsufficientFundsException> { poor.run() }
        }
    }
}
