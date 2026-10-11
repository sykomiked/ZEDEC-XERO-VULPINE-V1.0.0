// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import org.zedec.zxv.Capability;
import org.zedec.zxv.InsufficientFundsException;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;

/**
 * The exact triple ledger of a context, seen through one capability.
 * Amounts are {@code long} minor units (cents, kobo, ...), never floats;
 * the largest accepted amount and balance is {@link #AMOUNT_MAX} (2^53 - 1).
 *
 * <p>Posting and opening accounts need {@code LEDGER_POST}; reading
 * balances, accounts and journals and checking the invariants need
 * {@code LEDGER_READ} (ABI 1.2: every call goes through the capability).
 *
 * <pre>{@code
 * Ledger ledger = Ledger.of(cap);
 * Account bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER);
 * Account ada  = ledger.openAccount("ada", "NGN", AccountKind.HOLDER);
 * ledger.post(bank, ada, 150_00, "NGN", "E2E-1");
 * long spendable = ledger.balance(ada).equity();   // 15000
 * }</pre>
 */
public final class Ledger {
    /** Largest amount in one posting and largest |balance|: 2^53 - 1. */
    public static final long AMOUNT_MAX = 9007199254740991L;

    private final Capability cap;
    private final ZxvContext ctx;

    private Ledger(Capability cap) {
        this.cap = cap;
        this.ctx = Caps.ctx(cap);
    }

    /** The ledger of the capability's context, gated by that capability. */
    public static Ledger of(Capability cap) {
        return new Ledger(cap);
    }

    /** The capability this view uses. */
    public Capability capability() {
        return cap;
    }

    /**
     * Opens an account in an enabled currency. Needs {@code LEDGER_POST}.
     *
     * @param name at most 63 bytes of UTF-8
     */
    public Account openAccount(String name, String currency, AccountKind kind) {
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.ledger_open_account_cap, l.h(0), Ffi.cstr(a, name), Ffi.cstr(a, currency),
                    kind.ordinal(), id), ctx);
            return new Account(id.get(JAVA_INT, 0), name, currency, kind);
        }
    }

    /** The account with this id. Needs {@code LEDGER_READ}. */
    public Account account(int id) {
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            String name = Ffi.text(ctx, (b, c, n) -> Ffi.call(Native.ledger_account_name_cap, l.h(0), id, b, c, n));
            String ccy = Ffi.text(ctx, (b, c, n) -> Ffi.call(Native.ledger_account_ccy_cap, l.h(0), id, b, c, n));
            MemorySegment k = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.ledger_account_kind_cap, l.h(0), id, k), ctx);
            return new Account(id, name, ccy, AccountKind.fromCode(k.get(JAVA_INT, 0)));
        }
    }

    /** Every account, in id order. Needs {@code LEDGER_READ}. */
    public List<Account> accounts() {
        int n;
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.ledger_account_count_cap, l.h(0), c), ctx);
            n = c.get(JAVA_INT, 0);
        }
        List<Account> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(account(i));
        return List.copyOf(out);
    }

    /**
     * Posts a balanced transfer, atomically across the financial, provenance
     * and externality axes. Needs {@code LEDGER_POST}.
     *
     * @param amountMinor &gt; 0 and at most {@link #AMOUNT_MAX}
     * @param reference   unique in the context, at most 35 bytes (e.g. the EndToEndId)
     * @return the platform entry id (monotonic from 1)
     * @throws InsufficientFundsException if a holder would go below zero
     */
    public long post(Account from, Account to, long amountMinor, String currency, String reference) {
        return post(from.id(), to.id(), amountMinor, currency, reference);
    }

    /** {@link #post(Account, Account, long, String, String)} by account id. */
    public long post(int fromId, int toId, long amountMinor, String currency, String reference) {
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment e = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.ledger_post_cap, l.h(0), fromId, toId, amountMinor,
                    Ffi.cstr(a, currency), Ffi.cstr(a, reference), e), ctx);
            return e.get(JAVA_LONG, 0);
        }
    }

    /** The rail balances of an account. Needs {@code LEDGER_READ}. */
    public Balance balance(Account account) {
        return balance(account.id());
    }

    /** The rail balances of an account id. Needs {@code LEDGER_READ}. */
    public Balance balance(int accountId) {
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(JAVA_LONG), c = a.allocate(JAVA_LONG), q = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.ledger_balance_cap, l.h(0), accountId, d, c, q), ctx);
            return new Balance(d.get(JAVA_LONG, 0), c.get(JAVA_LONG, 0), q.get(JAVA_LONG, 0));
        }
    }

    /** The journal of an account, oldest first. Needs {@code LEDGER_READ}. */
    public List<JournalEntry> journal(Account account) {
        int id = account.id();
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment cnt = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.ledger_entry_count_cap, l.h(0), id, cnt), ctx);
            List<JournalEntry> out = new ArrayList<>();
            MemorySegment eid = a.allocate(JAVA_LONG), amt = a.allocate(JAVA_LONG), cp = a.allocate(JAVA_INT);
            for (int i = 0; i < cnt.get(JAVA_INT, 0); i++) {
                final int idx = i;
                String ref = Ffi.text(ctx, (b, c, n) -> Ffi.call(Native.ledger_entry_at_cap, l.h(0), id, idx,
                        eid, amt, cp, b, c, n));
                out.add(new JournalEntry(eid.get(JAVA_LONG, 0), amt.get(JAVA_LONG, 0), cp.get(JAVA_INT, 0), ref));
            }
            return List.copyOf(out);
        }
    }

    /**
     * Verifies the ledger: per currency debits equal credits, every holder is
     * non-negative, and the exact shadow equals the triple ledger. Needs
     * {@code LEDGER_READ}.
     *
     * @throws org.zedec.zxv.ZxvFatalError if an invariant is broken: the
     *         context is halted and poisoned
     */
    public void check() {
        try (Lease l = Lease.of(Caps.res(cap))) {
            Ffi.check(Ffi.call(Native.ledger_check_cap, l.h(0)), ctx);
        }
    }
}
