<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\DailyCapExceededException;

/**
 * ABI 1.6: a per-device daily spend counter (zxv_vino_meter). It counts
 * what one device approved per day index and refuses an amount that would
 * pass the deployment's daily cap. It is a counter, not a legal check, and
 * makes no exemption claim.
 *
 *     $meter = new VinoSpendMeter($cap, 10000);     // $cap needs Capability::VINO
 *     $left = $meter->admit(intdiv(time(), 86400), 2500);
 *
 * Thread-safe in C. Close it before its context.
 */
final class VinoSpendMeter extends NativeHandle
{
    /** The cap per day (an int, or a decimal string past PHP_INT_MAX). */
    public readonly int|string $dailyCap;

    /**
     * @param Capability $capability needs Capability::VINO
     * @param int|string $dailyCap 1..Vino::AMOUNT_MAX
     * @throws Exception\CapabilityDeniedException without VINO
     * @throws Exception\ZxvUsageException ZXV_E_RANGE (cap 0 or too large)
     */
    public function __construct(public readonly Capability $capability, int|string|float $dailyCap)
    {
        $cap = Native::u64($dailyCap, "dailyCap");
        $out = Native::new("zxv_vino_meter*");
        Native::check(Native::ffi16()->zxv_vino_meter_create($capability->ptr(), $cap, \FFI::addr($out)), $capability->context);
        $this->dailyCap = Native::u64Out($cap);
        parent::__construct($capability->context, $out, "zxv_vino_meter_destroy");
    }

    /**
     * Count $amount against day $day. A day above the last one counted
     * starts a fresh count; a day below it is refused (the clock went back).
     * Returns what is left of the day's cap after this amount.
     * @param int|string $day a day index of your clock, e.g. intdiv(time(), 86400)
     * @param int|string $amount 1..Vino::AMOUNT_MAX
     * @throws DailyCapExceededException the day's total would pass the cap; nothing
     *   was counted, and its $left says what is left
     * @throws Exception\ZxvUsageException ZXV_E_RANGE (amount 0 or too large, or a day below the last one)
     */
    public function admit(int|string|float $day, int|string|float $amount): int|string
    {
        [$l, $lP] = Native::out("uint64_t");
        $rc = Native::ffi16()->zxv_vino_meter_admit($this->capability->ptr(), $this->ptr(), Native::u64($day, "day"), Native::u64($amount, "amount"), $lP);
        if ($rc === Status::E_LIMIT) {
            $err = new DailyCapExceededException($rc, Native::lastError());
            $err->left = Native::u64Out($l->cdata);
            throw $err;
        }
        Native::check($rc, $this->context);
        return Native::u64Out($l->cdata);
    }
}
