<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * The emulation matrix: functions (engine contracts) served by
 * interchangeable implementations, each serving only after it reproduces
 * the function's golden vectors bit for bit. A new matrix already holds the
 * standard engines (Hw::FN_*), each with a proven SOFT implementation on
 * Hw::RES_HOST, so every call works on any device.
 *
 *     $mx = new HwMatrix($cap);                    // needs HW_MATRIX
 *     $digest = $mx->call(Hw::FN_SHA256, "abc");
 */
final class HwMatrix extends NativeHandle
{
    use HwAuditLog;

    /** @param Capability $capability needs HW_MATRIX */
    public function __construct(public readonly Capability $capability)
    {
        $out = Native::new("zxv_hw_matrix*");
        Native::check(Native::ffi()->zxv_hw_matrix_create_cap($capability->ptr(), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_hw_matrix_destroy");
    }

    /** Add a function; $name: UTF-8, at most 23 bytes. */
    public function addFunction(int $fnId, ?string $name = null): self
    {
        $this->check(Native::ffi()->zxv_hw_function_add($this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), $name === null ? null : Native::utf8($name, "name")));
        return $this;
    }

    /** Add a golden vector (output at most 512 bytes); every implementation becomes unproven. */
    public function addVector(int $fnId, string $input, string $expected, int $op = 0): self
    {
        $this->check(Native::ffi()->zxv_hw_vector_add(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($fnId, "fnId"),
            Native::uint32($op, "op"),
            Native::bytes($input, true),
            strlen($input),
            Native::bytes($expected, true),
            strlen($expected),
        ));
        return $this;
    }

    /** Start contract $version (strictly newer, else ZXV_E_ROLLBACK); every implementation must prove again. */
    public function startContract(int $fnId, int $version): self
    {
        $this->check(Native::ffi()->zxv_hw_function_contract($this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), Native::uint32($version, "version")));
        return $this;
    }

    /**
     * Placement: Hw::POLICY_* flags, a latency ceiling and a power budget (0:
     * none). Clearing KEY_BEARING once set throws ZxvRefusedException (ZXV_E_POLICY).
     */
    public function setPolicy(int $fnId, int $flags = 0, int $maxLatencyNs = 0, int $powerBudgetMw = 0): self
    {
        $this->check(Native::ffi()->zxv_hw_policy_set(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($fnId, "fnId"),
            Native::uint32($flags, "flags"),
            Native::uint32($maxLatencyNs, "maxLatencyNs"),
            Native::uint32($powerBudgetMw, "powerBudgetMw"),
        ));
        return $this;
    }

    /** @return array{contract:int, policy:int, vectors:int} */
    public function functionInfo(int $fnId): array
    {
        $c = Native::new("uint32_t");
        $p = Native::new("uint32_t");
        $v = Native::new("uint32_t");
        $this->check(Native::ffi()->zxv_hw_function_info($this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), \FFI::addr($c), \FFI::addr($p), \FFI::addr($v)));
        return ["contract" => $c->cdata, "policy" => $p->cdata, "vectors" => $v->cdata];
    }

    public function addResource(int $resId): self
    {
        $this->check(Native::ffi()->zxv_hw_resource_add($this->capability->ptr(), $this->ptr(), Native::uint32($resId, "resId")));
        return $this;
    }

    /** @param int $state Hw::RES_* */
    public function setResource(int $resId, int $state): self
    {
        $this->check(Native::ffi()->zxv_hw_resource_set($this->capability->ptr(), $this->ptr(), Native::uint32($resId, "resId"), Hw::int32($state, "state")));
        return $this;
    }

    /**
     * Add an implementation of $fnId on resource $resId; returns its index.
     *
     * $impl(int $op, string $input) returns the output bytes, or a negative
     * status; throwing a ZXV exception returns its status to the caller of
     * call() (any other exception: ZXV_E_IO). It runs on the calling thread
     * inside libzxv and may call() only the functions it declared with
     * addDependency(). It stays referenced until the context closes.
     *
     * @param int $kind Hw::KIND_*
     * @param callable(int, string): (string|int) $impl
     */
    public function addProvider(int $fnId, int $resId, int $kind, int $cost, callable $impl): int
    {
        $fn = $this->context->retain(static function ($user, int $op, ?CData $in, int $n, ?CData $out, int $cap, CData $outLen) use ($impl): int {
            return Hw::guard(static function () use ($impl, $op, $in, $n, $out, $cap, $outLen): int {
                $r = $impl($op, Hw::input($in, $n));
                if (is_int($r)) {
                    return $r;
                }
                $len = strlen($r);
                $outLen[0] = $len;
                if ($len > $cap) {
                    return Status::E_BUFFER;
                }
                if ($len > 0 && $out !== null) {
                    \FFI::memcpy($out, $r, $len);
                }
                return Status::OK;
            });
        });
        $idx = Native::new("uint32_t");
        $this->check(Native::ffi()->zxv_hw_provider_add(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($fnId, "fnId"),
            Native::uint32($resId, "resId"),
            Hw::int32($kind, "kind"),
            Native::uint32($cost, "cost"),
            $fn,
            null,
            \FFI::addr($idx),
        ));
        return $idx->cdata;
    }

    /** Declare traits used by placement and discovery. */
    public function setTraits(int $provider, int $latencyNs, int $powerMw, int $throughputClass): self
    {
        $this->check(Native::ffi()->zxv_hw_provider_traits(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($provider, "provider"),
            Native::uint32($latencyNs, "latencyNs"),
            Native::uint32($powerMw, "powerMw"),
            Native::uint32($throughputClass, "throughputClass"),
        ));
        return $this;
    }

    /** Let $provider call $depFnId through this matrix. */
    public function addDependency(int $provider, int $depFnId): self
    {
        $this->check(Native::ffi()->zxv_hw_provider_dep($this->capability->ptr(), $this->ptr(), Native::uint32($provider, "provider"), Native::uint32($depFnId, "depFnId")));
        return $this;
    }

    public function enableProvider(int $provider, bool $on = true): self
    {
        $this->check(Native::ffi()->zxv_hw_provider_enable($this->capability->ptr(), $this->ptr(), Native::uint32($provider, "provider"), $on ? 1 : 0));
        return $this;
    }

    /** Run the golden vectors: TRUE serves, FALSE a vector failed, NEITHER no vectors. */
    public function attest(int $provider): Truth
    {
        $lp = Native::new("int32_t");
        $this->check(Native::ffi()->zxv_hw_attest($this->capability->ptr(), $this->ptr(), Native::uint32($provider, "provider"), \FFI::addr($lp)));
        return Truth::of($lp->cdata);
    }

    /** Number of implementations (indexed 0 .. count-1). */
    public function providerCount(): int
    {
        $n = Native::new("uint32_t");
        $this->check(Native::ffi()->zxv_hw_provider_count($this->capability->ptr(), $this->ptr(), \FFI::addr($n)));
        return $n->cdata;
    }

    public function providerInfo(int $provider): HwProviderInfo
    {
        $fn = Native::new("uint32_t");
        $kind = Native::new("int32_t");
        $lp = Native::new("int32_t");
        $con = Native::new("uint32_t");
        $flags = Native::new("uint32_t");
        $tput = Native::new("uint32_t");
        $pwr = Native::new("uint32_t");
        $calls = Native::new("uint64_t");
        $spent = Native::new("uint64_t");
        $this->check(Native::ffi()->zxv_hw_provider_info(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($provider, "provider"),
            \FFI::addr($fn),
            \FFI::addr($kind),
            \FFI::addr($lp),
            \FFI::addr($con),
            \FFI::addr($flags),
            \FFI::addr($tput),
            \FFI::addr($pwr),
            \FFI::addr($calls),
            \FFI::addr($spent),
        ));
        return new HwProviderInfo($provider, $fn->cdata, $kind->cdata, Truth::of($lp->cdata), $con->cdata, $flags->cdata, $tput->cdata, $pwr->cdata, $calls->cdata, $spent->cdata);
    }

    /** @return list<HwProviderInfo> discovery: every implementation */
    public function providers(): array
    {
        $out = [];
        for ($i = 0, $n = $this->providerCount(); $i < $n; $i++) {
            $out[] = $this->providerInfo($i);
        }
        return $out;
    }

    /** The implementation serving $fnId now, or null if none may. */
    public function bound(int $fnId): ?int
    {
        $out = Native::new("uint32_t");
        $rc = Native::ffi()->zxv_hw_bound($this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), \FFI::addr($out));
        if ($rc === Status::E_NOPROVIDER) {
            return null;
        }
        $this->check($rc);
        return $out->cdata;
    }

    /** Call a function (ZxvRefusedException ZXV_E_NOPROVIDER when no proven implementation may serve). */
    public function call(int $fnId, string $input = "", int $op = 0, int $maxOutput = 4096): string
    {
        return $this->run(false, $fnId, $input, $op, $maxOutput)[0];
    }

    /**
     * call() that also runs the next-best implementation and compares; a
     * mismatch takes both out of service (ZXV_E_DISPUTE).
     *
     * @return array{0:string,1:bool} the output and whether a second implementation ran
     */
    public function shadow(int $fnId, string $input = "", int $op = 0, int $maxOutput = 4096): array
    {
        return $this->run(true, $fnId, $input, $op, $maxOutput);
    }

    /**
     * Give a pure function a memo of $entries (a power of two up to 65536; 0
     * removes it). Key-bearing functions: ZXV_E_POLICY.
     *
     * @return array{0:int,1:int} hits and misses
     */
    public function memo(int $fnId, int $entries): array
    {
        $h = Native::new("uint64_t");
        $m = Native::new("uint64_t");
        $this->check(Native::ffi()->zxv_hw_memo($this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), Native::uint32($entries, "entries"), \FFI::addr($h), \FFI::addr($m)));
        return [$h->cdata, $m->cdata];
    }

    /** Trust a release key for contract packages (at most 4). Needs HW_RECONF. */
    public function trustKey(string $publicKey): self
    {
        $this->check(Native::ffi()->zxv_hw_matrix_trust_key($this->capability->ptr(), $this->ptr(), Native::bytes(Native::exact($publicKey, "publicKey", Hw::PK_BYTES))));
        return $this;
    }

    /** Apply a signed Hw::IMG_VECTORS package; returns how many implementations prove the new contract. Needs HW_RECONF. */
    public function applyContract(string $package): int
    {
        $n = Native::new("uint32_t");
        $this->check(Native::ffi()->zxv_hw_contract_apply($this->capability->ptr(), $this->ptr(), Native::bytes($package, true), strlen($package), \FFI::addr($n)));
        return $n->cdata;
    }

    /** @return array{0:string,1:string} */
    protected function logFunctions(): array
    {
        return ["zxv_hw_matrix_log", "zxv_hw_matrix_log_entry"];
    }

    private function check(int $rc): void
    {
        Native::check($rc, $this->context);
    }

    /** @return array{0:string,1:bool} */
    private function run(bool $shadow, int $fnId, string $input, int $op, int $maxOutput): array
    {
        $cap = Native::int($maxOutput, "maxOutput", 1, 1 << 24);
        $in = Native::bytes($input, true);
        $len = Native::new("size_t");
        $checked = Native::new("int32_t");
        for ($attempt = 0; $attempt < 2; $attempt++) {
            $out = Native::new("uint8_t[$cap]");
            $args = [$this->capability->ptr(), $this->ptr(), Native::uint32($fnId, "fnId"), Native::uint32($op, "op"), $in, strlen($input), $out, $cap, \FFI::addr($len)];
            $rc = $shadow
                ? Native::ffi()->zxv_hw_shadow(...[...$args, \FFI::addr($checked)])
                : Native::ffi()->zxv_hw_call(...$args);
            if ($rc === Status::E_BUFFER && $len->cdata > $cap) {
                $cap = $len->cdata;
                continue;
            }
            $this->check($rc);
            return [Native::read($out, $len->cdata), $checked->cdata === 1];
        }
        throw new ZxvUsageException(Status::E_BUFFER, "the output grew between two calls");
    }
}
