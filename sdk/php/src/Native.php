<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI;
use FFI\CData;
use Zedec\Zxv\Exception\BudgetExhaustedException;
use Zedec\Zxv\Exception\CapabilityDeniedException;
use Zedec\Zxv\Exception\FloatRejectedException;
use Zedec\Zxv\Exception\InsufficientFundsException;
use Zedec\Zxv\Exception\NotImplementedException;
use Zedec\Zxv\Exception\VerificationFailedException;
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
        $errors = [];
        foreach ($candidates as $c) {
            try {
                $ffi = FFI::cdef($header, $c);
            } catch (FFI\Exception $e) {
                $errors[] = $c . ": " . $e->getMessage();
                continue;
            }
            $maj = $ffi->new("uint32_t");
            $min = $ffi->new("uint32_t");
            $ffi->zxv_version(FFI::addr($maj), FFI::addr($min), null);
            if ($maj->cdata !== 1 || $min->cdata < 3) {
                $errors[] = $c . ": ABI " . $maj->cdata . "." . $min->cdata . " is not 1.x (x >= 3)";
                continue;
            }
            self::$ffi = $ffi;
            self::$path = $c;
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
            Status::CLASS_USAGE => $status === Status::E_FLOAT ? new FloatRejectedException($detail) : new ZxvUsageException($status, $detail),
            Status::CLASS_REFUSED => match ($status) {
                Status::E_CAP => new CapabilityDeniedException($status, $detail),
                Status::E_BUDGET => new BudgetExhaustedException($status, $detail),
                Status::E_FUNDS => new InsufficientFundsException($status, $detail),
                Status::E_VERIFY => new VerificationFailedException($status, $detail),
                Status::E_NOTIMPL => new NotImplementedException($status, $detail),
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
