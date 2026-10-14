<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: the paradox engine (zxv_paradox_*).
 *
 * A PARADOX here is work whose four-valued state is Both (the evidence
 * points both ways) or Neither (no evidence). This group does not decide
 * which side is "really" true. It reads the work's Hexagram tag, gives the
 * paradox one of four KINDS and runs a fixed, logged operator for it:
 *
 *  - constructive: Both, margin growing: a bounded parameter update,
 *    applied only inside published bounds (ParadoxBook::constructive with a ParadoxPolicy);
 *  - destructive: Both, margin shrinking or stale: contain, quarantine,
 *    roll back from a checkpoint and write a ParadoxRepairProof;
 *  - neutral: Both and steady past the potential phase, or Neither: hold
 *    every candidate branch (ParadoxHold);
 *  - special: Both, steady, potential phase, unknown signature: a
 *    permanent frontier record and a draft classification for review.
 *
 * A registered signature overrides the trend. True and False are not
 * paradoxes ("none"). Operators take the (kind, tag) pair classify()
 * returned; a pair that breaks the class rules is ZxvUsageException
 * (ZXV_E_ARG). A FORBIDDEN tag is accepted with any kind, and every
 * operator refuses it and rolls back.
 *
 * OUTCOMES. An operator that ran returns its decision as an OUTCOMES name
 * ("refused" and "proposal_for_owner" are decisions, not errors) and has
 * appended one record to the operator log. An exception means it did not
 * run. A full log is ZxvRefusedException (ZXV_E_CAPACITY): halt the
 * context, since work that cannot be recorded must not continue.
 *
 * CAPABILITIES. Writing a log or the registry, applying a policy update
 * and creating any handle need Capability::PARADOX. Reading, classifying
 * and adding branches, nodes and updates to a handle you hold need none.
 * Nothing here signs: sign the bytes with Ed25519 or MlDsa87.
 *
 * Honest limits, in plain words:
 *  - Automatic changes happen only inside published bounds; anything else
 *    becomes a proposal for the owner. The operator has no balance input:
 *    it cannot move money or change a balance or a ledger invariant.
 *  - Policies that touch money or devices need the owner: they are never
 *    changed automatically. $ownerOk is YOUR assertion; inside one process
 *    this library cannot check who the owner is.
 *  - Detection is by the listed checks and is not instant; what they do
 *    not look at goes undetected.
 *  - Destructive work is contained, never answered: there is no way to
 *    reply to, or act against, a sender.
 *  - This is contradiction-tolerant bookkeeping: it contains, holds and
 *    routes contradictions; it never makes both sides true.
 *  - Peers agree by applying the same fixed order to the same signed
 *    inputs, not through any shared clock. Integers only.
 */
final class Paradox
{
    /** Kinds, by ZXV_PDX_KIND_* value. */
    public const KINDS = ["constructive", "destructive", "neutral", "special", "none"];
    /** What to do with a classified paradox, by ZXV_PDX_DO_* value. */
    public const DOS = ["none", "synthesize", "contain", "hold", "record", "rollback"];
    /** Operator outcomes, by ZXV_PDX_OUT_* value. */
    public const OUTCOMES = ["applied", "kept", "proposal_for_owner", "rolled_back", "contained", "deferred", "settled", "recorded",
        "accepted", "pending", "resolved", "escalate_to_owner", "refused", "log_full"];
    /** Graph node states, by ZXV_PDX_NS_* value. */
    public const NODE_STATES = ["open", "clear", "contained", "rolled_back", "tainted", "sandboxed", "waiting", "held", "blocked", "applied"];
    /** Flags of a policy, registry entry or draft: the owner decides. */
    public const FLAGS = ["money" => 1, "device" => 2];
    /** Which checks fired (a repair proof's trips; the quarantine reason). */
    public const TRIPS = ["steps" => 1, "budget" => 2, "cycle" => 4, "invariant" => 8, "forbidden" => 16];
    /** The book's three logs. */
    public const LOGS = ["ops" => 0, "frontier" => 1, "quarantine" => 2];

    public const REC_BYTES = 144;
    public const QREC_BYTES = 110;
    public const REPAIR_BYTES = 160;
    public const DRAFT_BYTES = 71;
    public const LOG_MAX = 65536;
    public const REGISTRY_MAX = 65536;
    public const POLICY_MAX = 64;
    public const HOLD_MAX = 64;
    public const GRAPH_MAX = 64;
    public const MAX_DEPS = 4;
    public const UPDATES_MAX = 256;
    public const TRACE_MAX = 4096;

    private function __construct()
    {
    }

    /** The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data), 32 bytes. */
    public static function signature(string $data): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi15()->zxv_paradox_signature(Native::bytes($data, true), strlen($data), $out));
        return Native::read($out, 32);
    }

    /** The kind a tag reads as on its own (a forbidden tag reads as "none"). */
    public static function kindOfTag(Hexagram|int $tag): string|int
    {
        [$o, $oP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_kind_of_tag(self::tag($tag), $oP));
        return self::name(self::KINDS, $o->cdata);
    }

    /**
     * What to do with a classified pair (a DOS name).
     * @throws ZxvUsageException ZXV_E_ARG (an invalid pair)
     */
    public static function action(string|int $kind, Hexagram|int $tag): string|int
    {
        [$o, $oP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_action(self::kind($kind), self::tag($tag), $oP));
        return self::name(self::DOS, $o->cdata);
    }

    /** The Hexagram action an outcome amounts to. */
    public static function outcomeAction(string|int $outcome): HexAction
    {
        [$o, $oP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_outcome_action(self::enum(self::OUTCOMES, $outcome, "outcome"), $oP));
        return HexAction::from($o->cdata);
    }

    /**
     * Classify $tag without a book (no registry lookup). See ParadoxBook::classify.
     * @param ?list<int> $evidence [pos0, neg0, pos1, neg1]
     */
    public static function classify(Hexagram|int $tag, ?array $evidence = null, bool $stale = false): ParadoxClassification
    {
        return ParadoxBook::classifyWith(null, $tag, $evidence, $stale, null);
    }

    /** The library's name of a kind ("INVALID" for an unknown value). */
    public static function kindName(string|int $kind): string
    {
        return self::cstring(Native::ffi15()->zxv_paradox_kind_name(self::enum(self::KINDS, $kind, "kind")));
    }

    /** The library's name of an outcome ("proposal-for-owner", ...). */
    public static function outcomeName(string|int $outcome): string
    {
        return self::cstring(Native::ffi15()->zxv_paradox_outcome_name(self::enum(self::OUTCOMES, $outcome, "outcome")));
    }

    /** The library's name of a node state. */
    public static function nodeStateName(string|int $state): string
    {
        return self::cstring(Native::ffi15()->zxv_paradox_node_state_name(self::enum(self::NODE_STATES, $state, "state")));
    }

    /** A const char * result: PHP's FFI already returns it as a string. */
    private static function cstring(mixed $p): string
    {
        return is_string($p) ? $p : \FFI::string($p);
    }

    /** @internal */
    public static function tag(Hexagram|int $tag): int
    {
        return $tag instanceof Hexagram ? $tag->tag : Native::uint8($tag, "tag");
    }

    /** @internal */
    public static function kind(string|int $kind): int
    {
        return self::enum(self::KINDS, $kind, "kind");
    }

    /**
     * @internal a name from $list, or a raw int32
     * @param list<string> $list
     */
    public static function enum(array $list, string|int $v, string $name): int
    {
        if (is_int($v)) {
            return Native::int32($v, $name);
        }
        $i = array_search($v, $list, true);
        return is_int($i) ? $i : throw new ZxvUsageException(Status::E_ARG, "unknown $name " . var_export($v, true));
    }

    /**
     * @internal
     * @param list<string> $list
     */
    public static function name(array $list, int $v): string|int
    {
        return $list[$v] ?? $v;
    }

    /**
     * @internal
     * @param string|int|list<string> $spec
     */
    public static function flags(string|int|array $spec): int
    {
        return Web4::flags(self::FLAGS, $spec, "flags");
    }

    /** @internal */
    public static function id(string $id, string $name = "id"): \FFI\CData
    {
        return Native::fixed($id, $name, 32);
    }

    /** @internal @return array{0:int,1:int} the (kind, tag) of a classification or explicit values */
    public static function pair(?ParadoxClassification $pair, string|int|null $kind, Hexagram|int|null $tag): array
    {
        if ($pair !== null) {
            return [self::kind($pair->kind), $pair->tag->tag];
        }
        if ($kind === null || $tag === null) {
            throw new ZxvUsageException(Status::E_ARG, "give a ParadoxClassification or both kind and tag");
        }
        return [self::kind($kind), self::tag($tag)];
    }
}
