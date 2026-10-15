// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.Objects;
import org.zedec.zxv.Capability;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.KeyAccess;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native16;
import org.zedec.zxv.crypto.SecretKey;

/**
 * A wallet's receipt for a payment request: its ML-DSA-87 signature approving the request at a
 * given time. A plain value; checking it is pure and works offline.
 *
 * <p><b>What is signed.</b> The wallet signs {@value Vino#RECEIPT_MESSAGE_BYTES} bytes
 * ({@link #message}): the tag {@code "zxv/vino-rcpt/v1"}, the request id (SHA-256 of the encoded
 * request), SHA-256 of the wallet's public key, and {@code approvedAt} as 8 little-endian bytes,
 * with ML-DSA-87 and an empty FIPS 204 context. So a receipt binds exactly one request, one wallet
 * and one approval time.
 *
 * <p><b>Who does what.</b> {@link #sign} is the wallet's call (it holds the key and needs
 * {@link org.zedec.zxv.Permission#CRYPTO_SIGN}); an app never holds a user's key. The app only
 * calls {@link #verify}, which needs no capability. Approving a request is the app-facing half of a
 * payment only: the wallet posts the payment on its own ledger.
 *
 * <p><b>Honest limits.</b> ML-DSA-87 here is the reference implementation, not FIPS 140-3
 * validated. A library built without post-quantum crypto ({@code ZXV_NO_PQC}) refuses signing and
 * verifying with {@link org.zedec.zxv.NotImplementedException}.
 *
 * <p>Arrays are copied in and out, so a receipt is immutable. {@code equals} compares contents.
 *
 * @param request         the encoded request, {@value Vino#REQUEST_BYTES} bytes
 * @param walletPublicKey the wallet's ML-DSA-87 public key, {@value Vino#WALLET_PUBLIC_KEY_BYTES} bytes
 * @param approvedAt      when the wallet approved it, in the request's clock (unsigned 64-bit)
 * @param signature       the ML-DSA-87 signature, {@value Vino#SIGNATURE_BYTES} bytes
 */
public record Receipt(byte[] request, byte[] walletPublicKey, long approvedAt, byte[] signature) {
    /**
     * Checks lengths and copies the arrays.
     *
     * @throws NullPointerException             when an array is null
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) when an array has the wrong length
     */
    public Receipt {
        Ffi.requireLength("request", request, Vino.REQUEST_BYTES);
        Ffi.requireLength("walletPublicKey", walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES);
        Ffi.requireLength("signature", signature, Vino.SIGNATURE_BYTES);
        request = request.clone();
        walletPublicKey = walletPublicKey.clone();
        signature = signature.clone();
    }

    /**
     * The exact {@value Vino#RECEIPT_MESSAGE_BYTES} bytes a wallet signs to approve a request. Pure.
     * Most callers never need this: {@link #sign} and {@link #verify} build it themselves.
     *
     * @param requestId       the request's id, 32 bytes ({@link PaymentRequest#id()})
     * @param walletPublicKey the wallet's ML-DSA-87 public key, 2592 bytes
     * @param approvedAt      the approval time (unsigned 64-bit)
     * @return the message bytes
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) when an array has the wrong length
     */
    public static byte[] message(byte[] requestId, byte[] walletPublicKey, long approvedAt) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Vino.RECEIPT_MESSAGE_BYTES);
            Ffi.check(Ffi.call(Native16.vino_receipt_msg, Buf.fixed(a, "requestId", requestId, Vino.REQUEST_ID_BYTES),
                    Buf.fixed(a, "walletPublicKey", walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES), approvedAt, out),
                    null);
            return Buf.read(out, Vino.RECEIPT_MESSAGE_BYTES);
        }
    }

    /**
     * Wallet side: approves a request at {@code approvedAt} and signs the receipt with the wallet's
     * ML-DSA-87 key. Signing is hedged (FIPS 204 with fresh randomness), so signing the same
     * approval twice gives two different signatures; both verify.
     *
     * <p>The secret key is read straight from its off-heap storage for the length of the call; it is
     * not copied, kept or logged. It stays the caller's to close.
     *
     * @param cap             a capability holding {@link org.zedec.zxv.Permission#CRYPTO_SIGN}
     * @param request         the encoded request, 160 bytes (validated as {@link PaymentRequest#decode})
     * @param walletSecretKey the wallet's ML-DSA-87 secret key ({@link SecretKey.Kind#MLDSA87})
     * @param walletPublicKey the matching public key, 2592 bytes
     * @param approvedAt      the approval time, within {@code [createdAt, expiresAt]} of the request
     * @return the receipt
     * @throws ExpiredException                         when {@code approvedAt} is outside the request's window
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code CRYPTO_SIGN}
     * @throws org.zedec.zxv.NotImplementedException   in a build without post-quantum crypto
     * @throws org.zedec.zxv.ZxvException               any refusal of {@link PaymentRequest#decode}
     * @throws IllegalStateException                    when the key or the capability is closed
     */
    public static Receipt sign(Capability cap, byte[] request, SecretKey walletSecretKey, byte[] walletPublicKey,
            long approvedAt) {
        Objects.requireNonNull(walletSecretKey, "walletSecretKey");
        KeyAccess.Keys keys = KeyAccess.keys();
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment req = Buf.fixed(a, "request", request, Vino.REQUEST_BYTES);
            MemorySegment pk = Buf.fixed(a, "walletPublicKey", walletPublicKey, Vino.WALLET_PUBLIC_KEY_BYTES);
            MemorySegment sig = a.allocate(Vino.SIGNATURE_BYTES);
            MemorySegment sk = keys.acquire(walletSecretKey, SecretKey.Kind.MLDSA87);
            int st;
            try {
                st = Ffi.call(Native16.vino_receipt_sign, l.h(0), req, sk, pk, approvedAt, sig);
            } finally {
                keys.release(walletSecretKey);
            }
            Ffi.check(st, Caps.ctx(cap));
            return new Receipt(request, walletPublicKey, approvedAt, Buf.read(sig, Vino.SIGNATURE_BYTES));
        }
    }

    /**
     * The request's id (SHA-256 of {@link #request()}), validating the request bytes.
     *
     * @return the 32-byte id
     * @throws org.zedec.zxv.ZxvException any refusal of {@link PaymentRequest#decode}
     */
    public byte[] requestId() {
        return PaymentRequest.idOf(request);
    }

    /**
     * The request this receipt approves, decoded.
     *
     * @return the request
     * @throws org.zedec.zxv.ZxvException any refusal of {@link PaymentRequest#decode}
     */
    public PaymentRequest paymentRequest() {
        return PaymentRequest.decode(request);
    }

    /**
     * App side, offline: is this the wallet's signature over this request and approval time, with
     * the approval inside the request's window and {@code now} not past its expiry? Pure: no
     * capability, no network, no key other than the wallet's public key.
     *
     * @param now the app's current time, in the request's clock (unsigned 64-bit)
     * @return true when the receipt is valid; false when the signature is not the wallet's over this
     *         request and time ({@code ZXV_E_VERIFY})
     * @throws ExpiredException                       when {@code now} is past the request's expiry,
     *                                                or {@code approvedAt} is outside its window
     * @throws org.zedec.zxv.NotImplementedException in a build without post-quantum crypto
     * @throws org.zedec.zxv.ZxvException             any refusal of {@link PaymentRequest#decode}
     */
    public boolean verify(long now) {
        int st = verifyStatus(now);
        if (st == -21) return false;
        Ffi.check(st, null);
        return true;
    }

    /**
     * Like {@link #verify(long)}, but a bad signature throws instead of returning false.
     *
     * @param now the app's current time, in the request's clock (unsigned 64-bit)
     * @throws org.zedec.zxv.VerificationFailedException ({@code ZXV_E_VERIFY}) when the signature is
     *                                                   not the wallet's over this request and time
     * @throws ExpiredException                          as {@link #verify(long)}
     * @throws org.zedec.zxv.ZxvException                as {@link #verify(long)}
     */
    public void requireValid(long now) {
        Ffi.check(verifyStatus(now), null);
    }

    private int verifyStatus(long now) {
        try (Arena a = Arena.ofConfined()) {
            return Ffi.call(Native16.vino_receipt_verify, Buf.in(a, request), Buf.in(a, walletPublicKey), approvedAt,
                    Buf.in(a, signature), now);
        }
    }

    /**
     * The encoded request.
     *
     * @return a copy of the 160 bytes
     */
    @Override
    public byte[] request() {
        return request.clone();
    }

    /**
     * The wallet's public key.
     *
     * @return a copy of the 2592 bytes
     */
    @Override
    public byte[] walletPublicKey() {
        return walletPublicKey.clone();
    }

    /**
     * The signature.
     *
     * @return a copy of the 4627 bytes
     */
    @Override
    public byte[] signature() {
        return signature.clone();
    }

    /**
     * Compares every field, arrays by content.
     *
     * @param o the other object
     * @return true when {@code o} is a receipt with the same contents
     */
    @Override
    public boolean equals(Object o) {
        return o instanceof Receipt r && approvedAt == r.approvedAt && Arrays.equals(request, r.request)
                && Arrays.equals(walletPublicKey, r.walletPublicKey) && Arrays.equals(signature, r.signature);
    }

    /**
     * A hash of every field, arrays by content.
     *
     * @return the hash code
     */
    @Override
    public int hashCode() {
        return Objects.hash(approvedAt, Arrays.hashCode(request), Arrays.hashCode(walletPublicKey),
                Arrays.hashCode(signature));
    }

    /**
     * A short readable form (the approval time and sizes, not the bytes).
     *
     * @return the text
     */
    @Override
    public String toString() {
        return "Receipt[approvedAt=" + Long.toUnsignedString(approvedAt) + ", request=" + request.length
                + " bytes, signature=" + signature.length + " bytes]";
    }
}
