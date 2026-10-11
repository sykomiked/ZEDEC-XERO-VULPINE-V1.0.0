// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.time.Duration
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withTimeout
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.use2

/** What a budget measures (`ZXV_RES_*`). */
enum class Resource(val code: Int) {
    COMPUTE_CYCLES(0), MEMORY(1), BANDWIDTH(2), ENERGY(3), STORAGE(4),

    /** One model token = one event. */
    TOKENS(12),
}

/**
 * A registry of exact rational resource budgets (`zxv_rmag`) for one
 * context. Every call passes [capability], which needs `BUDGET`.
 *
 * Closed with its context, or by [close].
 */
class Budgets private constructor(
    /** The capability every call passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    internal val res = NativeResource("budget registry", handle, Jni::rmagDestroy, capability.context.state, capability.context.res)
    internal val st get() = capability.context.state

    /**
     * Bumped after every successful release or limit change through this
     * registry: what [Budget.consumeAsync] waits on. The registry owns its
     * rmag handle, so every replenishment goes through here.
     */
    private val replenished = MutableStateFlow(0L)
    internal val replenishedFlow: StateFlow<Long> get() = replenished.asStateFlow()

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    companion object {
        /** A new registry. Needs `BUDGET`. */
        @JvmStatic
        fun create(cap: Capability): Budgets = cap.res.use { h ->
            val out = LongArray(1)
            Status.check(Jni.rmagCreateCap(h, out), cap.context.state)
            Budgets(cap, out[0])
        }
    }

    /**
     * A budget with an exact limit (>= 0).
     *
     * @param enforce false only counts (never refuses)
     */
    @JvmOverloads
    fun budget(name: String, resource: Resource, limit: Rational, enforce: Boolean = true): Budget =
        use2(capability.res, res) { c, r ->
            val out = LongArray(1)
            Status.check(
                Jni.rmagBudget(c, r, cstr(name, "name"), resource.code, limit.numerator, limit.denominator, if (enforce) 1 else 0, out),
                st,
            )
            Budget(this, out[0], name, resource)
        }

    internal fun signalReplenished() {
        replenished.update { it + 1 }
    }

    /** Destroys the registry. Idempotent. */
    override fun close() = res.close()
}

/**
 * One budget in a [Budgets] registry.
 *
 * [consume] raises [BudgetExhaustedException] (REFUSED; nothing consumed) or
 * a USAGE error for an invalid amount (`ZXV_E_RMAG_INVALID`, never retry).
 * [consumeAsync] is a coroutine that waits for a replenishment instead.
 */
class Budget internal constructor(
    /** The registry. */
    val registry: Budgets,
    /** The index in the registry (uint32). */
    val index: Long,
    /** The name given at creation. */
    val name: String,
    /** What it measures. */
    val resource: Resource,
) {
    /** Remaining (limit - used), used, and the replenishment epoch libzxv reports. */
    data class Status(val remaining: Rational, val used: Rational, val epoch: Long)

    private val listeners = CopyOnWriteArrayList<(Budget, Rational) -> Unit>()

    /** Consumes `amount` (> 0). */
    fun consume(amount: Rational) {
        val rc = use2(registry.capability.res, registry.res) { c, r ->
            Jni.rmagConsume(c, r, index, amount.numerator, amount.denominator)
        }
        if (rc == -19) listeners.forEach { it(this, amount) }
        org.zedec.zxv.mobile.internal.Status.check(rc, registry.st)
    }

    /** [consume] of a whole number of units. */
    fun consume(units: Long) = consume(Rational.of(units))

    /** Consumes if it fits; false (nothing consumed) on exhaustion. Other errors still throw. */
    fun tryConsume(amount: Rational): Boolean = try {
        consume(amount)
        true
    } catch (_: BudgetExhaustedException) {
        false
    }

    /**
     * Suspends until `amount` is consumed: on exhaustion it waits for a
     * release or a larger limit made through this registry, then retries.
     * Fails at once on an invalid amount or any non-budget error. Cancel it
     * by cancelling the coroutine; `timeout` bounds the whole wait
     * (`TimeoutCancellationException`).
     *
     * ```
     * lifecycleScope.launch { tokens.consumeAsync(Rational.of(1)) ; generate() }
     * ```
     */
    suspend fun consumeAsync(amount: Rational, timeout: Duration? = null) {
        if (timeout == null) consumeLoop(amount) else withTimeout(timeout) { consumeLoop(amount) }
    }

    private suspend fun consumeLoop(amount: Rational) {
        val flow = registry.replenishedFlow
        while (true) {
            // Read the signal BEFORE trying, so a release between a refused
            // consume and the wait is never missed.
            val seen = flow.value
            try {
                consume(amount)
                return
            } catch (_: BudgetExhaustedException) {
                flow.first { it != seen }
            }
        }
    }

    /** Gives back `amount` (clamped at zero used) and wakes waiting [consumeAsync] calls. */
    fun release(amount: Rational) {
        use2(registry.capability.res, registry.res) { c, r ->
            org.zedec.zxv.mobile.internal.Status.check(
                Jni.rmagRelease(c, r, index, amount.numerator, amount.denominator), registry.st,
            )
        }
        registry.signalReplenished()
    }

    /** Replaces the limit (>= 0) and wakes waiting [consumeAsync] calls. */
    fun setLimit(limit: Rational) {
        use2(registry.capability.res, registry.res) { c, r ->
            org.zedec.zxv.mobile.internal.Status.check(
                Jni.rmagSetLimit(c, r, index, limit.numerator, limit.denominator), registry.st,
            )
        }
        registry.signalReplenished()
    }

    /** Remaining, used and epoch. */
    fun status(): Status = registry.res.use { r ->
        val o = LongArray(5)
        org.zedec.zxv.mobile.internal.Status.check(Jni.rmagStatus(r, index, o), registry.st)
        Status(Rational.of(o[0], o[1]), Rational.of(o[2], o[3]), o[4])
    }

    /** Remaining (limit - used). */
    fun remaining(): Rational = status().remaining

    /** Used so far. */
    fun used(): Rational = status().used

    /**
     * Calls `handler` (on the consuming thread, before the exception is
     * thrown) for every refused consume. Close the returned handle to stop.
     */
    fun onExhausted(handler: (Budget, Rational) -> Unit): Closeable {
        listeners.add(handler)
        return Closeable { listeners.remove(handler) }
    }

    override fun toString(): String = "Budget($name, $resource)"
}
