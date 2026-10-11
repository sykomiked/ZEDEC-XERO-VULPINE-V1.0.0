<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * Secret key material in native memory (malloc'd, outside PHP's zval heap),
 * wiped with zxv_secure_zero on close() and in the destructor (which PHP's
 * reference counting runs as soon as the last reference goes).
 *
 * The bytes become a PHP string only through exportBytesUnsafe(), which
 * defeats the wiping.
 */
final class SecretKey
{
    private ?CData $mem;

    /** @internal */
    public function __construct(private readonly int $size)
    {
        $this->mem = Native::ffi()->new("uint8_t[$size]", false, true);
    }

    /** Copy a PHP string into a new SecretKey (the source string is NOT wiped). */
    public static function fromBytesUnsafe(string $bytes): self
    {
        $k = new self(strlen($bytes));
        \FFI::memcpy($k->pointer(), $bytes, strlen($bytes));
        return $k;
    }

    public function __destruct()
    {
        $this->close();
    }

    public function bytesize(): int
    {
        return $this->size;
    }

    /** Wipe and free. Idempotent. */
    public function close(): void
    {
        if ($this->mem !== null) {
            Native::ffi()->zxv_secure_zero($this->mem, $this->size);
            \FFI::free($this->mem);
            $this->mem = null;
        }
    }

    public function isClosed(): bool
    {
        return $this->mem === null;
    }

    /** A PHP-heap copy of the bytes. UNSAFE: the copy cannot be wiped. */
    public function exportBytesUnsafe(): string
    {
        return Native::read($this->pointer(), $this->size);
    }

    /** @internal */
    public function pointer(): CData
    {
        if ($this->mem === null) {
            throw new ZxvUsageException(Status::E_STATE, "SecretKey is closed");
        }
        return $this->mem;
    }

    public function __toString(): string
    {
        return "SecretKey({$this->size} bytes" . ($this->mem === null ? ", closed)" : ")");
    }

    /** @return array<string, mixed> never the bytes */
    public function __debugInfo(): array
    {
        return ["bytes" => $this->size, "closed" => $this->mem === null];
    }

    /** @internal */
    public static function expect(mixed $key, int $size): CData
    {
        if (!$key instanceof self || $key->size !== $size) {
            throw new ZxvUsageException(Status::E_ARG, "expected a $size-byte SecretKey");
        }
        return $key->pointer();
    }
}
