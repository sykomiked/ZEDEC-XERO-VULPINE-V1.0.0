<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.5: no valid consent token for a Web4 action (ZXV_E_CONSENT): none
 * given, a mismatch, expired, a bad signature, or already used or revoked.
 * The reason is in getDetail(). REFUSED class.
 */
class ConsentRequiredException extends ZxvRefusedException
{
}
