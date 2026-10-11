<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\BudgetExhaustedException;

/** One budget of a Budgets set. */
final class Budget
{
    /** @var list<\Closure(Budget): void> */
    private array $onExhausted = [];

    /** @internal use Budgets::budget() */
    public function __construct(public readonly Budgets $budgets, public readonly int $id, public readonly string $name)
    {
    }

    /**
     * Consume an exact amount (> 0).
     * @throws BudgetExhaustedException past an enforced limit (nothing consumed)
     * @throws Exception\ZxvUsageException ZXV_E_RMAG_INVALID: negative/overflowing (never retry)
     */
    public function consume(Rational|int|float|string $amount): self
    {
        $a = Rational::from($amount);
        Native::check(Native::ffi()->zxv_rmag_consume($this->budgets->capability->ptr(), $this->budgets->ptr(), $this->id, $a->numerator, $a->denominator), $this->budgets->context);
        return $this;
    }

    /**
     * Consume, waiting while the budget is exhausted: a Future that
     * completes (with this Budget) when the consume succeeds. It retries
     * whenever a release or larger limit replenishes the budget (the epoch
     * moves) and fails at once on an invalid amount. Cancel with cancel().
     */
    public function consumeAsync(Rational|int|float|string $amount, ?int $timeoutMs = null): Future
    {
        $a = Rational::from($amount);
        $lastEpoch = -1;
        return new Future(function (Future $f) use ($a, &$lastEpoch): bool {
            $epoch = $this->status()["epoch"];
            if ($epoch === $lastEpoch) {
                return false;
            }
            try {
                $this->consume($a);
                return $f->resolve($this);
            } catch (BudgetExhaustedException) {
                $lastEpoch = $epoch;
                return false;
            }
        }, $timeoutMs);
    }

    /** Give back (clamped at zero used); replenishes waiters. */
    public function release(Rational|int|float|string $amount): self
    {
        $a = Rational::from($amount);
        Native::check(Native::ffi()->zxv_rmag_release($this->budgets->capability->ptr(), $this->budgets->ptr(), $this->id, $a->numerator, $a->denominator), $this->budgets->context);
        return $this;
    }

    /** Replace the limit; a larger one replenishes waiters. */
    public function setLimit(Rational|int|float|string $limit): self
    {
        $l = Rational::from($limit);
        Native::check(Native::ffi()->zxv_rmag_set_limit($this->budgets->capability->ptr(), $this->budgets->ptr(), $this->id, $l->numerator, $l->denominator), $this->budgets->context);
        return $this;
    }

    /** @return array{remaining:Rational, used:Rational, epoch:int} */
    public function status(): array
    {
        $v = [];
        foreach (["rn", "rd", "un", "ud"] as $k) {
            $v[$k] = Native::new("int64_t");
        }
        $ep = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_rmag_status($this->budgets->ptr(), $this->id, \FFI::addr($v["rn"]), \FFI::addr($v["rd"]), \FFI::addr($v["un"]), \FFI::addr($v["ud"]), \FFI::addr($ep)), $this->budgets->context);
        return [
            "remaining" => Rational::of($v["rn"]->cdata, $v["rd"]->cdata),
            "used" => Rational::of($v["un"]->cdata, $v["ud"]->cdata),
            "epoch" => $ep->cdata,
        ];
    }

    public function remaining(): Rational
    {
        return $this->status()["remaining"];
    }

    public function used(): Rational
    {
        return $this->status()["used"];
    }

    /** Call $fn($budget) on every denied consume. */
    public function onExhausted(\Closure $fn): self
    {
        $this->onExhausted[] = $fn;
        return $this;
    }

    /** @internal */
    public function fire(int $event): void
    {
        if ($event === Budgets::EV_DENIED) {
            foreach ($this->onExhausted as $fn) {
                $fn($this);
            }
        }
    }
}
