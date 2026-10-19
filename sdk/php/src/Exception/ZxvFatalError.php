<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * FATAL class: the context halted after a broken invariant (or an operator
 * halt). Every later call on it is refused; never retry.
 *
 * It extends \Error, NOT \Exception, so `catch (\Exception $e)` and
 * `catch (ZxvException $e)` do not swallow it; only `catch (\Throwable)`,
 * `catch (\Error)` or `catch (ZxvFatalError)` do. Set
 * `Zxv::setFatalPolicy(FatalPolicy::Abort)` to end the process instead.
 */
final class ZxvFatalError extends \Error
{
    private int $status;
    private string $detail;

    public function __construct(int $status, string $detail = "")
    {
        $this->status = $status;
        $this->detail = $detail;
        parent::__construct(\Zedec\Zxv\Status::name($status) . " (" . $status . "): context halted"
            . ($detail === "" ? "" : ": " . $detail) . "; never retry", $status);
    }

    /** ZXV_E_POISONED or ZXV_E_INVARIANT. */
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
