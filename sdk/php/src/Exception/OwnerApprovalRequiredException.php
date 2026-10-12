<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.5: a paradox change flagged money or device needs the owner's
 * approval (ZXV_E_PARADOX_OWNER). REFUSED class.
 */
class OwnerApprovalRequiredException extends ZxvRefusedException
{
}
