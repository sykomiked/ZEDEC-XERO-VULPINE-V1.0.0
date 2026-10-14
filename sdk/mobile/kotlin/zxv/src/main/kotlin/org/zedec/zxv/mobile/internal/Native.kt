// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile.internal

import java.lang.ref.PhantomReference
import java.lang.ref.ReferenceQueue
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.locks.ReentrantReadWriteLock
import org.zedec.zxv.mobile.BudgetExhaustedException
import org.zedec.zxv.mobile.CapabilityDeniedException
import org.zedec.zxv.mobile.CidException
import org.zedec.zxv.mobile.ConsentException
import org.zedec.zxv.mobile.ErasureException
import org.zedec.zxv.mobile.FatalPolicy
import org.zedec.zxv.mobile.FloatRejectedException
import org.zedec.zxv.mobile.InsufficientFundsException
import org.zedec.zxv.mobile.LimitException
import org.zedec.zxv.mobile.NotADenominationException
import org.zedec.zxv.mobile.NotImplementedException
import org.zedec.zxv.mobile.OwnerApprovalRequiredException
import org.zedec.zxv.mobile.PegException
import org.zedec.zxv.mobile.StatusClass
import org.zedec.zxv.mobile.StepLimitException
import org.zedec.zxv.mobile.TreatyException
import org.zedec.zxv.mobile.VerificationFailedException
import org.zedec.zxv.mobile.ZxvFatalError
import org.zedec.zxv.mobile.ZxvRefusedException
import org.zedec.zxv.mobile.ZxvSystemException
import org.zedec.zxv.mobile.ZxvUsageException

internal const val OK = 0
internal const val E_ARG = -1
internal const val E_BUFFER = -2
internal const val E_NOT_FOUND = -4
internal const val E_RANGE = -8
internal const val E_CAP = -17
internal const val E_POISONED = -18
internal const val E_TREATY = -33
internal const val E_CID = -34
internal const val CID_MAX = 80
internal const val UINT32_MAX = 0xFFFF_FFFFL

/**
 * The fail-closed state of one context, shared by every handle made from it.
 * Kept apart from the public `ZxvContext` so the GC backstop can close a
 * context's handles without keeping the context object alive.
 */
internal class ContextState(val policy: FatalPolicy) {
    @Volatile var deadReason: String? = null

    fun markDead(why: String) {
        if (deadReason == null) deadReason = why
    }
}

/** Status mapping (SDK_DESIGN section 5). */
internal object Status {
    fun name(status: Int): String = Jni.statusName(status)

    fun cls(status: Int): StatusClass = StatusClass.fromCode(Jni.statusClass(status))

    /** Throws for a negative status; marks `ctx` dead on FATAL. */
    fun check(status: Int, ctx: ContextState?) {
        if (status == OK) return
        throw raise(status, lastError(), ctx)
    }

    /** Builds the exception for `status`; FATAL applies the context's policy first. */
    fun raise(status: Int, detail: String, ctx: ContextState?): Throwable {
        val name = name(status)
        return when (cls(status)) {
            StatusClass.FATAL -> fatal(status, name, detail, ctx)
            StatusClass.USAGE -> when (status) {
                -24 -> FloatRejectedException(detail)
                -32 -> NotADenominationException(status, name, detail)
                -33 -> TreatyException(status, name, detail)
                else -> ZxvUsageException(status, name, detail)
            }
            StatusClass.REFUSED -> when (status) {
                -17 -> CapabilityDeniedException(status, name, detail)
                -19 -> BudgetExhaustedException(status, name, detail)
                -6 -> InsufficientFundsException(status, name, detail)
                -21 -> VerificationFailedException(status, name, detail)
                -3 -> NotImplementedException(status, name, detail)
                // ABI 1.5
                -34 -> CidException(status, name, detail)
                -35 -> ConsentException(status, name, detail)
                -36 -> ErasureException(status, name, detail)
                -37 -> PegException(status, name, detail)
                -38 -> LimitException(status, name, detail)
                -39 -> StepLimitException(status, name, detail)
                -40 -> OwnerApprovalRequiredException(status, name, detail)
                else -> ZxvRefusedException(status, name, detail)
            }
            else -> ZxvSystemException(status, name, detail)
        }
    }

    fun fatal(status: Int, name: String, detail: String, ctx: ContextState?): ZxvFatalError {
        ctx?.markDead("$name: $detail")
        if (ctx?.policy == FatalPolicy.ABORT) {
            System.err.println("ZXV FATAL (FatalPolicy.ABORT): $name: $detail")
            System.err.flush()
            Runtime.getRuntime().halt(70)
        }
        return ZxvFatalError(status, name, detail)
    }

    /** A USAGE error raised by the SDK itself, before any native call. */
    fun usage(status: Int, detail: String): ZxvUsageException = ZxvUsageException(status, name(status), detail)

    /** The calling thread's `zxv_last_error` text (R7). */
    fun lastError(): String = text { buf, len -> Jni.lastError(buf, len) }.getOrDefault("")

    /** The R4 two-call pattern; the failing status is returned as a failure, not thrown. */
    inline fun text(call: (ByteArray?, LongArray) -> Int): Result<String> {
        val len = LongArray(1)
        val st = call(null, len)
        if (st == OK) return Result.success("")
        if (st != E_BUFFER) return Result.failure(StatusFailure(st))
        val buf = ByteArray(Math.toIntExact(len[0] + 1))
        val st2 = call(buf, len)
        if (st2 != OK) return Result.failure(StatusFailure(st2))
        return Result.success(String(buf, 0, len[0].toInt(), Charsets.UTF_8))
    }

    /**
     * The R4 two-call pattern for bytes: the first call (null buffer) asks
     * for the size, the second fills an exact buffer. Throws on failure.
     */
    inline fun bytes(ctx: ContextState?, call: (ByteArray?, LongArray) -> Int): ByteArray {
        val len = LongArray(1)
        val st = call(null, len)
        if (st == OK) return ByteArray(0)
        if (st != E_BUFFER) check(st, ctx)
        val buf = ByteArray(Math.toIntExact(len[0]))
        check(call(buf, len), ctx)
        return if (len[0].toInt() == buf.size) buf else buf.copyOf(len[0].toInt())
    }

    /** A CID out (`ByteArray(ZXV_CID_MAX)` + length), throwing on failure. */
    inline fun cid(ctx: ContextState?, call: (ByteArray, LongArray) -> Int): ByteArray {
        val buf = ByteArray(CID_MAX)
        val len = LongArray(1)
        check(call(buf, len), ctx)
        return buf.copyOf(len[0].toInt())
    }

    /** [text], throwing the mapped exception on failure. */
    inline fun checkedText(ctx: ContextState?, call: (ByteArray?, LongArray) -> Int): String =
        text(call).getOrElse { e ->
            if (e is StatusFailure) check(e.status, ctx)
            throw e
        }
}

internal class StatusFailure(val status: Int) : RuntimeException("status $status")

/** A NUL-terminated UTF-8 copy (R5). Refuses an embedded NUL, which C would silently cut at. */
internal fun cstr(s: String, what: String = "string"): ByteArray {
    val b = s.toByteArray(Charsets.UTF_8)
    if (b.contains(0)) throw Status.usage(E_ARG, "$what contains a NUL character")
    return b.copyOf(b.size + 1)
}

internal fun requireU8(v: Int, what: String): Int {
    if (v !in 0..255) throw Status.usage(E_ARG, "$what $v is not 0..255")
    return v
}

internal fun requireU32(v: Long, what: String): Long {
    if (v !in 0..UINT32_MAX) throw Status.usage(E_RANGE, "$what $v is not a uint32")
    return v
}

internal fun requireIndex(v: Int, what: String): Long {
    if (v < 0) throw Status.usage(E_ARG, "$what $v is negative")
    return v.toLong()
}

internal fun requireLength(b: ByteArray, n: Int, what: String) {
    if (b.size != n) throw Status.usage(E_ARG, "$what must be $n bytes, got ${b.size}")
}

/**
 * One native handle with a safe lifetime (ABI R6): calls hold the read lock,
 * close takes the write lock, so a handle is never destroyed while a call on
 * it is in flight. Children (handles made from this one) are closed first,
 * and the parent keeps a strong reference to each child resource so a child
 * can never be destroyed after its parent.
 */
internal class NativeResource(
    private val kind: String,
    private val handle: Long,
    private val destroy: (Long) -> Unit,
    val owner: ContextState?,
    private val parent: NativeResource?,
) {
    private val lock = ReentrantReadWriteLock()
    private val children: MutableSet<NativeResource> = ConcurrentHashMap.newKeySet()
    private val onClose = mutableListOf<() -> Unit>()

    @Volatile var isClosed = false
        private set

    init {
        if (parent != null) {
            parent.lock.readLock().lock()
            try {
                if (parent.isClosed) {
                    destroy(handle)
                    throw IllegalStateException("${parent.kind} is closed")
                }
                parent.children.add(this)
            } finally {
                parent.lock.readLock().unlock()
            }
        }
    }

    @Synchronized fun onClose(r: () -> Unit) {
        onClose.add(r)
    }

    /** Takes the read lock and returns the handle; pair with [release]. */
    fun acquire(allowDead: Boolean = false): Long {
        lock.readLock().lock()
        if (isClosed) {
            lock.readLock().unlock()
            throw IllegalStateException("this $kind is closed")
        }
        val why = if (allowDead) null else owner?.deadReason
        if (why != null) {
            lock.readLock().unlock()
            throw ZxvFatalError(E_POISONED, "ZXV_E_POISONED", "the context halted earlier: $why")
        }
        return handle
    }

    fun release() = lock.readLock().unlock()

    inline fun <T> use(allowDead: Boolean = false, block: (Long) -> T): T {
        val h = acquire(allowDead)
        try {
            return block(h)
        } finally {
            release()
        }
    }

    /** Destroys children, then this handle. Idempotent. */
    fun close() {
        check(lock.readHoldCount == 0) { "cannot close a $kind from inside a call on it" }
        lock.writeLock().lock()
        try {
            if (isClosed) return
            isClosed = true
            for (c in children.toList()) c.close()
            val hooks = synchronized(this) { onClose.toList() }
            hooks.forEach { it() }
            destroy(handle)
        } finally {
            lock.writeLock().unlock()
        }
        parent?.children?.remove(this)
    }
}

/** Holds two resources' read locks for one call, in argument order. */
internal inline fun <T> use2(a: NativeResource, b: NativeResource, block: (Long, Long) -> T): T =
    a.use { ha -> b.use { hb -> block(ha, hb) } }

/**
 * The GC backstop: when a public wrapper becomes unreachable without being
 * closed, its cleanup runs on a daemon thread. Uses PhantomReference (every
 * Android API level) rather than java.lang.ref.Cleaner (Android 13+).
 * Cleanups must not reference the wrapper itself.
 */
internal object Backstop {
    private val queue = ReferenceQueue<Any>()
    private val pending: MutableMap<Ref, Unit> = ConcurrentHashMap()

    private class Ref(referent: Any, val cleanup: () -> Unit) : PhantomReference<Any>(referent, queue)

    init {
        val t = Thread({
            while (true) {
                val r = try {
                    queue.remove() as Ref
                } catch (_: InterruptedException) {
                    continue
                }
                pending.remove(r)
                try {
                    r.cleanup()
                } catch (_: Throwable) {
                    // A backstop never throws into the GC thread; explicit close() reports errors.
                }
            }
        }, "zxv-backstop")
        t.isDaemon = true
        t.start()
    }

    /** Registers `cleanup` to run once `owner` is unreachable. */
    fun register(owner: Any, cleanup: () -> Unit) {
        pending[Ref(owner, cleanup)] = Unit
    }
}
