// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32
import org.zedec.zxv.mobile.internal.requireU8

/**
 * The V-Bill hub: test currencies pegged to the V-Bill by ML-DSA-87 signed
 * pegs, exact quotes and swaps through the V-Bill, and per-sender limit
 * rules. A demo hub: test currencies only (ISO 4217 codes are refused) and
 * no real money; its ledger lives in process memory.
 *
 * Creating, adding currencies and rules, setting pegs, opening and funding
 * accounts and swapping need `LEDGER_POST`; quotes, balances, currency info
 * and [check] need `LEDGER_READ`. Every call passes [capability]. The hub
 * belongs to the context and is internally serialized. Amounts are uint64
 * values in a `Long`; account ids are uint32 values in a `Long`.
 */
class VHub private constructor(
    /** The capability every call passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    private val res = NativeResource("V-Bill hub", handle, Jni15::vhubDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** One currency slot: its accounts and current peg (`pegDen == 0` until a peg is set). */
    data class CurrencyInfo(
        val slot: Int,
        val issuerAccount: Long,
        val poolAccount: Long,
        val vfloatAccount: Long,
        val pegNum: Long,
        val pegDen: Long,
        val pegEpoch: Long,
    ) {
        /** True once a peg is set. */
        val hasPeg: Boolean get() = pegDen != 0L
    }

    /** An account's DEBIT, CREDIT and EQUITY (= DEBIT - CREDIT). */
    data class Balance(val debit: Long, val credit: Long, val equity: Long)

    /** What a swap for `want` received would cost: the amount to send, the V-Bills in between, and what arrives. */
    data class Quote(val amount: Long, val vbills: Long, val received: Long)

    /** What a swap moved. */
    data class Swap(val vbills: Long, val received: Long)

    companion object {
        const val MAX_CURRENCIES = 8
        const val MAX_RULES = 4
        const val LIMIT_SENDERS = 16
        const val CODE_MAX = 12
        const val AMOUNT_MAX: Long = 1L shl 59

        /** The V-Bill issuer's account. */
        const val VBILL_ISSUER: Long = 0

        /** An issuer's ML-DSA-87 public key size. */
        const val ISSUER_KEY_BYTES = 2592

        /** An ML-DSA-87 peg signature size. */
        const val SIGNATURE_BYTES = 4627

        /** A peg message size. */
        const val PEG_MESSAGE_BYTES = 40

        /** A hub; `seed` (32 bytes, or null for random) derives every posting's keys. Needs `LEDGER_POST`. */
        @JvmStatic
        @JvmOverloads
        fun create(cap: Capability, seed: ByteArray? = null): VHub {
            seed?.let { requireLength(it, 32, "seed") }
            return cap.res.use { h ->
                val o = LongArray(1)
                Status.check(Jni15.vhubCreate(h, seed, o), cap.context.state)
                VHub(cap, o[0])
            }
        }

        /** The 40-byte message an issuer signs (ML-DSA-87, empty context) to set a peg. Pure. */
        @JvmStatic
        fun pegMessage(slot: Int, num: Long, den: Long, epoch: Long): ByteArray {
            requireU8(slot, "slot")
            return ByteArray(PEG_MESSAGE_BYTES).also { Status.check(Jni15.vhubPegMsg(slot, num, den, epoch, it), null) }
        }
    }

    private inline fun <T> gated(f: (Long, Long) -> T): T = capability.res.use { c -> res.use { h -> f(c, h) } }

    /** Adds a currency at `slot` with its issuer's key and initial funding; returns its info. */
    fun addCurrency(slot: Int, code: String, minorUnits: Int, issuerKey: ByteArray, poolFunding: Long, vbillBacking: Long): CurrencyInfo {
        requireU8(slot, "slot")
        requireU8(minorUnits, "minorUnits")
        requireLength(issuerKey, ISSUER_KEY_BYTES, "issuerKey")
        val c = cstr(code, "code")
        gated { cap, h -> Status.check(Jni15.vhubAddCcy(cap, h, slot, c, minorUnits, issuerKey, poolFunding, vbillBacking), st) }
        return currency(slot)
    }

    /** The currency at `slot`. */
    fun currency(slot: Int): CurrencyInfo {
        requireU8(slot, "slot")
        return gated { cap, h ->
            val o = LongArray(6)
            Status.check(Jni15.vhubCcyInfo(cap, h, slot, o), st)
            CurrencyInfo(slot, o[0], o[1], o[2], o[3], o[4], o[5])
        }
    }

    /** Sets a peg with the issuer's signature over [pegMessage]. [PegException] for a bad or stale peg. */
    fun setPeg(slot: Int, num: Long, den: Long, epoch: Long, signature: ByteArray) {
        requireU8(slot, "slot")
        requireLength(signature, SIGNATURE_BYTES, "signature")
        gated { cap, h -> Status.check(Jni15.vhubSetPeg(cap, h, slot, num, den, epoch, signature), st) }
    }

    /** Opens an account in `slot`'s currency for `owner` (uint32); returns its id. */
    fun openAccount(slot: Int, owner: Long): Long {
        requireU8(slot, "slot")
        requireU32(owner, "owner")
        return gated { cap, h -> LongArray(1).also { Status.check(Jni15.vhubOpenAccount(cap, h, slot, owner, it), st) }[0] }
    }

    /** Issues `amount` into `account` from its currency's issuer. */
    fun issue(account: Long, amount: Long) {
        requireU32(account, "account")
        gated { cap, h -> Status.check(Jni15.vhubIssue(cap, h, account, amount), st) }
    }

    /** An account's balance. */
    fun balance(account: Long): Balance {
        requireU32(account, "account")
        return gated { cap, h ->
            val o = LongArray(3)
            Status.check(Jni15.vhubBalance(cap, h, account, o), st)
            Balance(o[0], o[1], o[2])
        }
    }

    /** Adds a per-sender limit rule on swaps out of `slot` (per rule epoch). */
    fun addLimit(slot: Int, limit: Long) {
        requireU8(slot, "slot")
        gated { cap, h -> Status.check(Jni15.vhubAddLimit(cap, h, slot, limit), st) }
    }

    /** Starts a new rule epoch (every sender back at zero); returns it. */
    fun nextEpoch(): Long = gated { cap, h -> LongArray(1).also { Status.check(Jni15.vhubNextEpoch(cap, h, it), st) }[0] }

    /** What receiving `want` in `toSlot` costs in `fromSlot`. */
    fun quote(fromSlot: Int, toSlot: Int, want: Long): Quote {
        requireU8(fromSlot, "fromSlot")
        requireU8(toSlot, "toSlot")
        return gated { cap, h ->
            val o = LongArray(3)
            Status.check(Jni15.vhubQuote(cap, h, fromSlot, toSlot, want, o), st)
            Quote(o[0], o[1], o[2])
        }
    }

    /** Swaps `amount` from `sender` (in `fromSlot`) to `receiver` (in `toSlot`) at `tick`. */
    fun swap(fromSlot: Int, toSlot: Int, sender: Long, receiver: Long, amount: Long, tick: Long): Swap {
        requireU8(fromSlot, "fromSlot")
        requireU8(toSlot, "toSlot")
        requireU32(sender, "sender")
        requireU32(receiver, "receiver")
        return gated { cap, h ->
            val o = LongArray(2)
            Status.check(Jni15.vhubSwap(cap, h, fromSlot, toSlot, sender, receiver, amount, tick, o), st)
            Swap(o[0], o[1])
        }
    }

    /** Checks A = L + E on every account and DEBIT == CREDIT per asset; returns the number of postings checked. */
    fun check(): Long = gated { cap, h -> LongArray(1).also { Status.check(Jni15.vhubCheck(cap, h, it), st) }[0] }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the hub. Idempotent. */
    override fun close() = res.close()
}
