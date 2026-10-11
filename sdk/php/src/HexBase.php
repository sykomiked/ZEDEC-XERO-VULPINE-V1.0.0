<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * The four-valued base of a {@see Hexagram} (bits 5-4), numbered by line
 * pattern: FALSE 0, NEITHER 1, BOTH 2, TRUE 3. This is NOT Truth's
 * numbering (TRUE is 1 there): convert with Hexagram::toTruth() and
 * Hexagram::fromTruth() only.
 */
enum HexBase: int
{
    case False = 0;
    case Neither = 1;
    case Both = 2;
    case True = 3;
}
