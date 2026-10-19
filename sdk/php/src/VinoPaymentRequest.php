<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.6: a payment request, a plain immutable value. The app builds one,
 * sends its encode()d bytes to the user's wallet, and the wallet approves
 * it with a {@see VinoReceipt}. Encoding and decoding are pure: no
 * capability needed.
 *
 *     $req = new VinoPaymentRequest(VinoLane::Live, $shopId, Zxv::randomBytes(16),
 *         amount: 1500, createdAt: $now, expiresAt: $now + 600, memo: "Sword of Dawn");
 *     $bytes = $req->encode();                      // 160 bytes, canonical
 *     $req->id();                                   // SHA-256 of those bytes (32 bytes)
 *     VinoPaymentRequest::decode($bytes)->equals($req);   // true
 *
 * The library checks every field when encoding and decoding (only types
 * and byte sizes are checked when the value is built):
 * - lane: Play (play units) or Live (VFV minor units, approved in the
 *   user's wallet); any other number is LaneException (ZXV_E_LANE).
 * - flags: Vino::FLAG_CHANCE or 0; an unknown bit is ZXV_E_FORMAT, and a
 *   LIVE request with FLAG_CHANCE is ChanceRefusedException (paid chance
 *   outcomes are not payment requests).
 * - merchant: the payee's 32-byte id (e.g. SHA-256 of its key).
 * - nonce: 16 bytes, unique per request.
 * - amount: 1..Vino::AMOUNT_MAX (ZXV_E_RANGE otherwise).
 * - createdAt, expiresAt: seconds of the caller's clock (Unix time by
 *   convention); expiresAt > createdAt (ZXV_E_RANGE otherwise).
 * - memo: 0..64 bytes of printable ASCII (ZXV_E_TOO_LONG if longer,
 *   ZXV_E_FORMAT if not printable).
 *
 * Every uint64 is an int, or a decimal string past PHP_INT_MAX.
 */
final class VinoPaymentRequest
{
    /** Play or Live (an int only if the number is unknown to this SDK). */
    public readonly VinoLane|int $lane;
    /** 32 bytes. */
    public readonly string $merchant;
    /** 16 bytes. */
    public readonly string $nonce;
    public readonly int|string $amount;
    public readonly int|string $createdAt;
    public readonly int|string $expiresAt;
    /** Printable ASCII, possibly empty. */
    public readonly string $memo;
    /** Request flags (Vino::FLAG_CHANCE). */
    public readonly int $flags;

    /**
     * Build a request value.
     * @param VinoLane|int $lane an int is kept as is, so encode() can report ZXV_E_LANE
     * @throws Exception\FloatRejectedException a float for a number
     * @throws Exception\ZxvUsageException ZXV_E_ARG (wrong byte size, a NUL in the memo)
     */
    public function __construct(
        VinoLane|int $lane,
        string $merchant,
        string $nonce,
        int|string|float $amount,
        int|string|float $createdAt,
        int|string|float $expiresAt,
        string $memo = "",
        int $flags = 0,
    ) {
        Vino::lane($lane);
        $this->lane = is_int($lane) ? (VinoLane::tryFrom($lane) ?? $lane) : $lane;
        $this->merchant = Native::exact($merchant, "merchant", Vino::MERCHANT_BYTES);
        $this->nonce = Native::exact($nonce, "nonce", Vino::NONCE_BYTES);
        $this->amount = Native::u64Out(Native::u64($amount, "amount"));
        $this->createdAt = Native::u64Out(Native::u64($createdAt, "createdAt"));
        $this->expiresAt = Native::u64Out(Native::u64($expiresAt, "expiresAt"));
        $this->memo = Native::cstr($memo, "memo");
        $this->flags = Native::uint32($flags, "flags");
    }

    /**
     * Decode and validate 160 request bytes (the rules of encode(), plus the
     * magic, version and zero padding: ZXV_E_FORMAT).
     * @throws Exception\LaneException|Exception\ChanceRefusedException|Exception\ZxvUsageException
     *   (ZXV_E_FORMAT, ZXV_E_RANGE, ZXV_E_TOO_LONG) when the bytes are not a valid request
     */
    public static function decode(string $bytes): self
    {
        $in = Native::fixed($bytes, "bytes", Vino::REQUEST_BYTES);
        [$l, $lP] = Native::out("int32_t");
        [$f, $fP] = Native::out("uint32_t");
        $m = Native::new("uint8_t[32]");
        $n = Native::new("uint8_t[16]");
        [$a, $aP] = Native::out("uint64_t");
        [$c, $cP] = Native::out("uint64_t");
        [$e, $eP] = Native::out("uint64_t");
        $memo = Native::new("char[" . (Vino::MEMO_MAX + 1) . "]");
        Native::check(Native::ffi16()->zxv_vino_request_decode($in, $lP, $fP, $m, $n, $aP, $cP, $eP, $memo, null));
        return new self($l->cdata, Native::read($m, 32), Native::read($n, 16), Native::u64Out($a->cdata), Native::u64Out($c->cdata),
            Native::u64Out($e->cdata), \FFI::string($memo), $f->cdata);
    }

    /**
     * The id of encoded request bytes (SHA-256 of them), after checking that
     * they decode. Throws as decode().
     */
    public static function idOf(string $bytes): string
    {
        $in = Native::fixed($bytes, "bytes", Vino::REQUEST_BYTES);
        $id = Native::new("uint8_t[" . Vino::REQUEST_ID_BYTES . "]");
        Native::check(Native::ffi16()->zxv_vino_request_decode($in, null, null, null, null, null, null, null, null, $id));
        return Native::read($id, Vino::REQUEST_ID_BYTES);
    }

    /** Whether Vino::FLAG_CHANCE is set. */
    public function isChance(): bool
    {
        return ($this->flags & Vino::FLAG_CHANCE) !== 0;
    }

    /**
     * The canonical 160 bytes of this request.
     * @throws Exception\LaneException lane not Play or Live
     * @throws Exception\ChanceRefusedException a LIVE request with Vino::FLAG_CHANCE
     * @throws Exception\ZxvUsageException ZXV_E_FORMAT (unknown flag, memo not printable ASCII),
     *   ZXV_E_RANGE (amount 0 or above Vino::AMOUNT_MAX, expiresAt <= createdAt), ZXV_E_TOO_LONG (memo above 64 bytes)
     */
    public function encode(): string
    {
        return $this->encodeWithId()[0];
    }

    /** The request id: SHA-256 of encode(). A receipt names it. Throws as encode(). */
    public function id(): string
    {
        return $this->encodeWithId()[1];
    }

    /**
     * Both at once, in one library call. Throws as encode().
     * @return array{0:string, 1:string} [160 bytes, 32-byte id]
     */
    public function encodeWithId(): array
    {
        $out = Native::new("uint8_t[" . Vino::REQUEST_BYTES . "]");
        $id = Native::new("uint8_t[" . Vino::REQUEST_ID_BYTES . "]");
        Native::check(Native::ffi16()->zxv_vino_request_encode(Vino::lane($this->lane), $this->flags, Native::fixed($this->merchant, "merchant", 32),
            Native::fixed($this->nonce, "nonce", 16), Native::u64($this->amount, "amount"), Native::u64($this->createdAt, "createdAt"),
            Native::u64($this->expiresAt, "expiresAt"), $this->memo, $out, $id));
        return [Native::read($out, Vino::REQUEST_BYTES), Native::read($id, Vino::REQUEST_ID_BYTES)];
    }

    /** Whether $other has the same fields. */
    public function equals(self $other): bool
    {
        return $this->toArray() === $other->toArray();
    }

    /**
     * The fields, by name.
     * @return array{lane:VinoLane|int, merchant:string, nonce:string, amount:int|string, createdAt:int|string, expiresAt:int|string, memo:string, flags:int}
     */
    public function toArray(): array
    {
        return ["lane" => $this->lane, "merchant" => $this->merchant, "nonce" => $this->nonce, "amount" => $this->amount,
            "createdAt" => $this->createdAt, "expiresAt" => $this->expiresAt, "memo" => $this->memo, "flags" => $this->flags];
    }
}
