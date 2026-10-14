<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: a canonical AIPI function record: what a function is, how its
 * result is accepted and which module runs it. Immutable; cid() is its
 * function CID.
 */
final class AipiFunctionRecord
{
    public readonly string $bytes;
    /** An Aipi::INTENTS name. */
    public readonly string|int $intent;
    /** An Aipi::VERIFY_MODES name. */
    public readonly string|int $verify;
    /** "public" or "local". */
    public readonly string|int $privacy;
    /** "deterministic" or "nondeterministic". */
    public readonly string|int $determinism;
    public readonly int $quorumN;
    public readonly int $quorumK;
    /** The most fuel one run may use (uint64: an int, or a decimal string past PHP_INT_MAX). */
    public readonly int|string $fuelMax;
    public readonly Cid $moduleCid;
    public readonly ?Cid $contractCid;
    public readonly string $entry;
    /** The checker export (null when the mode has none). */
    public readonly ?string $checker;
    /** The certifier export (null unless "cert"). */
    public readonly ?string $certifier;

    /**
     * Decode (strictly) a function record.
     * @throws Exception\ZxvUsageException ZXV_E_FORMAT if not a valid, canonical record
     */
    public function __construct(string $bytes)
    {
        $m = Native::bytes($bytes, true);
        $n = strlen($bytes);
        $o = [];
        $p = [];
        for ($i = 0; $i < 6; $i++) {
            [$o[$i], $p[$i]] = Native::out("uint32_t");
        }
        [$fuel, $fuelP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_aipi_fn_decode($m, $n, $p[0], $p[1], $p[2], $p[3], $p[4], $p[5], $fuelP));
        $this->bytes = $bytes;
        $this->intent = Aipi::name(Aipi::INTENTS, $o[0]->cdata);
        $this->verify = Aipi::name(Aipi::VERIFY_MODES, $o[1]->cdata);
        $this->privacy = Aipi::name(Aipi::PRIVACY, $o[2]->cdata);
        $this->determinism = Aipi::name(Aipi::DETERMINISM, $o[3]->cdata);
        $this->quorumN = $o[4]->cdata;
        $this->quorumK = $o[5]->cdata;
        $this->fuelMax = Native::u64Out($fuel->cdata);
        $this->moduleCid = self::cidField($m, $n, 0) ?? throw new Exception\ZxvUsageException(Status::E_FORMAT, "no module CID");
        $this->contractCid = self::cidField($m, $n, 1);
        $names = [];
        for ($f = 0; $f < 3; $f++) {
            $names[$f] = Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_aipi_fn_get_name($m, $n, $f, $b, $c, $l));
        }
        $this->entry = $names[0];
        $this->checker = $names[1] === "" ? null : $names[1];
        $this->certifier = $names[2] === "" ? null : $names[2];
    }

    /**
     * Encode a function record; every field is checked, none is masked.
     *
     * @param string|int $intent an Aipi::INTENTS name
     * @param string|int $verify an Aipi::VERIFY_MODES name
     * @param int|string|float $fuelMax > 0 (uint64)
     * @param string|int $privacy "public" or "local"
     * @param int $quorumN "quorum" only: 2..8 (and deterministic); 0 otherwise
     * @param int $quorumK "quorum" only: n/2 < k <= n; 0 otherwise
     * @param ?string $checker "checker"/"cert" only (differs from entry)
     * @param ?string $certifier "cert" only
     * @throws Exception\ZxvUsageException ZXV_E_ARG (a field out of range or breaking a rule), ZXV_E_TOO_LONG
     * @throws Exception\ZxvRefusedException ZXV_E_CID (a CID that is not CIDv1 raw sha2-256)
     */
    public static function encode(
        string|int $intent,
        string|int $verify,
        string|int $determinism,
        int|string|float $fuelMax,
        Cid|string $moduleCid,
        string $entry,
        string|int $privacy = "public",
        int $quorumN = 0,
        int $quorumK = 0,
        Cid|string|null $contractCid = null,
        ?string $checker = null,
        ?string $certifier = null,
    ): self {
        $mod = Cid::coerce($moduleCid);
        $con = $contractCid === null ? null : Cid::coerce($contractCid);
        $args = [
            Aipi::enum(Aipi::INTENTS, $intent, "intent"), Aipi::enum(Aipi::VERIFY_MODES, $verify, "verify"),
            Aipi::enum(Aipi::PRIVACY, $privacy, "privacy"), Aipi::enum(Aipi::DETERMINISM, $determinism, "determinism"),
            Native::uint32($quorumN, "quorumN"), Native::uint32($quorumK, "quorumK"), Native::u64($fuelMax, "fuelMax"),
            $mod->mem(), strlen($mod->bytes), $con?->mem(), $con === null ? 0 : strlen($con->bytes),
            $entry === "" ? "" : Native::cstr($entry, "entry"),
            $checker === null ? null : Native::cstr($checker, "checker"),
            $certifier === null ? null : Native::cstr($certifier, "certifier"),
        ];
        return new self(Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_aipi_fn_encode(...[...$args, $b, $c, $l]), Aipi::FN_MAX));
    }

    /** The function CID: codec MC_FN + intent, sha2-256 of the record. */
    public function cid(): Cid
    {
        $m = Native::bytes($this->bytes);
        return Cid::fromOut(fn ($b, $l) => Native::ffi15()->zxv_aipi_fn_cid($m, strlen($this->bytes), $b, $l));
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }

    private static function cidField(?\FFI\CData $m, int $n, int $field): ?Cid
    {
        $buf = Native::new("uint8_t[" . Cid::MAX . "]");
        [$len, $lenP] = Native::out("size_t");
        $rc = Native::ffi15()->zxv_aipi_fn_get_cid($m, $n, $field, $buf, $lenP);
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc);
        return new Cid(Native::read($buf, $len->cdata));
    }
}
