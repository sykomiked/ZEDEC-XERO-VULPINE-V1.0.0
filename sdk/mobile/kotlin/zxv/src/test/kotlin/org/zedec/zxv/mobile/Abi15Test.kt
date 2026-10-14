// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertSame
import kotlin.test.assertTrue
import kotlin.reflect.KClass
import org.zedec.zxv.mobile.internal.Status

/** ABI 1.5 statuses, features and capability bits; handle lifetimes, the poisoned context and capability refusals. */
class Abi15Test {
    @Test
    fun statusVectors() {
        for (s in Vectors["status_abi15"].arr()) {
            val code = s.obj("code").i()
            assertEquals(s.obj("name").s(), Zxv.statusName(code))
            assertEquals(StatusClass.fromCode(s.obj("class").i()), Zxv.statusClass(code))
        }
    }

    @Test
    fun everyNewStatusMapsToItsTypedException() {
        val want: List<Triple<Int, KClass<*>, KClass<*>>> = listOf(
            Triple(ZxvStatus.E_DENOM, NotADenominationException::class, ZxvUsageException::class),
            Triple(ZxvStatus.E_TREATY, TreatyException::class, ZxvUsageException::class),
            Triple(ZxvStatus.E_CID, CidException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_CONSENT, ConsentException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_ERASURE, ErasureException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_PEG, PegException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_LIMIT, LimitException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_PARADOX_STEP, StepLimitException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_PARADOX_OWNER, OwnerApprovalRequiredException::class, ZxvRefusedException::class),
        )
        for ((code, exact, base) in want) {
            val e = Status.raise(code, "probe", null)
            assertSame(exact.java, e.javaClass, "status $code")
            assertTrue(base.isInstance(e))
            val z = assertIs<ZxvException>(e)
            assertEquals(code, z.status)
            assertEquals(Zxv.statusName(code), z.statusName)
        }
        assertEquals(-32, ZxvStatus.E_DENOM)
        assertEquals(-40, ZxvStatus.E_PARADOX_OWNER)
    }

    @Test
    fun featuresAndCapabilityBits() {
        assertTrue(Zxv.version().minor >= 5)
        for (f in Feature2.entries) assertTrue(Zxv.hasFeature(f), "$f is built in")
        assertEquals(Feature2.entries.toSet(), Zxv.features2())
        assertEquals(1L shl 12, Permission.AIPI.bit)
        assertEquals(1L shl 13, Permission.MINT.bit)
        assertEquals(1L shl 14, Permission.PARADOX.bit)
        assertEquals((1L shl 15) - 1, Permission.mask(Permission.all()))
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap -> assertEquals(Permission.all(), cap.info().permissions, "a 1.5 library keeps the new bits") }
        }
    }

    @Test
    fun useAfterCloseIsRefused() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                val t = Treaty.create()
                val node = AipiNode.create(cap, 1L shl 16, 8)
                val w = TokenWallet.create(cap, 1, Octane.PLAIN)
                val hub = VHub.create(cap)
                val join = FreightJoin.create(100)
                val wr = OpStreamWriter.create(1, null, 64)
                val key = AgentKey.create(cap, null, "a", null, 1)
                val dir = AgentDirectory.create(1)
                val m = ToolManifest.create(1, 0, 10)
                val log = ConsentLog.create(1)
                val book = ParadoxBook.create(cap, 4, 4)
                val pol = ParadoxPolicy.create(cap, longArrayOf(0), longArrayOf(1), longArrayOf(0), 0)
                val hold = ParadoxHold.create(cap, 2, 1)
                val graph = ParadoxGraph.create(cap, 1)
                val all: List<Pair<AutoCloseable, () -> Any?>> = listOf(
                    t to { t.name() }, node to { node.info() }, w to { w.info() }, hub to { hub.currency(0) },
                    join to { join.status() }, wr to { wr.copy(1, 1) }, key to { key.card() }, dir to { dir.count() },
                    m to { m.info() }, log to { log.prune(0) }, book to { book.registryCount() }, pol to { pol.values() },
                    hold to { hold.info() }, graph to { graph.info() },
                )
                for ((h, use) in all) {
                    h.close()
                    h.close() // idempotent
                    val e = assertFailsWith<IllegalStateException>(h.javaClass.name) { use() }
                    assertTrue(e.message!!.contains("closed"), e.message)
                }
                assertTrue(t.isClosed && node.isClosed && graph.isClosed)
            }
        }
    }

    @Test
    fun closingTheContextClosesItsHandles() {
        val ctx = ZxvContext.create()
        val cap = V15.root(ctx)
        val hub = VHub.create(cap)
        val key = AgentKey.create(cap, null, "a", null, 1)
        ctx.close()
        assertTrue(hub.isClosed)
        assertTrue(key.isClosed)
        assertFailsWith<IllegalStateException> { hub.check() }
    }

    @Test
    fun poisonedContextFailsClosed() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                VHub.create(cap).use { hub ->
                    TokenWallet.create(cap, 1, Octane.PLAIN).use { w ->
                        w.mint(10)
                        ctx.halt("test: poisoned context")
                        val e = assertFailsWith<ZxvFatalError> { hub.currency(0) }
                        assertEquals(ZxvStatus.E_POISONED, e.status)
                        assertTrue(ctx.isPoisoned)
                        // Once marked dead, every handle of the context refuses before reaching C.
                        assertFailsWith<ZxvFatalError> { w.info() }
                        assertFailsWith<ZxvFatalError> { w.mint(1) }
                        assertFailsWith<ZxvFatalError> { TokenWallet.create(cap, 2, Octane.PLAIN) }
                        assertFalse(hub.isClosed, "a poisoned handle can still be closed")
                    }
                }
            }
        }
    }

    @Test
    fun capabilityRefusals() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { root ->
                val noAipi = V15.without(root, Permission.AIPI)
                val noMint = V15.without(root, Permission.MINT)
                val noParadox = V15.without(root, Permission.PARADOX)
                assertEquals(ZxvStatus.E_CAP, assertFailsWith<CapabilityDeniedException> { AipiNode.create(noAipi, 1L shl 16, 8) }.status)
                assertFailsWith<CapabilityDeniedException> { TokenWallet.create(noMint, 1, Octane.PLAIN) }
                assertFailsWith<CapabilityDeniedException> { ParadoxBook.create(noParadox, 4, 4) }
                assertFailsWith<CapabilityDeniedException> { ParadoxHold.create(noParadox, 2, 1) }
                assertFailsWith<CapabilityDeniedException> { ParadoxGraph.create(noParadox, 1) }
                assertFailsWith<CapabilityDeniedException> {
                    ParadoxPolicy.create(noParadox, longArrayOf(0), longArrayOf(1), longArrayOf(0), 0)
                }
                TokenWallet.create(noAipi, 1, Octane.PLAIN).use { w ->
                    w.mint(5)
                    assertEquals(5, w.value())
                }
                // A capability of another context is refused.
                ZxvContext.create().use { other ->
                    V15.root(other).use { foreign ->
                        TokenWallet.create(root, 1, Octane.PLAIN).use { mine ->
                            TokenWallet.create(foreign, 1, Octane.PLAIN).use { theirs ->
                                mine.mint(10)
                                assertEquals(ZxvStatus.E_CAP, assertFailsWith<CapabilityDeniedException> { mine.transfer(theirs, 1) }.status)
                            }
                        }
                    }
                }
            }
        }
    }

    @Test
    fun mlDsa87SignsAndVerifies() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                val dsa = MlDsa87(cap)
                dsa.generate().use { kp ->
                    val msg = V15.utf8("peg")
                    val sig = dsa.sign(kp.secretKey, msg)
                    assertTrue(dsa.isValid(kp.publicKey, msg, sig))
                    assertFalse(dsa.isValid(kp.publicKey, V15.utf8("pег"), sig))
                    val det = dsa.signDeterministic(kp.secretKey, msg, V15.utf8("ctx"))
                    assertTrue(dsa.isValid(kp.publicKey, msg, det, V15.utf8("ctx")))
                    assertFalse(dsa.isValid(kp.publicKey, msg, det))
                }
                V15.without(cap, Permission.CRYPTO_SIGN).use { weak ->
                    assertFailsWith<CapabilityDeniedException> { MlDsa87(weak).generate() }
                }
                assertFailsWith<ZxvUsageException> { MlDsa87(cap).sign(SecretKey.fromBytes(ByteArray(32)), ByteArray(0)) }
            }
        }
    }
}
