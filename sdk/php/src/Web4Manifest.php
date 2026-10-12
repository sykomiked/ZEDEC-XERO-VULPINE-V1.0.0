<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\VerificationFailedException;
use Zedec\Zxv\Exception\ZxvRefusedException;

/**
 * ABI 1.5: a Web 4 tool manifest (zxv_w4_manifest): up to Web4::MAX_TOOLS
 * tools, each with a name, a price in VFV minor units (2 digits), a rate
 * limit and flags. Signing fixes the agent and peer ids; adding a tool
 * afterwards drops the signature. Pure; NOT thread-safe.
 *
 *     $m = new Web4Manifest(1, $t0, $t1);
 *     $m->addTool("pay", 150, 60, 10, "money", 256);
 *     $m->sign($alice);
 *     $m->isValid($alice->publicKey(), $t0);   // true
 */
final class Web4Manifest extends PureHandle
{
    /**
     * An empty, unsigned manifest valid in [issuedMs, expiresMs).
     * @throws Exception\ZxvUsageException ZXV_E_ARG (expiresMs <= issuedMs)
     */
    public function __construct(int $version, int|string|float $issuedMs, int|string|float $expiresMs)
    {
        $v = Native::uint32($version, "version");
        $i = Native::u64($issuedMs, "issuedMs");
        $e = Native::u64($expiresMs, "expiresMs");
        $this->own("zxv_w4_manifest_destroy", fn (CData $o) => Native::ffi15()->zxv_w4_manifest_create($v, $i, $e, $o));
    }

    /**
     * Parse the wire form. The signature is kept but NOT checked: call verify().
     * @throws Exception\ZxvUsageException ZXV_E_FORMAT
     */
    public static function decode(string $bytes): self
    {
        $out = Native::new("zxv_w4_manifest*");
        Native::check(Native::ffi15()->zxv_w4_manifest_decode(Native::bytes($bytes, true), strlen($bytes), \FFI::addr($out)));
        $m = (new \ReflectionClass(self::class))->newInstanceWithoutConstructor();
        $m->adopt("zxv_w4_manifest_destroy", $out);
        return $m;
    }

    /**
     * Add a tool.
     * @param string|int|list<string> $flags Web4::TOOL_FLAGS
     * @param int $maxInput <= Web4::MSG_MAX_PAYLOAD
     * @throws Exception\ZxvUsageException ZXV_E_DUPLICATE (name listed)
     * @throws ZxvRefusedException ZXV_E_CAPACITY (16 tools)
     */
    public function addTool(string $name, int|string|float $priceVfv, int $ratePerMin, int $burst, string|int|array $flags = 0, int $maxInput = 0): self
    {
        Native::check(Native::ffi15()->zxv_w4_manifest_add_tool($this->ptr(), Native::cstr($name, "name"), Native::u64($priceVfv, "priceVfv"),
            Native::uint32($ratePerMin, "ratePerMin"), Native::uint32($burst, "burst"), Web4::flags(Web4::TOOL_FLAGS, $flags, "flags"), Native::uint32($maxInput, "maxInput")));
        return $this;
    }

    /** Sign with $agent's key ($capability needs CRYPTO_SIGN, default the agent's). */
    public function sign(Web4Agent $agent, ?Capability $capability = null): self
    {
        $cap = $capability ?? $agent->capability;
        Native::check(Native::ffi15()->zxv_w4_manifest_sign($cap->ptr(), $agent->ptr(), $this->ptr()), $agent->context);
        return $this;
    }

    /**
     * Check it is signed by $publicKey, that key is the manifest's agent, and issuedMs <= nowMs < expiresMs.
     * @throws VerificationFailedException wrong key, or a bad or missing signature
     * @throws ZxvRefusedException ZXV_E_STATE (valid signature, not valid at nowMs)
     */
    public function verify(string $publicKey, int|string|float $nowMs): self
    {
        Native::check(Native::ffi15()->zxv_w4_manifest_verify($this->ptr(), Native::fixed($publicKey, "publicKey", Web4::PK_BYTES), Native::u64($nowMs, "nowMs")));
        return $this;
    }

    /** Whether verify() would pass. */
    public function isValid(string $publicKey, int|string|float $nowMs): bool
    {
        try {
            $this->verify($publicKey, $nowMs);
            return true;
        } catch (VerificationFailedException) {
            return false;
        } catch (ZxvRefusedException $e) {
            if ($e->getStatus() !== Status::E_STATE) {
                throw $e;
            }
            return false;
        }
    }

    /** The signed wire form. @throws ZxvRefusedException ZXV_E_STATE (not signed) */
    public function encode(): string
    {
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_manifest_encode($this->ptr(), $b, $c, $l), Web4::MANIFEST_MAX);
    }

    /** The Web 2 discovery face (JSON text; prices stay integers in minor units). */
    public function toJson(): string
    {
        return Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_manifest_to_json($this->ptr(), $b, $c, $l));
    }

    /** @return array{agentId:string, peerId:string, version:int, issuedMs:int|string, expiresMs:int|string, toolCount:int, signed:bool} */
    public function info(): array
    {
        $a = Native::new("uint8_t[32]");
        $p = Native::new("uint8_t[32]");
        [$v, $vP] = Native::out("uint32_t");
        [$i, $iP] = Native::out("uint64_t");
        [$e, $eP] = Native::out("uint64_t");
        [$n, $nP] = Native::out("uint32_t");
        [$s, $sP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_w4_manifest_info($this->ptr(), $a, $p, $vP, $iP, $eP, $nP, $sP));
        return ["agentId" => Native::read($a, 32), "peerId" => Native::read($p, 32), "version" => $v->cdata, "issuedMs" => Native::u64Out($i->cdata),
            "expiresMs" => Native::u64Out($e->cdata), "toolCount" => $n->cdata, "signed" => $s->cdata !== 0];
    }

    public function isSigned(): bool
    {
        return $this->info()["signed"];
    }

    /**
     * Tool $index (0-based).
     * @return array{name:string, priceVfv:int|string, ratePerMin:int, burst:int, flags:list<string>, maxInput:int}
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND past the last tool
     */
    public function tool(int $index): array
    {
        $name = Native::new("char[32]");
        [$pr, $prP] = Native::out("uint64_t");
        [$r, $rP] = Native::out("uint32_t");
        [$b, $bP] = Native::out("uint32_t");
        [$f, $fP] = Native::out("uint32_t");
        [$m, $mP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_w4_manifest_tool($this->ptr(), Native::uint32($index, "index"), $name, $prP, $rP, $bP, $fP, $mP));
        return ["name" => \FFI::string($name), "priceVfv" => Native::u64Out($pr->cdata), "ratePerMin" => $r->cdata, "burst" => $b->cdata,
            "flags" => Web4::flagNames(Web4::TOOL_FLAGS, $f->cdata), "maxInput" => $m->cdata];
    }

    /** @return list<array{name:string, priceVfv:int|string, ratePerMin:int, burst:int, flags:list<string>, maxInput:int}> */
    public function tools(): array
    {
        $out = [];
        for ($i = 0, $n = $this->info()["toolCount"]; $i < $n; $i++) {
            $out[] = $this->tool($i);
        }
        return $out;
    }

    /** The index of tool $name, or null if not listed. */
    public function find(string $name): ?int
    {
        [$o, $oP] = Native::out("uint32_t");
        $rc = Native::ffi15()->zxv_w4_manifest_find($this->ptr(), Native::cstr($name, "name"), $oP);
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc);
        return $o->cdata;
    }
}
