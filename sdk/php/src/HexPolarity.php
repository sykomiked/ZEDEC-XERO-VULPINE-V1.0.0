<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** The polarity of a {@see Hexagram}'s evidence (bits 3-2). Forbidden is absorbing. */
enum HexPolarity: int
{
    /** Evidence margin shrinking, or stale. */
    case Entropic = 0;
    case Neutral = 1;
    /** Forbidden work: rolled back, quarantined, never replayed. */
    case Forbidden = 2;
    /** Evidence margin growing. */
    case Negentropic = 3;
}
