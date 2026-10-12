// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.cid;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.HexFormat;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * An IPFS content identifier (ABI 1.5): an immutable value naming bytes by
 * WHAT they are (a hash) instead of WHERE they live. It always holds exactly
 * one canonical CIDv0 or CIDv1 with a sha2-256 or identity multihash, in its
 * binary form ({@link #bytes()}, at most {@value #MAX_BYTES} bytes); anything
 * else is refused with {@link CidException} when the value is made.
 *
 * <p>The CIDs are byte for byte the ones Kubo makes (libzxv's tests check them
 * against Kubo 0.32.1): {@link #compute} is {@code ipfs block put} (one block,
 * any codec), {@link #unixfs} is {@code ipfs add --cid-version=1} (256 KiB
 * chunks, raw leaves, balanced DAG of 174 links), and the two agree for data
 * of at most 262,144 bytes. {@link #toString()} prints what Kubo prints.
 *
 * <p>Honest limits: a CID proves which bytes are meant. It does not make
 * bytes available (a CID resolves only while someone holds them), and it does
 * not prove that a claimed output of a function is right. libzxv has no
 * network transport. Pure: no context, no capability. Thread-safe.
 *
 * <pre>{@code
 * Cid c = Cid.raw("hello world".getBytes(UTF_8));
 * c.toString();                          // "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e"
 * c.toText(CidBase.BASE58BTC);           // "zb2rh..."
 * c.matches(bytes);                      // re-hashes the bytes
 * Cid.parse("QmT78z...").toV1();         // the dag-pb CIDv1 of a CIDv0
 * }</pre>
 */
public final class Cid {
    /** Longest binary CID accepted or produced ({@code ZXV_CID_MAX}). */
    public static final int MAX_BYTES = 80;
    /** Longest CID text, excluding a NUL ({@code ZXV_CID_TEXT_MAX}). */
    public static final int TEXT_MAX = 139;
    /** Longest multihash digest, for identity CIDs ({@code ZXV_CID_DIGEST_MAX}). */
    public static final int DIGEST_MAX = 64;

    private final byte[] bytes;
    private final int version;
    private final long codec;
    private final int multihash;
    private final byte[] digest;

    private Cid(byte[] bytes, int version, long codec, int multihash, byte[] digest) {
        this.bytes = bytes;
        this.version = version;
        this.codec = codec;
        this.multihash = multihash;
        this.digest = digest;
    }

    /**
     * The CID whose binary form is {@code bytes} (checked: version, codec and
     * multihash; a copy is kept).
     *
     * @throws CidException if the bytes are not exactly one canonical CID
     */
    public static Cid of(byte[] bytes) {
        Buf.nonNull("bytes", bytes);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ver = a.allocate(JAVA_INT), codec = a.allocate(JAVA_INT), hash = a.allocate(JAVA_INT),
                    dig = a.allocate(DIGEST_MAX), dlen = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.cid_info, Buf.in(a, bytes), (long) bytes.length, ver, codec, hash, dig, dlen), null);
            return new Cid(bytes.clone(), ver.get(JAVA_INT, 0), Integer.toUnsignedLong(codec.get(JAVA_INT, 0)),
                    hash.get(JAVA_INT, 0), Buf.read(dig, dlen.get(JAVA_LONG, 0)));
        }
    }

    /**
     * Parses CID text: "b..." (CIDv1, base32 lower case), "Qm..." (CIDv0) or
     * "z..." (CIDv1, base58btc). Only the canonical text is accepted: no upper
     * case, padding, other multibases or extra leading zeros.
     *
     * @throws CidException if the text is not a canonical CID
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for "", ZXV_E_TOO_LONG past {@link #TEXT_MAX}
     */
    public static Cid parse(String text) {
        Buf.nonNull("text", text);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(MAX_BYTES), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.cid_from_text, Ffi.cstr(a, text), c, n), null);
            return of(Buf.read(c, n.get(JAVA_LONG, 0)));
        }
    }

    /**
     * The CIDv1 of {@code data} as ONE block: sha2-256 under multicodec
     * {@code codec} (Kubo {@code ipfs block put}).
     *
     * @param codec a multicodec, e.g. {@link Multicodec#RAW} (uint32)
     * @param data  the block (at most 2^32 - 1 bytes)
     */
    public static Cid compute(long codec, byte[] data) {
        Buf.nonNull("data", data);
        if (codec < 0 || codec > 0xFFFFFFFFL) throw Errors.usage(-1, "codec " + codec + " is not a uint32");
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(MAX_BYTES), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.cid_compute, (int) codec, Buf.in(a, data), (long) data.length, c, n), null);
            return of(Buf.read(c, n.get(JAVA_LONG, 0)));
        }
    }

    /** {@link #compute} with {@link Multicodec#RAW}: the CID of plain bytes. */
    public static Cid raw(byte[] data) {
        return compute(Multicodec.RAW, data);
    }

    /**
     * The root CID Kubo gives {@code data} as a file ({@code ipfs add
     * --cid-version=1} with default settings): a raw CID up to 262,144 bytes,
     * a dag-pb root above. The blocks themselves are not kept.
     */
    public static Cid unixfs(byte[] data) {
        Buf.nonNull("data", data);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(MAX_BYTES), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.cid_unixfs, Buf.in(a, data), (long) data.length, c, n), null);
            return of(Buf.read(c, n.get(JAVA_LONG, 0)));
        }
    }

    /** The binary form (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The binary length in bytes. */
    public int length() {
        return bytes.length;
    }

    /** 0 or 1. */
    public int version() {
        return version;
    }

    /** The multicodec (uint32), e.g. {@link Multicodec#RAW}; every CIDv0 is {@link Multicodec#DAG_PB}. */
    public long codec() {
        return codec;
    }

    /** The multihash function: {@link Multicodec#HASH_SHA2_256} or {@link Multicodec#HASH_IDENTITY}. */
    public int multihash() {
        return multihash;
    }

    /** The multihash digest (a copy): 32 bytes for sha2-256, the content itself for identity. */
    public byte[] digest() {
        return digest.clone();
    }

    /** The text form Kubo prints ({@link CidBase#DEFAULT}). */
    @Override
    public String toString() {
        return toText(CidBase.DEFAULT);
    }

    /**
     * The text form in {@code base}.
     *
     * @throws CidException for a CIDv0 in {@link CidBase#BASE58BTC}, which has no multibase form
     */
    public String toText(CidBase base) {
        Buf.nonNull("base", base);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = Buf.in(a, bytes);
            return Ffi.text(null, (b, cap, n) -> Ffi.call(Native15.cid_to_text, c, (long) bytes.length, base.code(), b, cap, n));
        }
    }

    /**
     * This CID as CIDv1 (a CIDv0 becomes dag-pb CIDv1 with the same hash, as
     * Kubo's {@code ipfs cid base32}); a CIDv1 is returned unchanged.
     */
    public Cid toV1() {
        if (version == 1) return this;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(MAX_BYTES), n = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.cid_to_v1, Buf.in(a, bytes), (long) bytes.length, c, n), null);
            return of(Buf.read(c, n.get(JAVA_LONG, 0)));
        }
    }

    /**
     * True if {@code data} is exactly the block this CID names (re-hashed;
     * identity CIDs compare the bytes). A UnixFS root (dag-pb) names its root
     * BLOCK, not the file: check a file with {@code Cid.unixfs(file).equals(cid)}.
     */
    public boolean matches(byte[] data) {
        Buf.nonNull("data", data);
        try (Arena a = Arena.ofConfined()) {
            int st = Ffi.call(Native15.cid_verify, Buf.in(a, bytes), (long) bytes.length, Buf.in(a, data), (long) data.length);
            if (st == -34) return false;
            Ffi.check(st, null);
            return true;
        }
    }

    /**
     * Like {@link #matches} but throws.
     *
     * @throws CidException if {@code data} is not the block this CID names
     */
    public void verify(byte[] data) {
        Buf.nonNull("data", data);
        try (Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.cid_verify, Buf.in(a, bytes), (long) bytes.length, Buf.in(a, data), (long) data.length), null);
        }
    }

    /**
     * The 32-byte Kademlia key under which providers of this CID are announced
     * on Vinea: SHA-256("zxv/aipi/v1/provider" || binary CID). Computing it
     * announces nothing: libzxv has no network.
     */
    public byte[] providerKey() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment k = a.allocate(32);
            Ffi.check(Ffi.call(Native15.aipi_provider_key, Buf.in(a, bytes), (long) bytes.length, k), null);
            return Buf.read(k, 32);
        }
    }

    /** The binary form as lower-case hex (for logs and tests). */
    public String toHex() {
        return HexFormat.of().formatHex(bytes);
    }

    /** Two CIDs are equal when their binary forms are (a CIDv0 never equals its CIDv1). */
    @Override
    public boolean equals(Object o) {
        return o instanceof Cid c && Arrays.equals(bytes, c.bytes);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }
}
