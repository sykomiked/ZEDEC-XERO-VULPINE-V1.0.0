<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** Hash functions (pure; no capability). Binary strings in and out. */
final class Hashes
{
    private function __construct()
    {
    }

    /** SHA3-256 (FIPS 202): 32 bytes. */
    public static function sha3_256(string $data): string
    {
        return self::digest("zxv_sha3_256", $data);
    }

    /** Ethereum Keccak-256 (pre-FIPS padding): 32 bytes. */
    public static function keccak256(string $data): string
    {
        return self::digest("zxv_keccak256", $data);
    }

    /** SHAKE256 with $length output bytes. */
    public static function shake256(string $data, int $length): string
    {
        Native::int($length, "length", 1, 1 << 24);
        $out = Native::new("uint8_t[$length]");
        Native::check(Native::ffi()->zxv_shake256(Native::bytes($data, true), strlen($data), $out, $length));
        return Native::read($out, $length);
    }

    private static function digest(string $fn, string $data): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi()->$fn(Native::bytes($data, true), strlen($data), $out));
        return Native::read($out, 32);
    }
}
