<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** Status codes and classes of zxv_api.h (stable; never renumbered). */
final class Status
{
    public const OK = 0;
    public const E_ARG = -1;
    public const E_BUFFER = -2;
    public const E_NOTIMPL = -3;
    public const E_NOT_FOUND = -4;
    public const E_CAPACITY = -5;
    public const E_FUNDS = -6;
    public const E_CURRENCY = -7;
    public const E_RANGE = -8;
    public const E_STATE = -9;
    public const E_INVARIANT = -10;
    public const E_UTF8 = -11;
    public const E_TOO_LONG = -12;
    public const E_DUPLICATE = -13;
    public const E_NOMEM = -14;
    public const E_DECLINED = -15;
    public const E_FIELD = -16;
    public const E_CAP = -17;
    public const E_POISONED = -18;
    public const E_BUDGET = -19;
    public const E_RMAG_INVALID = -20;
    public const E_VERIFY = -21;
    public const E_FORMAT = -22;
    public const E_IO = -23;
    public const E_FLOAT = -24;
    // ABI 1.4: hardware emulation and hardware updates.
    /** No proven implementation may serve the call. */
    public const E_NOPROVIDER = -25;
    /** A shadow check found two implementations disagree. */
    public const E_DISPUTE = -26;
    /** The image failed in hardware: rolled back, logged. */
    public const E_QUARANTINED = -27;
    /** Image or contract not newer, or below the floor. */
    public const E_ROLLBACK = -28;
    /** The image is already quarantined; never reloaded. */
    public const E_BLOCKED = -29;
    /** The image is for other hardware. */
    public const E_TARGET = -30;
    /** Refused by the function's placement policy. */
    public const E_POLICY = -31;
    public const E_INTERNAL = -99;

    public const CLASS_OK = 0;
    public const CLASS_USAGE = 1;
    public const CLASS_REFUSED = 2;
    public const CLASS_FATAL = 3;
    public const CLASS_SYSTEM = 4;

    private function __construct()
    {
    }

    /** Symbolic name of a status ("ZXV_E_FUNDS"), from the library. */
    public static function name(int $status): string
    {
        try {
            $n = Native::ffi()->zxv_status_name($status); // FFI returns const char * as a PHP string
            return is_string($n) ? $n : \FFI::string($n);
        } catch (\Throwable) {
            return "ZXV_E_UNKNOWN";
        }
    }

    /** ZXV_CLASS_* of a status (unknown negatives are SYSTEM). */
    public static function classOf(int $status): int
    {
        return Native::ffi()->zxv_status_class($status);
    }
}
