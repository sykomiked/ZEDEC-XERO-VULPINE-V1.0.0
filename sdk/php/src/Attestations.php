<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * An attestation registry (kernel lpres): named facts with four-valued
 * state and evidence counts. Actuator authority requires isGateClear().
 */
final class Attestations extends NativeHandle
{
    /** @param Capability $capability needs ATTEST for attest() and evidence() */
    public function __construct(public readonly Capability $capability)
    {
        $out = Native::new("zxv_lpres*");
        Native::check(Native::ffi()->zxv_lpres_create_cap($capability->ptr(), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_lpres_destroy");
    }

    /**
     * Record an attestation; returns its index. $name, $attester <= 31
     * bytes; $subject is lpres_subject_type_t (0 SOURCE .. 14 FIRMWARE);
     * $digest <= 32 bytes.
     */
    public function attest(string $name, int $subject, Truth $state, string $attester, string $digest = ""): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_lpres_attest($this->capability->ptr(), $this->ptr(), $name, $subject, $state->value, $attester, Native::bytes($digest, true), strlen($digest), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /** Add one piece of evidence. */
    public function evidence(int $index, bool $positive): void
    {
        Native::check(Native::ffi()->zxv_lpres_evidence($this->capability->ptr(), $this->ptr(), Native::uint32($index, "index"), $positive ? 1 : 0), $this->context);
    }

    /** Index of a named attestation, or null. */
    public function find(string $name): ?int
    {
        $out = Native::new("uint32_t");
        $rc = Native::ffi()->zxv_lpres_find($this->ptr(), $name, \FFI::addr($out));
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc, $this->context);
        return $out->cdata;
    }

    /** @return array{truth:Truth, positive:int, negative:int} */
    public function state(int $index): array
    {
        $st = Native::new("int32_t");
        $p = Native::new("uint32_t");
        $n = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_lpres_state($this->ptr(), Native::uint32($index, "index"), \FFI::addr($st), \FFI::addr($p), \FFI::addr($n)), $this->context);
        return ["truth" => Truth::of($st->cdata), "positive" => $p->cdata, "negative" => $n->cdata];
    }

    /** True when every attestation is certain: no BOTH, no NEITHER. */
    public function isGateClear(): bool
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_lpres_gate($this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata === 1;
    }
}
