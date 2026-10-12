<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: the paradox book (zxv_paradox): the operator log, the frontier
 * log (special records, never edited), the quarantine log (contained work)
 * and the signature registry. All three logs are append-only and SHA-256
 * hash-chained. Belongs to its capability's context and is freed before it
 * (R6); thread-safe in C. See Paradox for the rules and honest limits.
 *
 *     $book = new ParadoxBook($cap);                 // $cap needs PARADOX
 *     $c = $book->classify($tag, [3, 1, 5, 1]);
 *     $book->constructive($policy, 5, $c, synth: [60, 20, 12]);  // "applied"
 */
final class ParadoxBook extends NativeHandle
{
    /**
     * @param int $logCapacity records per log, 1..Paradox::LOG_MAX
     * @param int $registryCapacity 1..Paradox::REGISTRY_MAX
     * @throws Exception\CapabilityDeniedException without PARADOX
     */
    public function __construct(public readonly Capability $capability, int $logCapacity = 1024, int $registryCapacity = 256)
    {
        $out = Native::new("zxv_paradox*");
        Native::check(Native::ffi15()->zxv_paradox_create($capability->ptr(), Native::uint32($logCapacity, "logCapacity"),
            Native::uint32($registryCapacity, "registryCapacity"), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_paradox_destroy");
    }

    /**
     * @internal
     * @param ?list<int> $evidence
     */
    public static function classifyWith(?self $book, Hexagram|int $tag, ?array $evidence, bool $stale, ?string $sig): ParadoxClassification
    {
        $ev = null;
        if ($evidence !== null) {
            if (count($evidence) !== 4) {
                throw new ZxvUsageException(Status::E_ARG, "evidence must be [pos0, neg0, pos1, neg1]");
            }
            $ev = Native::new("uint32_t[4]");
            foreach (array_values($evidence) as $i => $n) {
                $ev[$i] = Native::uint32($n, "evidence[$i]");
            }
        }
        [$k, $kP] = Native::out("int32_t");
        [$t, $tP] = Native::out("uint8_t");
        [$x, $xP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_classify($book?->ptr(), Paradox::tag($tag), $ev, $stale ? 1 : 0,
            $sig === null ? null : Paradox::id($sig, "signature"), $kP, $tP, $xP), $book?->context);
        return new ParadoxClassification(Paradox::name(Paradox::KINDS, $k->cdata), Hexagram::fromTag($t->cdata), $x->cdata < 0 ? null : $x->cdata);
    }

    /**
     * Classify a tag. Reads only: no capability.
     * @param ?list<int> $evidence [pos0, neg0, pos1, neg1] before and after; null for no trend
     * @param bool $stale the newest evidence is past your freshness limit (ignored without evidence)
     * @param ?string $signature 32-byte anomaly signature; a registered one overrides the trend
     */
    public function classify(Hexagram|int $tag, ?array $evidence = null, bool $stale = false, ?string $signature = null): ParadoxClassification
    {
        return self::classifyWith($this, $tag, $evidence, $stale, $signature);
    }

    /**
     * Add a reviewed signature to the registry. Logged.
     * @param string|int $kind "constructive", "destructive" or "neutral"
     * @param string|int|list<string> $flags Paradox::FLAGS
     * @param bool $ownerOk your assertion that the owner approved (required for money or device)
     * @return int the entry's index
     * @throws Exception\OwnerApprovalRequiredException flagged and not $ownerOk
     * @throws ZxvUsageException ZXV_E_DUPLICATE (already registered)
     */
    public function registryAdd(string $signature, string|int $kind, int $reason = 0, string|int|array $flags = 0, bool $ownerOk = false): int
    {
        [$o, $oP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_paradox_registry_add($this->capability->ptr(), $this->ptr(), Paradox::id($signature, "signature"), Paradox::kind($kind),
            Native::uint8($reason, "reason"), Paradox::flags($flags), $ownerOk ? 1 : 0, $oP));
        return $o->cdata;
    }

    /** The registry index of $signature, or null. */
    public function registryFind(string $signature): ?int
    {
        [$o, $oP] = Native::out("uint32_t");
        $rc = Native::ffi15()->zxv_paradox_registry_find($this->ptr(), Paradox::id($signature, "signature"), $oP);
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        $this->rc($rc);
        return $o->cdata;
    }

    public function registrySize(): int
    {
        [$o, $oP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_paradox_registry_count($this->ptr(), $oP));
        return $o->cdata;
    }

    /**
     * Registry entry $index; source is the frontier record it was learned from, or null for a seed.
     * @return array{signature:string, kind:string|int, reason:int, flags:list<string>, source:int|string|null}
     */
    public function registryEntry(int $index): array
    {
        $sig = Native::new("uint8_t[32]");
        [$k, $kP] = Native::out("int32_t");
        [$r, $rP] = Native::out("uint8_t");
        [$f, $fP] = Native::out("uint8_t");
        [$s, $sP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_paradox_registry_at($this->ptr(), Native::uint32($index, "index"), $sig, $kP, $rP, $fP, $sP));
        return ["signature" => Native::read($sig, 32), "kind" => Paradox::name(Paradox::KINDS, $k->cdata), "reason" => $r->cdata,
            "flags" => Web4::flagNames(Paradox::FLAGS, $f->cdata), "source" => $s->cdata === -1 ? null : Native::u64Out($s->cdata)];
    }

    /** @return list<array{signature:string, kind:string|int, reason:int, flags:list<string>, source:int|string|null}> */
    public function registry(): array
    {
        $out = [];
        for ($i = 0, $n = $this->registrySize(); $i < $n; $i++) {
            $out[] = $this->registryEntry($i);
        }
        return $out;
    }

    /** Records in log $which ("ops", "frontier" or "quarantine"). */
    public function logCount(string|int $which): int
    {
        [$o, $oP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_paradox_log_count($this->ptr(), self::log($which), $oP));
        return $o->cdata;
    }

    /** The chain head: the last record's hash (32 zeros when empty). */
    public function logHead(string|int $which): string
    {
        $o = Native::new("uint8_t[32]");
        $this->rc(Native::ffi15()->zxv_paradox_log_head($this->ptr(), self::log($which), $o));
        return Native::read($o, 32);
    }

    /** Record $index of a log as its raw bytes (see zxv_api.h for the layout). */
    public function logRecordBytes(string|int $which, int $index): string
    {
        $w = self::log($which);
        $i = Native::uint32($index, "index");
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_paradox_log_record($this->ptr(), $w, $i, $b, $c, $l), Paradox::REC_BYTES, $this->context);
    }

    /**
     * Record $index of a log, decoded. An ops or frontier record has seq,
     * op, kind, tag, outcome, aux, a, b, prev, hash, bytes; a quarantine
     * record has seq, tag, reason (trips), submitter, subject, prev, hash, bytes.
     * @return array<string, mixed>
     */
    public function logRecord(string|int $which, int $index): array
    {
        $b = $this->logRecordBytes($which, $index);
        if (self::log($which) === Paradox::LOGS["quarantine"]) {
            $u = unpack("Pseq/Ctag/Creason/Vwho", $b);
            return ["seq" => Native::u64Out($u["seq"]), "tag" => Hexagram::fromTag($u["tag"]), "reason" => Web4::flagNames(Paradox::TRIPS, $u["reason"]),
                "submitter" => $u["who"], "subject" => substr($b, 14, 32), "prev" => substr($b, 46, 32), "hash" => substr($b, 78, 32), "bytes" => $b];
        }
        $u = unpack("Pseq/Cop/Ckind/Ctag/Coutcome/Vaux", $b);
        return ["seq" => Native::u64Out($u["seq"]), "op" => $u["op"], "kind" => Paradox::name(Paradox::KINDS, $u["kind"]), "tag" => Hexagram::fromTag($u["tag"]),
            "outcome" => Paradox::name(Paradox::OUTCOMES, $u["outcome"]), "aux" => $u["aux"], "a" => substr($b, 16, 32), "b" => substr($b, 48, 32),
            "prev" => substr($b, 80, 32), "hash" => substr($b, 112, 32), "bytes" => $b];
    }

    /** @return list<array<string, mixed>> every record of a log, oldest first */
    public function logRecords(string|int $which): array
    {
        $out = [];
        for ($i = 0, $n = $this->logCount($which); $i < $n; $i++) {
            $out[] = $this->logRecord($which, $i);
        }
        return $out;
    }

    /**
     * Recompute every link of a log.
     * @throws Exception\ZxvFatalError ZXV_E_INVARIANT if a link no longer matches (the context halts)
     */
    public function verifyLog(string|int $which): self
    {
        $this->rc(Native::ffi15()->zxv_paradox_log_verify($this->ptr(), self::log($which)));
        return $this;
    }

    /**
     * One evaluation of $policy and, if $synth is given, one proposed update
     * (your synthesis; this checks and applies it): forbidden tag -> an
     * armed update is restored, "rolled_back"; kind not constructive ->
     * "refused"; armed and tension grew -> previous values, "rolled_back";
     * no synth -> "kept"; a flagged policy (money/device) or any value
     * outside its bounds -> nothing changes, "proposal_for_owner"; else
     * applied and armed, "applied".
     * @param int $tension your uint32 measure of the paradox (higher is worse)
     * @param ?list<int> $synth one value per parameter
     */
    public function constructive(ParadoxPolicy $policy, int $tension, ?ParadoxClassification $pair = null, ?array $synth = null, string|int|null $kind = null, Hexagram|int|null $tag = null): string|int
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        $s = $synth === null ? null : $policy->valuesMem($synth, "synth");
        [$o, $oP] = Native::out("int32_t");
        $this->rc(Native::ffi15()->zxv_paradox_constructive($this->capability->ptr(), $this->ptr(), $policy->ptr(), $k, $t, $s, $policy->size,
            Native::uint32($tension, "tension"), $oP));
        return Paradox::name(Paradox::OUTCOMES, $o->cdata);
    }

    /**
     * Detect and contain. The checks: $steps >= $stepLimit (0: no limit),
     * $spent >= $budget (0: none), a repeated value in $trace (state
     * fingerprints in visit order), $invariantOk false, and a forbidden
     * tag. When the kind is destructive or any check fires, the work is
     * quarantined, the state is restored from $checkpoint and a repair proof
     * is made ("contained"); otherwise "refused" and nothing changes. It
     * contains; it never replies to or acts against the submitter.
     * @param list<int|string> $trace uint64 fingerprints
     * @return array{outcome:string|int, state:string, proof:?ParadoxRepairProof} state after the call
     * @throws Exception\ZxvRefusedException ZXV_E_CAPACITY (a log is full: halt)
     */
    public function destructive(string $state, string $checkpoint, ?ParadoxClassification $pair = null, string|int|null $kind = null, Hexagram|int|null $tag = null,
        int $steps = 0, int $stepLimit = 0, int $spent = 0, int $budget = 0, array $trace = [], bool $invariantOk = true, string $pattern = "", int $submitter = 0): array
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        if (strlen($state) !== strlen($checkpoint)) {
            throw new ZxvUsageException(Status::E_ARG, "state and checkpoint must have the same length");
        }
        $tr = null;
        if ($trace !== []) {
            $tr = Native::new("uint64_t[" . count($trace) . "]");
            foreach (array_values($trace) as $i => $v) {
                $tr[$i] = Native::u64($v, "trace[$i]");
            }
        }
        $sm = Native::bytes($state, true);
        [$o, $oP] = Native::out("int32_t");
        $proof = Native::new("uint8_t[" . Paradox::REPAIR_BYTES . "]");
        $this->rc(Native::ffi15()->zxv_paradox_destructive($this->capability->ptr(), $this->ptr(), $k, $t, Native::uint32($steps, "steps"),
            Native::uint32($stepLimit, "stepLimit"), Native::uint32($spent, "spent"), Native::uint32($budget, "budget"), $tr, count($trace),
            $invariantOk ? 1 : 0, $sm, Native::bytes($checkpoint, true), strlen($state), Native::bytes($pattern, true), strlen($pattern),
            Native::uint32($submitter, "submitter"), $oP, $proof));
        $oc = Paradox::name(Paradox::OUTCOMES, $o->cdata);
        return ["outcome" => $oc, "state" => $sm === null ? "" : Native::read($sm, strlen($state)),
            "proof" => $oc === "contained" ? new ParadoxRepairProof(Native::read($proof, Paradox::REPAIR_BYTES)) : null];
    }

    /**
     * Settle $hold on decisive evidence or on $ownerPick (a branch index); otherwise keep every branch.
     * @return array{0:string|int,1:?int} [outcome, settled branch or null]: "settled", "deferred", "rolled_back" or "refused"
     */
    public function neutral(ParadoxHold $hold, ?ParadoxClassification $pair = null, ?int $ownerPick = null, string|int|null $kind = null, Hexagram|int|null $tag = null): array
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        $pick = $ownerPick === null ? -1 : Native::int($ownerPick, "ownerPick", 0, 2147483647);
        [$o, $oP] = Native::out("int32_t");
        [$s, $sP] = Native::out("int32_t");
        $this->rc(Native::ffi15()->zxv_paradox_neutral($this->capability->ptr(), $this->ptr(), $hold->ptr(), $k, $t, $pick, $oP, $sP));
        return [Paradox::name(Paradox::OUTCOMES, $o->cdata), $s->cdata < 0 ? null : $s->cdata];
    }

    /**
     * Record an unclassified paradox: a permanent frontier record and an
     * operator record. The work stays sandboxed until a reviewed
     * classification exists.
     * @return array{0:string|int,1:?int} [outcome, frontier index or null]: "recorded", "refused" or "rolled_back"
     */
    public function special(string $signature, string $subject, ?ParadoxClassification $pair = null, string|int|null $kind = null, Hexagram|int|null $tag = null): array
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        [$o, $oP] = Native::out("int32_t");
        [$r, $rP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_paradox_special($this->capability->ptr(), $this->ptr(), $k, $t, Paradox::id($signature, "signature"),
            Paradox::id($subject, "subject"), $oP, $rP));
        return [Paradox::name(Paradox::OUTCOMES, $o->cdata), $r->cdata === 0xFFFFFFFF ? null : $r->cdata];
    }

    /**
     * A draft classification of frontier record $record, for review. Logged.
     * @param string|int|list<string> $flags
     * @throws ZxvUsageException ZXV_E_NOT_FOUND (no such record)
     */
    public function draft(int $record, string|int $kind, int $reason = 0, string|int|array $flags = 0): ParadoxDraft
    {
        $out = Native::new("uint8_t[" . Paradox::DRAFT_BYTES . "]");
        $this->rc(Native::ffi15()->zxv_paradox_draft($this->capability->ptr(), $this->ptr(), Native::uint32($record, "record"), Paradox::kind($kind),
            Native::uint8($reason, "reason"), Paradox::flags($flags), $out));
        return new ParadoxDraft(Native::read($out, Paradox::DRAFT_BYTES));
    }

    /**
     * Review a draft. A money or device draft needs $ownerOk; others
     * $ownerOk or $consensusOk (your quorum result). Logged.
     * @return string|int "accepted", "proposal_for_owner" or "refused"
     * @throws Exception\VerificationFailedException the hash or signature does not match the record
     */
    public function review(ParadoxDraft|string $draft, bool $ownerOk = false, bool $consensusOk = false): string|int
    {
        $d = $draft instanceof ParadoxDraft ? $draft->bytes : $draft;
        [$o, $oP] = Native::out("int32_t");
        $this->rc(Native::ffi15()->zxv_paradox_review($this->capability->ptr(), $this->ptr(), Native::fixed($d, "draft", Paradox::DRAFT_BYTES),
            $ownerOk ? 1 : 0, $consensusOk ? 1 : 0, $oP));
        return Paradox::name(Paradox::OUTCOMES, $o->cdata);
    }

    /**
     * One pass over $graph, in the fixed order. Logged.
     * @return string|int "resolved" (every node final) or "pending"
     * @throws Exception\ZxvRefusedException ZXV_E_PARADOX_STEP when the pass count reaches the step limit unresolved
     */
    public function pass(ParadoxGraph $graph): string|int
    {
        [$o, $oP] = Native::out("int32_t");
        $this->rc(Native::ffi15()->zxv_paradox_graph_pass($this->capability->ptr(), $this->ptr(), $graph->ptr(), $oP));
        return Paradox::name(Paradox::OUTCOMES, $o->cdata);
    }

    private static function log(string|int $which): int
    {
        if (is_int($which)) {
            return Native::int32($which, "which");
        }
        return Paradox::LOGS[$which] ?? throw new ZxvUsageException(Status::E_ARG, "log must be ops, frontier or quarantine");
    }

    private function rc(int $rc): void
    {
        Native::check($rc, $this->context);
    }
}
