<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: base of a native handle made without a context (a treaty, a
 * treaty result, a freight join, an op-stream writer, a Web4 directory,
 * manifest or consent log). close() frees it now; the destructor frees it
 * otherwise. A closed handle refuses use (ZxvUsageException, ZXV_E_STATE).
 * Not thread-safe: use one from one thread at a time.
 */
abstract class PureHandle
{
    private ?CData $handle = null;
    private string $destroyFn = "";

    public function __destruct()
    {
        $this->close();
    }

    /** Free the native handle now. Idempotent. */
    public function close(): void
    {
        if ($this->handle !== null) {
            $h = $this->handle;
            $this->handle = null;
            Native::ffi()->{$this->destroyFn}($h);
        }
    }

    public function isClosed(): bool
    {
        return $this->handle === null;
    }

    /** @internal */
    public function ptr(): CData
    {
        if ($this->handle === null) {
            throw new ZxvUsageException(Status::E_STATE, static::class . " is closed");
        }
        return $this->handle;
    }

    /**
     * @internal Create through $create(outPtr) (a status) and own the result.
     * @param callable(CData): int $create
     */
    protected function own(string $destroyFn, callable $create): void
    {
        $out = Native::new(self::typeOf($destroyFn) . "*");
        Native::check($create(\FFI::addr($out)));
        $this->handle = $out;
        $this->destroyFn = $destroyFn;
    }

    /** @internal Own an already created handle. */
    protected function adopt(string $destroyFn, CData $handle): void
    {
        $this->handle = $handle;
        $this->destroyFn = $destroyFn;
    }

    private static function typeOf(string $destroyFn): string
    {
        return substr($destroyFn, 0, -strlen("_destroy"));
    }
}
