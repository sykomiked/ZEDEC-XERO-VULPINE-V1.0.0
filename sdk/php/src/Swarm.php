<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\BudgetExhaustedException;

/**
 * The swarm token economy: each cycle's tokens are split across Fibonacci
 * levels (real axis) plus an emotional (imaginary) share by mood and
 * per-model feelings. All exact integers.
 */
final class Swarm extends NativeHandle
{
    /** @param Capability $capability needs SWARM */
    public function __construct(public readonly Capability $capability, int $levels, int $tokensPerCycle)
    {
        $out = Native::new("zxv_swarm*");
        Native::check(Native::ffi()->zxv_swarm_create_cap($capability->ptr(), Native::uint32($levels, "levels"), Native::uint($tokensPerCycle, "tokensPerCycle"), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_swarm_destroy");
    }

    /** Capacity of level $level (0-based): F(level + 2). */
    public static function levelCapacity(int $level): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_swarm_level_capacity(Native::uint32($level, "level"), \FFI::addr($out)));
        return $out->cdata;
    }

    /**
     * Behaviour profile of an emoji code point of the kernel palette.
     * @return array{valence:int, temperatureMilli:int, explorePermille:int, verifyPasses:int, priority:int}
     */
    public static function emotionProfile(int $codepoint): array
    {
        $v = Native::new("int32_t");
        $o = [];
        foreach (["t", "x", "p", "pr"] as $k) {
            $o[$k] = Native::new("uint32_t");
        }
        Native::check(Native::ffi()->zxv_swarm_emotion_profile(Native::uint32($codepoint, "codepoint"), \FFI::addr($v), \FFI::addr($o["t"]), \FFI::addr($o["x"]), \FFI::addr($o["p"]), \FFI::addr($o["pr"])));
        return ["valence" => $v->cdata, "temperatureMilli" => $o["t"]->cdata, "explorePermille" => $o["x"]->cdata,
            "verifyPasses" => $o["p"]->cdata, "priority" => $o["pr"]->cdata];
    }

    /** Register a model at a level (ZXV_E_CAPACITY, refused, when full). */
    public function register(int $modelId, int $level): self
    {
        Native::check(Native::ffi()->zxv_swarm_register($this->capability->ptr(), $this->ptr(), Native::uint32($modelId, "modelId"), Native::uint32($level, "level")), $this->context);
        return $this;
    }

    public function setActive(int $modelId, bool $active): self
    {
        Native::check(Native::ffi()->zxv_swarm_set_active($this->capability->ptr(), $this->ptr(), Native::uint32($modelId, "modelId"), $active ? 1 : 0), $this->context);
        return $this;
    }

    public function setTokensPerCycle(int $n): self
    {
        Native::check(Native::ffi()->zxv_swarm_set_rate($this->capability->ptr(), $this->ptr(), Native::uint($n, "tokensPerCycle")), $this->context);
        return $this;
    }

    /** Mood: an emoji code point of the palette, intensity 0..5. */
    public function setMood(int $codepoint, int $intensity): self
    {
        Native::check(Native::ffi()->zxv_swarm_set_mood($this->capability->ptr(), $this->ptr(), Native::uint32($codepoint, "codepoint"), Native::int($intensity, "intensity", 0, 5)), $this->context);
        return $this;
    }

    public function setFeeling(int $modelId, int $codepoint, int $intensity): self
    {
        Native::check(Native::ffi()->zxv_swarm_set_feeling($this->capability->ptr(), $this->ptr(), Native::uint32($modelId, "modelId"), Native::uint32($codepoint, "codepoint"), Native::int($intensity, "intensity", 0, 5)), $this->context);
        return $this;
    }

    /** Start a cycle; returns its number. */
    public function beginCycle(): int
    {
        $out = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_swarm_begin_cycle($this->capability->ptr(), $this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /**
     * Take $requested tokens; returns the number granted.
     * @throws BudgetExhaustedException when fewer were left; getGranted() is what was granted
     */
    public function consume(int $modelId, int $requested): int
    {
        $out = Native::new("uint64_t");
        $rc = Native::ffi()->zxv_swarm_consume($this->capability->ptr(), $this->ptr(), Native::uint32($modelId, "modelId"), Native::uint($requested, "requested"), \FFI::addr($out));
        if ($rc === Status::E_BUDGET) {
            $err = Native::errorFor($rc);
            assert($err instanceof BudgetExhaustedException);
            throw $err->withGranted($out->cdata);
        }
        Native::check($rc, $this->context);
        return $out->cdata;
    }

    /** @return array{allotted:int, imaginary:int, used:int} this cycle's allotment */
    public function allotment(int $modelId): array
    {
        $a = Native::new("uint64_t");
        $i = Native::new("uint64_t");
        $u = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_swarm_allotment($this->ptr(), Native::uint32($modelId, "modelId"), \FFI::addr($a), \FFI::addr($i), \FFI::addr($u)), $this->context);
        return ["allotted" => $a->cdata, "imaginary" => $i->cdata, "used" => $u->cdata];
    }

    /** Close the cycle; returns unused tokens (they never carry over). */
    public function endCycle(): int
    {
        $out = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_swarm_end_cycle($this->capability->ptr(), $this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }
}
