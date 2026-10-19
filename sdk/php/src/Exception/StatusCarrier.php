<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/** @internal shared state of the ZXV exception classes. */
trait StatusCarrier
{
    private int $status;
    private string $detail;

    public function __construct(int $status, string $detail = "")
    {
        $this->status = $status;
        $this->detail = $detail;
        $name = \Zedec\Zxv\Status::name($status);
        parent::__construct($name . " (" . $status . ")" . ($detail === "" ? "" : ": " . $detail), $status);
    }

    public function getStatus(): int
    {
        return $this->status;
    }

    public function getStatusName(): string
    {
        return \Zedec\Zxv\Status::name($this->status);
    }

    public function getDetail(): string
    {
        return $this->detail;
    }
}
