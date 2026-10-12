// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.Optional;
import org.zedec.zxv.cid.Cid;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * An AIPI function record (ABI 1.5): the canonical bytes that describe one
 * content-addressed function (docs/AIPI.md section 11), and its fields. One
 * function has exactly one record and so exactly one function CID
 * ({@link #cid()}: codec {@code zxv-fn + intent}, sha2-256 of the record).
 * Immutable and thread-safe; pure (no context, no capability).
 *
 * <p>Build one with {@link #builder()} (or {@link #encode} for every field
 * at once); read one with {@link #decode}, which checks the record strictly
 * (exact lengths, no trailing bytes, canonical encoding).
 *
 * <pre>{@code
 * FunctionRecord fn = FunctionRecord.builder()
 *         .module(moduleCid).entry("main").fuelMax(1_000_000)
 *         .verifyByChecker("check")
 *         .build();
 * Cid fnCid = fn.cid();
 * }</pre>
 *
 * <p>Honest limit: this library cannot RUN a function. Execution needs the
 * Z-Wasm sandbox (sdk/kits/aipi), which is not part of libzxv.
 */
public final class FunctionRecord {
    /** Longest function record in bytes ({@code ZXV_AIPI_FN_MAX}). */
    public static final int MAX_BYTES = 373;
    /** Longest export name: [A-Za-z0-9_]{1,63} ({@code ZXV_AIPI_NAME_MAX}). */
    public static final int NAME_MAX = 63;
    /** Largest QUORUM n ({@code ZXV_AIPI_QUORUM_MAX}). */
    public static final int QUORUM_MAX = 8;

    private static final int FIELD_MODULE = 0, FIELD_CONTRACT = 1;
    private static final int FIELD_ENTRY = 0, FIELD_CHECKER = 1, FIELD_CERTIFIER = 2;

    private final byte[] bytes;
    private final Intent intent;
    private final VerifyMode verify;
    private final Privacy privacy;
    private final Determinism determinism;
    private final int quorumN, quorumK;
    private final long fuelMax;
    private final Cid module;
    private final Cid contract;
    private final String entry, checker, certifier;
    private final Cid cid;

    private FunctionRecord(byte[] bytes, Intent intent, VerifyMode verify, Privacy privacy, Determinism determinism,
            int quorumN, int quorumK, long fuelMax, Cid module, Cid contract, String entry, String checker,
            String certifier, Cid cid) {
        this.bytes = bytes;
        this.intent = intent;
        this.verify = verify;
        this.privacy = privacy;
        this.determinism = determinism;
        this.quorumN = quorumN;
        this.quorumK = quorumK;
        this.fuelMax = fuelMax;
        this.module = module;
        this.contract = contract;
        this.entry = entry;
        this.checker = checker;
        this.certifier = certifier;
        this.cid = cid;
    }

    /**
     * Encodes a function record from every field; libzxv checks each one and
     * masks none.
     *
     * @param intent      what the function is for; it picks the function CID's codec
     * @param verify      how a remote result is judged
     * @param privacy     whether it may leave the node
     * @param determinism whether it is deterministic (QUORUM requires DETERMINISTIC)
     * @param quorumN     QUORUM only: 2..8 executors; 0 otherwise
     * @param quorumK     QUORUM only: n/2 &lt; k &lt;= n must agree; 0 otherwise
     * @param fuelMax     the most fuel one run may use, above 0 (uint64)
     * @param module      CIDv1 raw sha2-256 of the Z-Wasm module bytes
     * @param contract    CIDv1 raw sha2-256 of the z-contract text, or null for none
     * @param entry       the export computing the result
     * @param checker     CHECKER and CERT only: the checker export (differs from entry); else null
     * @param certifier   CERT only: the certifier export (differs from entry and checker); else null
     * @return the record
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for a field out of range or breaking a rule
     *         above or a name that is not [A-Za-z0-9_], ZXV_E_TOO_LONG for a name over 63
     * @throws org.zedec.zxv.cid.CidException if a CID is not CIDv1 raw sha2-256
     */
    public static FunctionRecord encode(Intent intent, VerifyMode verify, Privacy privacy, Determinism determinism,
            int quorumN, int quorumK, long fuelMax, Cid module, Cid contract, String entry, String checker,
            String certifier) {
        Buf.nonNull("intent", intent);
        Buf.nonNull("verify", verify);
        Buf.nonNull("privacy", privacy);
        Buf.nonNull("determinism", determinism);
        Buf.nonNull("module", module);
        Buf.nonNull("entry", entry);
        byte[] m = module.bytes(), c = contract == null ? null : contract.bytes();
        byte[] rec;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ms = Buf.in(a, m), cs = c == null ? MemorySegment.NULL : Buf.in(a, c);
            MemorySegment e = Ffi.cstr(a, entry), ch = Ffi.cstr(a, checker), ce = Ffi.cstr(a, certifier);
            rec = Buf.r4(null, MAX_BYTES, (b, cap, n) -> Ffi.call(Native15.aipi_fn_encode, intent.code(), verify.code(),
                    privacy.code(), determinism.code(), quorumN, quorumK, fuelMax, ms, (long) m.length, cs,
                    c == null ? 0L : (long) c.length, e, ch, ce, b, cap, n));
        }
        return decode(rec);
    }

    /**
     * Checks a function record strictly and reads every field.
     *
     * @param record the record bytes (copied)
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT if it is not a valid, canonical function record
     */
    public static FunctionRecord decode(byte[] record) {
        Buf.nonNull("record", record);
        byte[] rec = record.clone();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment r = Buf.in(a, rec);
            long len = rec.length;
            MemorySegment in = a.allocate(JAVA_INT), ve = a.allocate(JAVA_INT), pr = a.allocate(JAVA_INT),
                    de = a.allocate(JAVA_INT), qn = a.allocate(JAVA_INT), qk = a.allocate(JAVA_INT),
                    fu = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_fn_decode, r, len, in, ve, pr, de, qn, qk, fu), null);
            Cid module = cidField(a, r, len, FIELD_MODULE);
            Cid contract;
            MemorySegment cb = a.allocate(Cid.MAX_BYTES), cl = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native15.aipi_fn_get_cid, r, len, FIELD_CONTRACT, cb, cl);
            if (st == -4) {
                contract = null;
            } else {
                Ffi.check(st, null);
                contract = Cid.of(Buf.read(cb, cl.get(JAVA_LONG, 0)));
            }
            MemorySegment fc = a.allocate(Cid.MAX_BYTES), fl = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_fn_cid, r, len, fc, fl), null);
            return new FunctionRecord(rec, Intent.fromCode(in.get(JAVA_INT, 0)), VerifyMode.fromCode(ve.get(JAVA_INT, 0)),
                    Privacy.fromCode(pr.get(JAVA_INT, 0)), Determinism.fromCode(de.get(JAVA_INT, 0)),
                    qn.get(JAVA_INT, 0), qk.get(JAVA_INT, 0), fu.get(JAVA_LONG, 0), module, contract,
                    name(r, len, FIELD_ENTRY), name(r, len, FIELD_CHECKER), name(r, len, FIELD_CERTIFIER),
                    Cid.of(Buf.read(fc, fl.get(JAVA_LONG, 0))));
        }
    }

    private static Cid cidField(Arena a, MemorySegment r, long len, int field) {
        MemorySegment c = a.allocate(Cid.MAX_BYTES), n = a.allocate(JAVA_LONG);
        Ffi.check(Ffi.call(Native15.aipi_fn_get_cid, r, len, field, c, n), null);
        return Cid.of(Buf.read(c, n.get(JAVA_LONG, 0)));
    }

    private static String name(MemorySegment r, long len, int field) {
        return Ffi.text(null, (b, c, n) -> Ffi.call(Native15.aipi_fn_get_name, r, len, field, b, c, n));
    }

    /** A builder with intent FN, verify LOCAL, privacy PUBLIC and determinism DETERMINISTIC. */
    public static Builder builder() {
        return new Builder();
    }

    /** The canonical record bytes (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The function CID: codec {@code zxv-fn + intent}, sha2-256 of the record. */
    public Cid cid() {
        return cid;
    }

    /** What the function is for. */
    public Intent intent() {
        return intent;
    }

    /** How a remote result is judged. */
    public VerifyMode verify() {
        return verify;
    }

    /** Whether it may leave the node. */
    public Privacy privacy() {
        return privacy;
    }

    /** Whether it is deterministic. */
    public Determinism determinism() {
        return determinism;
    }

    /** QUORUM n (0 for other modes). */
    public int quorumN() {
        return quorumN;
    }

    /** QUORUM k (0 for other modes). */
    public int quorumK() {
        return quorumK;
    }

    /** The most fuel one run may use (uint64; read with {@link Long#toUnsignedString(long)}). */
    public long fuelMax() {
        return fuelMax;
    }

    /** The module CID. */
    public Cid module() {
        return module;
    }

    /** The contract CID, if the record names one. */
    public Optional<Cid> contract() {
        return Optional.ofNullable(contract);
    }

    /** The entry export. */
    public String entry() {
        return entry;
    }

    /** The checker export ("" unless CHECKER or CERT). */
    public String checker() {
        return checker;
    }

    /** The certifier export ("" unless CERT). */
    public String certifier() {
        return certifier;
    }

    /** Records are equal when their bytes are. */
    @Override
    public boolean equals(Object o) {
        return o instanceof FunctionRecord f && Arrays.equals(bytes, f.bytes);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    @Override
    public String toString() {
        return "FunctionRecord[" + cid.toText(org.zedec.zxv.cid.CidBase.BASE58BTC) + " " + intent + " " + entry + "]";
    }

    /** Builds a {@link FunctionRecord}; every rule is checked by libzxv in {@link #build()}. */
    public static final class Builder {
        private Intent intent = Intent.FN;
        private VerifyMode verify = VerifyMode.LOCAL;
        private Privacy privacy = Privacy.PUBLIC;
        private Determinism determinism = Determinism.DETERMINISTIC;
        private int quorumN, quorumK;
        private long fuelMax;
        private Cid module, contract;
        private String entry, checker, certifier;

        private Builder() {}

        /** What the function is for (default FN). */
        public Builder intent(Intent v) {
            intent = v;
            return this;
        }

        /** The Z-Wasm module's CID (CIDv1 raw sha2-256). Required. */
        public Builder module(Cid v) {
            module = v;
            return this;
        }

        /** The z-contract text's CID (CIDv1 raw sha2-256), or null for none. */
        public Builder contract(Cid v) {
            contract = v;
            return this;
        }

        /** The export computing the result. Required. */
        public Builder entry(String v) {
            entry = v;
            return this;
        }

        /** The most fuel one run may use; above 0 (uint64). Required. */
        public Builder fuelMax(long v) {
            fuelMax = v;
            return this;
        }

        /** Whether it may leave the node (default PUBLIC). */
        public Builder privacy(Privacy v) {
            privacy = v;
            return this;
        }

        /** Whether it is deterministic (default DETERMINISTIC). */
        public Builder determinism(Determinism v) {
            determinism = v;
            return this;
        }

        /** Never accept a remote result (the default). */
        public Builder verifyLocal() {
            return mode(VerifyMode.LOCAL, null, null, 0, 0);
        }

        /** A checker export judges (input, output). */
        public Builder verifyByChecker(String checkerExport) {
            return mode(VerifyMode.CHECKER, checkerExport, null, 0, 0);
        }

        /** A checker export judges, with a certificate from a certifier export. */
        public Builder verifyByCertificate(String checkerExport, String certifierExport) {
            return mode(VerifyMode.CERT, checkerExport, certifierExport, 0, 0);
        }

        /** k of n executors must agree byte for byte (2 &lt;= n &lt;= 8, n/2 &lt; k &lt;= n; needs DETERMINISTIC). */
        public Builder verifyByQuorum(int n, int k) {
            return mode(VerifyMode.QUORUM, null, null, n, k);
        }

        private Builder mode(VerifyMode m, String ch, String ce, int n, int k) {
            verify = m;
            checker = ch;
            certifier = ce;
            quorumN = n;
            quorumK = k;
            return this;
        }

        /**
         * Encodes the record.
         *
         * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG if a field breaks a rule (see {@link #encode})
         */
        public FunctionRecord build() {
            return encode(intent, verify, privacy, determinism, quorumN, quorumK, fuelMax, module, contract, entry,
                    checker, certifier);
        }
    }
}
