<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/** USAGE class: a caller bug. Fix the arguments; retrying fails again. */
class ZxvUsageException extends \InvalidArgumentException implements ZxvException
{
    use StatusCarrier;
}
