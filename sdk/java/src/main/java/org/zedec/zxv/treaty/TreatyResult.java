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
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * What running a {@link Treaty} reported ({@code zxv_treaty_result}), read
 * out in full: an immutable Java value that holds no native memory, so it
 * needs no closing and any number of threads may read it.
 *
 * <p>For {@link TreatyOp#NET} the fee and dividends are 0 and each balance is
 * the deposit. For {@link TreatyOp#SETL} each balance is what the party holds
 * after its legs, the fee and its claim, and each dividend is the party's
 * reported share of the V-Bill dividend bucket. Nothing was paid: this is a
 * deterministic report.
 */
public final class TreatyResult {
    /**
     * One party's numbers.
     *
     * @param net      what it is owed minus what it owes (gross)
     * @param balance  its deposit for NET; for SETL what it holds after the legs, the fee and its claim
     * @param dividend its reported share of the dividend bucket (0 for NET)
     */
    public record PartyResult(long net, long balance, long dividend) {}

    /**
     * One netted leg: party {@code from} pays party {@code to} {@code amount}.
     *
     * @param from   the payer's party index (0-based, treaty order)
     * @param to     the payee's party index
     * @param amount minor units of the asset
     */
    public record Leg(int from, int to, long amount) {}

    private final long fees;
    private final List<PartyResult> parties;
    private final List<Leg> legs;
    private final byte[] record;

    private TreatyResult(long fees, List<PartyResult> parties, List<Leg> legs, byte[] record) {
        this.fees = fees;
        this.parties = parties;
        this.legs = legs;
        this.record = record;
    }

    /** Reads every field of a native result (the caller destroys it). */
    static TreatyResult read(MemorySegment r) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment np = a.allocate(JAVA_INT), nl = a.allocate(JAVA_INT), fee = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.treaty_result_summary, r, np, nl, fee), null);
            int parties = np.get(JAVA_INT, 0), legs = nl.get(JAVA_INT, 0);
            List<PartyResult> ps = new ArrayList<>(parties);
            MemorySegment x = a.allocate(JAVA_LONG), y = a.allocate(JAVA_LONG), z = a.allocate(JAVA_LONG);
            for (int i = 0; i < parties; i++) {
                Ffi.check(Ffi.call(Native15.treaty_result_party, r, i, x, y, z), null);
                ps.add(new PartyResult(x.get(JAVA_LONG, 0), y.get(JAVA_LONG, 0), z.get(JAVA_LONG, 0)));
            }
            List<Leg> ls = new ArrayList<>(legs);
            MemorySegment f = a.allocate(JAVA_INT), t = a.allocate(JAVA_INT);
            for (int i = 0; i < legs; i++) {
                Ffi.check(Ffi.call(Native15.treaty_result_leg, r, i, f, t, x), null);
                ls.add(new Leg(f.get(JAVA_INT, 0), t.get(JAVA_INT, 0), x.get(JAVA_LONG, 0)));
            }
            byte[] rec = Buf.r4(null, Treaty.RESULT_RECORD_BYTES,
                    (b, c, n) -> Ffi.call(Native15.treaty_result_record, r, b, c, n));
            return new TreatyResult(fee.get(JAVA_LONG, 0), List.copyOf(ps), List.copyOf(ls), rec);
        }
    }

    /** How many parties the result covers (the treaty's party count). */
    public int partyCount() {
        return parties.size();
    }

    /** How many netted legs there are (at most parties - 1). */
    public int legCount() {
        return legs.size();
    }

    /** The total assurance fee charged (0 for NET). */
    public long fees() {
        return fees;
    }

    /**
     * Party {@code index}'s numbers (treaty order).
     *
     * @throws IndexOutOfBoundsException if there is no such party
     */
    public PartyResult party(int index) {
        return parties.get(index);
    }

    /** Every party's numbers, in treaty order. */
    public List<PartyResult> parties() {
        return parties;
    }

    /**
     * Netted leg {@code index}.
     *
     * @throws IndexOutOfBoundsException if there is no such leg
     */
    public Leg leg(int index) {
        return legs.get(index);
    }

    /** Every netted leg. */
    public List<Leg> legs() {
        return legs;
    }

    /**
     * The {@code ZXVRSLT.cpy} result record ({@link Treaty#RESULT_RECORD_BYTES}
     * bytes of ASCII, no NUL). Party numbers inside it are 1-based, as the
     * copybook says. A fresh copy each call.
     */
    public byte[] toRecord() {
        return record.clone();
    }

    @Override
    public String toString() {
        return "TreatyResult[fees=" + fees + ", parties=" + parties + ", legs=" + legs + "]";
    }
}
