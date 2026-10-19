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
    // ABI 1.5.
    /** Not a denomination (a coin face or token tier that does not exist). USAGE. */
    public const E_DENOM = -32;
    /** A treaty failed validation (TreatyException has the fault and offset). USAGE. */
    public const E_TREATY = -33;
    /** Not a valid or not the expected content id, or the bytes do not match it. */
    public const E_CID = -34;
    /** No valid consent token for the action (ConsentRequiredException). */
    public const E_CONSENT = -35;
    /** Too few freight rows survived to rebuild the payload. */
    public const E_ERASURE = -36;
    /** A V-Hub peg is unset, stale or replayed. */
    public const E_PEG = -37;
    /** A V-Hub limit rule refused the swap. */
    public const E_LIMIT = -38;
    /** A paradox graph reached its step limit unresolved (goes to the owner). */
    public const E_PARADOX_STEP = -39;
    /** The change needs the owner's approval (OwnerApprovalRequiredException). */
    public const E_PARADOX_OWNER = -40;
    // ABI 1.6: Vino Embed (-51 .. -59 are reserved for the vino group).
    /** Not the voucher's active rail, bearer or ghost (WrongRailException). REFUSED. */
    public const E_RAIL = -46;
    /** Would take a play issuer below 1.8x coverage (CoverageException). REFUSED. */
    public const E_COVERAGE = -47;
    /** Wrong lane for the call, play vs live (LaneException). USAGE. */
    public const E_LANE = -48;
    /** The payment request expired, or was approved too late (ExpiredException). REFUSED. */
    public const E_EXPIRED = -49;
    /** A paid chance outcome in a live payment request (ChanceRefusedException). REFUSED. */
    public const E_CHANCE = -50;

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
