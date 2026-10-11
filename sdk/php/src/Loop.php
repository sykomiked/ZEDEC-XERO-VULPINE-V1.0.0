<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * A minimal cooperative event loop on PHP Fibers: it resumes Fibers,
 * polls pending Futures and runs tickers (e.g. PhaseClock's real-time
 * driver). Integrate with another loop (ReactPHP, Amp, Revolt) by calling
 * Loop::tick() from one of its timers.
 *
 *     Loop::run(
 *         function () use ($f) { $f->await(); echo "got it\n"; },
 *         function () use ($budget) { $budget->release(3); },
 *     );
 */
final class Loop
{
    /** @var list<\Fiber> */
    private static array $fibers = [];
    /** @var array<int, \WeakReference<Future>> */
    private static array $futures = [];
    /** @var array<int, \Closure(): void> */
    private static array $tickers = [];
    private static int $nextTicker = 0;

    private function __construct()
    {
    }

    /** Run each task in its own Fiber until all of them finish. */
    public static function run(callable ...$tasks): void
    {
        foreach ($tasks as $t) {
            self::spawn($t);
        }
        while (self::$fibers !== []) {
            if (!self::tick()) {
                usleep(1000);
            }
        }
    }

    /** Start a task in a new Fiber (it runs until its first suspend). */
    public static function spawn(callable $task): \Fiber
    {
        $f = new \Fiber($task);
        self::$fibers[] = $f;
        $f->start();
        return $f;
    }

    /**
     * One turn: run tickers, poll futures, resume suspended Fibers.
     * Returns true if any future completed or any Fiber finished.
     */
    public static function tick(): bool
    {
        $progress = false;
        foreach (self::$tickers as $t) {
            $t();
        }
        foreach (self::$futures as $id => $ref) {
            $fut = $ref->get();
            if ($fut === null || $fut->poll()) {
                unset(self::$futures[$id]);
                $progress = $progress || $fut !== null;
            }
        }
        foreach (self::$fibers as $i => $f) {
            if ($f->isSuspended() && \Fiber::getCurrent() !== $f) {
                $f->resume();
            }
            if ($f->isTerminated()) {
                unset(self::$fibers[$i]);
                $progress = true;
            }
        }
        self::$fibers = array_values(self::$fibers);
        return $progress;
    }

    /** Run $fn on every tick; returns an id for removeTicker(). */
    public static function addTicker(\Closure $fn): int
    {
        $id = ++self::$nextTicker;
        self::$tickers[$id] = $fn;
        return $id;
    }

    public static function removeTicker(int $id): void
    {
        unset(self::$tickers[$id]);
    }

    /** @internal */
    public static function watch(Future $f): void
    {
        self::$futures[spl_object_id($f)] = \WeakReference::create($f);
    }
}
