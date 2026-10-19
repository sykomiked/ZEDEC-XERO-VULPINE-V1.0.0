// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vhub;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A demonstration V-Bill hub ({@code zxv_vhub}): test currencies, each with
 * one exact peg against the V-Bill set only by its issuer's ML-DSA-87
 * signature, and swaps from currency A to currency B that go A to V-Bill to
 * B as one balanced, all-or-nothing posting on the hub's own ledger.
 *
 * <p>EXACT. A peg is num/den: num minor units per den V-Bills. A swap whose
 * divisions are not exact is refused (ZXV_E_RANGE); nothing is rounded and
 * there is no float rate. {@link #quote} gives the largest exact amount not
 * above a requested one.
 *
 * <p>RULES. Every currency on both sides of a swap must have at least one
 * limit rule, or the swap is refused with ZXV_E_STATE (fail closed). A limit
 * caps the minor units one sender may move out of a currency per rule epoch
 * ({@link LimitException}). Arbitrary rule callbacks are not exposed: a host
 * callback in the middle of a swap is not clearly safe.
 *
 * <p>CAPABILITIES. The hub keeps the capability it was created with and
 * uses it for every call: changes need {@code LEDGER_POST}, reads need
 * {@code LEDGER_READ}, and the capability must belong to the hub's context.
 *
 * <p>Threading: libzxv serializes the hub, so any thread may call. Amounts
 * are unsigned 64-bit values in a Java {@code long}, at most
 * {@link #AMOUNT_MAX} each.
 *
 * <p>Honest limits: test currencies only (ISO 4217 codes are refused) and no
 * real money. The ledger lives in process memory, apart from the context's
 * ledger, and is lost on close. No network: it is connected to no
 * settlement rail, central bank or issuer. ML-DSA-87 here is not FIPS 140-3
 * validated. V-Bills are likely securities in many jurisdictions; real use
 * needs legal review and a participating issuer. A rule reports what it
 * decided; it is not itself a legal check.
 */
public final class VHub implements AutoCloseable {
    /** Currency slots 0..7 ({@code ZXV_VHUB_MAX_CCY}). */
    public static final int MAX_CURRENCIES = 8;
    /** Limit rules per currency ({@code ZXV_VHUB_MAX_RULES}). */
    public static final int MAX_RULES = 4;
    /** Distinct senders one limit tracks per epoch ({@code ZXV_VHUB_LIMIT_SENDERS}). */
    public static final int LIMIT_SENDERS = 16;
    /** Longest test currency code ({@code ZXV_VHUB_CODE_MAX}). */
    public static final int CODE_MAX = 12;
    /** Largest single amount ({@code ZXV_VHUB_AMOUNT_MAX}, 2^59). */
    public static final long AMOUNT_MAX = 1L << 59;
    /** The V-Bill issuer's account ({@code ZXV_VHUB_VBILL_ISSUER}). */
    public static final int VBILL_ISSUER = 0;
    /** ML-DSA-87 public key size. */
    public static final int ISSUER_KEY_BYTES = 2592;
    /** ML-DSA-87 signature size. */
    public static final int SIGNATURE_BYTES = 4627;

    /**
     * A currency's accounts and current peg (an unset peg reads 0/0 at epoch 0).
     *
     * @param slot           the slot
     * @param issuerAccount  the issuer's account
     * @param poolAccount    the hub's pool of the currency
     * @param vfloatAccount  the currency's V-Bill float
     * @param pegNum         minor units per {@code pegDen} V-Bills (unsigned)
     * @param pegDen         the peg's denominator (unsigned)
     * @param pegEpoch       the peg's epoch (unsigned)
     */
    public record CurrencyInfo(int slot, int issuerAccount, int poolAccount, int vfloatAccount, long pegNum,
            long pegDen, long pegEpoch) {
        /** Whether a peg has been set. */
        public boolean hasPeg() {
            return pegDen != 0;
        }
    }

    /**
     * One account's rails: A = L + E.
     *
     * @param debit  what it holds (unsigned)
     * @param credit what it owes, issuers only (unsigned)
     * @param equity debit minus credit (signed)
     */
    public record Balance(long debit, long credit, long equity) {}

    /**
     * A quote: the largest exact amount not above the request, and what it carries.
     *
     * @param amount   the amount of the sending currency (0 if none is exact)
     * @param vbills   the V-Bills it moves
     * @param received the units of the receiving currency
     */
    public record Quote(long amount, long vbills, long received) {}

    /**
     * A completed swap.
     *
     * @param vbills   the V-Bills moved
     * @param received the units received
     */
    public record Swap(long vbills, long received) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private VHub(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("V-Bill hub", h, x -> Ffi.callVoid(Native15.vhub_destroy, x), ctx, Caps.res(ctx));
    }

    /**
     * A new hub whose ledger starts with the V-Bill asset and the V-Bill
     * issuer's account.
     *
     * @param cap  a capability with {@code LEDGER_POST} (and {@code LEDGER_READ}
     *             for reads); the hub uses it for every call
     * @param seed 32 bytes from which every posting's idempotency key and UETR
     *             derive, or null for a fresh random seed
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code LEDGER_POST}
     */
    public static VHub create(Capability cap, byte[] seed) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            return new VHub(cap, ctx, Buf.create(ctx, Native15.vhub_create, l.h(0), Buf.fixedOrNull(a, "seed", seed, 32)));
        }
    }

    /**
     * Registers a test currency and opens its issuer, pool and V-Bill float
     * accounts (in that order), funding the pool and the float. The peg
     * starts unset.
     *
     * @param slot         0..7
     * @param code         3..12 characters of A-Z and 0-9; not an ISO 4217 code, "VBL" or "VFV"
     * @param minorUnits   0..4
     * @param issuerKey    the issuer's ML-DSA-87 public key (2592 bytes); it alone may set the peg
     * @param poolFunding  units issued to the pool (may be 0)
     * @param vbillBacking V-Bills issued to the float (may be 0)
     * @return the new currency's accounts
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_CURRENCY for an ISO 4217 code, ZXV_E_DUPLICATE for a taken slot or code
     */
    public CurrencyInfo addCurrency(int slot, String code, int minorUnits, byte[] issuerKey, long poolFunding,
            long vbillBacking) {
        Buf.nonNull("code", code);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.vhub_add_ccy, l.h(0), l.h(1), Buf.u8("slot", slot), Ffi.cstr(a, code),
                    Buf.u8("minorUnits", minorUnits), Buf.fixed(a, "issuerKey", issuerKey, ISSUER_KEY_BYTES),
                    poolFunding, vbillBacking), ctx);
        }
        return currency(slot);
    }

    /** A currency's accounts and current peg. Needs {@code LEDGER_READ}. */
    public CurrencyInfo currency(int slot) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment ia = a.allocate(JAVA_INT), pa = a.allocate(JAVA_INT), fa = a.allocate(JAVA_INT),
                    n = a.allocate(JAVA_LONG), d = a.allocate(JAVA_LONG), e = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_ccy_info, l.h(0), l.h(1), Buf.u8("slot", slot), ia, pa, fa, n, d, e), ctx);
            return new CurrencyInfo(slot, ia.get(JAVA_INT, 0), pa.get(JAVA_INT, 0), fa.get(JAVA_INT, 0),
                    n.get(JAVA_LONG, 0), d.get(JAVA_LONG, 0), e.get(JAVA_LONG, 0));
        }
    }

    /**
     * The exact 40 bytes an issuer signs to set a peg: "zxv/vhub-peg/v1" ||
     * slot || num || den || epoch (8 bytes little-endian each). Sign them
     * with ML-DSA-87 and an empty context. Pure.
     *
     * @param slot 0..255 (the message does not check the slot)
     */
    public static byte[] pegMessage(int slot, long num, long den, long epoch) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(40);
            Ffi.check(Ffi.call(Native15.vhub_peg_msg, Buf.u8("slot", slot), num, den, epoch, out), null);
            return out.toArray(JAVA_BYTE);
        }
    }

    /**
     * Sets a currency's peg to num/den at {@code epoch}, given the issuer's
     * signature over {@link #pegMessage}. A refused peg changes nothing.
     *
     * @throws PegException                                when {@code epoch} is not above the current one (stale or replayed)
     * @throws org.zedec.zxv.VerificationFailedException  when the signature is not the issuer's over these numbers
     * @throws org.zedec.zxv.ZxvUsageException             ZXV_E_RANGE when num or den is 0
     */
    public void setPeg(int slot, long num, long den, long epoch, byte[] signature) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.vhub_set_peg, l.h(0), l.h(1), Buf.u8("slot", slot), num, den, epoch,
                    Buf.fixed(a, "signature", signature, SIGNATURE_BYTES)), ctx);
        }
    }

    /**
     * Opens a holder account in currency {@code slot}.
     *
     * @param owner an id of the caller's choosing (uint32 bits)
     * @return the account number
     */
    public int openAccount(int slot, int owner) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment acct = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.vhub_open_account, l.h(0), l.h(1), Buf.u8("slot", slot), owner, acct), ctx);
            return acct.get(JAVA_INT, 0);
        }
    }

    /**
     * Test faucet: issues {@code amount} to a holder account or pool (by the
     * currency's issuer) or V-Bills to a float (by the V-Bill issuer).
     */
    public void issue(int account, long amount) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native15.vhub_issue, l.h(0), l.h(1), account, amount), ctx);
        }
    }

    /** One account's rails. Needs {@code LEDGER_READ}. */
    public Balance balance(int account) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(JAVA_LONG), c = a.allocate(JAVA_LONG), e = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_balance, l.h(0), l.h(1), account, d, c, e), ctx);
            return new Balance(d.get(JAVA_LONG, 0), c.get(JAVA_LONG, 0), e.get(JAVA_LONG, 0));
        }
    }

    /**
     * Adds a limit rule to currency {@code slot}: at most {@code limit} minor
     * units out per sender per rule epoch (inflows are not counted). One
     * limit tracks 16 senders per epoch and refuses a 17th.
     */
    public void addLimit(int slot, long limit) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native15.vhub_add_limit, l.h(0), l.h(1), Buf.u8("slot", slot), limit), ctx);
        }
    }

    /** Starts the next rule epoch (every limit starts every sender at zero); returns the new epoch. */
    public long nextEpoch() {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment e = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_next_epoch, l.h(0), l.h(1), e), ctx);
            return e.get(JAVA_LONG, 0);
        }
    }

    /**
     * The largest exact amount of {@code fromSlot} not above {@code want},
     * and what it carries. Does not run rules or check balances, so a quoted
     * swap may still be refused. Needs {@code LEDGER_READ}.
     *
     * @throws PegException when either peg is unset
     */
    public Quote quote(int fromSlot, int toSlot, long want) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment am = a.allocate(JAVA_LONG), v = a.allocate(JAVA_LONG), r = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_quote, l.h(0), l.h(1), Buf.u8("fromSlot", fromSlot),
                    Buf.u8("toSlot", toSlot), want, am, v, r), ctx);
            return new Quote(am.get(JAVA_LONG, 0), v.get(JAVA_LONG, 0), r.get(JAVA_LONG, 0));
        }
    }

    /**
     * Swaps {@code amount} minor units of {@code fromSlot} from holder
     * {@code sender} to holder {@code receiver} in {@code toSlot}, as one
     * six-line posting. A refused swap changes nothing.
     *
     * @param tick a caller-supplied time, never the wall clock (unsigned)
     * @throws LimitException                             when a limit rule refuses
     * @throws PegException                               when either peg is unset
     * @throws org.zedec.zxv.InsufficientFundsException  when the sender, A's float or B's pool would go below zero
     * @throws org.zedec.zxv.ZxvRefusedException         ZXV_E_STATE when a currency has no rule
     * @throws org.zedec.zxv.ZxvUsageException           ZXV_E_RANGE when the amount is not exact under the pegs
     */
    public Swap swap(int fromSlot, int toSlot, int sender, int receiver, long amount, long tick) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_LONG), r = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_swap, l.h(0), l.h(1), Buf.u8("fromSlot", fromSlot),
                    Buf.u8("toSlot", toSlot), sender, receiver, amount, tick, v, r), ctx);
            return new Swap(v.get(JAVA_LONG, 0), r.get(JAVA_LONG, 0));
        }
    }

    /**
     * Verifies the whole hub ledger (A = L + E per account, DEBIT = CREDIT
     * per asset, the posting hash chain) and returns the postings made so far.
     *
     * @throws org.zedec.zxv.ZxvFatalError when an invariant is broken (the context halts)
     */
    public long check() {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment p = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.vhub_check, l.h(0), l.h(1), p), ctx);
            return p.get(JAVA_LONG, 0);
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the hub and its ledger. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
