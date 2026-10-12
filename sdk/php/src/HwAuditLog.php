<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * @internal The hash-chained audit log reader shared by {@see HwMatrix}
 * and {@see HwRegion}. A chain that does not verify throws ZxvFatalError
 * (ZXV_E_INVARIANT) and poisons the context.
 */
trait HwAuditLog
{
    /** @return array{0:string,1:int} the chain head (32 bytes) and the record count */
    public function log(): array
    {
        $head = Native::new("uint8_t[32]");
        $n = Native::new("uint32_t");
        $fn = $this->logFunctions()[0];
        Native::check(Native::ffi()->$fn($this->capability->ptr(), $this->ptr(), $head, \FFI::addr($n)), $this->context);
        return [Native::read($head, 32), $n->cdata];
    }

    /** Record $index (0 = oldest). */
    public function logEntry(int $index): HwLogEntry
    {
        $ev = Native::new("int32_t");
        $reason = Native::new("uint32_t");
        $id = Native::new("uint32_t");
        $ver = Native::new("uint32_t");
        $tag = Native::new("uint32_t");
        $subj = Native::new("uint8_t[32]");
        $hash = Native::new("uint8_t[32]");
        $fn = $this->logFunctions()[1];
        Native::check(Native::ffi()->$fn(
            $this->capability->ptr(),
            $this->ptr(),
            Native::uint32($index, "index"),
            \FFI::addr($ev),
            \FFI::addr($reason),
            \FFI::addr($id),
            \FFI::addr($ver),
            \FFI::addr($tag),
            $subj,
            $hash,
        ), $this->context);
        return new HwLogEntry($ev->cdata, $reason->cdata, $id->cdata, $ver->cdata, $tag->cdata, Native::read($subj, 32), Native::read($hash, 32));
    }

    /** @return list<HwLogEntry> every record, oldest first */
    public function logEntries(): array
    {
        $out = [];
        for ($i = 0, $n = $this->log()[1]; $i < $n; $i++) {
            $out[] = $this->logEntry($i);
        }
        return $out;
    }

    /** @return array{0:string,1:string} */
    abstract protected function logFunctions(): array;
}
