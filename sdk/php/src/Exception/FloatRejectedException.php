<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * A PHP float reached an API that takes only exact numbers. Pass an int,
 * a numeric string to Rational::parse(), or call
 * Rational::fromDoubleExact($x) on purpose.
 */
class FloatRejectedException extends ZxvUsageException
{
    public function __construct(string $detail = "a float is not exact; pass an int or Rational, or call Rational::fromDoubleExact() on purpose")
    {
        parent::__construct(\Zedec\Zxv\Status::E_FLOAT, $detail);
    }
}
