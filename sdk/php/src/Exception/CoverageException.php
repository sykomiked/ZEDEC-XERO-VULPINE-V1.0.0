<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: the play issuer would fall below 1.8x coverage (ZXV_E_COVERAGE,
 * REFUSED class). An issuer may only issue, or withdraw backing, while
 * 10 * backing >= 18 * outstanding. Nothing changed.
 */
class CoverageException extends ZxvRefusedException
{
}
