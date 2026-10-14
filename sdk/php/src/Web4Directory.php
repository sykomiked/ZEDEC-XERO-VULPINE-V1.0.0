<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/** ABI 1.5: a directory of verified Web 4 peers, cards keyed by agent id (zxv_w4_dir). Pure; NOT thread-safe. */
final class Web4Directory extends PureHandle
{
    /** @param int $capacity 1..Web4::DIR_MAX @throws Exception\ZxvUsageException ZXV_E_RANGE */
    public function __construct(int $capacity)
    {
        $c = Native::uint32($capacity, "capacity");
        $this->own("zxv_w4_dir_destroy", fn (CData $o) => Native::ffi15()->zxv_w4_dir_create($c, $o));
    }

    /**
     * Verify a card, then add it or replace the entry for the same agent.
     * @throws Exception\VerificationFailedException
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (older binding than the one held: no rollback), ZXV_E_CAPACITY
     */
    public function add(string $card): self
    {
        Native::check(Native::ffi15()->zxv_w4_dir_add($this->ptr(), Native::bytes($card, true), strlen($card)));
        return $this;
    }

    /** The card held for $agentId, or null. */
    public function find(string $agentId): ?string
    {
        $id = Web4::id32($agentId, "agentId");
        $missing = false;
        $card = Native::bytesOut(function ($b, $c, $l) use ($id, &$missing) {
            $rc = Native::ffi15()->zxv_w4_dir_find($this->ptr(), $id, $b, $c, $l);
            if ($rc === Status::E_NOT_FOUND) {
                $missing = true;
                $l[0] = 0;
                return Status::OK;
            }
            return $rc;
        }, Web4::CARD_MAX);
        return $missing ? null : $card;
    }

    /** Number of cards held. */
    public function count(): int
    {
        [$o, $oP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_w4_dir_count($this->ptr(), $oP));
        return $o->cdata;
    }
}
