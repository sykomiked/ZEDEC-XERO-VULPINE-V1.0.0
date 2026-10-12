<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * A ZHP-1 machine profile, the "hardware ROM": a tree of abstract
 * components with integer capability descriptors and an opaque per-node
 * extension. Plain data: no context, no capability; freed by close() or the
 * destructor.
 *
 *     $host = HwProfile::probeHost();
 *     $r1 = HwProfile::reference();
 *     [$native, $emulated] = $host->match($r1);
 */
final class HwProfile implements \Countable
{
    private ?CData $handle;

    private function __construct(CData $handle)
    {
        $this->handle = $handle;
    }

    /** An empty profile. */
    public static function create(): self
    {
        return self::fromOut(fn (CData $o) => Native::ffi()->zxv_hw_profile_create($o));
    }

    /** Describe this machine (only what the OS reports reliably; nothing is guessed). */
    public static function probeHost(): self
    {
        return self::fromOut(fn (CData $o) => Native::ffi()->zxv_hw_profile_probe_host($o));
    }

    /** A copy of the public VULPINE-class reference machine R1. */
    public static function reference(): self
    {
        return self::fromOut(fn (CData $o) => Native::ffi()->zxv_hw_profile_reference($o));
    }

    /** $target as served on $host: every running node carries Hw::CAP_KIND. */
    public static function present(self $host, self $target): self
    {
        return self::fromOut(fn (CData $o) => Native::ffi()->zxv_hw_profile_present($host->ptr(), $target->ptr(), $o));
    }

    /** Parse a canonical ZHP-1 encoding (anything non-canonical: ZXV_E_FORMAT). */
    public static function decode(string $bytes): self
    {
        $b = Native::bytes($bytes, true);
        return self::fromOut(fn (CData $o) => Native::ffi()->zxv_hw_profile_decode($b, strlen($bytes), $o));
    }

    public function __destruct()
    {
        $this->close();
    }

    /** Free it now. Idempotent. */
    public function close(): void
    {
        if ($this->handle !== null) {
            Native::ffi()->zxv_hw_profile_destroy($this->handle);
            $this->handle = null;
        }
    }

    public function isClosed(): bool
    {
        return $this->handle === null;
    }

    /** The canonical encoding. */
    public function encode(): string
    {
        $len = Native::new("size_t");
        $rc = Native::ffi()->zxv_hw_profile_encode($this->ptr(), null, 0, \FFI::addr($len));
        if ($rc !== Status::E_BUFFER) {
            Native::check($rc);
        }
        $n = $len->cdata;
        $buf = Native::new("uint8_t[" . max($n, 1) . "]");
        Native::check(Native::ffi()->zxv_hw_profile_encode($this->ptr(), $buf, $n, \FFI::addr($len)));
        return Native::read($buf, $len->cdata);
    }

    /** SHA-256 of the canonical encoding: the profile's identity (32 bytes). */
    public function digest(): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi()->zxv_hw_profile_digest($this->ptr(), $out));
        return Native::read($out, 32);
    }

    /** Add a node: ids strictly increase, parent first (or Hw::ROOT). */
    public function addNode(int $id, int $parent, int $cls): self
    {
        Native::check(Native::ffi()->zxv_hw_profile_node_add($this->ptr(), Native::uint32($id, "id"), Native::uint32($parent, "parent"), Native::uint32($cls, "cls")));
        return $this;
    }

    public function setCap(int $nodeId, int $key, int $value): self
    {
        Native::check(Native::ffi()->zxv_hw_profile_cap_set($this->ptr(), Native::uint32($nodeId, "nodeId"), Native::uint32($key, "key"), Native::uint($value, "value")));
        return $this;
    }

    /** The cap value, or null if the node lacks it. */
    public function cap(int $nodeId, int $key): ?int
    {
        $out = Native::new("uint64_t");
        $rc = Native::ffi()->zxv_hw_profile_cap_get($this->ptr(), Native::uint32($nodeId, "nodeId"), Native::uint32($key, "key"), \FFI::addr($out));
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc);
        return $out->cdata;
    }

    /** Set the node's private extension: opaque bytes, at most Hw::EXT_MAX. */
    public function setExt(int $nodeId, string $ext): self
    {
        Native::check(Native::ffi()->zxv_hw_profile_ext_set($this->ptr(), Native::uint32($nodeId, "nodeId"), Native::bytes($ext, true), strlen($ext)));
        return $this;
    }

    /** The node's private extension ("" if none). */
    public function ext(int $nodeId): string
    {
        $buf = Native::new("uint8_t[" . Hw::EXT_MAX . "]");
        $len = Native::new("size_t");
        Native::check(Native::ffi()->zxv_hw_profile_ext_get($this->ptr(), Native::uint32($nodeId, "nodeId"), $buf, Hw::EXT_MAX, \FFI::addr($len)));
        return Native::read($buf, $len->cdata);
    }

    /** Number of nodes. */
    public function count(): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_hw_profile_count($this->ptr(), \FFI::addr($out)));
        return $out->cdata;
    }

    /** @return array{id:int, parent:int, cls:int, ncaps:int} node $index (id order; parent Hw::ROOT for a root) */
    public function node(int $index): array
    {
        $id = Native::new("uint32_t");
        $parent = Native::new("uint32_t");
        $cls = Native::new("uint32_t");
        $ncaps = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_hw_profile_node($this->ptr(), Native::uint32($index, "index"), \FFI::addr($id), \FFI::addr($parent), \FFI::addr($cls), \FFI::addr($ncaps)));
        return ["id" => $id->cdata, "parent" => $parent->cdata, "cls" => $cls->cdata, "ncaps" => $ncaps->cdata];
    }

    /** @return list<array{id:int, parent:int, cls:int, ncaps:int}> */
    public function nodes(): array
    {
        $out = [];
        for ($i = 0, $n = $this->count(); $i < $n; $i++) {
            $out[] = $this->node($i);
        }
        return $out;
    }

    /** @return array{0:int,1:int} how many $target components run natively on this host, and how many are emulated */
    public function match(self $target): array
    {
        $n = Native::new("uint32_t");
        $e = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_hw_profile_match($this->ptr(), $target->ptr(), \FFI::addr($n), \FFI::addr($e)));
        return [$n->cdata, $e->cdata];
    }

    /**
     * How target node $index (target order) is served on this host: kind
     * Hw::NATIVE with haveId, Hw::EMULATE with the first unmet cap key (0
     * if the host has no component of that class), or Hw::GROUP.
     *
     * @return array{needId:int, kind:int, haveId:int, unmetKey:int}
     */
    public function pair(self $target, int $index): array
    {
        $need = Native::new("uint32_t");
        $kind = Native::new("int32_t");
        $have = Native::new("uint32_t");
        $unmet = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_hw_profile_pair($this->ptr(), $target->ptr(), Native::uint32($index, "index"), \FFI::addr($need), \FFI::addr($kind), \FFI::addr($have), \FFI::addr($unmet)));
        return ["needId" => $need->cdata, "kind" => $kind->cdata, "haveId" => $have->cdata, "unmetKey" => $unmet->cdata];
    }

    /** @internal */
    public function ptr(): CData
    {
        if ($this->handle === null) {
            throw new ZxvUsageException(Status::E_STATE, "HwProfile is closed");
        }
        return $this->handle;
    }

    /** @param callable(CData): int $fn */
    private static function fromOut(callable $fn): self
    {
        $out = Native::new("zxv_hw_profile*");
        Native::check($fn(\FFI::addr($out)));
        return new self($out);
    }
}
