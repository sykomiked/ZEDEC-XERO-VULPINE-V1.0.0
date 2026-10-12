<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * A capability: what a holder may do, verified by libzxv on EVERY gated
 * call. Objects that make gated calls (Ledger, Budgets, Swarm, ...) take one
 * when constructed. Narrow with attenuate(); there is no widening and no
 * global "current capability".
 *
 * Honest limit: inside one process this is least-privilege discipline, not
 * a sandbox.
 */
final class Capability extends NativeHandle
{
    public const LEDGER_READ = 1 << 0;
    public const LEDGER_POST = 1 << 1;
    public const CRYPTO_SIGN = 1 << 2;
    public const CRYPTO_KEM = 1 << 3;
    public const AI_INFERENCE = 1 << 4;
    public const BUDGET = 1 << 5;
    public const PHASE = 1 << 6;
    public const ATTEST = 1 << 7;
    public const SWARM = 1 << 8;
    public const EVM = 1 << 9;
    /** ABI 1.4: build and call the emulation matrix. */
    public const HW_MATRIX = 1 << 10;
    /** ABI 1.4: load hardware images and contracts. */
    public const HW_RECONF = 1 << 11;
    /** ZXV_CAP_ALL: the ABI 1.1 bits, a released value that never changes. */
    public const ALL = (1 << 10) - 1;
    /** ZXV_CAP_ALL_1_4: every bit as of ABI 1.4. */
    public const ALL_1_4 = (1 << 12) - 1;

    public const NAMES = [
        "ledger_read" => self::LEDGER_READ, "ledger_post" => self::LEDGER_POST, "crypto_sign" => self::CRYPTO_SIGN,
        "crypto_kem" => self::CRYPTO_KEM, "ai_inference" => self::AI_INFERENCE, "budget" => self::BUDGET,
        "phase" => self::PHASE, "attest" => self::ATTEST, "swarm" => self::SWARM, "evm" => self::EVM,
        "hw_matrix" => self::HW_MATRIX, "hw_reconf" => self::HW_RECONF,
    ];

    /** @internal use Context::rootCapability() */
    public static function create(Context $ctx, int $caps, int $processId, ?string $identity): self
    {
        $id = $identity === null ? null : Native::bytes(Native::exact($identity, "identity", 21));
        $out = Native::new("zxv_cap*");
        Native::check(Native::ffi()->zxv_cap_create($ctx->raw(), Native::uint32($processId, "processId"), $id, Native::uint($caps, "caps"), \FFI::addr($out)), $ctx);
        return new self($ctx, $out, "zxv_cap_destroy");
    }

    /** A child holding ($this & $keep). The parent must stay open while it is used. */
    public function attenuate(int $keep): self
    {
        $out = Native::new("zxv_cap*");
        Native::check(Native::ffi()->zxv_cap_attenuate($this->ptr(), Native::uint($keep, "keep"), \FFI::addr($out)), $this->context);
        return new self($this->context, $out, "zxv_cap_destroy");
    }

    /** Permanently revoke this capability and every descendant. */
    public function revoke(): void
    {
        Native::check(Native::ffi()->zxv_cap_revoke($this->ptr()), $this->context);
    }

    /** Anything but Truth::true() blocks gated calls (BOTH: "contradictory attestation"). */
    public function setAttestation(Truth $truth): void
    {
        Native::check(Native::ffi()->zxv_cap_set_attestation($this->ptr(), $truth->value), $this->context);
    }

    /** @return array{bits:int, names:list<string>, processId:int, attestation:Truth, revoked:bool} */
    public function info(): array
    {
        $caps = Native::new("uint64_t");
        $pid = Native::new("uint32_t");
        $att = Native::new("int32_t");
        $rev = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_cap_info($this->ptr(), \FFI::addr($caps), \FFI::addr($pid), \FFI::addr($att), \FFI::addr($rev)), $this->context);
        $bits = $caps->cdata;
        return [
            "bits" => $bits,
            "names" => array_keys(array_filter(self::NAMES, fn (int $b) => ($bits & $b) !== 0)),
            "processId" => $pid->cdata,
            "attestation" => Truth::of($att->cdata),
            "revoked" => $rev->cdata === 1,
        ];
    }

    /** @throws Exception\CapabilityDeniedException unless a call needing $needed would pass */
    public function check(int $needed): void
    {
        Native::check(Native::ffi()->zxv_cap_check($this->ptr(), Native::uint($needed, "needed")), $this->context);
    }

    public function allows(int $needed): bool
    {
        return Native::ffi()->zxv_cap_check($this->ptr(), Native::uint($needed, "needed")) === Status::OK;
    }
}
