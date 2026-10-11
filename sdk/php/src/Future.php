<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\CancelledException;
use Zedec\Zxv\Exception\TimeoutException;

/**
 * A cooperative future for PHP's single thread. It completes when its
 * condition is met (a budget replenished, a tick passed), checked each time
 * it is polled: by {@see Loop} ticks, or by await().
 *
 * await() inside a Fiber run by Loop::run() suspends that Fiber, so other
 * Fibers (the code that releases a budget or advances a clock) run
 * meanwhile. await() outside a Fiber drives the Loop itself until done.
 *
 *     $f = $budget->consumeAsync(3, timeoutMs: 2000);
 *     Loop::run(function () use ($f) { $f->await(); });
 */
final class Future
{
    private const PENDING = 0;
    private const DONE = 1;
    private const FAILED = 2;

    private int $state = self::PENDING;
    private mixed $result = null;
    private ?\Throwable $error = null;
    private bool $cancelled = false;
    private ?int $deadlineNs;

    /**
     * @internal
     * @param \Closure(self): bool $step tries once; returns true after resolve()/reject()
     */
    public function __construct(private readonly \Closure $step, ?int $timeoutMs = null)
    {
        $this->deadlineNs = $timeoutMs === null ? null : hrtime(true) + Native::int($timeoutMs, "timeoutMs", 0) * 1_000_000;
        Loop::watch($this);
    }

    /** Try to complete now. Returns true once complete (either way). */
    public function poll(): bool
    {
        if ($this->state !== self::PENDING) {
            return true;
        }
        if ($this->cancelled) {
            $this->reject(new CancelledException("future cancelled"));
            return true;
        }
        try {
            if (($this->step)($this)) {
                return true;
            }
        } catch (\Throwable $e) {
            $this->reject($e);
            return true;
        }
        if ($this->deadlineNs !== null && hrtime(true) >= $this->deadlineNs) {
            $this->reject(new TimeoutException("future did not complete in time"));
            return true;
        }
        return false;
    }

    /**
     * Wait for the result (or rethrow the failure).
     * @param ?int $timeoutMs limits this wait only
     * @throws TimeoutException|CancelledException
     */
    public function await(?int $timeoutMs = null): mixed
    {
        $limit = $timeoutMs === null ? null : hrtime(true) + $timeoutMs * 1_000_000;
        while (!$this->poll()) {
            if ($limit !== null && hrtime(true) >= $limit) {
                throw new TimeoutException("await timed out after {$timeoutMs} ms");
            }
            if (\Fiber::getCurrent() !== null) {
                \Fiber::suspend();
            } else {
                if (!Loop::tick()) {
                    usleep(1000);
                }
            }
        }
        if ($this->state === self::FAILED) {
            throw $this->error;
        }
        return $this->result;
    }

    /** Ask it to stop; await() then throws CancelledException. */
    public function cancel(): void
    {
        $this->cancelled = true;
    }

    public function isCancelled(): bool
    {
        return $this->cancelled;
    }

    public function isDone(): bool
    {
        return $this->state !== self::PENDING;
    }

    /** @internal */
    public function resolve(mixed $value): bool
    {
        $this->result = $value;
        $this->state = self::DONE;
        return true;
    }

    /** @internal */
    public function reject(\Throwable $e): bool
    {
        $this->error = $e;
        $this->state = self::FAILED;
        return true;
    }
}
