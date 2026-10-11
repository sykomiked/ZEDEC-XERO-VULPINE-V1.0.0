<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\TruthConversionException;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * Four-valued (Belnap / K3 paraconsistent) logic: the kernel's
 * lpres_state_t. Exactly four values, numbered as the kernel numbers them:
 * NEITHER = 0, TRUE = 1, FALSE = 2, BOTH = 3. BOTH is a held contradiction,
 * not an error and not null.
 *
 * No truthiness shortcut: every PHP object is truthy, so `if ($truth)` is
 * ALWAYS taken, even for Truth::false(). Never write it. Use an explicit
 * policy: isCertainlyTrue(), toBool(onBoth:, onNeither:) or requireTrue().
 *
 * Instances are interned (compare with ===). A class, not an enum, so the
 * kernel's coming orthogonal 2-bit mode can be added ({@see $mode}, 0 today).
 */
final class Truth implements \Stringable
{
    public const NEITHER = 0;
    public const TRUE = 1;
    public const FALSE = 2;
    public const BOTH = 3;
    private const NAMES = ["NEITHER", "TRUE", "FALSE", "BOTH"];

    /** @var array<int, self> */
    private static array $all = [];

    private function __construct(
        /** 0..3 */
        public readonly int $value,
        /** Reserved for the kernel's 2-bit mode; always 0 in ABI 1.1. */
        public readonly int $mode = 0,
    ) {
    }

    /** The Truth for a kernel value 0..3. */
    public static function of(int $value): self
    {
        if ($value < 0 || $value > 3) {
            throw new ZxvUsageException(Status::E_ARG, "truth value must be 0..3, got $value");
        }
        return self::$all[$value] ??= new self($value);
    }

    public static function neither(): self
    {
        return self::of(self::NEITHER);
    }

    public static function true(): self
    {
        return self::of(self::TRUE);
    }

    public static function false(): self
    {
        return self::of(self::FALSE);
    }

    public static function both(): self
    {
        return self::of(self::BOTH);
    }

    /** @return list<self> NEITHER, TRUE, FALSE, BOTH */
    public static function all(): array
    {
        return [self::neither(), self::true(), self::false(), self::both()];
    }

    /** From evidence counts (both -> BOTH, none -> NEITHER). */
    public static function fromEvidence(int $positive, int $negative): self
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_lp_from_evidence(Native::uint32($positive, "positive"), Native::uint32($negative, "negative"), \FFI::addr($out)));
        return self::of($out->cdata);
    }

    public function name(): string
    {
        return self::NAMES[$this->value];
    }

    public function __toString(): string
    {
        return $this->name();
    }

    /** Four-valued conjunction (libzxv table). */
    public function and(self $other): self
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_lp_and($this->value, $other->value, \FFI::addr($out)));
        return self::of($out->cdata);
    }

    /** Four-valued disjunction (libzxv table). */
    public function or(self $other): self
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_lp_or($this->value, $other->value, \FFI::addr($out)));
        return self::of($out->cdata);
    }

    /** Four-valued negation: TRUE <-> FALSE; BOTH and NEITHER stay. */
    public function not(): self
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_lp_not($this->value, \FFI::addr($out)));
        return self::of($out->cdata);
    }

    public function isCertainlyTrue(): bool
    {
        return $this->value === self::TRUE;
    }

    public function isCertainlyFalse(): bool
    {
        return $this->value === self::FALSE;
    }

    /** TRUE or FALSE: no contradiction, no gap. */
    public function isCertain(): bool
    {
        return $this->value === self::TRUE || $this->value === self::FALSE;
    }

    public function isBoth(): bool
    {
        return $this->value === self::BOTH;
    }

    public function isNeither(): bool
    {
        return $this->value === self::NEITHER;
    }

    /** Collapse to bool with an explicit policy for BOTH and NEITHER. */
    public function toBool(bool $onBoth, bool $onNeither): bool
    {
        return match ($this->value) {
            self::TRUE => true,
            self::FALSE => false,
            self::BOTH => $onBoth,
            default => $onNeither,
        };
    }

    /** @throws TruthConversionException unless TRUE */
    public function requireTrue(): self
    {
        if ($this->value !== self::TRUE) {
            throw new TruthConversionException("expected TRUE, got " . $this->name());
        }
        return $this;
    }
}
