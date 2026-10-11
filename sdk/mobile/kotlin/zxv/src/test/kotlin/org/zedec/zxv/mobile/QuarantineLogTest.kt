// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class QuarantineLogTest {
    private val qlog = Vectors["hex"].obj("qlog")

    @Test
    fun theVectorChainIsReproducedAndRecomputable() {
        ZxvContext.create().use { ctx ->
            val cap = ctx.rootCapability(Permission.ATTEST)
            QuarantineLog.create(cap, 16).use { log ->
                assertContentEquals(ByteArray(32), log.head())
                for (r in qlog.obj("records").arr()) {
                    val seq = log.append(
                        Hexagram.fromTag(r.obj("tag").i()), r.obj("reason").i(), r.obj("submitter").l(),
                        hex(r.obj("subject").s()),
                    )
                    assertEquals(r.obj("seq").l(), seq)
                }
                assertEquals(3L, log.count())
                assertEquals(qlog.obj("head").s(), log.head().toHex())
                log.verify()
                val domain = qlog.obj("domain").s().toByteArray()
                var prev = ByteArray(32)
                for ((i, r) in qlog.obj("records").arr().withIndex()) {
                    val rec = log.record(i.toLong())
                    assertEquals(r.obj("hash").s(), rec.hash.toHex())
                    assertEquals(r.obj("prev").s(), rec.prev.toHex())
                    assertEquals(r.obj("submitter").l(), rec.submitter)
                    // Recompute outside the library: SHA-256(domain || seq u64 LE || tag || reason || submitter u32 LE || subject || prev).
                    val md = MessageDigest.getInstance("SHA-256")
                    md.update(domain)
                    md.update(ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putLong(rec.seq).array())
                    md.update(rec.tag.tag.toByte())
                    md.update(rec.reason.toByte())
                    md.update(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(rec.submitter.toInt()).array())
                    md.update(rec.subject)
                    md.update(prev)
                    val h = md.digest()
                    assertContentEquals(rec.hash, h)
                    prev = h
                }
            }
        }
    }

    @Test
    fun refusalsAndCapability() {
        ZxvContext.create().use { ctx ->
            val all = ctx.rootCapability(Permission.all())
            val noAttest = all.attenuate(Permission.LEDGER_READ)
            assertFailsWith<CapabilityDeniedException> { QuarantineLog.create(noAttest, 4) }
            val log = QuarantineLog.create(all, 1)
            val forbidden = Hexagram.of(HexBase.TRUE, HexPolarity.FORBIDDEN, HexPhase.MANIFEST)
            val notForbidden = Hexagram.of(HexBase.TRUE, HexPolarity.NEUTRAL, HexPhase.MANIFEST)
            assertEquals(-1, assertFailsWith<ZxvUsageException> { log.append(notForbidden, 0, 0, ByteArray(32)) }.status)
            assertFailsWith<ZxvUsageException> { log.append(forbidden, 256, 0, ByteArray(32)) }
            assertFailsWith<ZxvUsageException> { log.append(forbidden, 0, -1, ByteArray(32)) }
            assertFailsWith<ZxvUsageException> { log.append(forbidden, 0, 0, ByteArray(31)) }
            log.append(forbidden, 1, 7, ByteArray(32))
            // Full: REFUSED (ZXV_E_CAPACITY); the caller must halt.
            assertEquals(-5, assertFailsWith<ZxvRefusedException> { log.append(forbidden, 1, 7, ByteArray(32)) }.status)
            ctx.halt("quarantine log full")
            assertFailsWith<ZxvFatalError> { log.count() }
        }
    }
}
