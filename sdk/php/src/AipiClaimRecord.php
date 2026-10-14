<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: the AIPI claim record an executor signs: its output (and, for
 * "cert" functions, a certificate) for one call, with its 32-byte node id.
 * Immutable. Signing and checking the signature are the caller's job.
 */
final class AipiClaimRecord
{
    public readonly string $bytes;
    public readonly Cid $callCid;
    /** The executor's 32-byte node id. */
    public readonly string $executor;
    public readonly string $output;
    /** The certificate ("" if none). */
    public readonly string $cert;

    /** Decode (strictly). @throws Exception\ZxvUsageException ZXV_E_FORMAT */
    public function __construct(string $bytes)
    {
        $o = [];
        $p = [];
        for ($i = 0; $i < 7; $i++) {
            [$o[$i], $p[$i]] = Native::out("size_t");
        }
        Native::check(Native::ffi15()->zxv_aipi_claim_decode(Native::bytes($bytes, true), strlen($bytes), ...$p));
        $this->bytes = $bytes;
        $this->callCid = new Cid(substr($bytes, $o[0]->cdata, $o[1]->cdata));
        $this->executor = substr($bytes, $o[2]->cdata, 32);
        $this->output = substr($bytes, $o[3]->cdata, $o[4]->cdata);
        $this->cert = substr($bytes, $o[5]->cdata, $o[6]->cdata);
    }

    /**
     * @param string $executor 32 bytes
     * @param string $output at most Aipi::IO_MAX bytes
     * @param string $cert at most Aipi::CERT_MAX bytes
     */
    public static function encode(Cid|string $callCid, string $executor, string $output, string $cert = ""): self
    {
        $c = Cid::coerce($callCid);
        $cm = $c->mem();
        $e = Native::fixed($executor, "executor", 32);
        $om = Native::bytes($output, true);
        $zm = Native::bytes($cert, true);
        return new self(Native::bytesOut(
            fn ($b, $cap, $l) => Native::ffi15()->zxv_aipi_claim_encode($cm, strlen($c->bytes), $e, $om, strlen($output), $zm, strlen($cert), $b, $cap, $l),
            Aipi::CLAIM_MAX,
        ));
    }
}
