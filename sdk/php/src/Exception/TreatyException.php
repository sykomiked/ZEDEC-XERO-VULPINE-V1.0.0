<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.5: a treaty failed validation (ZXV_E_TREATY, USAGE class). The
 * fault says why (a name from Treaty::FAULTS, e.g. "syntax", "weight") and
 * the offset where: a byte offset into the Sutra text or the record.
 */
class TreatyException extends ZxvUsageException
{
    /** Why: a Treaty::FAULTS name (or the raw number if unknown). */
    public string|int $fault = "none";
    /** Where: the byte offset of the fault in the text or record. */
    public int $offset = 0;
}
