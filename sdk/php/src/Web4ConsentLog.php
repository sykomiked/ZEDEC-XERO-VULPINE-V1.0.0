<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/** ABI 1.5: a single-use log of consumed and revoked consent tokens (zxv_w4_consent_log). Pure; NOT thread-safe. */
final class Web4ConsentLog extends PureHandle
{
    /** @param int $capacity 1..Web4::CONSENT_LOG_MAX */
    public function __construct(int $capacity)
    {
        $c = Native::uint32($capacity, "capacity");
        $this->own("zxv_w4_consent_log_destroy", fn (CData $o) => Native::ffi15()->zxv_w4_consent_log_create($c, $o));
    }

    /**
     * Check that $token lets $agentId do $action with scope $need for
     * $amount VFV minor units, signed by the human whose key is
     * $humanPublicKey, valid at $nowMs; then record it as used. Returning
     * means consent was given AND is now spent.
     * @param ?string $token null means "no token" (always refused)
     * @param string|int|list<string> $need Web4::CONSENT_SCOPES (nonzero)
     * @throws Exception\ConsentRequiredException no token, a mismatch, expired, a bad signature, or already used or revoked
     * @throws Exception\ZxvRefusedException ZXV_E_CAPACITY (full even after pruning: fail closed)
     */
    public function check(?string $token, string $humanPublicKey, string $agentId, string $action, string|int|array $need, int|string|float $amount, int|string|float $nowMs): self
    {
        $t = $token === null ? null : Web4::token($token);
        Native::check(Native::ffi15()->zxv_w4_consent_check($this->ptr(), $t, Native::fixed($humanPublicKey, "humanPublicKey", Web4::PK_BYTES),
            Web4::id32($agentId, "agentId"), Web4::id32($action, "action"), Web4::flags(Web4::CONSENT_SCOPES, $need, "need"),
            Native::u64($amount, "amount"), Native::u64($nowMs, "nowMs")));
        return $this;
    }

    /** Revoke a token before use (needs no capability; twice is fine). */
    public function revoke(string $token, int|string|float $nowMs): self
    {
        Native::check(Native::ffi15()->zxv_w4_consent_revoke($this->ptr(), Web4::token($token), Native::u64($nowMs, "nowMs")));
        return $this;
    }

    /** Drop entries whose token expired at or before $nowMs; returns entries left. */
    public function prune(int|string|float $nowMs): int
    {
        [$o, $oP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_w4_consent_log_prune($this->ptr(), Native::u64($nowMs, "nowMs"), $oP));
        return $o->cdata;
    }
}
