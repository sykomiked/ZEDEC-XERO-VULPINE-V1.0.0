<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: an IPFS content identifier (zxv_cid_*), an immutable value.
 *
 * A CID names bytes by what they are (a hash), not where they live. It holds
 * the binary form; one can only be made from bytes that are exactly one
 * canonical CIDv0 or CIDv1 with a sha2-256 or identity multihash. The CIDs
 * are byte for byte Kubo's: compute() is `ipfs block put`, unixfs() is
 * `ipfs add --cid-version=1`, toString() prints Kubo's text ("b..." or
 * "Qm..."), toString(Cid::BASE58BTC) the "z..." AIPI form.
 *
 *     $cid = Cid::compute("hello");
 *     $cid->verify("hello");                 // true
 *     Cid::parse((string) $cid)->equals($cid);  // true
 *
 * Pure: no context, no capability. Honest limits: a CID proves which bytes
 * are meant; it does not make them available, and libzxv fetches nothing.
 */
final class Cid implements \Stringable
{
    /** Longest binary CID (ZXV_CID_MAX). */
    public const MAX = 80;
    /** Longest multihash digest. */
    public const DIGEST_MAX = 64;
    public const CODEC_RAW = 0x55;
    public const CODEC_DAG_PB = 0x70;
    public const HASH_IDENTITY = 0x00;
    public const HASH_SHA2_256 = 0x12;
    /** Text base: Kubo's default form. */
    public const BASE_DEFAULT = 0;
    /** Text base: "z..." base58btc (CIDv1 only). */
    public const BASE58BTC = 1;

    /** The binary CID. */
    public readonly string $bytes;
    /** 0 or 1. */
    public readonly int $version;
    /** The multicodec (CODEC_RAW, CODEC_DAG_PB, an AIPI codec, ...). */
    public readonly int $codec;
    /** The multihash code. */
    public readonly int $hashCode;
    /** The multihash digest. */
    public readonly string $digest;

    /**
     * Wrap binary CID bytes.
     * @throws Exception\ZxvRefusedException ZXV_E_CID if they are not exactly one canonical CID
     */
    public function __construct(string $bytes)
    {
        [$ver, $verP] = Native::out("uint32_t");
        [$codec, $codecP] = Native::out("uint32_t");
        [$hash, $hashP] = Native::out("uint32_t");
        [$dlen, $dlenP] = Native::out("size_t");
        $dig = Native::new("uint8_t[" . self::DIGEST_MAX . "]");
        $b = Native::bytes($bytes, true);
        Native::check(Native::ffi15()->zxv_cid_info($b, strlen($bytes), $verP, $codecP, $hashP, $dig, $dlenP));
        $this->bytes = $bytes;
        $this->version = $ver->cdata;
        $this->codec = $codec->cdata;
        $this->hashCode = $hash->cdata;
        $this->digest = Native::read($dig, $dlen->cdata);
    }

    /** The CIDv1 of $data as ONE block: sha2-256 under $codec (Kubo `ipfs block put`). */
    public static function compute(string $data, int $codec = self::CODEC_RAW): self
    {
        $d = Native::bytes($data, true);
        $c = Native::uint32($codec, "codec");
        return self::fromOut(fn ($b, $l) => Native::ffi15()->zxv_cid_compute($c, $d, strlen($data), $b, $l));
    }

    /** The root CID Kubo gives $data as a file (raw up to 262144 bytes, dag-pb above). */
    public static function unixfs(string $data): self
    {
        $d = Native::bytes($data, true);
        return self::fromOut(fn ($b, $l) => Native::ffi15()->zxv_cid_unixfs($d, strlen($data), $b, $l));
    }

    /**
     * Parse canonical CID text: "b..." (CIDv1 base32), "Qm..." (CIDv0) or "z..." (base58btc).
     * @throws Exception\ZxvRefusedException ZXV_E_CID for anything else (ZXV_E_ARG for "")
     */
    public static function parse(string $text): self
    {
        $t = Native::cstr($text, "text");
        return self::fromOut(fn ($b, $l) => Native::ffi15()->zxv_cid_from_text($t, $b, $l));
    }

    /** @internal a Cid or its binary bytes */
    public static function coerce(self|string $v): self
    {
        return $v instanceof self ? $v : new self($v);
    }

    /** @internal */
    public static function fromOut(callable $fn): self
    {
        $buf = Native::new("uint8_t[" . self::MAX . "]");
        [$len, $lenP] = Native::out("size_t");
        Native::check($fn($buf, $lenP));
        return new self(Native::read($buf, $len->cdata));
    }

    /**
     * CID text in $base (BASE_DEFAULT or BASE58BTC).
     * @throws Exception\ZxvRefusedException ZXV_E_CID for a CIDv0 in base58btc
     */
    public function text(int $base = self::BASE_DEFAULT): string
    {
        if ($base !== self::BASE_DEFAULT && $base !== self::BASE58BTC) {
            throw new ZxvUsageException(Status::E_ARG, "base must be Cid::BASE_DEFAULT or Cid::BASE58BTC");
        }
        $b = $this->mem();
        return Native::textOut(fn ($buf, $c, $l) => Native::ffi15()->zxv_cid_to_text($b, strlen($this->bytes), $base, $buf, $c, $l));
    }

    public function __toString(): string
    {
        return $this->text();
    }

    /** This CID as CIDv1 (a CIDv0 becomes dag-pb CIDv1 with the same hash). */
    public function toV1(): self
    {
        $b = $this->mem();
        return self::fromOut(fn ($o, $l) => Native::ffi15()->zxv_cid_to_v1($b, strlen($this->bytes), $o, $l));
    }

    /**
     * Whether $data is exactly the block this CID names. A UnixFS root
     * (dag-pb) names its root block: check a file with Cid::unixfs($data).
     */
    public function verify(string $data): bool
    {
        $rc = $this->verifyRc($data);
        if ($rc === Status::E_CID) {
            return false;
        }
        Native::check($rc);
        return true;
    }

    /** Like verify(), but throws ZXV_E_CID on a mismatch. */
    public function verifyOrThrow(string $data): self
    {
        Native::check($this->verifyRc($data));
        return $this;
    }

    /** The Kademlia provider key: SHA-256("zxv/aipi/v1/provider" || binary CID), 32 bytes. */
    public function providerKey(): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi15()->zxv_aipi_provider_key($this->mem(), strlen($this->bytes), $out));
        return Native::read($out, 32);
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }

    /** @internal */
    public function mem(): CData
    {
        return Native::bytes($this->bytes) ?? throw new ZxvUsageException(Status::E_ARG, "empty CID");
    }

    private function verifyRc(string $data): int
    {
        $d = Native::bytes($data, true);
        return Native::ffi15()->zxv_cid_verify($this->mem(), strlen($this->bytes), $d, strlen($data));
    }
}
