<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ExpiredException;
use Zedec\Zxv\Exception\VerificationFailedException;

/**
 * ABI 1.6: a wallet's approval of one payment request, an immutable value:
 * the wallet's ML-DSA-87 signature (empty FIPS 204 context) over
 * VinoReceipt::message(). The app receives it from the user's wallet and
 * checks it offline with verify(); no capability and no network are needed.
 *
 *     $receipt = new VinoReceipt($requestBytes, $walletPk, $approvedAt, $sig);
 *     $receipt->verify(time());    // throws unless genuine and in time
 *
 * ML-DSA-87 here is the reference implementation, not FIPS 140-3 validated.
 */
final class VinoReceipt
{
    /** The 160 request bytes this receipt approves. */
    public readonly string $request;
    /** The wallet's ML-DSA-87 public key (2592 bytes). */
    public readonly string $walletPublicKey;
    /** When the wallet approved it, on the request's clock. */
    public readonly int|string $approvedAt;
    /** The 4627-byte ML-DSA-87 signature. */
    public readonly string $signature;

    /**
     * A receipt as received from a wallet. Nothing is checked until verify()
     * or isValid().
     * @param string|VinoPaymentRequest $request the encoded request (160 bytes), or the request itself
     * @throws Exception\ZxvUsageException ZXV_E_ARG (wrong byte sizes)
     */
    public function __construct(string|VinoPaymentRequest $request, string $walletPublicKey, int|string|float $approvedAt, string $signature)
    {
        $this->request = self::requestBytes($request);
        $this->walletPublicKey = Native::exact($walletPublicKey, "walletPublicKey", Vino::PUBLIC_KEY_BYTES);
        $this->approvedAt = Native::u64Out(Native::u64($approvedAt, "approvedAt"));
        $this->signature = Native::exact($signature, "signature", Vino::SIGNATURE_BYTES);
    }

    /**
     * The 88 bytes a wallet signs to approve request $requestId with key
     * $walletPublicKey at $approvedAt: the tag "zxv/vino-rcpt/v1", the
     * request id, SHA-256 of the wallet key, approvedAt (8 bytes LE).
     * @param string $requestId 32 bytes (VinoPaymentRequest::id())
     * @param string $walletPublicKey 2592 bytes
     */
    public static function message(string $requestId, string $walletPublicKey, int|string|float $approvedAt): string
    {
        $out = Native::new("uint8_t[" . Vino::RECEIPT_MSG_BYTES . "]");
        Native::check(Native::ffi16()->zxv_vino_receipt_msg(Native::fixed($requestId, "requestId", Vino::REQUEST_ID_BYTES),
            Native::fixed($walletPublicKey, "walletPublicKey", Vino::PUBLIC_KEY_BYTES), Native::u64($approvedAt, "approvedAt"), $out));
        return Native::read($out, Vino::RECEIPT_MSG_BYTES);
    }

    /**
     * Wallet side: approve $request at $approvedAt (which must lie in the
     * request's window) and sign with the wallet's key. This is the wallet's
     * call, not the app's: an app never holds a wallet key.
     *
     * The secret key stays in its SecretKey (off the PHP heap); it is used
     * for this one call and not kept by the returned receipt.
     * @param Capability $capability needs Capability::CRYPTO_SIGN
     * @param SecretKey $walletSecretKey 4896 bytes (MlDsa87::generate())
     * @throws ExpiredException $approvedAt is outside [createdAt, expiresAt]
     * @throws Exception\CapabilityDeniedException without CRYPTO_SIGN
     * @throws Exception\NotImplementedException the library was built without PQC
     * @throws Exception\ZxvUsageException|Exception\ChanceRefusedException the request bytes do not decode
     */
    public static function sign(Capability $capability, string|VinoPaymentRequest $request, SecretKey $walletSecretKey, string $walletPublicKey,
        int|string|float $approvedAt): self
    {
        $sk = SecretKey::expect($walletSecretKey, Vino::SECRET_KEY_BYTES);
        $req = self::requestBytes($request);
        $pk = Native::exact($walletPublicKey, "walletPublicKey", Vino::PUBLIC_KEY_BYTES);
        $at = Native::u64($approvedAt, "approvedAt");
        $sig = Native::new("uint8_t[" . Vino::SIGNATURE_BYTES . "]");
        Native::check(Native::ffi16()->zxv_vino_receipt_sign($capability->ptr(), Native::fixed($req, "request", Vino::REQUEST_BYTES), $sk,
            Native::fixed($pk, "walletPublicKey", Vino::PUBLIC_KEY_BYTES), $at, $sig), $capability->context);
        return new self($req, $pk, Native::u64Out($at), Native::read($sig, Vino::SIGNATURE_BYTES));
    }

    /**
     * App side, offline: check that this is the wallet's signature over the
     * request and approval time, that approvedAt lies within the request's
     * window, and that $now is not past its expiry.
     * @param int|string $now the current time on the request's clock
     * @throws VerificationFailedException not that wallet's signature over this request and time
     * @throws ExpiredException expired, or approved outside the window
     * @throws Exception\ZxvUsageException|Exception\ChanceRefusedException the request bytes do not decode
     * @throws Exception\NotImplementedException the library was built without PQC
     */
    public function verify(int|string|float $now): self
    {
        Native::check(Native::ffi16()->zxv_vino_receipt_verify(Native::fixed($this->request, "request", Vino::REQUEST_BYTES),
            Native::fixed($this->walletPublicKey, "walletPublicKey", Vino::PUBLIC_KEY_BYTES), Native::u64($this->approvedAt, "approvedAt"),
            Native::fixed($this->signature, "signature", Vino::SIGNATURE_BYTES), Native::u64($now, "now")));
        return $this;
    }

    /**
     * Like verify(), but answers false (instead of throwing) when the
     * signature does not verify or the request has expired. Other errors
     * (malformed request bytes, no PQC) still throw.
     */
    public function isValid(int|string|float $now): bool
    {
        try {
            $this->verify($now);
            return true;
        } catch (VerificationFailedException | ExpiredException) {
            return false;
        }
    }

    /** The id of the request this receipt approves (SHA-256 of $request). Throws as VinoPaymentRequest::decode(). */
    public function requestId(): string
    {
        return VinoPaymentRequest::idOf($this->request);
    }

    /** The decoded request this receipt approves. Throws as VinoPaymentRequest::decode(). */
    public function paymentRequest(): VinoPaymentRequest
    {
        return VinoPaymentRequest::decode($this->request);
    }

    private static function requestBytes(string|VinoPaymentRequest $request): string
    {
        return $request instanceof VinoPaymentRequest ? $request->encode() : Native::exact($request, "request", Vino::REQUEST_BYTES);
    }
}
