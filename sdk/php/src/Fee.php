<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: the 0.08889% assurance fee and its split (zxv_fee_*).
 *
 *     fee(g) = floor(g * 8889 / 10,000,000)
 *
 * g is the gross in the unit's own minor measure, exact for every g up to
 * 2^64-1: fee(1124) = 0 and fee(1125) = 1. The fee is charged ONCE per
 * transaction, on its gross: never on a fee, never compounded.
 *
 * CARRY. The sub-unit remainder of an account (0 <= carry < DEN) is carried
 * to its next charge, so many small charges pay exactly what one charge of
 * their sum pays. Store the carry per account:
 *
 *     [$fee, $carry] = Fee::assureWithCarry(100, $carry);
 *
 * THE FOUR BUCKETS (split): 50% reserve floor, 25% V-Bill dividend pool,
 * 15% infrastructure / node bounties, 10% regenerative capital. The last
 * three get floor(fee * pct / 100); the reserve floor gets the rest, so the
 * parts sum to the fee exactly.
 *
 * Pure: no context, no capability. Honest limits: arithmetic rules only.
 * How a fee, reserve or dividend pool is treated for tax, securities or
 * consumer law is a question for counsel; nothing here is a licence or a
 * certification.
 */
final class Fee
{
    public const NUM = 8889;
    /** Fee denominator, 10^7; a carry is always below this. */
    public const DEN = 10000000;
    /** The four buckets, in ZXV_FEE_* order. */
    public const BUCKETS = ["reserveFloor", "vbillDividend", "infraBounty", "regenCapital"];
    /** The unit kinds, in ZXV_FEE_UNIT_* order. */
    public const UNITS = ["money", "compute", "bandwidth", "storage"];

    private function __construct()
    {
    }

    /** The fee on $gross (0..2^64-1), with no carry. */
    public static function assure(int|string|float $gross): int|string
    {
        [$out, $outP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_fee_assure(Native::u64($gross, "gross"), $outP));
        return Native::u64Out($out->cdata);
    }

    /**
     * The fee on $gross given the account's incoming carry.
     * @return array{0:int|string,1:int} [fee, new carry]
     * @throws Exception\ZxvUsageException ZXV_E_RANGE (carry >= DEN: a corrupt carry)
     */
    public static function assureWithCarry(int|string|float $gross, int|string|float $carry = 0): array
    {
        [$fee, $feeP] = Native::out("uint64_t");
        [$rem, $remP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_fee_assure_carry(Native::u64($carry, "carry"), Native::u64($gross, "gross"), $feeP, $remP));
        return [Native::u64Out($fee->cdata), $rem->cdata];
    }

    /**
     * Split $fee into the four buckets (they sum to $fee exactly).
     * @return array{reserveFloor:int|string, vbillDividend:int|string, infraBounty:int|string, regenCapital:int|string}
     */
    public static function split(int|string|float $fee): array
    {
        $out = Native::new("uint64_t[4]");
        Native::check(Native::ffi15()->zxv_fee_split(Native::u64($fee, "fee"), $out));
        $r = [];
        foreach (self::BUCKETS as $i => $k) {
            $r[$k] = Native::u64Out($out[$i]);
        }
        return $r;
    }

    /**
     * The default fee policy (zxv_fee_policy_default): bucket percents,
     * allowBelow (false: a contribution below the fee is raised to it),
     * creditRates per unit as [num, den] (money 1/1, others 0/1), the
     * operator multiplier [num, den] (1/1) and the commons cap [num, den] (8/21).
     * @return array{percent:array<string,int>, allowBelow:bool, creditRates:array<string,array{0:int,1:int}>, multiplier:array{0:int,1:int}, commonsCap:array{0:int,1:int}}
     */
    public static function defaultPolicy(): array
    {
        $pct = Native::new("uint32_t[4]");
        [$allow, $allowP] = Native::out("int32_t");
        $rn = Native::new("uint64_t[4]");
        $rd = Native::new("uint64_t[4]");
        [$mn, $mnP] = Native::out("uint64_t");
        [$md, $mdP] = Native::out("uint64_t");
        [$cn, $cnP] = Native::out("uint64_t");
        [$cd, $cdP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_fee_policy_default($pct, $allowP, $rn, $rd, $mnP, $mdP, $cnP, $cdP));
        $percent = [];
        foreach (self::BUCKETS as $i => $k) {
            $percent[$k] = $pct[$i];
        }
        $rates = [];
        foreach (self::UNITS as $i => $k) {
            $rates[$k] = [$rn[$i], $rd[$i]];
        }
        return ["percent" => $percent, "allowBelow" => $allow->cdata !== 0, "creditRates" => $rates,
            "multiplier" => [$mn->cdata, $md->cdata], "commonsCap" => [$cn->cdata, $cd->cdata]];
    }
}
