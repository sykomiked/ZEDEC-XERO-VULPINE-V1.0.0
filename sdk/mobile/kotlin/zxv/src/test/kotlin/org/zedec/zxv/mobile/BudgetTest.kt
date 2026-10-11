// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield

class BudgetTest {
    @Test
    fun consumeReleaseAndExhaustion() {
        ZxvContext.create().use { ctx ->
            val cap = ctx.rootCapability(Permission.BUDGET)
            Budgets.create(cap).use { budgets ->
                val tokens = budgets.budget("tokens", Resource.TOKENS, Rational.of(10))
                val denied = AtomicInteger()
                val sub = tokens.onExhausted { _, _ -> denied.incrementAndGet() }
                tokens.consume(Rational.of(7, 2))
                assertEquals(Rational.of(13, 2), tokens.remaining())
                assertEquals(Rational.of(7, 2), tokens.used())
                assertEquals(-19, assertFailsWith<BudgetExhaustedException> { tokens.consume(7) }.status)
                assertEquals(1, denied.get())
                assertFalse(tokens.tryConsume(Rational.of(7)))
                assertEquals(2, denied.get())
                sub.close()
                assertEquals(Rational.of(7, 2), tokens.used()) // nothing consumed on refusal
                // Invalid amount: USAGE, never retried.
                assertEquals(-20, assertFailsWith<ZxvUsageException> { tokens.consume(Rational.of(-1)) }.status)
                val e0 = tokens.status().epoch
                tokens.release(Rational.of(1))
                assertTrue(tokens.status().epoch > e0)
                tokens.consume(Rational.of(7))
            }
        }
    }

    @Test
    fun budgetNeedsTheBudgetPermission() {
        ZxvContext.create().use { ctx ->
            val cap = ctx.rootCapability(Permission.LEDGER_READ)
            assertFailsWith<CapabilityDeniedException> { Budgets.create(cap) }
        }
    }

    @Test
    fun consumeAsyncWaitsForAReleaseThenCompletes() = runBlocking {
        ZxvContext.create().use { ctx ->
            val cap = ctx.rootCapability(Permission.BUDGET)
            Budgets.create(cap).use { budgets ->
                val b = budgets.budget("energy", Resource.ENERGY, Rational.of(1))
                b.consume(1)
                val waiter = async(Dispatchers.Default) { b.consumeAsync(Rational.of(1), 5.seconds) }
                delay(50)
                assertFalse(waiter.isCompleted)
                b.release(Rational.of(1))
                withTimeout(5.seconds) { waiter.await() }
                assertEquals(Rational.ONE, b.used())
                // A larger limit also wakes waiters.
                val w2 = async(Dispatchers.Default) { b.consumeAsync(Rational.of(2)) }
                delay(20)
                b.setLimit(Rational.of(3))
                withTimeout(5.seconds) { w2.await() }
                assertEquals(Rational.of(3), b.used())
            }
        }
    }

    @Test
    fun consumeAsyncTimesOutAndCancels() = runBlocking {
        ZxvContext.create().use { ctx ->
            val cap = ctx.rootCapability(Permission.BUDGET)
            Budgets.create(cap).use { budgets ->
                val b = budgets.budget("cpu", Resource.COMPUTE_CYCLES, Rational.of(1))
                assertFailsWith<TimeoutCancellationException> { b.consumeAsync(Rational.of(2), 50.milliseconds) }
                val job = launch(Dispatchers.Default) { b.consumeAsync(Rational.of(2)) }
                yield()
                job.cancel()
                job.join()
                assertTrue(job.isCancelled)
                assertFailsWith<ZxvUsageException> { b.consumeAsync(Rational.of(-1)) }
                assertEquals(Rational.ZERO, b.used())
            }
        }
    }
}
