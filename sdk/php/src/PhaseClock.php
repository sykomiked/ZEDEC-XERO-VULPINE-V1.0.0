<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * The K6 phase coordinator's logical clock. ZXV time is a tick count; one
 * harmonic fundamental is CYCLE = 27,720 ticks. Drive it manually with
 * advance() (tests, simulations) or from wall time with startRealtime(),
 * a Loop ticker.
 */
final class PhaseClock extends NativeHandle
{
    public const CYCLE = 27720;

    public const ADMIT = 0;
    public const VETO = 1;
    public const DEFER = 2;
    public const RETRY = 3;

    public const STEP_REQUIRES_HEALTH = 0x1;
    public const STEP_HARDWARE = 0x2;
    public const STEP_S0_PENDING = 0x4;

    public const HEALTH_UNKNOWN = 0;
    public const HEALTH_HEALTHY = 1;
    public const HEALTH_DEGRADED = 2;
    public const HEALTH_UNHEALTHY = 3;

    private ?int $ticker = null;

    /** @param Capability $capability needs PHASE for advance/admit/defer */
    public function __construct(public readonly Capability $capability)
    {
        $out = Native::new("zxv_phase*");
        Native::check(Native::ffi()->zxv_phase_create_cap($capability->ptr(), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_phase_destroy");
    }

    public function now(): int
    {
        $out = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_phase_now($this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /** Advance by $delta ticks; returns how many deferred events cleared. */
    public function advance(int $delta): int
    {
        $now = Native::new("uint64_t");
        $c = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_phase_advance($this->capability->ptr(), $this->ptr(), Native::uint($delta, "delta"), \FFI::addr($now), \FFI::addr($c)), $this->context);
        return $c->cdata;
    }

    /**
     * Ask to admit a step of phase $phaseId (1..13).
     * @return array{decision:int, token:int} decision is ADMIT/VETO/DEFER/RETRY; token non-zero only on ADMIT
     */
    public function admit(int $phaseId, int $stepId, int $flags = 0): array
    {
        $d = Native::new("int32_t");
        $t = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_phase_admit($this->capability->ptr(), $this->ptr(), Native::uint32($phaseId, "phaseId"), Native::uint32($stepId, "stepId"), Native::uint32($flags, "flags"), \FFI::addr($d), \FFI::addr($t)), $this->context);
        return ["decision" => $d->cdata, "token" => $t->cdata];
    }

    /** Report a phase's health (HEALTH_* constants). */
    public function setHealth(int $phaseId, int $health): void
    {
        Native::check(Native::ffi()->zxv_phase_set_health($this->capability->ptr(), $this->ptr(), Native::uint32($phaseId, "phaseId"), $health), $this->context);
    }

    /** Park an S0 event until absolute tick $dueTick (> now). */
    public function defer(int $dueTick): DeferredEvent
    {
        $out = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_phase_defer($this->capability->ptr(), $this->ptr(), Native::uint($dueTick, "dueTick"), \FFI::addr($out)), $this->context);
        return new DeferredEvent($this, $out->cdata);
    }

    /** Earliest due tick of a pending event, or null. */
    public function nextDue(): ?int
    {
        $out = Native::new("uint64_t");
        $rc = Native::ffi()->zxv_phase_next_due($this->ptr(), \FFI::addr($out));
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc, $this->context);
        return $out->cdata;
    }

    /** Advance from wall time at $ticksPerSecond, on every Loop tick. */
    public function startRealtime(int $ticksPerSecond): void
    {
        $tps = Native::int($ticksPerSecond, "ticksPerSecond", 1);
        $this->stopRealtime();
        $t0 = hrtime(true);
        $done = 0;
        $this->ticker = Loop::addTicker(function () use ($t0, $tps, &$done): void {
            $due = intdiv((hrtime(true) - $t0) * $tps, 1_000_000_000);
            if ($due > $done) {
                $this->advance($due - $done);
                $done = $due;
            }
        });
    }

    public function stopRealtime(): void
    {
        if ($this->ticker !== null) {
            Loop::removeTicker($this->ticker);
            $this->ticker = null;
        }
    }

    public function isRealtime(): bool
    {
        return $this->ticker !== null;
    }

    public function close(): void
    {
        $this->stopRealtime();
        parent::close();
    }
}
