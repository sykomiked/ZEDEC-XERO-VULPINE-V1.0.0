<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** FIPS 203 ML-KEM-1024 key encapsulation (not FIPS 140-3 validated). */
final class MlKem1024
{
    public const ENCAPSULATION_KEY_BYTES = 1568;
    public const DECAPSULATION_KEY_BYTES = 3168;
    public const CIPHERTEXT_BYTES = 1568;
    public const SHARED_SECRET_BYTES = 32;

    /** @param Capability $capability needs CRYPTO_KEM */
    public function __construct(public readonly Capability $capability)
    {
    }

    /**
     * publicKey = encapsulation key, secretKey = decapsulation key;
     * deterministic from 32-byte $d and $z when both are given (KATs).
     */
    public function generate(?string $d = null, ?string $z = null): KeyPair
    {
        $ek = Native::new("uint8_t[" . self::ENCAPSULATION_KEY_BYTES . "]");
        $dk = new SecretKey(self::DECAPSULATION_KEY_BYTES);
        $rc = ($d === null && $z === null)
            ? Native::ffi()->zxv_mlkem1024_keygen($this->capability->ptr(), $ek, $dk->pointer())
            : Native::ffi()->zxv_mlkem1024_keygen_derand($this->capability->ptr(), Native::bytes(Native::exact((string) $d, "d", 32)), Native::bytes(Native::exact((string) $z, "z", 32)), $ek, $dk->pointer());
        Native::check($rc, $this->capability->context);
        return new KeyPair(Native::read($ek, self::ENCAPSULATION_KEY_BYTES), $dk);
    }

    /**
     * Encapsulate; deterministic from a 32-byte $m when given.
     * @return array{ciphertext:string, sharedSecret:SecretKey}
     */
    public function encapsulate(string $encapsulationKey, ?string $m = null): array
    {
        $ek = Native::bytes(Native::exact($encapsulationKey, "encapsulationKey", self::ENCAPSULATION_KEY_BYTES));
        $ct = Native::new("uint8_t[" . self::CIPHERTEXT_BYTES . "]");
        $ss = new SecretKey(self::SHARED_SECRET_BYTES);
        $rc = $m === null
            ? Native::ffi()->zxv_mlkem1024_encaps($this->capability->ptr(), $ek, $ct, $ss->pointer())
            : Native::ffi()->zxv_mlkem1024_encaps_derand($this->capability->ptr(), $ek, Native::bytes(Native::exact($m, "m", 32)), $ct, $ss->pointer());
        Native::check($rc, $this->capability->context);
        return ["ciphertext" => Native::read($ct, self::CIPHERTEXT_BYTES), "sharedSecret" => $ss];
    }

    /** Recover the shared secret. */
    public function decapsulate(SecretKey $decapsulationKey, string $ciphertext): SecretKey
    {
        $ss = new SecretKey(self::SHARED_SECRET_BYTES);
        Native::check(Native::ffi()->zxv_mlkem1024_decaps($this->capability->ptr(), SecretKey::expect($decapsulationKey, self::DECAPSULATION_KEY_BYTES), Native::bytes(Native::exact($ciphertext, "ciphertext", self::CIPHERTEXT_BYTES)), $ss->pointer()), $this->capability->context);
        return $ss;
    }
}
