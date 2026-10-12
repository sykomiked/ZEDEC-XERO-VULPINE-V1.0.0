<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: AIPI, content-addressed functions (zxv_aipi_*).
 *
 * Four identifiers, all CIDv1 with a full sha2-256 (Cid): data (raw or
 * dag-pb blobs, Z-Wasm modules, contracts); a function (codec MC_FN +
 * intent, the hash of an AipiFunctionRecord); a call (codec MC_CALL, the
 * hash of an AipiCallRecord); and a claim, which is not a CID but an
 * executor's signed answer (AipiClaimRecord; signing it is the caller's
 * job). The 0x3A10xx codes are multicodec private-use values. Records
 * decode strictly, so one function has exactly one CID.
 *
 * Honest limits: no network transport (no Vinea mesh, IPFS peers or
 * gateway: a node finds only what was put into it); no execution (running
 * a function needs the Z-Wasm sandbox, which is not part of libzxv;
 * AipiNode::plan() runs nothing); no money moves in this group.
 */
final class Aipi
{
    public const MC_FN = 0x3A1000;
    public const MC_TENSOR_INFER = 0x3A1001;
    public const MC_SETTLE_LEDGER = 0x3A1002;
    public const MC_SWARM_ROUTE = 0x3A1003;
    public const MC_CALL = 0x3A10C0;
    /** What a function is for (picks the function CID's codec). */
    public const INTENTS = ["fn" => 0, "tensor_infer" => 1, "settle_ledger" => 2, "swarm_route" => 3];
    /** How a remote result is accepted. */
    public const VERIFY_MODES = ["checker" => 1, "cert" => 2, "quorum" => 3, "local" => 4];
    public const PRIVACY = ["public" => 0, "local" => 1];
    public const DETERMINISM = ["nondeterministic" => 0, "deterministic" => 1];
    /** Routes AipiNode::plan() reports; offline, only "local" or "unavailable". */
    public const ROUTES = [1 => "cache", 2 => "local", 3 => "fetch_local", 4 => "mesh", 5 => "unavailable"];
    public const NAME_MAX = 63;
    public const IO_MAX = 1024;
    public const CERT_MAX = 1024;
    public const QUORUM_MAX = 8;
    public const FN_MAX = 373;
    public const CALL_MAX = 1112;
    public const CLAIM_MAX = 2170;
    public const BLOCK_MAX = 2097152;

    private function __construct()
    {
    }

    /**
     * The call CID of (function CID, input): codec MC_CALL, sha2-256 of the call record.
     * @throws Exception\ZxvRefusedException ZXV_E_CID if $fnCid is not a function CID
     */
    public static function callCid(Cid|string $fnCid, string $input = ""): Cid
    {
        $f = Cid::coerce($fnCid);
        $fm = $f->mem();
        $i = Native::bytes($input, true);
        return Cid::fromOut(fn ($b, $l) => Native::ffi15()->zxv_aipi_call_cid($fm, strlen($f->bytes), $i, strlen($input), $b, $l));
    }

    /**
     * @internal a name of $map or its int value
     * @param array<string,int> $map
     */
    public static function enum(array $map, string|int $v, string $name): int
    {
        if (is_int($v)) {
            return Native::uint32($v, $name);
        }
        return $map[$v] ?? throw new ZxvUsageException(Status::E_ARG, "unknown $name " . var_export($v, true));
    }

    /**
     * @internal the name of $v in $map (or $v)
     * @param array<string,int> $map
     */
    public static function name(array $map, int $v): string|int
    {
        $k = array_search($v, $map, true);
        return $k === false ? $v : $k;
    }
}
