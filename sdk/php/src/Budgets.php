<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * A set of exact rational resource budgets (kernel rmag, K2).
 *
 *     $budgets = new Budgets($cap);
 *     $tokens = $budgets->budget("tokens", Budgets::TOKENS, 10);
 *     $tokens->consume(Rational::of(7, 2));
 */
final class Budgets extends NativeHandle
{
    public const COMPUTE_CYCLES = 0;
    public const MEMORY = 1;
    public const BANDWIDTH = 2;
    public const ENERGY = 3;
    public const STORAGE = 4;
    public const TOKENS = 12;

    public const EV_DENIED = 1;
    public const EV_INVALID = 2;
    public const EV_REPLENISHED = 3;

    /** @var array<int, Budget> */
    private array $budgets = [];
    private \Closure $listener;

    /** @param Capability $capability needs BUDGET */
    public function __construct(public readonly Capability $capability)
    {
        $out = Native::new("zxv_rmag*");
        Native::check(Native::ffi()->zxv_rmag_create_cap($capability->ptr(), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_rmag_destroy");
        // Kept as a property: the C side holds this callback while the rmag lives.
        $this->listener = function ($user, int $budget, int $event): void {
            ($this->budgets[$budget] ?? null)?->fire($event);
        };
        Native::check(Native::ffi()->zxv_rmag_listen($this->ptr(), $this->listener, null), $this->context);
    }

    /**
     * A budget with an exact limit (>= 0); $enforce = false only counts.
     * @param int $resource one of the resource constants
     */
    public function budget(string $name, int $resource, Rational|int|float|string $limit, bool $enforce = true): Budget
    {
        $l = Rational::from($limit);
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_rmag_budget($this->capability->ptr(), $this->ptr(), $name, $resource, $l->numerator, $l->denominator, $enforce ? 1 : 0, \FFI::addr($out)), $this->context);
        return $this->budgets[$out->cdata] = new Budget($this, $out->cdata, $name);
    }
}
