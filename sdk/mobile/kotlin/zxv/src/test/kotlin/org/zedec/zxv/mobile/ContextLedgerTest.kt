// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ContextLedgerTest {
    @Test
    fun aFirstLedger() {
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.all()).use { root ->
                ctx.enableCurrency(root, "NGN")
                val ledger = Ledger(root)
                val bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER)
                val alice = ledger.openAccount("alice", "NGN")
                val bob = ledger.openAccount("bob", "NGN")
                val e1 = ledger.post(bank, alice, 10_000, "NGN", "fund-1")
                val e2 = ledger.post(alice, bob, 2_550, "NGN", "pay-1")
                assertTrue(e2 > e1)
                assertEquals(Balance(10_000, 2_550, 7_450), ledger.balance(alice))
                assertEquals(7_450, ledger.balance(alice).equityMinor)
                assertEquals(3L, ledger.accountCount())
                assertEquals(alice, ledger.account(alice.id))
                val j = ledger.journal(alice)
                assertEquals(listOf("fund-1", "pay-1"), j.map { it.reference })
                assertEquals(listOf(10_000L, -2_550L), j.map { it.signedMinor })
                assertEquals(-6, assertFailsWith<InsufficientFundsException> {
                    ledger.post(bob, alice, 1_000_000, "NGN", "too-much")
                }.status)
                assertFailsWith<ZxvUsageException> { ledger.post(alice, bob, 1, "NGN", "pay-1") } // duplicate ref
                ledger.check()
            }
        }
    }

    @Test
    fun capabilitiesAreCheckedOnEveryCall() {
        ZxvContext.create().use { ctx ->
            val root = ctx.rootCapability(Permission.all())
            ctx.enableCurrency(root, "USD")
            val a = Ledger(root).openAccount("a", "USD", AccountKind.ISSUER)
            val b = Ledger(root).openAccount("b", "USD")
            val reader = root.attenuate(Permission.LEDGER_READ)
            assertEquals(setOf(Permission.LEDGER_READ), reader.info().permissions)
            assertTrue(reader.allows(Permission.LEDGER_READ))
            assertFalse(reader.allows(Permission.LEDGER_POST))
            assertEquals(-17, assertFailsWith<CapabilityDeniedException> {
                Ledger(reader).post(a, b, 1, "USD", "r1")
            }.status)
            assertEquals(0, Ledger(reader).balance(b).equityMinor)
            // Attestation: anything but TRUE blocks.
            reader.setAttestation(Truth.BOTH)
            assertFailsWith<CapabilityDeniedException> { Ledger(reader).balance(b) }
            assertEquals(Truth.BOTH, reader.info().attestation)
            reader.setAttestation(Truth.TRUE)
            Ledger(reader).balance(b)
            // Revocation propagates to children.
            root.revoke()
            assertTrue(reader.info().revoked)
            assertFailsWith<CapabilityDeniedException> { Ledger(reader).balance(b) }
            assertFailsWith<CapabilityDeniedException> { reader.require(Permission.LEDGER_READ) }
        }
    }

    @Test
    fun aCapabilityFromAnotherContextIsRefused() {
        ZxvContext.create().use { c1 ->
            ZxvContext.create().use { c2 ->
                val foreign = c2.rootCapability(Permission.all())
                assertFailsWith<CapabilityDeniedException> { c1.enableCurrency(foreign, "EUR") }
            }
        }
    }

    @Test
    fun haltIsFatalAndCannotBeSwallowed() {
        val ctx = ZxvContext.create()
        val root = ctx.rootCapability(Permission.all())
        ctx.enableCurrency(root, "NGN")
        val ledger = Ledger(root)
        val a = ledger.openAccount("a", "NGN")
        assertFalse(ctx.isPoisoned)
        ctx.halt("test kill switch")
        assertTrue(ctx.isPoisoned)
        // ZxvFatalError is an Error: catch (e: Exception) does not swallow it.
        val thrown = assertFailsWith<ZxvFatalError> {
            try {
                ledger.balance(a)
            } catch (e: Exception) {
                error("swallowed: $e")
            }
        }
        assertEquals(-18, thrown.status)
        assertFalse(Exception::class.java.isInstance(thrown))
        // Every later call fails the same way, before reaching C.
        assertFailsWith<ZxvFatalError> { ledger.openAccount("b", "NGN") }
        assertFailsWith<ZxvFatalError> { root.attenuate(Permission.LEDGER_READ) }
        // Revoking still works on a halted context (it only takes authority away).
        root.revoke()
        ctx.close()
    }

    @Test
    fun closingTheContextClosesItsChildrenFirst() {
        val ctx = ZxvContext.create()
        val root = ctx.rootCapability(Permission.all())
        val child = root.attenuate(Permission.LEDGER_READ)
        val grandchild = child.attenuate(Permission.LEDGER_READ)
        val log = QuarantineLog.create(root, 4)
        val budgets = Budgets.create(root)
        ctx.close()
        for (op in listOf<() -> Unit>({ root.info() }, { child.info() }, { grandchild.info() }, { log.count() },
            { budgets.budget("x", Resource.TOKENS, Rational.ONE) })) {
            assertFailsWith<IllegalStateException> { op() }
        }
        ctx.close() // idempotent
        grandchild.close()
    }

    @Test
    fun manyUnclosedContextsAreReclaimedByTheBackstop() {
        repeat(2_000) {
            val ctx = ZxvContext.create()
            ctx.rootCapability(Permission.LEDGER_READ).attenuate(Permission.LEDGER_READ)
        }
        System.gc()
        ZxvContext.create().use { it.rootCapability(Permission.all()).info() }
    }

    @Test
    fun badArgumentsAreUsageErrors() {
        ZxvContext.create().use { ctx ->
            assertFailsWith<ZxvUsageException> { ctx.rootCapability(Permission.all(), processId = -1) }
            assertFailsWith<ZxvUsageException> { ctx.rootCapability(Permission.all(), identity = ByteArray(20)) }
            val root = ctx.rootCapability(Permission.all(), processId = 0xFFFF_FFFFL, identity = ByteArray(21))
            assertEquals(0xFFFF_FFFFL, root.info().processId)
            assertFailsWith<ZxvUsageException> { ctx.enableCurrency(root, "USD", 256) }
            assertFailsWith<ZxvUsageException> { ctx.enableCurrency(root, "NOPE") }
            assertFailsWith<ZxvUsageException> { Ledger(root).balance(-1) }
        }
    }
}
