<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\CancelledException;

/** An S0 event parked on a PhaseClock. */
final class DeferredEvent
{
    public const PENDING = 0;
    public const CLEARED = 1;
    public const CANCELLED = 2;

    /** @internal */
    public function __construct(public readonly PhaseClock $clock, public readonly int $id)
    {
    }

    /** PENDING, CLEARED or CANCELLED. */
    public function state(): int
    {
        return $this->info()[0];
    }

    public function dueTick(): int
    {
        return $this->info()[1];
    }

    public function isCleared(): bool
    {
        return $this->state() === self::CLEARED;
    }

    public function cancel(): void
    {
        Native::check(Native::ffi()->zxv_phase_cancel($this->clock->capability->ptr(), $this->clock->ptr(), $this->id), $this->clock->context);
    }

    /** A Future completing (with this event) when the clock passes its tick. */
    public function awaitCleared(?int $timeoutMs = null): Future
    {
        return new Future(function (Future $f): bool {
            return match ($this->state()) {
                self::CLEARED => $f->resolve($this),
                self::CANCELLED => $f->reject(new CancelledException("event {$this->id} was cancelled")),
                default => false,
            };
        }, $timeoutMs);
    }

    /** @return array{0:int,1:int} */
    private function info(): array
    {
        $st = Native::new("int32_t");
        $due = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_phase_event($this->clock->ptr(), $this->id, \FFI::addr($st), \FFI::addr($due)), $this->clock->context);
        return [$st->cdata, $due->cdata];
    }
}
