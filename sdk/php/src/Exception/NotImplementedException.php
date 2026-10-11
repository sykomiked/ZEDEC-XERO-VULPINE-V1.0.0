<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/** The feature is not built into this libzxv (ZXV_E_NOTIMPL), e.g. LoRA. */
class NotImplementedException extends ZxvRefusedException
{
}
