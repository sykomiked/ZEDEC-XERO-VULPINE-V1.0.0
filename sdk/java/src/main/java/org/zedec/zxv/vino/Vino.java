// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

/**
 * The fixed sizes and limits of Vino Embed (ABI 1.6), as named constants. See the
 * {@link org.zedec.zxv.vino package overview} for what the group does and does not do.
 */
public final class Vino {
    private Vino() {}

    /** Largest amount, face, backing step or daily cap: 2^59 ({@code ZXV_VINO_AMOUNT_MAX}). */
    public static final long AMOUNT_MAX = 1L << 59;
    /** Most accounts (issuers plus holders) in one play sandbox ({@code ZXV_VINO_MAX_ACCOUNTS}). */
    public static final int MAX_ACCOUNTS = 256;
    /** Most active (unredeemed) vouchers in one play sandbox ({@code ZXV_VINO_MAX_VOUCHERS}). */
    public static final int MAX_VOUCHERS = 4096;
    /** Size of an encoded payment request ({@code ZXV_VINO_REQ_BYTES}). */
    public static final int REQUEST_BYTES = 160;
    /** Size of a request id: SHA-256 of the encoded request. */
    public static final int REQUEST_ID_BYTES = 32;
    /** Size of a merchant id in a request (for example SHA-256 of the merchant's key). */
    public static final int MERCHANT_BYTES = 32;
    /** Size of a request nonce. */
    public static final int NONCE_BYTES = 16;
    /** Longest memo: 64 bytes of printable ASCII, 0x20 to 0x7e ({@code ZXV_VINO_MEMO_MAX}). */
    public static final int MEMO_MAX = 64;
    /** Size of the message a wallet signs to approve a request ({@code ZXV_VINO_RCPT_MSG_BYTES}). */
    public static final int RECEIPT_MESSAGE_BYTES = 88;
    /** Size of a wallet's ML-DSA-87 public key. */
    public static final int WALLET_PUBLIC_KEY_BYTES = 2592;
    /** Size of a wallet's ML-DSA-87 secret key. */
    public static final int WALLET_SECRET_KEY_BYTES = 4896;
    /** Size of a receipt signature (ML-DSA-87). */
    public static final int SIGNATURE_BYTES = 4627;
}
