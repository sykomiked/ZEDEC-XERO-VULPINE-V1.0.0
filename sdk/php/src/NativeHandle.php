<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * Base of every wrapper of a native handle created under a Context. close()
 * frees it early; anything left is freed when its Context closes. (Child
 * handles are not freed by their own destructors: the Context frees them
 * in a safe order, as R6 requires.)
 */
abstract class NativeHandle
{
    private ?CData $handle;
    private int $childId;

    /** @internal */
    protected function __construct(public readonly Context $context, CData $handle, string $destroyFn)
    {
        $this->handle = $handle;
        $this->childId = $context->adopt($destroyFn, $handle);
    }

    /** Free the native handle now. Idempotent. */
    public function close(): void
    {
        if ($this->handle !== null) {
            $this->context->release($this->childId);
            $this->handle = null;
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
        $this->context->raw(); // refuses a closed or dead context
        return $this->handle;
    }
}
