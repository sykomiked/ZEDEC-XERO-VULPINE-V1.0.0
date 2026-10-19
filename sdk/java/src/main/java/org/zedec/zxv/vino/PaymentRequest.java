// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.Objects;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native16;

/**
 * A payment request: what an app asks a user's wallet to approve. A plain value; building one,
 * encoding it and decoding it are pure (no capability, no handle, no network).
 *
 * <p><b>How it is used.</b> On the {@link Lane#LIVE} lane, real Vino stays in the user's own ZXV
 * wallet and never in the app. The app builds a request, {@link #encode() encodes} it to
 * {@value Vino#REQUEST_BYTES} canonical bytes and hands them to the wallet. The user approves it
 * there, and the wallet returns a signed {@link Receipt}, which the app checks offline with
 * {@link Receipt#verify}. The app never sees a balance, a key or a custody account. On the
 * {@link Lane#PLAY} lane the same request format names an amount of play units, which are not money.
 *
 * <p><b>Fields.</b>
 * <ul>
 *   <li>{@code lane}: {@link Lane#PLAY} or {@link Lane#LIVE}.</li>
 *   <li>{@code flags}: a bit set; the only known flag is {@link #FLAG_CHANCE}. An unknown bit is
 *       refused ({@code ZXV_E_FORMAT}).</li>
 *   <li>{@code merchant}: the payee's 32-byte id (for example SHA-256 of its key).</li>
 *   <li>{@code nonce}: 16 bytes, unique per request, so two otherwise equal requests differ.</li>
 *   <li>{@code amount}: 1 to {@link Vino#AMOUNT_MAX}; VFV minor units on LIVE, play units on PLAY
 *       (unsigned 64-bit).</li>
 *   <li>{@code createdAt}, {@code expiresAt}: seconds of the caller's clock (Unix time by
 *       convention), with {@code expiresAt > createdAt} (unsigned 64-bit).</li>
 *   <li>{@code memo}: 0 to {@value Vino#MEMO_MAX} characters of printable ASCII (0x20 to 0x7e),
 *       shown to the user; never null (null becomes "").</li>
 * </ul>
 *
 * <p>The constructor only checks shapes (non-null, array lengths); every value rule is checked by
 * libzxv when the request is encoded, so the rules live in one place. A request for a paid chance
 * outcome (a loot box or a paid draw) on the LIVE lane is refused with {@link ChanceException}.
 *
 * <p>Arrays are copied in and out, so a request is immutable. {@code equals} compares contents.
 *
 * <pre>{@code
 * PaymentRequest req = PaymentRequest.live(merchantId, nonce, 1_250, now, now + 600, "Sword of Dawn");
 * byte[] wire = req.encode();     // 160 bytes for the wallet
 * byte[] id = req.id();           // SHA-256 of those bytes
 * }</pre>
 *
 * @param lane      the lane
 * @param flags     the flag bits ({@link #FLAG_CHANCE} or 0)
 * @param merchant  the payee's id, {@value Vino#MERCHANT_BYTES} bytes
 * @param nonce     a unique nonce, {@value Vino#NONCE_BYTES} bytes
 * @param amount    the amount, 1 to {@link Vino#AMOUNT_MAX} (unsigned 64-bit)
 * @param createdAt when the request was made, in seconds (unsigned 64-bit)
 * @param expiresAt when it expires, in seconds, above {@code createdAt} (unsigned 64-bit)
 * @param memo      0 to 64 printable ASCII characters; null is treated as ""
 */
public record PaymentRequest(Lane lane, int flags, byte[] merchant, byte[] nonce, long amount, long createdAt,
        long expiresAt, String memo) {
    /**
     * The purchase has a paid chance outcome ({@code ZXV_VINO_REQ_CHANCE}): a loot box, a paid draw.
     * Allowed on the PLAY lane only; a LIVE request with it is refused ({@link ChanceException}).
     */
    public static final int FLAG_CHANCE = 0x1;

    /**
     * Checks shapes and copies the arrays.
     *
     * @throws NullPointerException             when {@code lane}, {@code merchant} or {@code nonce} is null
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) when an array has the wrong length
     */
    public PaymentRequest {
        Objects.requireNonNull(lane, "lane");
        Ffi.requireLength("merchant", merchant, Vino.MERCHANT_BYTES);
        Ffi.requireLength("nonce", nonce, Vino.NONCE_BYTES);
        merchant = merchant.clone();
        nonce = nonce.clone();
        memo = memo == null ? "" : memo;
    }

    /**
     * A LIVE request with no flags: real Vino, approved in the user's wallet.
     *
     * @param merchant  the payee's id, 32 bytes
     * @param nonce     a unique nonce, 16 bytes
     * @param amount    VFV minor units, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @param createdAt when the request was made, in seconds
     * @param expiresAt when it expires, in seconds, above {@code createdAt}
     * @param memo      0 to 64 printable ASCII characters, or null
     * @return the request (not yet validated: {@link #encode()} validates)
     */
    public static PaymentRequest live(byte[] merchant, byte[] nonce, long amount, long createdAt, long expiresAt,
            String memo) {
        return new PaymentRequest(Lane.LIVE, 0, merchant, nonce, amount, createdAt, expiresAt, memo);
    }

    /**
     * A PLAY request with no flags: an amount of play units, which are not money.
     *
     * @param merchant  the payee's id, 32 bytes
     * @param nonce     a unique nonce, 16 bytes
     * @param amount    play units, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @param createdAt when the request was made, in seconds
     * @param expiresAt when it expires, in seconds, above {@code createdAt}
     * @param memo      0 to 64 printable ASCII characters, or null
     * @return the request (not yet validated: {@link #encode()} validates)
     */
    public static PaymentRequest play(byte[] merchant, byte[] nonce, long amount, long createdAt, long expiresAt,
            String memo) {
        return new PaymentRequest(Lane.PLAY, 0, merchant, nonce, amount, createdAt, expiresAt, memo);
    }

    /**
     * The payee's id.
     *
     * @return a copy of the 32 bytes
     */
    @Override
    public byte[] merchant() {
        return merchant.clone();
    }

    /**
     * The request's nonce.
     *
     * @return a copy of the 16 bytes
     */
    @Override
    public byte[] nonce() {
        return nonce.clone();
    }

    /**
     * Whether {@link #FLAG_CHANCE} is set.
     *
     * @return true when the request marks a paid chance outcome
     */
    public boolean isPaidChance() {
        return (flags & FLAG_CHANCE) != 0;
    }

    /**
     * Encodes the request to its canonical {@value Vino#REQUEST_BYTES}-byte form, checking every rule.
     * The layout is fixed ("ZXVR", version 1, lane, flags, merchant, nonce, amount, times,
     * memo; little-endian numbers; zero padding), so the same request always gives the same bytes.
     *
     * @return the 160 bytes
     * @throws ChanceException                  for a LIVE request with {@link #FLAG_CHANCE}
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_RANGE}) when the amount is 0 or above
     *                                          {@link Vino#AMOUNT_MAX}, or {@code expiresAt <= createdAt};
     *                                          ({@code ZXV_E_FORMAT}) for an unknown flag or a memo
     *                                          that is not printable ASCII; ({@code ZXV_E_TOO_LONG})
     *                                          for a memo above 64 bytes
     */
    public byte[] encode() {
        return encodeTo(false)[0];
    }

    /**
     * The request's id: SHA-256 of its encoding. A wallet's receipt names the request by this id.
     *
     * @return the 32-byte id
     * @throws org.zedec.zxv.ZxvException as {@link #encode()}
     */
    public byte[] id() {
        return encodeTo(true)[1];
    }

    private byte[][] encodeTo(boolean wantId) {
        byte[] memoBytes = memo.getBytes(StandardCharsets.UTF_8);
        for (byte b : memoBytes) {
            // A NUL would silently end the C string early; refuse it like any other non-printable byte.
            if (b == 0) throw Errors.raise(-22, "the memo contains a NUL character", null);
        }
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Vino.REQUEST_BYTES), id = a.allocate(Vino.REQUEST_ID_BYTES);
            Ffi.check(Ffi.call(Native16.vino_request_encode, lane.code(), flags, Buf.in(a, merchant),
                    Buf.in(a, nonce), amount, createdAt, expiresAt, Ffi.cstr(a, memo), out, id), null);
            return new byte[][] {Buf.read(out, Vino.REQUEST_BYTES), Buf.read(id, Vino.REQUEST_ID_BYTES)};
        }
    }

    /**
     * Decodes and validates request bytes: the same rules as {@link #encode()}, plus the magic
     * "ZXVR", the version and the zero padding.
     *
     * @param bytes exactly {@value Vino#REQUEST_BYTES} bytes
     * @return the request; {@code decode(r.encode()).equals(r)} holds for every valid {@code r}
     * @throws LaneException                    when the lane byte is not 1 or 2
     * @throws ChanceException                  for a LIVE request with {@link #FLAG_CHANCE}
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_FORMAT}) for a bad magic, version,
     *                                          flag, memo or padding; ({@code ZXV_E_RANGE}) or
     *                                          ({@code ZXV_E_TOO_LONG}) as for encode;
     *                                          ({@code ZXV_E_ARG}) when {@code bytes} is not 160 bytes
     */
    public static PaymentRequest decode(byte[] bytes) {
        Ffi.requireLength("request", bytes, Vino.REQUEST_BYTES);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment lane = a.allocate(JAVA_INT), flags = a.allocate(JAVA_INT),
                    merchant = a.allocate(Vino.MERCHANT_BYTES), nonce = a.allocate(Vino.NONCE_BYTES),
                    amount = a.allocate(JAVA_LONG), created = a.allocate(JAVA_LONG), expires = a.allocate(JAVA_LONG),
                    memo = a.allocate(Vino.MEMO_MAX + 1);
            Ffi.check(Ffi.call(Native16.vino_request_decode, Buf.in(a, bytes), lane, flags, merchant, nonce, amount,
                    created, expires, memo, MemorySegment.NULL), null);
            return new PaymentRequest(Lane.fromCode(lane.get(JAVA_INT, 0)), flags.get(JAVA_INT, 0),
                    Buf.read(merchant, Vino.MERCHANT_BYTES), Buf.read(nonce, Vino.NONCE_BYTES),
                    amount.get(JAVA_LONG, 0), created.get(JAVA_LONG, 0), expires.get(JAVA_LONG, 0),
                    memo.getString(0, StandardCharsets.US_ASCII));
        }
    }

    /**
     * The id of encoded request bytes (SHA-256 of them), after validating them as
     * {@link #decode(byte[])} does.
     *
     * @param bytes exactly {@value Vino#REQUEST_BYTES} bytes
     * @return the 32-byte id
     * @throws org.zedec.zxv.ZxvException as {@link #decode(byte[])}
     */
    public static byte[] idOf(byte[] bytes) {
        Ffi.requireLength("request", bytes, Vino.REQUEST_BYTES);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(Vino.REQUEST_ID_BYTES);
            MemorySegment n = MemorySegment.NULL;
            Ffi.check(Ffi.call(Native16.vino_request_decode, Buf.in(a, bytes), n, n, n, n, n, n, n, n, id), null);
            return Buf.read(id, Vino.REQUEST_ID_BYTES);
        }
    }

    /**
     * Compares every field, arrays by content.
     *
     * @param o the other object
     * @return true when {@code o} is a request with the same contents
     */
    @Override
    public boolean equals(Object o) {
        return o instanceof PaymentRequest r && lane == r.lane && flags == r.flags && amount == r.amount
                && createdAt == r.createdAt && expiresAt == r.expiresAt && Arrays.equals(merchant, r.merchant)
                && Arrays.equals(nonce, r.nonce) && memo.equals(r.memo);
    }

    /**
     * A hash of every field, arrays by content.
     *
     * @return the hash code
     */
    @Override
    public int hashCode() {
        return Objects.hash(lane, flags, amount, createdAt, expiresAt, Arrays.hashCode(merchant),
                Arrays.hashCode(nonce), memo);
    }

    /**
     * A readable form, with the byte fields in hex and numbers unsigned.
     *
     * @return the text
     */
    @Override
    public String toString() {
        HexFormat hex = HexFormat.of();
        return "PaymentRequest[lane=" + lane + ", flags=" + flags + ", merchant=" + hex.formatHex(merchant)
                + ", nonce=" + hex.formatHex(nonce) + ", amount=" + Long.toUnsignedString(amount) + ", createdAt="
                + Long.toUnsignedString(createdAt) + ", expiresAt=" + Long.toUnsignedString(expiresAt) + ", memo="
                + memo + "]";
    }
}
