<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/** SYSTEM class: out of memory, I/O, internal. */
class ZxvSystemException extends \RuntimeException implements ZxvException
{
    use StatusCarrier;
}
