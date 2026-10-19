<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Budgets;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\DeferredEvent;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Loop;
use Zedec\Zxv\PhaseClock;
use Zedec\Zxv\Rational;
use Zedec\Zxv\Status;

final class AsyncTest extends TestCase
{
    public function testExactBudgetAndExhaustion(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $b = (new Budgets($cap))->budget("tokens", Budgets::TOKENS, 10);
            $denied = [];
            $b->onExhausted(function ($x) use (&$denied) {
                $denied[] = $x->name;
            });
            $b->consume(Rational::of(7, 2))->consume(Rational::of(13, 2));
            $e = $this->assertThrows(Exception\BudgetExhaustedException::class, fn () => $b->consume(Rational::of(1, 3)));
            $this->assertSame("ZXV_E_BUDGET", $e->getStatusName());
            $this->assertSame(["tokens"], $denied);
            $e = $this->assertThrows(Exception\ZxvUsageException::class, fn () => $b->consume(-1));
            $this->assertSame(Status::E_RMAG_INVALID, $e->getStatus());
            $this->assertThrows(Exception\FloatRejectedException::class, fn () => $b->consume(0.5));
            $s = $b->status();
            $this->assertTrue($s["remaining"]->isZero());
            $this->assertTrue($s["used"]->equals(10));
            $this->assertSame(0, $s["epoch"]);
            $b->release("0.5");
            $this->assertTrue($b->remaining()->equals(Rational::of(1, 2)));
            $this->assertSame(1, $b->status()["epoch"]);
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => (new Budgets($cap->attenuate(Capability::PHASE)))->budget("x", Budgets::MEMORY, 1));
        });
    }

    public function testConsumeAsyncWaitsForReplenishInTheLoop(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $b = (new Budgets($cap))->budget("gpu", Budgets::COMPUTE_CYCLES, 5);
            $b->consume(5);
            $f = $b->consumeAsync(3, timeoutMs: 5000);
            $log = [];
            Loop::run(
                function () use ($f, &$log): void {
                    $f->await();
                    $log[] = "consumed";
                },
                function () use ($b, $f, &$log): void {
                    \Fiber::suspend();
                    $b->release(2); // 2 free: not enough
                    \Fiber::suspend();
                    \Fiber::suspend();
                    $log[] = $f->isDone() ? "early" : "still waiting";
                    $b->setLimit(8); // now 3 free
                },
            );
            $this->assertSame(["still waiting", "consumed"], $log);
            $this->assertTrue($b->used()->equals(6));
        });
    }

    public function testConsumeAsyncTimeoutCancelInvalid(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $b = (new Budgets($cap))->budget("io", Budgets::BANDWIDTH, 1);
            $b->consume(1);
            $this->assertThrows(Exception\TimeoutException::class, fn () => $b->consumeAsync(1, timeoutMs: 100)->await());
            $f = $b->consumeAsync(1);
            $f->cancel();
            $this->assertThrows(Exception\CancelledException::class, fn () => $f->await(1000));
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $b->consumeAsync(-1)->await(1000));
        });
    }

    public function testPhaseDeferralManualClock(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $clock = new PhaseClock($cap);
            $cycle = PhaseClock::CYCLE;
            $this->assertSame(self::vectors()["phase_cycle"], $cycle);
            $ev = $clock->defer($cycle);
            $this->assertSame($cycle, $clock->nextDue());
            $w = $ev->awaitCleared(timeoutMs: 5000);
            $this->assertSame(0, $clock->advance($cycle - 1));
            $this->assertSame(DeferredEvent::PENDING, $ev->state());
            $this->assertFalse($w->poll());
            $this->assertSame(1, $clock->advance(1));
            $this->assertTrue($w->await() === $ev);
            $this->assertSame($cycle, $clock->now());
            $this->assertSame(null, $clock->nextDue());

            $a = $clock->admit(6, 1, PhaseClock::STEP_S0_PENDING);
            $this->assertSame([PhaseClock::DEFER, 0], [$a["decision"], $a["token"]]);
            $clock->setHealth(6, PhaseClock::HEALTH_HEALTHY);
            $a = $clock->admit(6, 2, PhaseClock::STEP_REQUIRES_HEALTH);
            $this->assertSame(PhaseClock::ADMIT, $a["decision"]);
            $this->assertTrue($a["token"] !== 0);

            $ev2 = $clock->defer($clock->now() + 10);
            $w2 = $ev2->awaitCleared();
            $ev2->cancel();
            $this->assertThrows(Exception\CancelledException::class, fn () => $w2->await(1000));
        });
    }

    public function testPhaseRealtimeDriver(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $clock = new PhaseClock($cap);
            $ev = $clock->defer(50);
            $clock->startRealtime(1000);
            $got = null;
            Loop::run(function () use ($ev, &$got): void {
                $got = $ev->awaitCleared(timeoutMs: 5000)->await();
            });
            $clock->stopRealtime();
            $this->assertTrue($got === $ev);
            $this->assertFalse($clock->isRealtime());
            $this->assertTrue($clock->now() >= 50);
        });
    }
}
