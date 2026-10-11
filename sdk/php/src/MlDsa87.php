<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\VerificationFailedException;

/** FIPS 204 ML-DSA-87 post-quantum signatures (not FIPS 140-3 validated). */
final class MlDsa87
{
    public const PUBLIC_KEY_BYTES = 2592;
    public const SECRET_KEY_BYTES = 4896;
    public const SIGNATURE_BYTES = 4627;
    public const CONTEXT_MAX = 255;

    /** @param Capability $capability needs CRYPTO_SIGN */
    public function __construct(public readonly Capability $capability)
    {
    }

    /** A key pair; deterministic from a 32-byte $seed when given. */
    public function generate(?string $seed = null): KeyPair
    {
        $pk = Native::new("uint8_t[" . self::PUBLIC_KEY_BYTES . "]");
        $sk = new SecretKey(self::SECRET_KEY_BYTES);
        $rc = $seed === null
            ? Native::ffi()->zxv_mldsa87_keygen($this->capability->ptr(), $pk, $sk->pointer())
            : Native::ffi()->zxv_mldsa87_keygen_seed($this->capability->ptr(), Native::bytes(Native::exact($seed, "seed", 32)), $pk, $sk->pointer());
        Native::check($rc, $this->capability->context);
        return new KeyPair(Native::read($pk, self::PUBLIC_KEY_BYTES), $sk);
    }

    /** Sign; hedged (fresh randomness) unless $deterministic. $context <= 255 bytes. */
    public function sign(SecretKey $secretKey, string $message, string $context = "", bool $deterministic = false): string
    {
        $sig = Native::new("uint8_t[" . self::SIGNATURE_BYTES . "]");
        $fn = $deterministic ? "zxv_mldsa87_sign_deterministic" : "zxv_mldsa87_sign";
        Native::check(Native::ffi()->$fn($this->capability->ptr(), SecretKey::expect($secretKey, self::SECRET_KEY_BYTES), Native::bytes($message, true), strlen($message), Native::bytes($context, true), strlen($context), $sig), $this->capability->context);
        return Native::read($sig, self::SIGNATURE_BYTES);
    }

    public function verify(string $publicKey, string $message, string $signature, string $context = ""): bool
    {
        $rc = Native::ffi()->zxv_mldsa87_verify($this->capability->ptr(), Native::bytes(Native::exact($publicKey, "publicKey", self::PUBLIC_KEY_BYTES)), Native::bytes($message, true), strlen($message), Native::bytes($context, true), strlen($context), Native::bytes(Native::exact($signature, "signature", self::SIGNATURE_BYTES)));
        if ($rc === Status::E_VERIFY) {
            return false;
        }
        Native::check($rc, $this->capability->context);
        return true;
    }

    /** @throws VerificationFailedException when invalid */
    public function verifyOrThrow(string $publicKey, string $message, string $signature, string $context = ""): void
    {
        if (!$this->verify($publicKey, $message, $signature, $context)) {
            throw new VerificationFailedException(Status::E_VERIFY, "ML-DSA-87 signature did not verify");
        }
    }
}
