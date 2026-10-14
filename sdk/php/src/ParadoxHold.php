<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: a neutral hold (zxv_paradox_hold): up to $capacity candidate
 * branches (32-byte ids). A branch settles when its net evidence leads
 * every other by at least $decisive, or alone reaches it, or the owner
 * picks it. NOT thread-safe.
 */
final class ParadoxHold extends NativeHandle
{
    /**
     * @param int $capacity 1..Paradox::HOLD_MAX
     * @param int $decisive the lead that settles (0 means 1)
     * @throws Exception\CapabilityDeniedException without PARADOX
     */
    public function __construct(Capability $capability, int $capacity, int $decisive = 0)
    {
        $out = Native::new("zxv_paradox_hold*");
        Native::check(Native::ffi15()->zxv_paradox_hold_create($capability->ptr(), Native::uint32($capacity, "capacity"), Native::uint32($decisive, "decisive"),
            \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_paradox_hold_destroy");
    }

    /** Add a candidate (the same id again is the same candidate). @throws Exception\ZxvRefusedException ZXV_E_STATE (settled), ZXV_E_CAPACITY */
    public function add(string $id): self
    {
        Native::check(Native::ffi15()->zxv_paradox_hold_add($this->ptr(), Paradox::id($id)), $this->context);
        return $this;
    }

    /** One piece of evidence for ($positive) or against a candidate. */
    public function evidence(string $id, bool $positive): self
    {
        Native::check(Native::ffi15()->zxv_paradox_hold_evidence($this->ptr(), Paradox::id($id), $positive ? 1 : 0), $this->context);
        return $this;
    }

    /** Branch count. */
    public function size(): int
    {
        return $this->info()[0];
    }

    /** The settled branch, or null while deferred. */
    public function settled(): ?int
    {
        return $this->info()[1];
    }

    /** The branch with decisive evidence now, or null. */
    public function decisiveBranch(): ?int
    {
        return $this->info()[2];
    }

    /** @return array{id:string, for:int, against:int} branch $index */
    public function branch(int $index): array
    {
        $id = Native::new("uint8_t[32]");
        [$p, $pP] = Native::out("uint32_t");
        [$n, $nP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_paradox_hold_branch($this->ptr(), Native::uint32($index, "index"), $id, $pP, $nP), $this->context);
        return ["id" => Native::read($id, 32), "for" => $p->cdata, "against" => $n->cdata];
    }

    /** @return list<array{id:string, for:int, against:int}> */
    public function branches(): array
    {
        $out = [];
        for ($i = 0, $n = $this->size(); $i < $n; $i++) {
            $out[] = $this->branch($i);
        }
        return $out;
    }

    /** @return array{0:int,1:?int,2:?int} */
    private function info(): array
    {
        [$n, $nP] = Native::out("uint32_t");
        [$s, $sP] = Native::out("int32_t");
        [$d, $dP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_hold_info($this->ptr(), $nP, $sP, $dP), $this->context);
        return [$n->cdata, $s->cdata < 0 ? null : $s->cdata, $d->cdata < 0 ? null : $d->cdata];
    }
}
