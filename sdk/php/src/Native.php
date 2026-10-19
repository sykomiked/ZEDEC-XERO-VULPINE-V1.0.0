<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI;
use FFI\CData;
use Zedec\Zxv\Exception\BudgetExhaustedException;
use Zedec\Zxv\Exception\CapabilityDeniedException;
use Zedec\Zxv\Exception\ChanceRefusedException;
use Zedec\Zxv\Exception\ConsentRequiredException;
use Zedec\Zxv\Exception\CoverageException;
use Zedec\Zxv\Exception\ExpiredException;
use Zedec\Zxv\Exception\FloatRejectedException;
use Zedec\Zxv\Exception\InsufficientFundsException;
use Zedec\Zxv\Exception\LaneException;
use Zedec\Zxv\Exception\NotImplementedException;
use Zedec\Zxv\Exception\OwnerApprovalRequiredException;
use Zedec\Zxv\Exception\TreatyException;
use Zedec\Zxv\Exception\VerificationFailedException;
use Zedec\Zxv\Exception\WrongRailException;
use Zedec\Zxv\Exception\ZxvFatalError;
use Zedec\Zxv\Exception\ZxvRefusedException;
use Zedec\Zxv\Exception\ZxvSystemException;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * @internal The FFI binding to libzxv and the helpers every wrapper uses:
 * status mapping, out parameters, the R4 two-call text pattern, byte
 * buffers and argument guards. Application code uses the classes.
 */
final class Native
{
    private static ?FFI $ffi = null;
    private static ?string $path = null;
    private static bool $has15 = false;
    private static bool $has16 = false;

    /** 2^64 = 1844674407 * 10^10 + 3709551616 (for uint64 text). */
    private const U64_HI = 1844674407;
    private const U64_LO = 3709551616;

    private function __construct()
    {
    }

    /**
     * Load libzxv. Order: $path, ZXV_LIBRARY_PATH, a copy bundled in
     * lib/ of this package, the in-repository build (sdk/libzxv/build),
     * then the system loader path.
     */
    public static function load(?string $path = null): string
    {
        if (self::$ffi !== null) {
            return (string) self::$path;
        }
        if (PHP_INT_SIZE < 8) {
            // Every amount, id and time in the ABI is 64-bit; a 32-bit PHP
            // int would silently turn them into floats.
            throw new ZxvSystemException(Status::E_IO, "this SDK needs a 64-bit PHP build (PHP_INT_SIZE is " . PHP_INT_SIZE . ")");
        }
        if (!extension_loaded("ffi")) {
            throw new ZxvSystemException(Status::E_IO, "the PHP FFI extension is not loaded (php -d extension=ffi -d ffi.enable=1)");
        }
        $candidates = [];
        if ($path !== null) {
            $candidates[] = $path;
        }
        $env = getenv("ZXV_LIBRARY_PATH");
        if (is_string($env) && $env !== "") {
            $candidates[] = $env;
        }
        foreach (["libzxv.so", "libzxv.dylib", "zxv.dll"] as $n) {
            $p = dirname(__DIR__) . "/lib/" . $n;
            if (is_file($p)) {
                $candidates[] = $p;
            }
        }
        foreach (["libzxv.so", "libzxv.dylib", "zxv.dll"] as $n) {
            $repo = dirname(__DIR__, 2) . "/libzxv/build/" . $n;
            if (is_file($repo)) {
                $candidates[] = $repo;
            }
        }
        $candidates[] = PHP_OS_FAMILY === "Darwin" ? "libzxv.dylib" : (PHP_OS_FAMILY === "Windows" ? "zxv.dll" : "libzxv.so");
        $header = (string) file_get_contents(__DIR__ . "/zxv.h");
        $header15 = $header . "\n" . (string) file_get_contents(__DIR__ . "/zxv15.h");
        $header16 = $header15 . "\n" . (string) file_get_contents(__DIR__ . "/zxv16.h");
        $errors = [];
        foreach ($candidates as $c) {
            // ABI 1.6 first; an older library lacks those symbols, so fall
            // back to the 1.5, then the 1.4 declarations, and refuse the
            // newer calls (ZXV_E_NOTIMPL).
            $has15 = true;
            $has16 = true;
            try {
                $ffi = FFI::cdef($header16, $c);
            } catch (FFI\Exception) {
                $has16 = false;
                try {
                    $ffi = FFI::cdef($header15, $c);
                } catch (FFI\Exception) {
                    $has15 = false;
                    try {
                        $ffi = FFI::cdef($header, $c);
                    } catch (FFI\Exception $e) {
                        $errors[] = $c . ": " . $e->getMessage();
                        continue;
                    }
                }
            }
            $maj = $ffi->new("uint32_t");
            $min = $ffi->new("uint32_t");
            $ffi->zxv_version(FFI::addr($maj), FFI::addr($min), null);
            if ($maj->cdata !== 1 || $min->cdata < 4) {
                $errors[] = $c . ": ABI " . $maj->cdata . "." . $min->cdata . " is not 1.x (x >= 4)";
                continue;
            }
            self::$ffi = $ffi;
            self::$path = $c;
            self::$has15 = $has15;
            self::$has16 = $has16;
            return $c;
        }
        throw new ZxvSystemException(Status::E_IO, "libzxv not found (" . implode("; ", $errors) . ")");
    }

    /** The loaded FFI instance (loading on first use). */
    public static function ffi(): FFI
    {
        if (self::$ffi === null) {
            self::load();
        }
        /** @var FFI */
        return self::$ffi;
    }

    /** Whether the loaded library has the ABI 1.5 functions. */
    public static function has15(): bool
    {
        self::ffi();
        return self::$has15;
    }

    /**
     * The FFI instance for an ABI 1.5 call.
     * @throws NotImplementedException (ZXV_E_NOTIMPL) on a library older than 1.5
     */
    public static function ffi15(): FFI
    {
        $f = self::ffi();
        if (!self::$has15) {
            throw new NotImplementedException(Status::E_NOTIMPL, "the loaded libzxv predates ABI 1.5");
        }
        return $f;
    }

    /** Whether the loaded library has the ABI 1.6 functions (vino). */
    public static function has16(): bool
    {
        self::ffi();
        return self::$has16;
    }

    /**
     * The FFI instance for an ABI 1.6 call.
     * @throws NotImplementedException (ZXV_E_NOTIMPL) on a library older than 1.6
     */
    public static function ffi16(): FFI
    {
        $f = self::ffi();
        if (!self::$has16) {
            throw new NotImplementedException(Status::E_NOTIMPL, "the loaded libzxv predates ABI 1.6");
        }
        return $f;
    }

    /** The calling thread's last error detail (R7). */
    public static function lastError(): string
    {
        $buf = self::ffi()->new("char[512]");
        $len = self::ffi()->new("size_t");
        self::ffi()->zxv_last_error($buf, 512, FFI::addr($len));
        return FFI::string($buf, min($len->cdata, 511));
    }

    /** The exception for a negative status (not thrown). */
    public static function errorFor(int $status): \Throwable
    {
        $detail = self::lastError();
        return match (Status::classOf($status)) {
            Status::CLASS_USAGE => match ($status) {
                Status::E_FLOAT => new FloatRejectedException($detail),
                Status::E_TREATY => new TreatyException($status, $detail),
                Status::E_LANE => new LaneException($status, $detail),
                default => new ZxvUsageException($status, $detail),
            },
            Status::CLASS_REFUSED => match ($status) {
                Status::E_CAP => new CapabilityDeniedException($status, $detail),
                Status::E_BUDGET => new BudgetExhaustedException($status, $detail),
                Status::E_FUNDS => new InsufficientFundsException($status, $detail),
                Status::E_VERIFY => new VerificationFailedException($status, $detail),
                Status::E_NOTIMPL => new NotImplementedException($status, $detail),
                Status::E_CONSENT => new ConsentRequiredException($status, $detail),
                Status::E_PARADOX_OWNER => new OwnerApprovalRequiredException($status, $detail),
                Status::E_RAIL => new WrongRailException($status, $detail),
                Status::E_COVERAGE => new CoverageException($status, $detail),
                Status::E_EXPIRED => new ExpiredException($status, $detail),
                Status::E_CHANCE => new ChanceRefusedException($status, $detail),
                default => new ZxvRefusedException($status, $detail),
            },
            Status::CLASS_FATAL => new ZxvFatalError($status, $detail),
            default => new ZxvSystemException($status, $detail),
        };
    }

    /** Throw unless $status is OK; a FATAL status marks $context dead. */
    public static function check(int $status, ?Context $context = null): void
    {
        if ($status === Status::OK) {
            return;
        }
        $err = self::errorFor($status);
        if ($err instanceof ZxvFatalError) {
            $context?->markDead();
            Zxv::onFatal($err);
        }
        throw $err;
    }

    /**
     * R4 two-call text: $fn(buf, cap, outLenPtr) returns a status.
     * @return array{0:int,1:?string}
     */
    public static function text(callable $fn): array
    {
        $len = self::ffi()->new("size_t");
        $cap = 64;
        while (true) {
            $buf = self::ffi()->new("char[$cap]");
            $rc = $fn($buf, $cap, FFI::addr($len));
            $need = $len->cdata;
            if ($rc === Status::E_BUFFER && $need + 1 > $cap) {
                $cap = $need + 1;
                continue;
            }
            return [$rc, $rc === Status::OK ? FFI::string($buf, $need) : null];
        }
    }

    /** A new C value of $type (e.g. "uint32_t", "zxv_ctx*"). */
    public static function new(string $type): CData
    {
        return self::ffi()->new($type);
    }

    /** Copy a PHP byte string into a fresh uint8_t[] (null if empty and allowed). */
    public static function bytes(string $data, bool $allowNull = false): ?CData
    {
        $n = strlen($data);
        if ($n === 0 && $allowNull) {
            return null;
        }
        $buf = self::ffi()->new("uint8_t[" . max($n, 1) . "]");
        if ($n > 0) {
            FFI::memcpy($buf, $data, $n);
        }
        return $buf;
    }

    /** Read $n bytes of a CData buffer as a PHP string. */
    public static function read(CData $buf, int $n): string
    {
        return $n === 0 ? "" : FFI::string($buf, $n);
    }

    /**
     * "R4 for bytes": $fn(buf, cap, outLenPtr) returns a status; a result
     * that needs more room returns ZXV_E_BUFFER with *outLen set, so
     * allocate that and call again. $hint is the first buffer size.
     * Throws on any other status.
     */
    public static function bytesOut(callable $fn, int $hint = 256, ?Context $context = null): string
    {
        $len = self::ffi()->new("size_t");
        $cap = max($hint, 1);
        for ($i = 0; $i < 3; $i++) {
            $buf = self::ffi()->new("uint8_t[$cap]");
            $rc = $fn($buf, $cap, FFI::addr($len));
            $need = $len->cdata;
            if ($rc === Status::E_BUFFER && $need > $cap) {
                $cap = $need;
                continue;
            }
            self::check($rc, $context);
            return self::read($buf, $need);
        }
        self::check(Status::E_BUFFER, $context);
        return "";
    }

    /** Like text(), but throws on any status other than OK. */
    public static function textOut(callable $fn, ?Context $context = null): string
    {
        [$rc, $s] = self::text($fn);
        self::check($rc, $context);
        return (string) $s;
    }

    /** FFI::addr of a fresh C value of $type: [value, pointer]. @return array{0:CData,1:CData} */
    public static function out(string $type): array
    {
        $v = self::ffi()->new($type);
        return [$v, FFI::addr($v)];
    }

    // --- 64-bit unsigned values ------------------------------------------

    /**
     * A uint64 argument as the int FFI passes (the same 64 bits): an int
     * 0..PHP_INT_MAX, or a decimal string 0..18446744073709551615 for
     * values past PHP_INT_MAX. Floats are refused.
     */
    public static function u64(int|string|float $v, string $name): int
    {
        if (is_float($v)) {
            throw new FloatRejectedException($name . ": float " . var_export($v, true) . " is not exact; pass an int or a decimal string");
        }
        if (is_int($v)) {
            if ($v < 0) {
                throw new ZxvUsageException(Status::E_RANGE, "$name $v is negative (uint64)");
            }
            return $v;
        }
        if (preg_match('/^(0|[1-9][0-9]{0,19})$/D', $v) !== 1) {
            throw new ZxvUsageException(Status::E_ARG, "$name must be a decimal uint64 string, got " . var_export($v, true));
        }
        if (strlen($v) < 19 || (strlen($v) === 19 && strcmp($v, (string) PHP_INT_MAX) <= 0)) {
            return (int) $v;
        }
        if (strlen($v) === 20 && strcmp($v, "18446744073709551615") > 0) {
            throw new ZxvUsageException(Status::E_RANGE, "$name $v is past 2^64-1");
        }
        // v - 2^64 (the same 64 bits), in two halves so no step leaves
        // the int range: (hi - HI + 1) * 10^10 + (lo - LO - 10^10).
        $hi = (int) substr($v, 0, -10);
        $lo = (int) substr($v, -10);
        return ($hi - self::U64_HI + 1) * 10000000000 + ($lo - self::U64_LO - 10000000000);
    }

    /** A uint64 result: an int when it fits, else its decimal string. */
    public static function u64Out(int $raw): int|string
    {
        return $raw >= 0 ? $raw : sprintf("%u", $raw);
    }

    /** An int64 argument (floats refused). */
    public static function i64(mixed $v, string $name): int
    {
        return self::int($v, $name);
    }

    public static function uint8(mixed $v, string $name): int
    {
        return self::int($v, $name, 0, 0xFF);
    }

    public static function uint16(mixed $v, string $name): int
    {
        return self::int($v, $name, 0, 0xFFFF);
    }

    public static function int32(mixed $v, string $name): int
    {
        return self::int($v, $name, -2147483648, 2147483647);
    }

    /** A C string argument: valid UTF-8 without a NUL byte. */
    public static function cstr(string $v, string $name): string
    {
        if (str_contains($v, "\0")) {
            throw new ZxvUsageException(Status::E_ARG, "$name must not contain a NUL byte");
        }
        return self::utf8($v, $name);
    }

    /** A bool as the int32 0/1 libzxv takes. */
    public static function bool(mixed $v, string $name): int
    {
        if (!is_bool($v)) {
            throw new ZxvUsageException(Status::E_ARG, "$name must be true or false, got " . get_debug_type($v));
        }
        return $v ? 1 : 0;
    }

    /** A byte string of exactly $size bytes in a fresh uint8_t[]. */
    public static function fixed(string $v, string $name, int $size): CData
    {
        return self::bytes(self::exact($v, $name, $size)) ?? throw new ZxvUsageException(Status::E_ARG, $name);
    }

    // --- argument guards -------------------------------------------------

    /** An int within [min, max]; floats raise FloatRejectedException. */
    public static function int(mixed $v, string $name, int $min = PHP_INT_MIN, int $max = PHP_INT_MAX): int
    {
        if (is_float($v)) {
            throw new FloatRejectedException($name . ": float " . var_export($v, true) . " is not exact; pass an int");
        }
        if (!is_int($v)) {
            throw new ZxvUsageException(Status::E_ARG, $name . " must be an int, got " . get_debug_type($v));
        }
        if ($v < $min || $v > $max) {
            throw new ZxvUsageException(Status::E_RANGE, "$name $v is outside $min..$max");
        }
        return $v;
    }

    public static function uint32(mixed $v, string $name): int
    {
        return self::int($v, $name, 0, 0xFFFFFFFF);
    }

    public static function uint(mixed $v, string $name): int
    {
        return self::int($v, $name, 0);
    }

    /** A byte string of an exact length (when given). */
    public static function exact(string $v, string $name, ?int $size = null): string
    {
        if ($size !== null && strlen($v) !== $size) {
            throw new ZxvUsageException(Status::E_ARG, "$name must be $size bytes, got " . strlen($v));
        }
        return $v;
    }

    /** Valid UTF-8 text. */
    public static function utf8(string $v, string $name): string
    {
        if (preg_match("//u", $v) !== 1) {
            throw new ZxvUsageException(Status::E_UTF8, "$name is not valid UTF-8");
        }
        return $v;
    }
}
