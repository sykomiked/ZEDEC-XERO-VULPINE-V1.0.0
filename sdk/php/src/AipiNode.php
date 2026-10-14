<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: an offline, in-memory IPFS node (block store and pins) with the
 * AIPI context over it (zxv_aipi). It belongs to the capability's context,
 * is freed before it (R6) and fails closed with it. Storing (put, publish)
 * needs Capability::AIPI; reads take only the node. Thread-safe in C (the
 * node is serialized).
 *
 *     $node = new AipiNode($cap);
 *     $mod = $node->put($wasmBytes);
 *     $fn = $node->publish(AipiFunctionRecord::encode("fn", "local", "deterministic", 1000, $mod, "main"));
 *     $node->plan($fn, "input");   // "local"
 *
 * Honest limit: nothing is fetched from a network; plan() runs nothing.
 */
final class AipiNode extends NativeHandle
{
    public const STORE_MIN = 4096;
    public const STORE_MAX = 268435456;
    public const BLOCKS_MAX = 65536;

    /**
     * @param int $storeBytes STORE_MIN..STORE_MAX (each block costs its length plus about 100 bytes)
     * @param int $maxBlocks 1..BLOCKS_MAX
     * @param SecretKey|string|null $nodeKey 32 bytes sealing private blocks
     *   (ChaCha20-Poly1305), copied into the node and wiped on close; a
     *   string is copied to a native buffer that is zeroed after the call
     * @throws Exception\CapabilityDeniedException without AIPI
     */
    public function __construct(public readonly Capability $capability, int $storeBytes = 1 << 20, int $maxBlocks = 1024, SecretKey|string|null $nodeKey = null)
    {
        $key = null;
        if ($nodeKey instanceof SecretKey) {
            $key = SecretKey::expect($nodeKey, 32);
        } elseif ($nodeKey !== null) {
            $key = Native::fixed($nodeKey, "nodeKey", 32);
        }
        $out = Native::new("zxv_aipi*");
        $rc = Native::ffi15()->zxv_aipi_create($capability->ptr(), Native::uint($storeBytes, "storeBytes"), Native::uint32($maxBlocks, "maxBlocks"), $key, \FFI::addr($out));
        if ($key !== null && !($nodeKey instanceof SecretKey)) {
            \FFI::memset($key, 0, 32);
        }
        Native::check($rc, $capability->context);
        parent::__construct($capability->context, $out, "zxv_aipi_destroy");
    }

    /**
     * Store and pin $data as one raw block (the same bytes again is a no-op).
     * @param bool $private store sealed, never announceable (needs a node key)
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (no node key, or public/private flip), ZXV_E_CAPACITY
     */
    public function put(string $data, bool $private = false): Cid
    {
        $d = Native::bytes($data, true);
        return Cid::fromOut(fn ($b, $l) => $this->rc(Native::ffi15()->zxv_aipi_put($this->capability->ptr(), $this->ptr(), $d, strlen($data), $private ? 1 : 0, $b, $l)));
    }

    /**
     * The bytes $cid names, re-hashed before they are returned; null when
     * not held here (there is no network).
     * @throws Exception\ZxvRefusedException ZXV_E_CID (stored bytes no longer match; dropped)
     */
    public function get(Cid|string $cid): ?string
    {
        $c = Cid::coerce($cid);
        $cm = $c->mem();
        return $this->orNull(fn () => Native::bytesOut(fn ($b, $cap, $l) => Native::ffi15()->zxv_aipi_get($this->ptr(), $cm, strlen($c->bytes), $b, $cap, $l), 4096, $this->context));
    }

    /** Store a function record and return its function CID. */
    public function publish(AipiFunctionRecord $record): Cid
    {
        $r = Native::bytes($record->bytes);
        return Cid::fromOut(fn ($b, $l) => $this->rc(Native::ffi15()->zxv_aipi_publish_fn($this->capability->ptr(), $this->ptr(), $r, strlen($record->bytes), $b, $l)));
    }

    /**
     * Get a function record by function CID, verified and decoded strictly; null when not held.
     * @throws Exception\ZxvRefusedException ZXV_E_CID (not a function CID, codec/intent mismatch)
     */
    public function loadFunction(Cid|string $fnCid): ?AipiFunctionRecord
    {
        $c = Cid::coerce($fnCid);
        $cm = $c->mem();
        $b = $this->orNull(fn () => Native::bytesOut(fn ($buf, $cap, $l) => Native::ffi15()->zxv_aipi_load_fn($this->ptr(), $cm, strlen($c->bytes), $buf, $cap, $l), Aipi::FN_MAX, $this->context));
        return $b === null ? null : new AipiFunctionRecord($b);
    }

    /**
     * The route a call would take now (an Aipi::ROUTES name). Runs and
     * changes nothing; offline: "local" when the module (and contract) are
     * held, else "unavailable".
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND when the function record is not held
     */
    public function plan(Cid|string $fnCid, string $input = "", bool $inputPrivate = false): string|int
    {
        $c = Cid::coerce($fnCid);
        $i = Native::bytes($input, true);
        [$r, $rP] = Native::out("int32_t");
        $this->rc(Native::ffi15()->zxv_aipi_plan($this->ptr(), $c->mem(), strlen($c->bytes), $i, strlen($input), $inputPrivate ? 1 : 0, $rP));
        return Aipi::ROUTES[$r->cdata] ?? $r->cdata;
    }

    /** @return array{blocks:int, storeUsed:int, getsVerified:int, forbidden:int} audit counters */
    public function info(): array
    {
        [$b, $bP] = Native::out("uint32_t");
        [$u, $uP] = Native::out("uint64_t");
        [$g, $gP] = Native::out("uint32_t");
        [$f, $fP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_aipi_info($this->ptr(), $bP, $uP, $gP, $fP));
        return ["blocks" => $b->cdata, "storeUsed" => $u->cdata, "getsVerified" => $g->cdata, "forbidden" => $f->cdata];
    }

    private function rc(int $rc): int
    {
        Native::check($rc, $this->context);
        return $rc;
    }

    /** @param callable(): string $fn */
    private function orNull(callable $fn): ?string
    {
        try {
            return $fn();
        } catch (ZxvUsageException $e) {
            if ($e->getStatus() === Status::E_NOT_FOUND) {
                return null;
            }
            throw $e;
        }
    }
}
