// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireU32

/** Account kind (`ZXV_ACCT_*`). */
enum class AccountKind(val code: Int) {
    /** May never go below zero. */
    HOLDER(0),

    /** May carry a credit balance (issuance, nostro). */
    ISSUER(1),
    ;

    companion object {
        @JvmStatic
        fun fromCode(code: Int): AccountKind = entries.firstOrNull { it.code == code } ?: HOLDER
    }
}

/** One ledger account (uint32 id). */
data class Account(val id: Long, val name: String, val currency: String, val kind: AccountKind)

/** Rail balances in minor units: debit (received), credit (sent), equity = debit - credit. */
data class Balance(val debitMinor: Long, val creditMinor: Long, val equityMinor: Long)

/** One journal line: entry id, signed amount (+ received, - sent), counterparty, reference. */
data class JournalEntry(val entryId: Long, val signedMinor: Long, val counterparty: Long, val reference: String)

/**
 * The context's triple ledger, reached through a capability: every call
 * passes [capability] and needs `LEDGER_POST` (open, post) or `LEDGER_READ`
 * (balances, accounts, journals). Money is exact `Long` minor units, never a
 * float.
 */
class Ledger(
    /** The capability every call passes. */
    val capability: Capability,
) {
    companion object {
        /** Largest amount in one posting and largest |balance| (2^53 - 1). */
        const val AMOUNT_MAX: Long = 9007199254740991L
    }

    private val st get() = capability.context.state

    /** Opens an account in an enabled currency. Needs `LEDGER_POST`. */
    fun openAccount(name: String, currency: String, kind: AccountKind = AccountKind.HOLDER): Account =
        capability.res.use { h ->
            val out = LongArray(1)
            Status.check(Jni.ledgerOpenAccountCap(h, cstr(name, "name"), cstr(currency, "currency"), kind.code, out), st)
            Account(out[0], name, currency, kind)
        }

    /** The account with this id. Needs `LEDGER_READ`. */
    fun account(id: Long): Account {
        requireU32(id, "account")
        return capability.res.use { h ->
            val name = Status.checkedText(st) { b, l -> Jni.ledgerAccountNameCap(h, id, b, l) }
            val ccy = Status.checkedText(st) { b, l -> Jni.ledgerAccountCcyCap(h, id, b, l) }
            val k = IntArray(1)
            Status.check(Jni.ledgerAccountKindCap(h, id, k), st)
            Account(id, name, ccy, AccountKind.fromCode(k[0]))
        }
    }

    /** Number of accounts. Needs `LEDGER_READ`. */
    fun accountCount(): Long = capability.res.use { h ->
        val out = LongArray(1)
        Status.check(Jni.ledgerAccountCountCap(h, out), st)
        out[0]
    }

    /**
     * Posts a balanced transfer, atomically. Needs `LEDGER_POST`.
     *
     * @param amountMinor > 0 and at most [AMOUNT_MAX]
     * @param reference unique in the context, at most 35 bytes
     * @return the entry id
     * @throws InsufficientFundsException if a holder would go below zero
     */
    fun post(from: Account, to: Account, amountMinor: Long, currency: String, reference: String): Long =
        post(from.id, to.id, amountMinor, currency, reference)

    /** [post] by account id. */
    fun post(fromId: Long, toId: Long, amountMinor: Long, currency: String, reference: String): Long {
        requireU32(fromId, "from")
        requireU32(toId, "to")
        return capability.res.use { h ->
            val out = LongArray(1)
            Status.check(
                Jni.ledgerPostCap(h, fromId, toId, amountMinor, cstr(currency, "currency"), cstr(reference, "reference"), out),
                st,
            )
            out[0]
        }
    }

    /** Balances of an account. Needs `LEDGER_READ`. */
    fun balance(account: Account): Balance = balance(account.id)

    /** [balance] by id. */
    fun balance(accountId: Long): Balance {
        requireU32(accountId, "account")
        return capability.res.use { h ->
            val o = LongArray(3)
            Status.check(Jni.ledgerBalanceCap(h, accountId, o), st)
            Balance(o[0], o[1], o[2])
        }
    }

    /** The journal of an account, oldest first. Needs `LEDGER_READ`. */
    fun journal(account: Account): List<JournalEntry> {
        val id = account.id
        return capability.res.use { h ->
            val n = LongArray(1)
            Status.check(Jni.ledgerEntryCountCap(h, id, n), st)
            (0 until n[0]).map { i ->
                val o = LongArray(3)
                val ref = Status.checkedText(st) { b, l -> Jni.ledgerEntryAtCap(h, id, i, o, b, l) }
                JournalEntry(o[0], o[1], o[2], ref)
            }
        }
    }

    /**
     * Verifies the ledger's invariants. A broken invariant poisons the
     * context and raises [ZxvFatalError]. Needs `LEDGER_READ`.
     */
    fun check() {
        capability.res.use { h -> Status.check(Jni.ledgerCheckCap(h), st) }
    }

}
