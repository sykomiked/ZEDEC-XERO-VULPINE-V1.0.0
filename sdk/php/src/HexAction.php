<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** What happens next to work with a given {@see Hexagram} (zxv_hex_action). */
enum HexAction: int
{
    case Proceed = 0;
    case Wait = 1;
    case Refuse = 2;
    case Escalate = 3;
    case Rollback = 4;
}
