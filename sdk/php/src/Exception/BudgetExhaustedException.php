<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * An rmag or swarm budget is exhausted. For a swarm consume,
 * {@see getGranted()} is what was granted before it ran out.
 */
class BudgetExhaustedException extends ZxvRefusedException
{
    private ?int $granted = null;

    /** @internal */
    public function withGranted(int $granted): self
    {
        $this->granted = $granted;
        return $this;
    }

    /** Tokens granted by a partial swarm consume, or null. */
    public function getGranted(): ?int
    {
        return $this->granted;
    }
}
