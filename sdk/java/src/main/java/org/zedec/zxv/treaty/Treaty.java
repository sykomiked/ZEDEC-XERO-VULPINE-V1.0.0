// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.treaty;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Locks;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * One treaty (ABI 1.5, {@code zxv_treaty}): a batch of settlement work, who
 * takes part, what each party deposits and may claim back, how the batch's
 * dividend is shared (each party's weight num/den), and who owes whom.
 *
 * <p>A treaty has two exact spellings that hold the same content:
 * <ul>
 *   <li>the fixed {@value #RECORD_BYTES}-byte COBOL copybook RECORD
 *       ({@code ZXVTRTY.cpy}), all DISPLAY text, so plain ASCII everywhere;</li>
 *   <li>compact SUTRA text, for example
 *       <pre>
 * TREATY CORRIDOR-7 USD SETL
 * PARTY ACME 5000 4000 1/2
 * PARTY BETA 3000 3000 1/2
 * OWE ACME BETA 1200
 * END</pre>
 *       with single spaces, {@code '\n'} line ends (the last one too), no
 *       leading zeros, fractions in lowest terms and nothing after "END\n".</li>
 * </ul>
 * Each spelling is canonical: parsing then emitting gives the input back byte
 * for byte, and the two forms of one treaty compare equal
 * ({@link #contentEquals}). Every parse and emit checks the rules:
 * R1 the weights are in lowest terms, each at most 1 and all adding up to
 * exactly 1; R2 no party claims more than it deposits; R3 every amount fits
 * PIC S9(18), deposits and claims are not negative and obligations are
 * positive; R4 there are 1..16 parties with distinct ids and 0..64
 * obligations between two different known parties. A refusal is a
 * {@link TreatyException} carrying the {@link TreatyFault} and, for a parse,
 * the byte offset of the first fault.
 *
 * <p>Everything here is pure: no context and no capability. {@link #run()}
 * works in a private scratch engine freed before it returns, so it changes no
 * ledger anyone holds; its numbers are a deterministic report, not a payment.
 *
 * <p>Honest limits: a treaty is a batch description, not a contract in law
 * (signatures, consent and authority live elsewhere); one asset per treaty;
 * the dividend shares are reported only, nothing is paid out of the bucket.
 *
 * <p>Thread safety: the C handle is not thread-safe; this wrapper serializes
 * its own calls, so a {@code Treaty} may be shared, but one thread at a time
 * runs. Close it when done (it holds native memory).
 *
 * <pre>{@code
 * try (Treaty t = Treaty.parse(sutraText)) {
 *     byte[] record = t.toRecord();          // the 2542-byte copybook record
 *     TreatyResult r = t.run();              // netting, fee and claims, as a report
 *     long fees = r.fees();
 * } catch (TreatyException e) {
 *     System.out.println(e.fault() + " at byte " + e.offset());
 * }
 * }</pre>
 */
public final class Treaty implements AutoCloseable {
    /** Length of a treaty copybook record ({@code ZXV_TREATY_REC_LEN}). */
    public static final int RECORD_BYTES = 2542;
    /** Length of a result copybook record ({@code ZXV_TREATY_RESULT_LEN}). */
    public static final int RESULT_RECORD_BYTES = 2422;
    /** Longest canonical Sutra text in bytes ({@code ZXV_TREATY_SUTRA_MAX}). */
    public static final int SUTRA_MAX = 3843;
    /** Most parties in a treaty ({@code ZXV_TREATY_MAX_PARTIES}). */
    public static final int MAX_PARTIES = 16;
    /** Most obligations in a treaty ({@code ZXV_TREATY_MAX_OWES}). */
    public static final int MAX_OBLIGATIONS = 64;
    /** Longest treaty name: A-Z, 0-9 and '-', starting with a letter ({@code ZXV_TREATY_NAME_MAX}). */
    public static final int NAME_MAX = 30;
    /** Longest party id, same alphabet ({@code ZXV_TREATY_ID_MAX}). */
    public static final int ID_MAX = 8;
    /** Largest weight numerator or denominator, PIC 9(9) ({@code ZXV_TREATY_WEIGHT_MAX}). */
    public static final int WEIGHT_MAX = 999_999_999;

    /**
     * One party.
     *
     * @param id          its id (1..8 of A-Z, 0-9 and '-')
     * @param deposit     what it deposits, in minor units of the asset
     * @param claim       what it may claim back (at most the deposit)
     * @param weightNum   its dividend weight's numerator
     * @param weightDen   its dividend weight's denominator (the fraction is in lowest terms)
     */
    public record Party(String id, long deposit, long claim, int weightNum, int weightDen) {}

    /**
     * One obligation: party {@code from} owes party {@code to} {@code amount}.
     *
     * @param from   the debtor's party index (0-based, in treaty order)
     * @param to     the creditor's party index
     * @param amount minor units of the asset, above zero
     */
    public record Obligation(int from, int to, long amount) {}

    private final NativeResource res;

    private Treaty(MemorySegment h) {
        this.res = new NativeResource("treaty", h, x -> Ffi.callVoid(Native15.treaty_destroy, x), null, null);
    }

    /** A new, empty treaty. It fails validation (fault {@link TreatyFault#NAME}) until a parse fills it. */
    public static Treaty create() {
        return new Treaty(Buf.create(null, Native15.treaty_create));
    }

    /**
     * A treaty parsed from canonical Sutra text.
     *
     * @param sutra the text (ASCII; at most {@link #SUTRA_MAX} bytes)
     * @return a new treaty; close it when done
     * @throws TreatyException with the fault and its byte offset if the text is refused
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG if the text is too long
     */
    public static Treaty parse(String sutra) {
        Treaty t = create();
        try {
            t.load(sutra);
            return t;
        } catch (RuntimeException e) {
            t.close();
            throw e;
        }
    }

    /**
     * A treaty parsed from its copybook record.
     *
     * @param record exactly {@link #RECORD_BYTES} bytes
     * @return a new treaty; close it when done
     * @throws TreatyException with the fault and its byte offset if the record is refused
     */
    public static Treaty parseRecord(byte[] record) {
        Treaty t = create();
        try {
            t.loadRecord(record);
            return t;
        } catch (RuntimeException e) {
            t.close();
            throw e;
        }
    }

    /**
     * Replaces this treaty's content with parsed Sutra text. On any failure
     * the treaty is unchanged.
     *
     * @param sutra the text
     * @throws TreatyException with the fault and the byte offset of the first fault
     */
    public synchronized void load(String sutra) {
        Buf.nonNull("sutra", sutra);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment off = a.allocate(JAVA_INT), fault = a.allocate(JAVA_INT);
            int st = Ffi.call(Native15.treaty_parse_sutra, l.h(0), Ffi.cstr(a, sutra), off, fault);
            checkParse(st, off, fault);
        }
    }

    /**
     * Replaces this treaty's content with a parsed copybook record. Unused
     * party and obligation slots must be exactly what COBOL's INITIALIZE
     * leaves, and "-0" is refused. On any failure the treaty is unchanged.
     *
     * @param record exactly {@link #RECORD_BYTES} bytes (any other length is ZXV_E_ARG)
     * @throws TreatyException with the fault and the byte offset of the first fault
     */
    public synchronized void loadRecord(byte[] record) {
        Buf.nonNull("record", record);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment off = a.allocate(JAVA_INT), fault = a.allocate(JAVA_INT);
            int st = Ffi.call(Native15.treaty_parse_record, l.h(0), Buf.in(a, record), (long) record.length, off, fault);
            checkParse(st, off, fault);
        }
    }

    private static void checkParse(int st, MemorySegment off, MemorySegment fault) {
        if (st == -33) {
            String detail = Ffi.lastError();
            throw new TreatyException(st, Errors.name(st), detail, TreatyFault.fromCode(fault.get(JAVA_INT, 0)),
                    Integer.toUnsignedLong(off.get(JAVA_INT, 0)));
        }
        Ffi.check(st, null);
    }

    /** Empties the treaty again, as {@link #create()} left it. */
    public synchronized void clear() {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native15.treaty_clear, l.h(0)), null);
        }
    }

    /**
     * The canonical Sutra text.
     *
     * @throws TreatyException (with its fault) if the treaty does not validate, e.g. an empty one
     */
    public synchronized String toSutra() {
        try (Lease l = Lease.of(res)) {
            try {
                return Ffi.text(null, (b, c, n) -> Ffi.call(Native15.treaty_emit_sutra, l.h(0), b, c, n));
            } catch (TreatyException e) {
                throw withFault(l.h(0), e);
            }
        }
    }

    /**
     * The canonical copybook record: {@link #RECORD_BYTES} bytes of ASCII,
     * with no NUL.
     *
     * @throws TreatyException (with its fault) if the treaty does not validate
     */
    public synchronized byte[] toRecord() {
        try (Lease l = Lease.of(res)) {
            try {
                return Buf.r4(null, RECORD_BYTES, (b, c, n) -> Ffi.call(Native15.treaty_emit_record, l.h(0), b, c, n));
            } catch (TreatyException e) {
                throw withFault(l.h(0), e);
            }
        }
    }

    /** Adds the fault code (from zxv_treaty_validate) to a refusal that came without one. */
    private static TreatyException withFault(MemorySegment h, TreatyException e) {
        TreatyFault f = faultOf(h);
        return new TreatyException(e.status(), e.statusName(), e.detail(), f, -1);
    }

    private static TreatyFault faultOf(MemorySegment h) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment fault = a.allocate(JAVA_INT);
            int st = Ffi.call(Native15.treaty_validate, h, fault);
            if (st != 0 && st != -33) Ffi.check(st, null);
            return TreatyFault.fromCode(fault.get(JAVA_INT, 0));
        }
    }

    /**
     * Checks rules R1-R4 and the spelling rules.
     *
     * @throws TreatyException with the first fault if the treaty does not validate
     */
    public synchronized void validate() {
        TreatyFault f;
        try (Lease l = Lease.of(res)) {
            f = faultOf(l.h(0));
        }
        if (f != TreatyFault.NONE) {
            throw new TreatyException(-33, Errors.name(-33), "the treaty does not validate: " + f, f, -1);
        }
    }

    /** The first rule this treaty breaks, or {@link TreatyFault#NONE} if it validates. */
    public synchronized TreatyFault fault() {
        try (Lease l = Lease.of(res)) {
            return faultOf(l.h(0));
        }
    }

    /**
     * True if both treaties hold the same content: every field and every slot
     * ({@code zxv_treaty_equal}). The Sutra and record spellings of one treaty
     * compare equal; two empty treaties are equal. This is not
     * {@link Object#equals}, which stays identity because a treaty is mutable.
     */
    public boolean contentEquals(Treaty other) {
        Buf.nonNull("other", other);
        return Locks.both(this, other, () -> {
            try (Lease l = Lease.of(res, other.res); Arena a = Arena.ofConfined()) {
                MemorySegment eq = a.allocate(JAVA_INT);
                Ffi.check(Ffi.call(Native15.treaty_equal, l.h(0), l.h(1), eq), null);
                return eq.get(JAVA_INT, 0) != 0;
            }
        });
    }

    /** The treaty name ("" when empty). */
    public synchronized String name() {
        try (Lease l = Lease.of(res)) {
            return Ffi.text(null, (b, c, n) -> Ffi.call(Native15.treaty_name, l.h(0), b, c, n));
        }
    }

    /** The asset: three capital letters ("" when empty). */
    public synchronized String asset() {
        try (Lease l = Lease.of(res)) {
            return Ffi.text(null, (b, c, n) -> Ffi.call(Native15.treaty_asset, l.h(0), b, c, n));
        }
    }

    /** The operation ({@link TreatyOp#NONE} when empty). */
    public synchronized TreatyOp op() {
        return TreatyOp.fromCode(header()[0]);
    }

    /** How many parties the treaty holds. */
    public synchronized int partyCount() {
        return header()[1];
    }

    /** How many obligations the treaty holds. */
    public synchronized int obligationCount() {
        return header()[2];
    }

    private int[] header() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment op = a.allocate(JAVA_INT), p = a.allocate(JAVA_INT), o = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.treaty_header, l.h(0), op, p, o), null);
            return new int[] {op.get(JAVA_INT, 0), p.get(JAVA_INT, 0), o.get(JAVA_INT, 0)};
        }
    }

    /**
     * Party {@code index} (0-based, in treaty order).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_NOT_FOUND if {@code index >= partyCount()}
     */
    public synchronized Party party(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            String id = Ffi.text(null, (b, c, n) -> Ffi.call(Native15.treaty_party_id, l.h(0), index, b, c, n));
            MemorySegment dep = a.allocate(JAVA_LONG), claim = a.allocate(JAVA_LONG),
                    wn = a.allocate(JAVA_INT), wd = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.treaty_party, l.h(0), index, dep, claim, wn, wd), null);
            return new Party(id, dep.get(JAVA_LONG, 0), claim.get(JAVA_LONG, 0), wn.get(JAVA_INT, 0), wd.get(JAVA_INT, 0));
        }
    }

    /** Every party, in treaty order. */
    public synchronized List<Party> parties() {
        int n = partyCount();
        List<Party> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(party(i));
        return List.copyOf(out);
    }

    /**
     * Obligation {@code index} (0-based).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_NOT_FOUND if {@code index >= obligationCount()}
     */
    public synchronized Obligation obligation(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment from = a.allocate(JAVA_INT), to = a.allocate(JAVA_INT), amt = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.treaty_owe, l.h(0), index, from, to, amt), null);
            return new Obligation(from.get(JAVA_INT, 0), to.get(JAVA_INT, 0), amt.get(JAVA_LONG, 0));
        }
    }

    /** Every obligation, in treaty order. */
    public synchronized List<Obligation> obligations() {
        int n = obligationCount();
        List<Obligation> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(obligation(i));
        return List.copyOf(out);
    }

    /**
     * Runs the treaty in a fresh private engine for its asset: funds each
     * deposit, nets the obligations multilaterally (at most parties - 1
     * legs), and for {@link TreatyOp#SETL} posts the legs with the 0.08889%
     * assurance fee (all or nothing), pays each claim back out and reports
     * each party's weighted share of what the fee put in the V-Bill dividend
     * bucket (floor shares, then leftover units to the largest remainders,
     * ties to the lowest index; reported, not paid). Nothing persists and no
     * capability is needed; the same treaty always gives the same result, the
     * one {@code CALL 'ZXV_ENGINE'} returns for its record. It takes about
     * 500 KB of native heap for the call.
     *
     * @return the report, read out in full (it holds no native memory)
     * @throws TreatyException if the treaty does not validate
     * @throws org.zedec.zxv.InsufficientFundsException ZXV_E_FUNDS if a payer cannot cover its legs plus the
     *         fee, or a claim exceeds what is left after settlement
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE if a net position leaves PIC S9(18) or an amount
     *         passes the engine's per-line limit of 2^59
     */
    public synchronized TreatyResult run() {
        MemorySegment r;
        try (Lease l = Lease.of(res)) {
            try {
                r = Buf.create(null, Native15.treaty_run, l.h(0));
            } catch (TreatyException e) {
                throw withFault(l.h(0), e);
            }
        }
        try {
            return TreatyResult.read(r);
        } finally {
            Ffi.callVoid(Native15.treaty_result_destroy, r);
        }
    }

    /** True once closed. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the native treaty. Idempotent; any later call throws {@link IllegalStateException}. */
    @Override
    public void close() {
        res.close();
    }

    /** "Treaty[name ASSET OP, n parties]" (or "Treaty[closed]"). */
    @Override
    public String toString() {
        if (res.isClosed()) return "Treaty[closed]";
        return "Treaty[" + name() + " " + asset() + " " + op() + ", " + partyCount() + " parties]";
    }
}
