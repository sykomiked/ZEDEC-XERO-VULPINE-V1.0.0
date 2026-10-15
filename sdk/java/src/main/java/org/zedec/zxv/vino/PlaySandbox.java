// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native16;
import org.zedec.zxv.internal.NativeResource;

/**
 * A play-lane sandbox ({@code zxv_vino_play}): an app's or game's own little economy of play units
 * and Fibonacci-face vouchers, living in process memory only.
 *
 * <p><b>What it is.</b> A sandbox holds issuers and holders (up to {@link Vino#MAX_ACCOUNTS} in all)
 * and the vouchers between them (up to {@link Vino#MAX_VOUCHERS} active). Its unit is the play unit
 * (asset "PLAY", never VFV). <b>Play units are not money</b>: they come from nowhere
 * ({@link #back} is a faucet), are worth nothing, and are lost when the sandbox closes. Nothing in
 * the API takes a play sandbox together with anything live, so play units can never be converted
 * into, redeemed for, or moved into live value. Games may use the sandbox freely.
 *
 * <p><b>Vouchers.</b> {@link #issue} creates one voucher: a Fibonacci ladder face (1, 2, 3, 5, 8,
 * ...) from an issuer to a holder, on one {@link Rail}. The holder can pass it to another holder
 * ({@link #transfer}), naming its active rail so it can never be spent twice; switch its rail
 * ({@link #setRail}); or {@link #redeem} it, which returns it to its issuer and retires it.
 * <b>Redeeming pays nothing out</b>: the issuer's outstanding total falls by the face and that is
 * all. No call turns a voucher into backing, play units or anything else, and there is no interest,
 * yield or return of any kind.
 *
 * <p><b>Coverage.</b> An issuer may only issue while its backing stays at least 1.8 times everything
 * it has outstanding: {@code 10 * backing >= 18 * outstanding}, in exact integers. An issue, or a
 * withdrawal of backing ({@link #unback}), that would break this is refused with
 * {@link CoverageException}. Every refusal changes nothing.
 *
 * <p><b>Capabilities.</b> The sandbox keeps the {@link Capability} it was created with and uses it
 * for every call; it must hold {@link org.zedec.zxv.Permission#VINO} (else
 * {@link org.zedec.zxv.CapabilityDeniedException}). If the context halts, every later call raises
 * {@link org.zedec.zxv.ZxvFatalError} ({@code ZXV_E_POISONED}).
 *
 * <p><b>Numbers.</b> Amounts, faces and voucher ids are unsigned 64-bit integers carried in a Java
 * {@code long}; account numbers and owner ids are unsigned 32-bit integers in an {@code int}. Use
 * {@link Long#toUnsignedString(long)} to print them. There are no floats.
 *
 * <p><b>Lifetime and threads.</b> Close the sandbox (try-with-resources) before its context;
 * closing the context closes it too. After {@link #close()}, every call throws
 * {@link IllegalStateException}. Any thread may call: libzxv serializes the sandbox, and the SDK
 * never frees it while a call is in flight.
 *
 * <pre>{@code
 * try (ZxvContext ctx = ZxvContext.create();
 *      Capability cap = ctx.rootCapability(Permission.VINO);
 *      PlaySandbox shop = PlaySandbox.create(cap)) {
 *     int shopIssuer = shop.openIssuer(1);
 *     int player = shop.openHolder(42);
 *     shop.back(shopIssuer, 90);                      // play units, worth nothing
 *     long v = shop.issue(shopIssuer, player, 50, Rail.GHOST);
 *     shop.redeem(v, player);                         // retired; nothing is paid out
 * }
 * }</pre>
 */
public final class PlaySandbox implements AutoCloseable {
    /** The {@code is_issuer} value of {@code zxv_vino_play_open} for a holder. */
    private static final int HOLDER = 0;
    /** The {@code is_issuer} value of {@code zxv_vino_play_open} for an issuer. */
    private static final int ISSUER = 1;

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private PlaySandbox(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("Vino play sandbox", h, x -> Ffi.callVoid(Native16.vino_play_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * Creates an empty play sandbox (no accounts, no vouchers) on the capability's context.
     *
     * @param cap a capability holding {@link org.zedec.zxv.Permission#VINO}; the sandbox keeps it and
     *            uses it for every later call, so keep it open as long as the sandbox
     * @return the new sandbox; close it before its context
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code VINO}, or if the capability is revoked
     * @throws org.zedec.zxv.ZxvFatalError              if the context has halted ({@code ZXV_E_POISONED})
     * @throws IllegalStateException                    if the capability or its context is closed
     */
    public static PlaySandbox create(Capability cap) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new PlaySandbox(cap, ctx, Buf.create(ctx, Native16.vino_play_create, l.h(0)));
        }
    }

    /**
     * Opens an issuer account: one that posts play backing and issues vouchers.
     *
     * @param owner an id of the caller's choosing (unsigned 32-bit; owners need not be distinct)
     * @return the new account's number (numbers count up from 0)
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_CAPACITY}) when {@link Vino#MAX_ACCOUNTS} are open
     */
    public int openIssuer(int owner) {
        return open(owner, true);
    }

    /**
     * Opens a holder account: one that receives, transfers and redeems vouchers.
     *
     * @param owner an id of the caller's choosing (unsigned 32-bit; owners need not be distinct)
     * @return the new account's number (numbers count up from 0)
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_CAPACITY}) when {@link Vino#MAX_ACCOUNTS} are open
     */
    public int openHolder(int owner) {
        return open(owner, false);
    }

    /**
     * Opens an issuer or holder account ({@code zxv_vino_play_open}).
     *
     * @param owner  an id of the caller's choosing (unsigned 32-bit; owners need not be distinct)
     * @param issuer true for an issuer, false for a holder
     * @return the new account's number (numbers count up from 0)
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_CAPACITY}) when {@link Vino#MAX_ACCOUNTS} are open
     */
    public int open(int owner, boolean issuer) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native16.vino_play_open, l.h(0), l.h(1), owner, issuer ? ISSUER : HOLDER, out), ctx);
            return out.get(JAVA_INT, 0);
        }
    }

    /**
     * Posts {@code amount} play units of backing to an issuer. This is the play faucet: the units come
     * from nowhere and are worth nothing. Backing is what lets an issuer issue (see the coverage rule
     * in the class description).
     *
     * @param issuer the issuer's account number
     * @param amount play units, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) when the account is a holder;
     *                                         ({@code ZXV_E_NOT_FOUND}) when there is no such account;
     *                                         ({@code ZXV_E_RANGE}) for 0, above the maximum, or when
     *                                         the issuer's backing would pass 2^62
     */
    public void back(int issuer, long amount) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_play_back, l.h(0), l.h(1), issuer, amount), ctx);
        }
    }

    /**
     * Withdraws {@code amount} of an issuer's backing, only while the issuer stays at 1.8x coverage
     * of what it has outstanding. A refusal changes nothing.
     *
     * @param issuer the issuer's account number
     * @param amount play units, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @throws CoverageException                      when the issuer would fall below 1.8x coverage
     * @throws org.zedec.zxv.InsufficientFundsException ({@code ZXV_E_FUNDS}) when it is more than the backing
     * @throws org.zedec.zxv.ZxvUsageException        ({@code ZXV_E_ARG}) for a holder, ({@code ZXV_E_NOT_FOUND})
     *                                                when there is no such account, ({@code ZXV_E_RANGE}) for 0
     *                                                or too large
     */
    public void unback(int issuer, long amount) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_play_unback, l.h(0), l.h(1), issuer, amount), ctx);
        }
    }

    /**
     * Issues one voucher of face {@code face} from {@code issuer} to {@code holder}, on {@code rail}.
     *
     * @param issuer the issuing account (must be an issuer)
     * @param holder the receiving account (must be a holder)
     * @param face   a Fibonacci ladder face in play units (1, 2, 3, 5, 8, 13, ...; unsigned)
     * @param rail   the rail the voucher starts on
     * @return the new voucher's id: positive and never reused in this sandbox (unsigned 64-bit)
     * @throws org.zedec.zxv.coin.NotADenominationException ({@code ZXV_E_DENOM}) when {@code face} is not a ladder face
     * @throws CoverageException                       when the issuer would fall below 1.8x coverage
     * @throws org.zedec.zxv.ZxvUsageException         ({@code ZXV_E_ARG}) when the issuer is not an
     *                                                 issuer or the holder not a holder;
     *                                                 ({@code ZXV_E_NOT_FOUND}) for an unknown account
     * @throws org.zedec.zxv.ZxvRefusedException       ({@code ZXV_E_CAPACITY}) when {@link Vino#MAX_VOUCHERS} are active
     */
    public long issue(int issuer, int holder, long face, Rail rail) {
        Buf.nonNull("rail", rail);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native16.vino_issue, l.h(0), l.h(1), issuer, holder, face, rail.code(), id), ctx);
            return id.get(JAVA_LONG, 0);
        }
    }

    /**
     * Moves an active voucher from holder {@code from} to holder {@code to}, spending it on
     * {@code rail}, which must be its active rail. The rail does not change.
     *
     * @param voucher the voucher's id
     * @param from    the holder that holds it now
     * @param to      the holder that receives it (a different holder)
     * @param rail    the voucher's active rail
     * @throws RailException                     when {@code rail} is not the voucher's active rail
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_STATE}) when the voucher has been redeemed
     * @throws org.zedec.zxv.ZxvUsageException   ({@code ZXV_E_ARG}) when {@code from} is not its
     *                                           holder, {@code to} is not a holder, or they are equal;
     *                                           ({@code ZXV_E_NOT_FOUND}) for an unknown voucher or account
     */
    public void transfer(long voucher, int from, int to, Rail rail) {
        Buf.nonNull("rail", rail);
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_transfer, l.h(0), l.h(1), voucher, from, to, rail.code()), ctx);
        }
    }

    /**
     * Switches an active voucher, by its holder, to {@code rail} (bearer to ghost or back). Switching
     * moves nothing and changes no totals.
     *
     * @param voucher the voucher's id
     * @param holder  the holder that holds it
     * @param rail    the rail to switch to (not the one it is on)
     * @throws RailException                     when the voucher is already on {@code rail}
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_STATE}) when it has been redeemed
     * @throws org.zedec.zxv.ZxvUsageException   ({@code ZXV_E_ARG}) when {@code holder} is not its holder;
     *                                           ({@code ZXV_E_NOT_FOUND}) for an unknown voucher
     */
    public void setRail(long voucher, int holder, Rail rail) {
        Buf.nonNull("rail", rail);
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_set_rail, l.h(0), l.h(1), voucher, holder, rail.code()), ctx);
        }
    }

    /**
     * Redeems an active voucher, by its holder, to its issuer: the voucher is retired and the
     * issuer's outstanding total falls by its face. <b>Nothing is paid out</b> to anyone, and the
     * issuer's backing does not change. The voucher stays readable with {@link #voucher(long)} as
     * {@link VoucherState#REDEEMED}.
     *
     * @param voucher the voucher's id
     * @param holder  the holder that holds it
     * @throws org.zedec.zxv.ZxvRefusedException ({@code ZXV_E_STATE}) when it is already redeemed
     * @throws org.zedec.zxv.ZxvUsageException   ({@code ZXV_E_ARG}) when {@code holder} is not its holder;
     *                                           ({@code ZXV_E_NOT_FOUND}) for an unknown voucher
     */
    public void redeem(long voucher, int holder) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_redeem, l.h(0), l.h(1), voucher, holder), ctx);
        }
    }

    /**
     * Reads one voucher, active or redeemed.
     *
     * @param id the voucher's id
     * @return a snapshot of the voucher
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_NOT_FOUND}) when there is no such voucher
     */
    public Voucher voucher(long id) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment face = a.allocate(JAVA_LONG), iss = a.allocate(JAVA_INT), hol = a.allocate(JAVA_INT),
                    rail = a.allocate(JAVA_INT), state = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native16.vino_voucher_info, l.h(0), l.h(1), id, face, iss, hol, rail, state), ctx);
            return new Voucher(id, face.get(JAVA_LONG, 0), iss.get(JAVA_INT, 0), hol.get(JAVA_INT, 0),
                    Rail.fromCode(rail.get(JAVA_INT, 0)), VoucherState.fromCode(state.get(JAVA_INT, 0)));
        }
    }

    /**
     * Reads one account: its owner, kind and totals.
     *
     * @param account the account number
     * @return a snapshot of the account
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_NOT_FOUND}) when there is no such account
     */
    public PlayAccount account(int account) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment owner = a.allocate(JAVA_INT), isIssuer = a.allocate(JAVA_INT), backing = a.allocate(JAVA_LONG),
                    faces = a.allocate(JAVA_LONG), count = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native16.vino_account_info, l.h(0), l.h(1), account, owner, isIssuer, backing, faces,
                    count), ctx);
            return new PlayAccount(account, owner.get(JAVA_INT, 0), isIssuer.get(JAVA_INT, 0) == ISSUER,
                    backing.get(JAVA_LONG, 0), faces.get(JAVA_LONG, 0), count.get(JAVA_INT, 0));
        }
    }

    /**
     * The ids of the active vouchers an account issued (an issuer) or holds (a holder), in ascending
     * order. Redeemed vouchers are not listed. The SDK sizes the buffer for you.
     *
     * @param account the account number
     * @return an unmodifiable list of voucher ids (unsigned 64-bit), possibly empty
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_NOT_FOUND}) when there is no such account
     */
    public List<Long> vouchers(int account) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment count = a.allocate(JAVA_LONG);
            MemorySegment ids = MemorySegment.NULL;
            long capLen = 0;
            int st = Ffi.call(Native16.vino_vouchers, l.h(0), l.h(1), account, ids, capLen, count);
            // Two-call pattern: ask for the count, then fetch. Another thread may issue a voucher
            // between the two calls, so retry (a few times) while the list has grown.
            for (int i = 0; st == Ffi.E_BUFFER && i < 4; i++) {
                capLen = count.get(JAVA_LONG, 0);
                ids = a.allocate(JAVA_LONG, capLen);
                st = Ffi.call(Native16.vino_vouchers, l.h(0), l.h(1), account, ids, capLen, count);
            }
            Ffi.check(st, ctx);
            long n = count.get(JAVA_LONG, 0);
            List<Long> out = new ArrayList<>((int) n);
            for (long i = 0; i < n; i++) out.add(ids.getAtIndex(JAVA_LONG, i));
            return Collections.unmodifiableList(out);
        }
    }

    /**
     * Re-checks every invariant of the sandbox: each active voucher has a ladder face, one rail, an
     * issuer and a holder; each issuer's outstanding total equals the sum of its active faces and it
     * meets the 1.8x coverage rule; each holder's total equals what it holds.
     *
     * @throws org.zedec.zxv.ZxvFatalError ({@code ZXV_E_INVARIANT}) when an invariant is broken; the
     *                                     context halts and every later call on it is refused
     */
    public void check() {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native16.vino_play_check, l.h(0), l.h(1)), ctx);
        }
    }

    /**
     * Whether {@link #close()} has run (or the context was closed).
     *
     * @return true once closed
     */
    public boolean isClosed() {
        return res.isClosed();
    }

    /**
     * Frees the sandbox and everything in it; its play units and vouchers are gone for good.
     * Idempotent. Waits for calls in flight on other threads to finish first.
     */
    @Override
    public void close() {
        res.close();
    }
}
