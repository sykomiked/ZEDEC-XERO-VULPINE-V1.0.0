<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** How a FATAL status is delivered (see Zxv::setFatalPolicy). */
enum FatalPolicy
{
    /** Throw ZxvFatalError (an \Error, outside the \Exception hierarchy). */
    case Throw;
    /** Print the error and end the process at once with exit status 70. */
    case Abort;
}
