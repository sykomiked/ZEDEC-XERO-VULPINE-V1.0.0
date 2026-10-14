// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertSame
import kotlin.test.assertTrue
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 freight vectors: headers, erasure coding, sets, op streams. */
class FreightTest {
    private val v = V15.sec("freight")

    @Test
    fun headers() {
        for (c in v.obj("header_serialize").arr()) {
            val h = V15.expect(c.obj("status")) {
                FreightHeader.of(
                    c.obj("k").i(), c.obj("flags").i(), c.obj("payload_len").i(), c.obj("seq").i(), c.obj("seq_count").i(),
                    V15.u64(c.obj("freight_id")), V15.u64(c.obj("expand_limit")), V15.hexOf(c.obj("payload_sha256")),
                )
            } ?: continue
            assertEquals(c.obj("header").s(), h.toBytes().toHex())
            val p = FreightHeader.parse(h.toBytes())
            assertEquals(h, p)
            assertEquals(V15.u64(c.obj("freight_id")), p.freightId)
            assertEquals(c.obj("seq_count").i(), p.seqCount)
            assertTrue(FreightHeader.isValid(h.toBytes()))
        }
        assertFalse(FreightHeader.isValid(ByteArray(64)))
        for (c in v.obj("header_init_reject").arr()) {
            val flags = c.opt("flags")?.i() ?: 0
            val limit = c.opt("expand_limit")?.let { V15.u64(it) } ?: 0
            V15.expect(c.obj("status")) { FreightHeader.forPayload(0, c.obj("k").i(), flags, limit, ByteArray(c.obj("len").i())) }
        }
    }

    @Test
    fun codec() {
        for (c in v.obj("codec").arr()) {
            val payload = V15.hexOf(c.obj("payload"))
            val h = FreightHeader.forPayload(V15.u64(c.obj("freight_id")), c.obj("k").i(), 0, 0, payload)
            assertEquals(c.obj("header").s(), h.toBytes().toHex())
            val packets = Freight.encode(h, payload)
            assertEquals(c.obj("packets").s(), packets.toHex())
            for (dc in c.obj("decode").arr()) {
                val rows = dc.obj("rows").arr().map { it.i() }
                val pk = rows.map { Freight.packet(packets, it) }
                dc.opt("flip")?.let { f ->
                    val p = pk[f.obj("index_in_rows").i()]
                    val at = f.obj("byte").i()
                    p[at] = (p[at].toInt() xor V15.hexOf(f.obj("xor"))[0].toInt()).toByte()
                }
                V15.expect(dc.obj("status")) { Freight.decode(h, Freight.packets(*pk.toTypedArray())) }
                    ?.let { assertContentEquals(payload, it) }
            }
        }
        val h = FreightHeader.forPayload(1, 10, 0, 0, V15.ramp(50))
        val few = Freight.packets(Freight.packet(Freight.encode(h, V15.ramp(50)), 3))
        val e = assertFailsWith<ErasureException> { Freight.decode(h, few) }
        assertSame(StatusClass.REFUSED, Zxv.statusClass(e.status))
    }

    @Test
    fun setsAndJoin() {
        for (c in v.obj("set_count").arr()) {
            V15.expect(c.obj("status")) { Freight.setCount(V15.u64(c.obj("len")), c.obj("k").i()) }
                ?.let { assertEquals(c.obj("count").i(), it) }
        }
        val s = v.obj("split")
        val data = V15.ramp(s.obj("len").i())
        val k = s.obj("k").i()
        val id = V15.u64(s.obj("freight_id"))
        FreightJoin.create(data.size.toLong()).use { join ->
            for (m in s.obj("members").arr().reversed()) { // any order
                val mem = V15.expect(m.obj("status")) { Freight.split(id, k, 0, 0, data, m.obj("seq").i()) } ?: continue
                assertEquals(m.obj("header").s(), mem.header.toBytes().toHex())
                assertEquals(m.obj("packets_sha3_256").s(), Hashes.sha3_256(mem.packets).toHex())
                join.add(mem)
            }
            assertEquals(FreightJoin.Status(3, 3, true, data.size.toLong()), join.status())
            assertContentEquals(data, join.result())
        }
        // Buffer growth: a joined result larger than one freight.
        val big = V15.ramp(10_000)
        val count = Freight.setCount(big.size.toLong(), 168)
        FreightJoin.create(big.size.toLong()).use { join ->
            assertEquals(ZxvStatus.E_STATE, assertFailsWith<ZxvException> { join.result() }.status)
            for (q in 0 until count) join.add(Freight.split(9, 168, 0, 0, big, q))
            assertContentEquals(big, join.result())
        }
    }

    @Test
    fun opStreams() {
        for (c in v.obj("ops_encode").arr()) {
            val input = V15.utf8(c.obj("utf8"))
            val sha = V15.bool(c.obj("with_sha"))
            val ops = OpStream.encode(input, sha)
            assertEquals(c.obj("ops").s(), ops.toHex())
            assertEquals(V15.u64(c.obj("bound")), OpStream.bound(input.size.toLong(), sha))
            assertContentEquals(input, OpStream.expand(ops, OpStream.OPS_MAX))
            assertEquals(OpStream.Peek(input.size.toLong(), sha), OpStream.peek(ops))
        }
        val ws = v.obj("ops_writer").arr()
        val w0 = ws[0]
        OpStreamWriter.create(V15.u64(w0.obj("declared_total")), null, 4096).use { w ->
            for (op in w0.obj("ops_list").arr()) {
                when (op.obj("op").s()) {
                    "literal" -> w.literal(V15.utf8(op.obj("utf8")))
                    "copy" -> w.copy(V15.u64(op.obj("distance")), V15.u64(op.obj("len")))
                    else -> w.seed(Generator.fromCode(op.obj("gen").i()), V15.hexOf(op.obj("params")), V15.u64(op.obj("len")))
                }
            }
            val ops = w.finish()
            assertEquals(w0.obj("ops").s(), ops.toHex())
            val out = OpStream.expand(ops, 1L shl 20)
            assertEquals(w0.obj("expanded").s(), out.toHex())
            assertEquals(OpStream.Peek(V15.u64(w0.obj("peek_total")), V15.bool(w0.obj("peek_has_sha"))), OpStream.peek(ops))
            V15.expect(w0.obj("expand_limit_too_small")) { OpStream.expand(ops, out.size - 1L) }
            val cut = ops.copyOf(ops.size - 1)
            V15.expect(w0.obj("expand_truncated")) { OpStream.expand(cut, 1L shl 20) }
        }
        val w1 = ws[1]
        val ops = V15.hexOf(w1.obj("ops"))
        var cid: ByteArray? = null
        OpStreamWriter.create(V15.u64(w1.obj("declared_total")), null, 1024).use { w ->
            for (op in w1.obj("ops_list").arr()) {
                if (op.obj("op").s() == "literal") {
                    w.literal(V15.utf8(op.obj("utf8")))
                } else {
                    cid = V15.hexOf(op.obj("cid"))
                    w.cidRef(cid!!, V15.u64(op.obj("len")))
                }
            }
            assertContentEquals(ops, w.finish())
        }
        V15.expect(w1.obj("expand_without_resolver")) { OpStream.expand(ops, 100) }
        val want = cid!!
        val out = OpStream.expand(ops, 100) { id, _ -> if (id.contentEquals(want)) V15.utf8("abc") else null }
        assertEquals(w1.obj("expanded_with_resolver_utf8").s(), String(out, Charsets.UTF_8))
        // A resolver that answers null: NOT_FOUND, as with no resolver.
        assertEquals(ZxvStatus.E_NOT_FOUND, assertFailsWith<ZxvException> { OpStream.expand(ops, 100) { _, _ -> null } }.status)
        // A resolver that throws: it never runs inside C, the exception is rethrown here as is.
        val boom = IllegalStateException("resolver failed")
        assertSame(boom, assertFailsWith<IllegalStateException> { OpStream.expand(ops, 100) { _, _ -> throw boom } })
        // A resolver that answers the wrong length fails the expansion.
        assertFailsWith<ZxvUsageException> { OpStream.expand(ops, 100) { _, _ -> ByteArray(1) } }
        assertEquals("<abc>", String(OpStream.expand(ops, 100) { _, _ -> V15.utf8("abc") }, Charsets.UTF_8))
    }

    @Test
    fun generators() {
        for (c in v.obj("gen_run").arr()) {
            V15.expect(c.obj("status")) { OpStream.generate(Generator.fromCode(c.obj("gen").i()), V15.hexOf(c.obj("params")), c.obj("len").i()) }
                ?.let { assertEquals(c.obj("out").s(), it.toHex()) }
        }
    }
}
