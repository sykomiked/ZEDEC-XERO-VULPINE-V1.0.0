<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * The 64-state hexagram tag of ABI 1.3 (the kernel's lpres_hex): an
 * immutable value tagging one unit of work with a four-valued
 * {@see HexBase}, the {@see HexPolarity} of its evidence and the
 * {@see HexPhase} of its lifecycle, packed as
 * tag = base << 4 | polarity << 2 | phase (0..63).
 *
 *     $h = Hexagram::of(HexBase::True, HexPolarity::Negentropic, HexPhase::Potential);
 *     $h->action();                 // HexAction::Proceed
 *     $h = $h->advance();           // HexPhase::Emerging
 *     $h->step(HexPhase::Dissolving); // ZxvRefusedException (ZXV_E_STATE): a jump
 *
 * Instances are interned (compare with ===). Every rule is computed by
 * libzxv. The base is numbered differently from {@see Truth}: convert only
 * with toTruth() and fromTruth(). Like Truth, a Hexagram has no boolean
 * meaning: every PHP object is truthy, so never write `if ($h)`.
 */
final class Hexagram implements \Stringable
{
    /** Largest tag + 1 (ZXV_HEX_STATES). */
    public const STATES = 64;

    /** @var array<int, self> */
    private static array $all = [];

    private function __construct(
        /** The byte value, 0..63. */
        public readonly int $tag,
    ) {
    }

    /**
     * The tag with this byte value.
     *
     * @throws ZxvUsageException (ZXV_E_ARG) outside 0..63
     */
    public static function fromTag(int $tag): self
    {
        Native::int($tag, "tag", 0, 255);
        Native::check(Native::ffi()->zxv_hex_unpack($tag, null, null, null));
        return self::$all[$tag] ??= new self($tag);
    }

    /** The tag from its three fields. */
    public static function of(HexBase $base, HexPolarity $polarity, HexPhase $phase): self
    {
        return self::fromTag(self::call8("zxv_hex_pack", $base->value, $polarity->value, $phase->value));
    }

    /** All 64 tags, in tag order. @return list<self> */
    public static function all(): array
    {
        return array_map(static fn (int $t): self => self::fromTag($t), range(0, self::STATES - 1));
    }

    /** A tag whose base is $truth (mapped by zxv_hex_from_lp). */
    public static function fromTruth(Truth $truth, HexPolarity $polarity, HexPhase $phase): self
    {
        return self::of(HexBase::from(self::call8("zxv_hex_from_lp", $truth->value)), $polarity, $phase);
    }

    /**
     * Polarity from evidence counts before and after: $stale gives
     * Entropic; otherwise the margin |pos - neg| growing gives Negentropic,
     * shrinking Entropic, unchanged Neutral. Never Forbidden. Counts are
     * uint32.
     */
    public static function polarityFromEvidence(int $pos0, int $neg0, int $pos1, int $neg1, bool $stale): HexPolarity
    {
        return HexPolarity::from(self::call8(
            "zxv_hex_polarity",
            Native::uint32($pos0, "pos0"),
            Native::uint32($neg0, "neg0"),
            Native::uint32($pos1, "pos1"),
            Native::uint32($neg1, "neg1"),
            $stale ? 1 : 0,
        ));
    }

    public function base(): HexBase
    {
        return HexBase::from($this->tag >> 4);
    }

    public function polarity(): HexPolarity
    {
        return HexPolarity::from(($this->tag >> 2) & 3);
    }

    public function phase(): HexPhase
    {
        return HexPhase::from($this->tag & 3);
    }

    /** True if the polarity is Forbidden (absorbing). */
    public function isForbidden(): bool
    {
        return $this->polarity() === HexPolarity::Forbidden;
    }

    /** What happens next: one lookup in the kernel's 64-entry table. */
    public function action(): HexAction
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_hex_action($this->tag, \FFI::addr($out)));
        return HexAction::from($out->cdata);
    }

    /** "base/polarity/phase", e.g. "true/negentropic/manifest". */
    public function name(): string
    {
        /** @var string */
        return Native::ffi()->zxv_hex_name($this->tag);
    }

    public function __toString(): string
    {
        return $this->name();
    }

    /** The base as a Truth: the explicit conversion (zxv_hex_to_lp). */
    public function toTruth(): Truth
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_hex_to_lp($this->tag >> 4, \FFI::addr($out)));
        return Truth::of($out->cdata);
    }

    /**
     * The same work in $phase: the same phase or the next one only.
     *
     * @throws Exception\ZxvRefusedException (ZXV_E_STATE) for a jump
     */
    public function step(HexPhase $phase): self
    {
        return self::fromTag(self::call8("zxv_hex_step", $this->tag, $phase->value));
    }

    /** One step along the cycle. */
    public function advance(): self
    {
        return $this->step($this->phase()->next());
    }

    /**
     * New base and polarity, same phase.
     *
     * @throws Exception\ZxvRefusedException (ZXV_E_STATE) if this tag is
     *     forbidden and $polarity is not Forbidden (forbidden is absorbing)
     */
    public function withState(HexBase $base, HexPolarity $polarity): self
    {
        return self::fromTag(self::call8("zxv_hex_set", $this->tag, $base->value, $polarity->value));
    }

    /** The same tag with polarity Forbidden. */
    public function forbid(): self
    {
        return self::fromTag(self::call8("zxv_hex_forbid", $this->tag));
    }

    /** Conjunction: four-valued AND on the base, worst polarity, least advanced phase. */
    public function and(self $other): self
    {
        return self::fromTag(self::call8("zxv_hex_and", $this->tag, $other->tag));
    }

    /** Disjunction: four-valued OR on the base, worst polarity, least advanced phase. */
    public function or(self $other): self
    {
        return self::fromTag(self::call8("zxv_hex_or", $this->tag, $other->tag));
    }

    /** Negation of the base; polarity and phase are kept. */
    public function not(): self
    {
        return self::fromTag(self::call8("zxv_hex_not", $this->tag));
    }

    /** @internal calls a pure zxv_hex_* function whose last argument is a uint8_t out pointer. */
    public static function call8(string $fn, int ...$args): int
    {
        $out = Native::new("uint8_t");
        Native::check(Native::ffi()->$fn(...[...$args, \FFI::addr($out)]));
        return $out->cdata;
    }
}
