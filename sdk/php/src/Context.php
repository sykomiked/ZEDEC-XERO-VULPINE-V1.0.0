<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvFatalError;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * One platform instance: a ledger, its currencies, and every handle made
 * from it. Closing it (or dropping the last reference) frees every child
 * handle first, in reverse creation order.
 *
 *     $ctx = new Context();
 *     $cap = $ctx->rootCapability();
 *     ...
 *     $ctx->close();
 */
final class Context
{
    private ?CData $ptr;
    private bool $dead = false;
    /** @var array<int, array{0:string,1:CData}> */
    private array $children = [];
    private int $nextChild = 0;
    /** @var list<\Closure> FFI callbacks native handles hold */
    private array $retained = [];

    public function __construct()
    {
        $out = Native::new("zxv_ctx*");
        Native::check(Native::ffi()->zxv_ctx_create(\FFI::addr($out)));
        $this->ptr = $out;
    }

    public function __destruct()
    {
        $this->close();
    }

    /**
     * A root capability. $caps: Capability::ALL (default; the ABI 1.1 bits),
     * Capability::ALL_1_4 (adds HW_MATRIX and HW_RECONF) or an OR of
     * Capability::LEDGER_READ etc. $identity: 21 bytes or null (zeros).
     */
    public function rootCapability(int $caps = Capability::ALL, ?int $processId = null, ?string $identity = null): Capability
    {
        return Capability::create($this, $caps, $processId ?? getmypid(), $identity);
    }

    /** True once the context halted (FATAL). */
    public function isPoisoned(): bool
    {
        if ($this->dead) {
            return true;
        }
        if ($this->ptr === null) {
            throw new ZxvUsageException(Status::E_STATE, "context is closed");
        }
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_ctx_is_poisoned($this->ptr, \FFI::addr($out)), $this);
        $this->dead = $out->cdata === 1;
        return $this->dead;
    }

    /** Halt the context on purpose (operator kill switch); reason <= 150 bytes. */
    public function halt(?string $reason = null): void
    {
        Native::check(Native::ffi()->zxv_ctx_halt($this->raw(), $reason), $this);
        $this->dead = true;
    }

    /** Free the context and every handle made from it. Idempotent. */
    public function close(): void
    {
        if ($this->ptr === null) {
            return;
        }
        krsort($this->children);
        foreach ($this->children as [$fn, $p]) {
            Native::ffi()->$fn($p);
        }
        $this->children = [];
        Native::ffi()->zxv_ctx_destroy($this->ptr);
        $this->ptr = null;
        $this->retained = [];
    }

    public function isClosed(): bool
    {
        return $this->ptr === null;
    }

    // --- currencies -------------------------------------------------------

    /**
     * Enable an ISO 4217 currency; $minorUnits only asserts the expected value.
     * $cap needs LEDGER_POST and must belong to this context (else
     * CapabilityDeniedException). Without $cap this is deprecated since ABI
     * 1.2 and raises E_USER_DEPRECATED.
     */
    public function enableCurrency(string $alpha, ?int $minorUnits = null, ?Capability $cap = null): Currency
    {
        $mu = $minorUnits === null ? null : Native::int($minorUnits, "minorUnits", 0, 255);
        if ($cap !== null) {
            $c = $this->ownCap($cap);
            $rc = $mu === null
                ? Native::ffi()->zxv_ccy_enable_cap($c, $alpha)
                : Native::ffi()->zxv_ccy_enable_with_minor_cap($c, $alpha, $mu);
        } else {
            trigger_error("Context::enableCurrency() without \$cap is deprecated since ABI 1.2: pass cap: (a Capability with LEDGER_POST)", E_USER_DEPRECATED);
            $rc = $mu === null
                ? Native::ffi()->zxv_ccy_enable($this->raw(), $alpha)
                : Native::ffi()->zxv_ccy_enable_with_minor($this->raw(), $alpha, $mu);
        }
        Native::check($rc, $this);
        return $this->currency($alpha) ?? throw new ZxvUsageException(Status::E_CURRENCY, $alpha);
    }

    /**
     * Register a platform-private unit (e.g. "VFV", 2): ledger only, never ISO
     * 20022. $cap needs LEDGER_POST; without it this is deprecated since ABI 1.2.
     */
    public function registerPrivateCurrency(string $code, int $minorUnits, ?Capability $cap = null): Currency
    {
        $mu = Native::int($minorUnits, "minorUnits", 0, 255);
        if ($cap !== null) {
            $rc = Native::ffi()->zxv_ccy_register_private_cap($this->ownCap($cap), $code, $mu);
        } else {
            trigger_error("Context::registerPrivateCurrency() without \$cap is deprecated since ABI 1.2: pass cap: (a Capability with LEDGER_POST)", E_USER_DEPRECATED);
            $rc = Native::ffi()->zxv_ccy_register_private($this->raw(), $code, $mu);
        }
        Native::check($rc, $this);
        return $this->currency($code) ?? throw new ZxvUsageException(Status::E_CURRENCY, $code);
    }

    /** The handle of $cap, after checking it belongs to this context. */
    private function ownCap(Capability $cap): \FFI\CData
    {
        if ($cap->context !== $this) {
            throw new Exception\CapabilityDeniedException(Status::E_CAP, "the capability belongs to a different context");
        }
        $this->raw();
        return $cap->ptr();
    }

    /** An enabled currency, or null. */
    public function currency(string $code): ?Currency
    {
        $minor = Native::new("uint8_t");
        $flags = Native::new("uint32_t");
        $rc = Native::ffi()->zxv_ccy_get($this->raw(), $code, \FFI::addr($minor), \FFI::addr($flags));
        if ($rc === Status::E_NOT_FOUND || $rc === Status::E_CURRENCY) {
            return null;
        }
        Native::check($rc, $this);
        return new Currency($code, $minor->cdata, $flags->cdata);
    }

    /** @internal */
    public function raw(): CData
    {
        if ($this->ptr === null) {
            throw new ZxvUsageException(Status::E_STATE, "context is closed");
        }
        if ($this->dead) {
            // The SDK refuses a dead context itself, so even the few C calls
            // that do not consult the poison flag are refused.
            $err = new ZxvFatalError(Status::E_POISONED, "context halted earlier; this handle is dead");
            Zxv::onFatal($err);
            throw $err;
        }
        return $this->ptr;
    }

    /** @internal */
    public function markDead(): void
    {
        $this->dead = true;
    }

    /**
     * @internal keep an FFI callback referenced until the context closes, so
     * libzxv never calls a freed closure.
     */
    public function retain(\Closure $fn): \Closure
    {
        $this->retained[] = $fn;
        return $fn;
    }

    /** @internal register a child handle; returns its id */
    public function adopt(string $destroyFn, CData $ptr): int
    {
        $id = ++$this->nextChild;
        $this->children[$id] = [$destroyFn, $ptr];
        return $id;
    }

    /** @internal free one child handle now */
    public function release(int $id): void
    {
        if (isset($this->children[$id])) {
            [$fn, $p] = $this->children[$id];
            unset($this->children[$id]);
            Native::ffi()->$fn($p);
        }
    }
}
