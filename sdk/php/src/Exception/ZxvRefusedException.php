<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/** REFUSED class: a well-formed request the platform declined. Handle it; a retry may succeed later. */
class ZxvRefusedException extends \RuntimeException implements ZxvException
{
    use StatusCarrier;
}
