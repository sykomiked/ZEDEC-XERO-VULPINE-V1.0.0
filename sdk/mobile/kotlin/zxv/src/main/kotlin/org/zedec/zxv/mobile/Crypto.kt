// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.locks.ReentrantReadWriteLock
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.requireLength

/** SHA3-256, Keccak-256 (Ethereum) and SHAKE256. Pure: no capability. */
object Hashes {
    /** FIPS 202 SHA3-256. */
    @JvmStatic
    fun sha3_256(data: ByteArray): ByteArray = ByteArray(32).also { Status.check(Jni.sha3256(data, it), null) }

    /** Keccak-256 as Ethereum uses it (the pre-FIPS padding). */
    @JvmStatic
    fun keccak256(data: ByteArray): ByteArray = ByteArray(32).also { Status.check(Jni.keccak256(data, it), null) }

    /** SHAKE256 with `outLen` bytes of output. */
    @JvmStatic
    fun shake256(data: ByteArray, outLen: Int): ByteArray {
        if (outLen < 0) throw Status.usage(E_ARG, "outLen must be >= 0")
        return ByteArray(outLen).also { Status.check(Jni.shake256(data, it), null) }
    }
}

/**
 * Secret key material in C-heap memory the garbage collector never sees,
 * moves or copies. Wiped with `zxv_secure_zero` and freed by [close], and by
 * a GC backstop if it is dropped unclosed.
 *
 * The only way to get a heap copy is [exportBytesUnsafe], whose name says it
 * defeats the wiping. What cannot be guaranteed: bytes you passed in from
 * the heap ([fromBytes] wipes your array, but the GC may already have copied
 * it), swap, and core dumps or crash reports.
 */
class SecretKey private constructor(
    /** Length in bytes. */
    val size: Int,
) : Closeable {
    private val ptr = AtomicLong(Jni.secretAlloc(size.toLong()))

    init {
        if (ptr.get() == 0L) throw OutOfMemoryError("secret key allocation failed")
        val p = ptr
        val n = size.toLong()
        Backstop.register(this) { p.getAndSet(0L).let { if (it != 0L) Jni.secretFree(it, n) } }
    }

    companion object {
        internal fun allocate(size: Int) = SecretKey(size)

        /**
         * Copies `bytes` into secret memory and then fills `bytes` with zeros
         * (pass a copy if you need to keep it).
         */
        @JvmStatic
        fun fromBytes(bytes: ByteArray): SecretKey {
            val k = SecretKey(bytes.size)
            try {
                k.withPointer { p -> Status.check(Jni.secretCopyIn(p, bytes), null) }
            } finally {
                bytes.fill(0)
            }
            return k
        }
    }

    private val lock = ReentrantReadWriteLock()

    /** Runs `block` with the key's address; [close] waits for it to return. */
    internal fun <T> withPointer(block: (Long) -> T): T {
        lock.readLock().lock()
        try {
            val p = ptr.get()
            check(p != 0L) { "this secret key is closed" }
            return block(p)
        } finally {
            lock.readLock().unlock()
        }
    }

    /** True once wiped. */
    val isClosed: Boolean get() = ptr.get() == 0L

    /**
     * A heap copy of the key. UNSAFE: the copy is outside the SDK's control
     * and is never wiped; wipe it yourself (`fill(0)`) when done.
     */
    fun exportBytesUnsafe(): ByteArray =
        ByteArray(size).also { out -> withPointer { p -> Status.check(Jni.secretCopyOut(p, out), null) } }

    /** Wipes and frees the key, after any call using it returns. Idempotent. */
    override fun close() {
        lock.writeLock().lock()
        try {
            val p = ptr.getAndSet(0L)
            if (p != 0L) Jni.secretFree(p, size.toLong())
        } finally {
            lock.writeLock().unlock()
        }
    }
}

/**
 * RFC 8032 Ed25519 through a capability that needs `CRYPTO_SIGN`. The secret
 * key (the 32-byte seed) stays in [SecretKey] memory.
 */
class Ed25519(
    /** The capability every call passes. */
    val capability: Capability,
) {
    /** A key pair: the 32-byte public key and the secret key. Close the secret key when done. */
    class KeyPair(publicKey: ByteArray, val secretKey: SecretKey) : Closeable {
        private val pk = publicKey.copyOf()

        /** A copy of the 32-byte public key. */
        val publicKey: ByteArray get() = pk.copyOf()

        override fun close() = secretKey.close()
    }

    private val st get() = capability.context.state

    /** A fresh key pair from the OS random source. */
    fun generate(): KeyPair {
        val sk = SecretKey.allocate(32)
        try {
            val pk = ByteArray(32)
            capability.res.use { h -> sk.withPointer { p -> Status.check(Jni.ed25519Keygen(h, pk, p), st) } }
            return KeyPair(pk, sk)
        } catch (t: Throwable) {
            sk.close()
            throw t
        }
    }

    /** The public key of a 32-byte secret key. */
    fun publicKey(secretKey: SecretKey): ByteArray {
        requireSize(secretKey)
        val pk = ByteArray(32)
        capability.res.use { h -> secretKey.withPointer { p -> Status.check(Jni.ed25519Public(h, p, pk), st) } }
        return pk
    }

    /** A 64-byte signature of `message`. */
    fun sign(secretKey: SecretKey, message: ByteArray): ByteArray {
        requireSize(secretKey)
        val sig = ByteArray(64)
        capability.res.use { h -> secretKey.withPointer { p -> Status.check(Jni.ed25519Sign(h, p, message, sig), st) } }
        return sig
    }

    /** Returns normally if `signature` is valid; [VerificationFailedException] if not. */
    fun verify(publicKey: ByteArray, message: ByteArray, signature: ByteArray) {
        requireLength(publicKey, 32, "publicKey")
        requireLength(signature, 64, "signature")
        capability.res.use { h -> Status.check(Jni.ed25519Verify(h, publicKey, message, signature), st) }
    }

    /** [verify] as a boolean: false only for an invalid signature; other errors still throw. */
    fun isValid(publicKey: ByteArray, message: ByteArray, signature: ByteArray): Boolean = try {
        verify(publicKey, message, signature)
        true
    } catch (_: VerificationFailedException) {
        false
    }

    private fun requireSize(k: SecretKey) {
        if (k.size != 32) throw Status.usage(E_ARG, "an Ed25519 secret key is 32 bytes, got ${k.size}")
    }
}
