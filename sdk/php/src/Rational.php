<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\FloatRejectedException;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * An exact rational number: the kernel's rat_t (orbital_compat IR numeric
 * normal form). Immutable, always reduced, denominator > 0, numerator and
 * denominator fit in int64. Arithmetic runs in libzxv, so overflow throws
 * (ZXV_E_RANGE) instead of wrapping.
 *
 *     Rational::parse("0.1")->add(Rational::of(1, 5));   // 3/10 exactly
 */
final class Rational implements \Stringable
{
    private function __construct(
        /** Numerator (carries the sign). */
        public readonly int $numerator,
        /** Denominator (> 0). */
        public readonly int $denominator,
    ) {
    }

    /**
     * From integers (reduced). Floats throw FloatRejectedException.
     */
    public static function of(int|float $numerator, int|float $denominator = 1): self
    {
        $n = Native::int($numerator, "numerator");
        $d = Native::int($denominator, "denominator");
        $on = Native::new("int64_t");
        $od = Native::new("int64_t");
        Native::check(Native::ffi()->zxv_rat_normalize($n, $d, \FFI::addr($on), \FFI::addr($od)));
        return new self($on->cdata, $od->cdata);
    }

    /**
     * Parse exact text: "12.75", "-0.1", "3/4", "42". No exponents, spaces or "NaN".
     * @throws ZxvUsageException ZXV_E_FORMAT, ZXV_E_RANGE or ZXV_E_ARG
     */
    public static function parse(string $text): self
    {
        $on = Native::new("int64_t");
        $od = Native::new("int64_t");
        Native::check(Native::ffi()->zxv_rat_parse(Native::utf8($text, "text"), \FFI::addr($on), \FFI::addr($od)));
        return new self($on->cdata, $od->cdata);
    }

    /**
     * Coerce an exact value: a Rational, an int, or exact numeric text.
     * Floats throw FloatRejectedException.
     */
    public static function from(self|int|float|string $value): self
    {
        return match (true) {
            $value instanceof self => $value,
            is_float($value) => throw new FloatRejectedException(),
            is_int($value) => self::of($value),
            default => self::parse($value),
        };
    }

    /**
     * THE FLOAT BOUNDARY: the exact value of a binary64 float. 0.1 becomes
     * 3602879701896397/36028797018963968, not 1/10: the drift is visible.
     * @throws FloatRejectedException for NaN and infinities
     * @throws ZxvUsageException ZXV_E_RANGE if the exact value needs > 63 bits
     */
    public static function fromDoubleExact(float $x): self
    {
        $bits = unpack("q", pack("d", $x))[1];
        return self::fromIeee754Bits($bits);
    }

    /** The same from the raw IEEE-754 bits (as a signed 64-bit int). */
    public static function fromIeee754Bits(int $bits): self
    {
        $on = Native::new("int64_t");
        $od = Native::new("int64_t");
        Native::check(Native::ffi()->zxv_rat_from_ieee754($bits, \FFI::addr($on), \FFI::addr($od)));
        return new self($on->cdata, $od->cdata);
    }

    public function add(self|int|float|string $other): self
    {
        return $this->op("zxv_rat_add", $other);
    }

    public function sub(self|int|float|string $other): self
    {
        return $this->op("zxv_rat_sub", $other);
    }

    public function mul(self|int|float|string $other): self
    {
        return $this->op("zxv_rat_mul", $other);
    }

    /** @throws ZxvUsageException ZXV_E_ARG on division by zero */
    public function div(self|int|float|string $other): self
    {
        return $this->op("zxv_rat_div", $other);
    }

    public function negate(): self
    {
        return self::of(-$this->numerator, $this->denominator);
    }

    /** Exact comparison: -1, 0 or 1. No epsilon. */
    public function compare(self|int|float|string $other): int
    {
        $o = self::from($other);
        $c = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_rat_cmp($this->numerator, $this->denominator, $o->numerator, $o->denominator, \FFI::addr($c)));
        return $c->cdata;
    }

    /** Same value (both are stored reduced). */
    public function equals(self|int|float|string $other): bool
    {
        $o = self::from($other);
        return $o->numerator === $this->numerator && $o->denominator === $this->denominator;
    }

    public function isZero(): bool
    {
        return $this->numerator === 0;
    }

    public function isInteger(): bool
    {
        return $this->denominator === 1;
    }

    /** Exact text: "12.75" when a terminating decimal exists, else "1/3". */
    public function __toString(): string
    {
        [$rc, $s] = Native::text(fn ($buf, $cap, $len) => Native::ffi()->zxv_rat_format($this->numerator, $this->denominator, $buf, $cap, $len));
        Native::check($rc);
        return (string) $s;
    }

    /** Q16.16 fixed point (round half away from zero), for engine parameters. */
    public function toQ16(): int
    {
        $q = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_rat_to_q16($this->numerator, $this->denominator, \FFI::addr($q)));
        return $q->cdata;
    }

    // There is deliberately no toFloat().

    private function op(string $fn, self|int|float|string $other): self
    {
        $o = self::from($other);
        $on = Native::new("int64_t");
        $od = Native::new("int64_t");
        Native::check(Native::ffi()->$fn($this->numerator, $this->denominator, $o->numerator, $o->denominator, \FFI::addr($on), \FFI::addr($od)));
        return new self($on->cdata, $od->cdata);
    }
}
