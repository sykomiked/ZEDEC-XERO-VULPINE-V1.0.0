// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class CryptoTest {
    private val v = Vectors["ed25519"]

    @Test
    fun ed25519Vector() {
        ZxvContext.create().use { ctx ->
            val ed = Ed25519(ctx.rootCapability(Permission.CRYPTO_SIGN))
            val seed = hex(v.obj("sk").s())
            SecretKey.fromBytes(seed).use { sk ->
                assertContentEquals(ByteArray(32), seed) // the caller's copy was wiped
                assertEquals(v.obj("pk").s(), ed.publicKey(sk).toHex())
                val msg = v.obj("msg_utf8").s().toByteArray()
                val sig = ed.sign(sk, msg)
                assertEquals(v.obj("sig").s(), sig.toHex())
                ed.verify(hex(v.obj("pk").s()), msg, sig)
                sig[0] = (sig[0].toInt() xor 1).toByte()
                assertFalse(ed.isValid(hex(v.obj("pk").s()), msg, sig))
                assertFailsWith<VerificationFailedException> { ed.verify(hex(v.obj("pk").s()), msg, sig) }
                assertEquals(v.obj("sk").s(), sk.exportBytesUnsafe().toHex())
            }
        }
    }

    @Test
    fun generatedKeysSignAndCloseWipes() {
        ZxvContext.create().use { ctx ->
            val ed = Ed25519(ctx.rootCapability(Permission.CRYPTO_SIGN))
            val kp = ed.generate()
            val sig = ed.sign(kp.secretKey, ByteArray(0))
            assertTrue(ed.isValid(kp.publicKey, ByteArray(0), sig))
            kp.close()
            assertTrue(kp.secretKey.isClosed)
            assertFailsWith<IllegalStateException> { ed.sign(kp.secretKey, ByteArray(1)) }
            kp.close()
        }
    }

    @Test
    fun signingNeedsCryptoSign() {
        ZxvContext.create().use { ctx ->
            val ed = Ed25519(ctx.rootCapability(Permission.LEDGER_READ))
            SecretKey.fromBytes(ByteArray(32) { it.toByte() }).use { sk ->
                assertFailsWith<CapabilityDeniedException> { ed.sign(sk, ByteArray(1)) }
            }
            assertFailsWith<CapabilityDeniedException> { ed.generate() }
        }
    }
}
