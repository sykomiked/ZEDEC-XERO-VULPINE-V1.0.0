<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** ABI 1.5: the AIPI call record naming one invocation: a function CID and its input. Immutable. */
final class AipiCallRecord
{
    public readonly string $bytes;
    public readonly Cid $fnCid;
    public readonly string $input;

    /**
     * Decode (strictly) a call record.
     * @throws Exception\ZxvUsageException ZXV_E_FORMAT
     */
    public function __construct(string $bytes)
    {
        $o = [];
        $p = [];
        for ($i = 0; $i < 4; $i++) {
            [$o[$i], $p[$i]] = Native::out("size_t");
        }
        Native::check(Native::ffi15()->zxv_aipi_call_decode(Native::bytes($bytes, true), strlen($bytes), $p[0], $p[1], $p[2], $p[3]));
        $this->bytes = $bytes;
        $this->fnCid = new Cid(substr($bytes, $o[0]->cdata, $o[1]->cdata));
        $this->input = substr($bytes, $o[2]->cdata, $o[3]->cdata);
    }

    /**
     * @param string $input at most Aipi::IO_MAX bytes
     * @throws Exception\ZxvRefusedException ZXV_E_CID (not a function CID)
     * @throws Exception\ZxvUsageException ZXV_E_TOO_LONG (input)
     */
    public static function encode(Cid|string $fnCid, string $input = ""): self
    {
        $f = Cid::coerce($fnCid);
        $fm = $f->mem();
        $i = Native::bytes($input, true);
        return new self(Native::bytesOut(
            fn ($b, $c, $l) => Native::ffi15()->zxv_aipi_call_encode($fm, strlen($f->bytes), $i, strlen($input), $b, $c, $l),
            Aipi::CALL_MAX,
        ));
    }

    /** The call CID. */
    public function cid(): Cid
    {
        return Aipi::callCid($this->fnCid, $this->input);
    }
}
