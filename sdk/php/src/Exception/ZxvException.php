<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * Marker implemented by every non-fatal ZXV exception (usage, refused,
 * system). `catch (ZxvException $e)` never catches {@see ZxvFatalError}.
 */
interface ZxvException extends \Throwable
{
    /** The negative ZXV_E_* status. */
    public function getStatus(): int;

    /** The symbolic name, e.g. "ZXV_E_FUNDS". */
    public function getStatusName(): string;

    /** Detail text from zxv_last_error ("" if none). */
    public function getDetail(): string;
}
