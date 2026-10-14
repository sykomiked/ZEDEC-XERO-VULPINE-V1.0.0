<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: PIC S9(18) arithmetic as COBOL does it with ON SIZE ERROR:
 * exact, on ints in -Treaty::PIC18_MAX..Treaty::PIC18_MAX. An operand or
 * exact result outside that range, or a division by zero, throws
 * ZxvUsageException (ZXV_E_RANGE: a size error).
 */
final class TreatyPic
{
    private function __construct()
    {
    }

    public static function add(int|float $a, int|float $b): int
    {
        return self::op("zxv_treaty_pic_add", $a, $b);
    }

    public static function sub(int|float $a, int|float $b): int
    {
        return self::op("zxv_treaty_pic_sub", $a, $b);
    }

    public static function mul(int|float $a, int|float $b): int
    {
        return self::op("zxv_treaty_pic_mul", $a, $b);
    }

    /**
     * DIVIDE a BY b GIVING q REMAINDER r: q truncates toward zero and r has
     * the dividend's sign (-7 / 2 = -3 rem -1), like PHP's intdiv and %.
     * @return array{0:int,1:int} [q, r]
     */
    public static function divide(int|float $a, int|float $b): array
    {
        [$q, $qP] = Native::out("int64_t");
        [$r, $rP] = Native::out("int64_t");
        Native::check(Native::ffi15()->zxv_treaty_pic_div(Native::i64($a, "a"), Native::i64($b, "b"), $qP, $rP));
        return [$q->cdata, $r->cdata];
    }

    private static function op(string $fn, int|float $a, int|float $b): int
    {
        [$out, $outP] = Native::out("int64_t");
        Native::check(Native::ffi15()->$fn(Native::i64($a, "a"), Native::i64($b, "b"), $outP));
        return $out->cdata;
    }
}
