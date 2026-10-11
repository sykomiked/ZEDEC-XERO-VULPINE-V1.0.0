<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvFatalError;

/**
 * Library entry point: version, features, random bytes and the fatal policy.
 *
 *     Zxv::loadLibrary("/opt/zxv/libzxv.so");   // optional
 *     $ctx = new Context();
 */
final class Zxv
{
    /** Feature bits of zxv_features, by name. */
    public const FEATURES = [
        "ledger" => 0x1, "mx_pacs008" => 0x2, "mx_camt053" => 0x4, "iso4217" => 0x8, "mx_pacs002" => 0x10,
        "mx_pacs004" => 0x20, "mx_pacs009" => 0x40, "mx_camt056" => 0x80, "mx_camt029" => 0x100,
        "netting" => 0x200, "cards" => 0x400, "card_check" => 0x800, "vss" => 0x1000, "rational" => 0x2000,
        "lpres" => 0x4000, "caps" => 0x8000, "rmag" => 0x10000, "phase" => 0x20000, "wire" => 0x40000,
        "swarm" => 0x80000, "hash" => 0x100000, "mlkem1024" => 0x200000, "mldsa87" => 0x400000,
        "ed25519" => 0x800000, "tensor" => 0x1000000, "lora" => 0x2000000, "evm" => 0x4000000, "hex" => 0x8000000,
    ];

    private static FatalPolicy $fatalPolicy = FatalPolicy::Throw;

    private function __construct()
    {
    }

    /** Load libzxv from an explicit path (otherwise it loads on first use). */
    public static function loadLibrary(?string $path = null): string
    {
        return Native::load($path);
    }

    /** @return array{0:int,1:int,2:int} the loaded library's ABI version */
    public static function abiVersion(): array
    {
        $v = [Native::new("uint32_t"), Native::new("uint32_t"), Native::new("uint32_t")];
        Native::ffi()->zxv_version(\FFI::addr($v[0]), \FFI::addr($v[1]), \FFI::addr($v[2]));
        return [$v[0]->cdata, $v[1]->cdata, $v[2]->cdata];
    }

    /** @return list<string> feature groups live in this build */
    public static function features(): array
    {
        $m = Native::new("uint32_t");
        Native::ffi()->zxv_features(\FFI::addr($m));
        return array_keys(array_filter(self::FEATURES, fn (int $bit) => ($m->cdata & $bit) !== 0));
    }

    public static function hasFeature(string $name): bool
    {
        return in_array($name, self::features(), true);
    }

    /** Bytes from the OS CSPRNG. */
    public static function randomBytes(int $n): string
    {
        Native::int($n, "n", 0, 1 << 24);
        $buf = Native::new("uint8_t[" . max($n, 1) . "]");
        Native::check(Native::ffi()->zxv_random($buf, $n));
        return Native::read($buf, $n);
    }

    /**
     * Throw (default) raises ZxvFatalError; Abort prints it and ends the
     * process at once with exit status 70 (no finally blocks, no shutdown
     * functions): the only truly uncatchable form.
     */
    public static function setFatalPolicy(FatalPolicy $policy): void
    {
        self::$fatalPolicy = $policy;
    }

    public static function fatalPolicy(): FatalPolicy
    {
        return self::$fatalPolicy;
    }

    /** @internal */
    public static function onFatal(ZxvFatalError $err): void
    {
        if (self::$fatalPolicy !== FatalPolicy::Abort) {
            return;
        }
        fwrite(STDERR, "zxv: FATAL " . $err->getMessage() . "; aborting (FatalPolicy::Abort)\n");
        try {
            // libc _exit: no finally blocks, destructors or shutdown functions run.
            \FFI::cdef("void _exit(int status);")->_exit(70);
        } catch (\Throwable) {
            // no libc symbol reachable (unusual): fall back to exit()
        }
        exit(70);
    }
}
