// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.ContextState
import org.zedec.zxv.mobile.internal.E_CAP
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32
import org.zedec.zxv.mobile.internal.requireU8

/**
 * One ZXV platform instance: a ledger, its currencies, and the root of every
 * capability, budget registry and quarantine log made for it. Thread-safe.
 *
 * Close it (`use { }`). Closing it first closes everything made from it,
 * children before parents (ABI R6), each after its in-flight calls return. If
 * it becomes unreachable without being closed, a GC backstop closes it the
 * same way; do not rely on that on Android, where an Activity may live long.
 *
 * ```
 * ZxvContext.create().use { ctx ->
 *     ctx.rootCapability(Permission.all()).use { root -> ... }
 * }
 * ```
 */
class ZxvContext private constructor(handle: Long, val fatalPolicy: FatalPolicy) : Closeable {
    internal val state = ContextState(fatalPolicy)
    internal val res = NativeResource("context", handle, Jni::ctxDestroy, state, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    companion object {
        /** A new context: an empty ledger, no currencies enabled. */
        @JvmStatic
        @JvmOverloads
        fun create(policy: FatalPolicy = FatalPolicy.THROW): ZxvContext {
            val out = LongArray(1)
            Status.check(Jni.ctxCreate(out), null)
            return ZxvContext(out[0], policy)
        }
    }

    /**
     * A root capability. Its attestation starts [Truth.TRUE].
     *
     * @param processId the kernel process id it acts for (uint32)
     * @param identity the 21-byte (168-bit) identity, or null for zeros
     */
    @JvmOverloads
    fun rootCapability(permissions: Set<Permission>, processId: Long = 0, identity: ByteArray? = null): Capability {
        requireU32(processId, "processId")
        identity?.let { requireLength(it, 21, "identity") }
        return res.use { h ->
            val out = LongArray(1)
            Status.check(Jni.capCreate(h, processId, identity, Permission.mask(permissions), out), state)
            Capability(this, out[0], res)
        }
    }

    /** [rootCapability] with varargs. */
    fun rootCapability(vararg permissions: Permission): Capability = rootCapability(permissions.toSet())

    /** True if the context halted. A halted context refuses every call; it can only be closed. */
    val isPoisoned: Boolean
        get() {
            if (state.deadReason != null) return true
            check(!res.isClosed) { "this context is closed" }
            val out = IntArray(1)
            res.use { h -> Status.check(Jni.ctxIsPoisoned(h, out), state) }
            return out[0] != 0
        }

    /**
     * Halts the context on purpose: an operator kill switch, and how tests
     * exercise the fatal path. The next call on it, or on anything made from
     * it, raises [ZxvFatalError] (or ends the process under [FatalPolicy.ABORT]).
     *
     * @param reason at most 150 bytes of UTF-8, or null
     */
    fun halt(reason: String?) {
        val r = reason?.let { cstr(it, "reason") }
        res.use { h -> Status.check(Jni.ctxHalt(h, r), state) }
    }

    /** Enables an ISO 4217 currency for ledger accounts. Needs `LEDGER_POST`. */
    fun enableCurrency(cap: Capability, alpha: String) {
        own(cap).res.use { h -> Status.check(Jni.ccyEnableCap(h, cstr(alpha, "alpha")), state) }
    }

    /** Enables a currency asserting its minor units (a mismatch is `ZXV_E_CURRENCY`). Needs `LEDGER_POST`. */
    fun enableCurrency(cap: Capability, alpha: String, minorUnits: Int) {
        requireU8(minorUnits, "minorUnits")
        own(cap).res.use { h -> Status.check(Jni.ccyEnableWithMinorCap(h, cstr(alpha, "alpha"), minorUnits), state) }
    }

    /** Registers a platform-private unit (3 to 8 upper-case letters or digits). Needs `LEDGER_POST`. */
    fun registerPrivateCurrency(cap: Capability, code: String, minorUnits: Int) {
        requireU8(minorUnits, "minorUnits")
        own(cap).res.use { h -> Status.check(Jni.ccyRegisterPrivateCap(h, cstr(code, "code"), minorUnits), state) }
    }

    private fun own(cap: Capability): Capability {
        if (cap.context !== this) {
            throw Status.raise(E_CAP, "the capability belongs to a different context", state)
        }
        return cap
    }

    /** Closes everything made from this context, then the context. Idempotent. */
    override fun close() = res.close()
}

/**
 * A grant of permissions over one [ZxvContext]. Every object that makes
 * gated calls holds one and passes it on every call; libzxv checks it every
 * time (live, permission set, attestation TRUE). Hand narrower ones
 * ([attenuate]) to code that needs less. There is no global "current"
 * capability.
 *
 * Honest limit: in one process this is least privilege, not a sandbox; code
 * in the same app can create its own root capability from a context it can
 * reach.
 *
 * Closing a capability closes the ones attenuated from it.
 */
class Capability internal constructor(
    /** The context this capability belongs to. */
    val context: ZxvContext,
    handle: Long,
    parent: NativeResource,
) : Closeable {
    internal val res = NativeResource("capability", handle, Jni::capDestroy, context.state, parent)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** What [info] reports. */
    data class Info(val permissions: Set<Permission>, val processId: Long, val attestation: Truth, val revoked: Boolean)

    /** A child with only `keep` (and only what this one has). Revoking this one kills it. */
    fun attenuate(keep: Set<Permission>): Capability = res.use { h ->
        val out = LongArray(1)
        Status.check(Jni.capAttenuate(h, Permission.mask(keep), out), context.state)
        Capability(context, out[0], res)
    }

    /** [attenuate] with varargs. */
    fun attenuate(vararg keep: Permission): Capability = attenuate(keep.toSet())

    /** Revokes this capability and every descendant, permanently. Works on a halted context. */
    fun revoke() {
        res.use(allowDead = true) { h -> Status.check(Jni.capRevoke(h), context.state) }
    }

    /** Sets the attestation; anything but TRUE blocks gated calls. Does not propagate to existing children. */
    fun setAttestation(state: Truth) {
        res.use { h -> Status.check(Jni.capSetAttestation(h, state.code), context.state) }
    }

    /** Permissions, process id, attestation and revocation. */
    fun info(): Info = res.use { h ->
        val o = LongArray(4)
        Status.check(Jni.capInfo(h, o), context.state)
        Info(Permission.fromMask(o[0]), o[1], Truth.fromCode(o[2].toInt()), o[3] != 0L)
    }

    /** True if a call needing all of `needed` would pass the capability check now. */
    fun allows(vararg needed: Permission): Boolean = res.use { h ->
        when (val st = Jni.capCheck(h, Permission.mask(needed.toSet()))) {
            0 -> true
            E_CAP -> false
            else -> {
                Status.check(st, context.state)
                false
            }
        }
    }

    /** Throws [CapabilityDeniedException] unless [allows]. */
    fun require(vararg needed: Permission) {
        res.use { h -> Status.check(Jni.capCheck(h, Permission.mask(needed.toSet())), context.state) }
    }

    /** Destroys this capability and its children. Idempotent. Does not revoke anything. */
    override fun close() = res.close()
}
