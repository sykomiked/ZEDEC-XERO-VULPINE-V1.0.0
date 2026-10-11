<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\VerificationFailedException;

/** RFC 8032 Ed25519 signatures. */
final class Ed25519
{
    public const PUBLIC_KEY_BYTES = 32;
    public const SECRET_KEY_BYTES = 32;
    public const SIGNATURE_BYTES = 64;

    /** @param Capability $capability needs CRYPTO_SIGN */
    public function __construct(public readonly Capability $capability)
    {
    }

    /** A fresh key pair (OS randomness). */
    public function generate(): KeyPair
    {
        $pk = Native::new("uint8_t[32]");
        $sk = new SecretKey(32);
        Native::check(Native::ffi()->zxv_ed25519_keygen($this->capability->ptr(), $pk, $sk->pointer()), $this->capability->context);
        return new KeyPair(Native::read($pk, 32), $sk);
    }

    public function publicKey(SecretKey $secretKey): string
    {
        $pk = Native::new("uint8_t[32]");
        Native::check(Native::ffi()->zxv_ed25519_public($this->capability->ptr(), SecretKey::expect($secretKey, 32), $pk), $this->capability->context);
        return Native::read($pk, 32);
    }

    /** A 64-byte signature. */
    public function sign(SecretKey $secretKey, string $message): string
    {
        $sig = Native::new("uint8_t[64]");
        Native::check(Native::ffi()->zxv_ed25519_sign($this->capability->ptr(), SecretKey::expect($secretKey, 32), Native::bytes($message, true), strlen($message), $sig), $this->capability->context);
        return Native::read($sig, 64);
    }

    public function verify(string $publicKey, string $message, string $signature): bool
    {
        $rc = Native::ffi()->zxv_ed25519_verify($this->capability->ptr(), Native::bytes(Native::exact($publicKey, "publicKey", 32)), Native::bytes($message, true), strlen($message), Native::bytes(Native::exact($signature, "signature", 64)));
        if ($rc === Status::E_VERIFY) {
            return false;
        }
        Native::check($rc, $this->capability->context);
        return true;
    }

    /** @throws VerificationFailedException when invalid */
    public function verifyOrThrow(string $publicKey, string $message, string $signature): void
    {
        if (!$this->verify($publicKey, $message, $signature)) {
            throw new VerificationFailedException(Status::E_VERIFY, "Ed25519 signature did not verify");
        }
    }
}
