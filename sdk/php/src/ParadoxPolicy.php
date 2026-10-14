<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: a constructive policy (zxv_paradox_policy): n parameters
 * (1..Paradox::POLICY_MAX) with published bounds lo[i] <= hi[i] that never
 * change. The values are swarm parameters (budget shares, routing weights,
 * escrow timing), never balances. A policy flagged money or device is
 * never changed automatically: every update becomes a proposal for the
 * owner. NOT thread-safe.
 */
final class ParadoxPolicy extends NativeHandle
{
    /** Number of parameters. */
    public readonly int $size;

    /**
     * @param list<int> $lo lower bounds (int64)
     * @param list<int> $hi upper bounds
     * @param list<int> $initial initial values, inside the bounds
     * @param string|int|list<string> $flags Paradox::FLAGS
     * @throws ZxvUsageException ZXV_E_RANGE (lo > hi, or an initial value outside)
     */
    public function __construct(Capability $capability, array $lo, array $hi, array $initial, string|int|array $flags = 0)
    {
        if (count($lo) !== count($hi) || count($hi) !== count($initial)) {
            throw new ZxvUsageException(Status::E_ARG, "lo, hi and initial must have one length");
        }
        $this->size = count($lo);
        $out = Native::new("zxv_paradox_policy*");
        Native::check(Native::ffi15()->zxv_paradox_policy_create($capability->ptr(), $this->size, $this->valuesMem($lo, "lo"), $this->valuesMem($hi, "hi"),
            $this->valuesMem($initial, "initial"), Paradox::flags($flags), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_paradox_policy_destroy");
    }

    /** @return list<int> the live values */
    public function values(): array
    {
        return $this->readValues()[0];
    }

    /** @return list<int> the values before the last applied update */
    public function previous(): array
    {
        return $this->readValues()[1];
    }

    /** Whether an applied update awaits evaluation. */
    public function isArmed(): bool
    {
        return $this->readValues()[2];
    }

    /** The tension the armed update was applied at. */
    public function tensionAt(): int
    {
        return $this->readValues()[3];
    }

    /** @return array{0:list<int>,1:list<int>} [lo, hi] */
    public function bounds(): array
    {
        $n = max($this->size, 1);
        $lo = Native::new("int64_t[$n]");
        $hi = Native::new("int64_t[$n]");
        Native::check(Native::ffi15()->zxv_paradox_policy_bounds($this->ptr(), $lo, $hi, $this->size, null), $this->context);
        return [self::list($lo, $this->size), self::list($hi, $this->size)];
    }

    /** @return list<string> Paradox::FLAGS names */
    public function flags(): array
    {
        [$f, $fP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_paradox_policy_bounds($this->ptr(), null, null, $this->size, $fP), $this->context);
        return Web4::flagNames(Paradox::FLAGS, $f->cdata);
    }

    /**
     * A proposed update as bytes for the owner or for signing (12 + 32 *
     * size bytes, little-endian: n, how many values are out of bounds,
     * flags, then lo, hi, current, proposed per parameter). Reads only.
     * @param list<int> $synth
     */
    public function proposal(array $synth): string
    {
        $s = $this->valuesMem($synth, "synth");
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_paradox_policy_proposal($this->ptr(), $s, $this->size, $b, $c, $l), 12 + 32 * $this->size, $this->context);
    }

    /**
     * @internal
     * @param list<int> $list
     */
    public function valuesMem(array $list, string $name): CData
    {
        if (count($list) !== $this->size) {
            throw new ZxvUsageException(Status::E_ARG, "$name must have {$this->size} values");
        }
        $m = Native::new("int64_t[" . max($this->size, 1) . "]");
        foreach (array_values($list) as $i => $v) {
            $m[$i] = Native::i64($v, "{$name}[$i]");
        }
        return $m;
    }

    /** @return array{0:list<int>,1:list<int>,2:bool,3:int} */
    private function readValues(): array
    {
        $n = max($this->size, 1);
        $v = Native::new("int64_t[$n]");
        $p = Native::new("int64_t[$n]");
        [$a, $aP] = Native::out("int32_t");
        [$t, $tP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_paradox_policy_values($this->ptr(), $v, $p, $this->size, $aP, $tP), $this->context);
        return [self::list($v, $this->size), self::list($p, $this->size), $a->cdata !== 0, $t->cdata];
    }

    /** @return list<int> */
    private static function list(CData $a, int $n): array
    {
        $out = [];
        for ($i = 0; $i < $n; $i++) {
            $out[] = $a[$i];
        }
        return $out;
    }
}
