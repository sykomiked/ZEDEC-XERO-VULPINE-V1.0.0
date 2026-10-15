<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.6: the rail a play-lane voucher is on (ZXV_VINO_RAIL_*). A voucher
 * is on exactly one rail at a time and can only be spent on that one.
 */
enum VinoRail: int
{
    /** The note in the holder's hand. */
    case Bearer = 0;
    /** Its twin on the play ledger. */
    case Ghost = 1;
}
