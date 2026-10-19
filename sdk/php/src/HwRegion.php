<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/**
 * A reconfigurable region: hardware updates shipped as signed images,
 * staged into the inactive slot, read back, tested against the golden
 * vectors of its implementation in a matrix, then committed or rolled
 * back. Every step is an event and a record in a hash-chained audit log;
 * when the log fills, the region halts and the context is poisoned (fail
 * closed).
 *
 * The device side is three callables, each returning null/OK or a negative
 * status (a thrown ZXV exception returns its status, anything else
 * ZXV_E_IO):
 *   $load(int $slot, string $payload)
 *   $readback(int $slot): string     the SHA-256 (32 bytes) of what the slot holds
 *   $activate(int $slot)
 * They run on the calling thread inside libzxv and stay referenced until
 * the context closes.
 */
final class HwRegion extends NativeHandle
{
    use HwAuditLog;

    private ?\Throwable $eventError = null;

    /**
     * @param Capability $capability needs HW_RECONF
     * @param ?HwMatrix $matrix whose golden vectors test each image (null: no test)
     * @param int $provider the region's FABRIC or MICRO implementation in $matrix
     * @param ?string $targetDigest 32 bytes or null (zeros)
     * @param int $logCap the audit log size
     * @param callable(int, string): mixed $load
     * @param callable(int): string $readback
     * @param callable(int): mixed $activate
     */
    public function __construct(
        public readonly Capability $capability,
        ?HwMatrix $matrix,
        int $provider,
        int $regionId,
        int $targetClass,
        callable $load,
        callable $readback,
        callable $activate,
        ?string $targetDigest = null,
        int $logCap = 64,
    ) {
        $ctx = $capability->context;
        $td = $targetDigest === null ? null : Native::bytes(Native::exact($targetDigest, "targetDigest", 32));
        $loadFn = $ctx->retain(static function ($user, int $slot, ?CData $p, int $n) use ($load): int {
            return Hw::guard(static fn () => $load($slot, Hw::input($p, $n)));
        });
        $readbackFn = $ctx->retain(static function ($user, int $slot, CData $d) use ($readback): int {
            return Hw::guard(static function () use ($readback, $slot, $d): int {
                $digest = $readback($slot);
                if (!is_string($digest) || strlen($digest) !== 32) {
                    return Status::E_ARG;
                }
                \FFI::memcpy($d, $digest, 32);
                return Status::OK;
            });
        });
        $activateFn = $ctx->retain(static function ($user, int $slot) use ($activate): int {
            return Hw::guard(static fn () => $activate($slot));
        });
        $out = Native::new("zxv_hw_region*");
        Native::check(Native::ffi()->zxv_hw_region_create_cap(
            $capability->ptr(),
            $matrix?->ptr(),
            Native::uint32($provider, "provider"),
            Native::uint32($regionId, "regionId"),
            Native::uint32($targetClass, "targetClass"),
            $td,
            $loadFn,
            $readbackFn,
            $activateFn,
            null,
            Native::uint32($logCap, "logCap"),
            \FFI::addr($out),
        ), $ctx);
        parent::__construct($ctx, $out, "zxv_hw_region_destroy");
    }

    /**
     * Receive every recorded step, after the region call released its
     * locks: $fn(int $event (Hw::EV_*), int $reason, int $version). It may
     * call into the region again. Exceptions cannot cross libzxv: they are
     * kept, the last one in eventError().
     *
     * @param callable(int, int, int): mixed $fn
     */
    public function onEvent(callable $fn): self
    {
        $cb = $this->context->retain(function ($user, int $regionId, int $event, int $reason, int $version) use ($fn): void {
            try {
                $fn($event, $reason, $version);
            } catch (\Throwable $e) {
                $this->eventError = $e;
            }
        });
        Native::check(Native::ffi()->zxv_hw_region_events($this->capability->ptr(), $this->ptr(), $cb, null), $this->context);
        return $this;
    }

    /** The last exception an event callback threw, if any. */
    public function eventError(): ?\Throwable
    {
        return $this->eventError;
    }

    /** Trust a release key (at most 4). */
    public function trustKey(string $publicKey): self
    {
        $this->check(Native::ffi()->zxv_hw_region_trust_key($this->capability->ptr(), $this->ptr(), Native::bytes(Native::exact($publicKey, "publicKey", Hw::PK_BYTES))));
        return $this;
    }

    /**
     * Verify and load an image into the inactive slot. Refusals:
     * ZXV_E_VERIFY, ZXV_E_ROLLBACK, ZXV_E_QUARANTINED, ZXV_E_BLOCKED; usage:
     * ZXV_E_TARGET.
     */
    public function stage(string $image): self
    {
        $this->check(Native::ffi()->zxv_hw_region_stage($this->capability->ptr(), $this->ptr(), Native::bytes($image, true), strlen($image)));
        return $this;
    }

    /** Switch to the staged image and prove it; passing starts a probation of $ticks (ZXV_E_QUARANTINED if it fails). */
    public function trial(int $ticks): self
    {
        $this->check(Native::ffi()->zxv_hw_region_trial($this->capability->ptr(), $this->ptr(), Native::uint32($ticks, "ticks")));
        return $this;
    }

    /** Make the tested image permanent and raise the floor. */
    public function commit(): self
    {
        $this->check(Native::ffi()->zxv_hw_region_commit($this->capability->ptr(), $this->ptr()));
        return $this;
    }

    public function rollback(): self
    {
        $this->check(Native::ffi()->zxv_hw_region_rollback($this->capability->ptr(), $this->ptr()));
        return $this;
    }

    /** One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED). */
    public function tick(): self
    {
        $this->check(Native::ffi()->zxv_hw_region_tick($this->capability->ptr(), $this->ptr()));
        return $this;
    }

    /** @return array{state:int, version:int, floor:int, quarantined:int} state is Hw::REGION_* */
    public function info(): array
    {
        $st = Native::new("int32_t");
        $ver = Native::new("uint32_t");
        $floor = Native::new("uint32_t");
        $nq = Native::new("uint32_t");
        $this->check(Native::ffi()->zxv_hw_region_info($this->capability->ptr(), $this->ptr(), \FFI::addr($st), \FFI::addr($ver), \FFI::addr($floor), \FFI::addr($nq)));
        return ["state" => $st->cdata, "version" => $ver->cdata, "floor" => $floor->cdata, "quarantined" => $nq->cdata];
    }

    /** @return array{0:string,1:string} */
    protected function logFunctions(): array
    {
        return ["zxv_hw_region_log", "zxv_hw_region_log_entry"];
    }

    private function check(int $rc): void
    {
        Native::check($rc, $this->context);
    }
}
