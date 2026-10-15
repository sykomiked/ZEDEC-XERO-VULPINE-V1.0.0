<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: VinoSpendMeter::admit() refused an amount that would pass the
 * device's daily cap (ZXV_E_LIMIT, REFUSED class). Nothing was counted.
 * $left is what may still be approved that day. The meter is a counter, not
 * a legal check.
 */
class DailyCapExceededException extends ZxvRefusedException
{
    /** What is left of the day's cap (an int, or a decimal string past PHP_INT_MAX). */
    public int|string $left = 0;
}
