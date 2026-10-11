<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception\CapabilityDeniedException;
use Zedec\Zxv\Exception\ZxvFatalError;
use Zedec\Zxv\Exception\ZxvRefusedException;
use Zedec\Zxv\Exception\ZxvUsageException;
use Zedec\Zxv\HexAction;
use Zedec\Zxv\HexBase;
use Zedec\Zxv\Hexagram;
use Zedec\Zxv\HexPhase;
use Zedec\Zxv\HexPolarity;
use Zedec\Zxv\QuarantineLog;
use Zedec\Zxv\Truth;
use Zedec\Zxv\Zxv;

/** ABI 1.3: the 64-state hexagram tag and quarantine log against vectors.json "hex". */
final class HexagramTest extends TestCase
{
    /** @return array<string, mixed> */
    private static function hx(): array
    {
        return self::vectors()["hex"];
    }

    /** @param list<\UnitEnum> $cases @return list<string> */
    private static function names(array $cases): array
    {
        return array_map(static fn (\UnitEnum $c): string => strtolower($c->name), $cases);
    }

    public function testTagsMatchVectors(): void
    {
        $hx = self::hx();
        $this->assertTrue(Zxv::hasFeature("hex"));
        $this->assertSame($hx["base_names"], self::names(HexBase::cases()));
        $this->assertSame($hx["polarity_names"], self::names(HexPolarity::cases()));
        $this->assertSame($hx["phase_names"], self::names(HexPhase::cases()));
        $this->assertSame($hx["action_names"], self::names(HexAction::cases()));
        foreach ($hx["tags"] as $v) {
            $h = Hexagram::fromTag($v["tag"]);
            $this->assertSame($v["name"], $h->name());
            $this->assertSame($v["base"], $h->base()->value);
            $this->assertSame($v["polarity"], $h->polarity()->value);
            $this->assertSame($v["phase"], $h->phase()->value);
            $this->assertSame($v["action"], $h->action()->value);
            $this->assertSame($h, Hexagram::of($h->base(), $h->polarity(), $h->phase()));
        }
        foreach (HexPhase::cases() as $p) {
            $this->assertSame($hx["phase_next"][$p->value], $p->next()->value);
        }
    }

    public function testLogicTables(): void
    {
        $hx = self::hx();
        $all = Hexagram::all();
        foreach ($all as $a) {
            $this->assertSame($hx["not"][$a->tag], $a->not()->tag);
            foreach ($all as $b) {
                $this->assertSame($hx["and"][$a->tag][$b->tag], $a->and($b)->tag);
                $this->assertSame($hx["or"][$a->tag][$b->tag], $a->or($b)->tag);
            }
        }
    }

    public function testStepAndWithState(): void
    {
        $hx = self::hx();
        foreach (Hexagram::all() as $h) {
            foreach (HexPhase::cases() as $p) {
                $want = $hx["step"][$h->tag][$p->value];
                if ($want < 0) {
                    $e = $this->assertThrows(ZxvRefusedException::class, static fn () => $h->step($p));
                    $this->assertSame("ZXV_E_STATE", $e->getStatusName());
                } else {
                    $this->assertSame($want, $h->step($p)->tag);
                }
            }
            foreach (HexBase::cases() as $b) {
                foreach (HexPolarity::cases() as $pol) {
                    $want = $hx["set"][$h->tag][$b->value * 4 + $pol->value];
                    if ($want < 0) {
                        $this->assertThrows(ZxvRefusedException::class, static fn () => $h->withState($b, $pol));
                    } else {
                        $this->assertSame($want, $h->withState($b, $pol)->tag);
                    }
                }
            }
            $this->assertSame(Hexagram::of($h->base(), HexPolarity::Forbidden, $h->phase()), $h->forbid());
            $this->assertSame($h->phase()->next(), $h->advance()->phase());
        }
    }

    public function testPolarityAndTruthBridge(): void
    {
        $hx = self::hx();
        foreach ($hx["polarity"] as $c) {
            $this->assertSame($c["polarity"], Hexagram::polarityFromEvidence($c["pos0"], $c["neg0"], $c["pos1"], $c["neg1"], $c["stale"])->value);
        }
        $this->assertThrows(ZxvUsageException::class, static fn () => Hexagram::polarityFromEvidence(-1, 0, 0, 0, false));
        foreach (Truth::all() as $t) {
            $h = Hexagram::fromTruth($t, HexPolarity::Neutral, HexPhase::Potential);
            $this->assertSame($hx["lp_to_base"][$t->value], $h->base()->value);
            $this->assertSame($t, $h->toTruth());
        }
        foreach (HexBase::cases() as $b) {
            $this->assertSame($hx["base_to_lp"][$b->value], Hexagram::of($b, HexPolarity::Neutral, HexPhase::Potential)->toTruth()->value);
        }
        // The numbering differs on purpose: Truth TRUE is 1, HexBase::True is 3.
        $this->assertSame(1, Truth::TRUE);
        $this->assertSame(HexBase::True, Hexagram::fromTruth(Truth::true(), HexPolarity::Neutral, HexPhase::Potential)->base());
    }

    public function testRefusalsAndValueSemantics(): void
    {
        foreach ([64, 100, 255, 256, -1] as $bad) {
            $this->assertThrows(ZxvUsageException::class, static fn () => Hexagram::fromTag($bad));
        }
        $h = Hexagram::of(HexBase::True, HexPolarity::Negentropic, HexPhase::Manifest);
        $this->assertSame(63, $h->tag);
        $this->assertSame("true/negentropic/manifest", (string) $h);
        $f = Hexagram::of(HexBase::True, HexPolarity::Forbidden, HexPhase::Potential);
        $this->assertSame(HexAction::Rollback, $f->action());
        for ($i = 0; $i < 4; $i++) {
            $f = $f->advance();
            $this->assertTrue($f->isForbidden());
        }
        $this->assertTrue($f->and($h)->isForbidden() && $f->or($h)->isForbidden() && $f->not()->isForbidden());
        $this->assertThrows(ZxvRefusedException::class, static fn () => $f->withState(HexBase::True, HexPolarity::Neutral));
        $this->assertSame(HexBase::False, $f->withState(HexBase::False, HexPolarity::Forbidden)->base());
    }

    private static function link(int $seq, int $tag, int $reason, int $who, string $subj, string $prev): string
    {
        return hash("sha256", "zxv/lpx-qlog/v1" . pack("P", $seq) . pack("CC", $tag, $reason) . pack("V", $who) . $subj . $prev, true);
    }

    public function testQuarantineChainMatchesVectors(): void
    {
        $q = self::hx()["qlog"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($q): void {
            $log = new QuarantineLog($cap, 3);
            $this->assertSame(0, count($log));
            $this->assertSame(str_repeat("\0", 32), $log->head());
            $prev = str_repeat("\0", 32);
            foreach ($q["records"] as $r) {
                $subj = (string) hex2bin($r["subject"]);
                $seq = $log->append(Hexagram::fromTag($r["tag"]), $r["reason"], $r["submitter"], $subj);
                $this->assertSame($r["seq"], $seq);
                $want = self::link($seq, $r["tag"], $r["reason"], $r["submitter"], $subj, $prev);
                $this->assertSame($r["hash"], bin2hex($want));
                $rec = $log->record($seq);
                $this->assertSame($want, $rec->hash);
                $this->assertSame($prev, $rec->prev);
                $this->assertSame($subj, $rec->subject);
                $this->assertSame($r["tag"], $rec->tag->tag);
                $this->assertSame($r["reason"], $rec->reason);
                $this->assertSame($r["submitter"], $rec->submitter);
                $prev = $want;
            }
            $this->assertSame($q["head"], bin2hex($log->head()));
            $this->assertSame(3, $log->count());
            $seqs = [];
            foreach ($log as $rec) {
                $seqs[] = $rec->seq;
            }
            $this->assertSame([0, 1, 2], $seqs);
            $log->verify();
            $e = $this->assertThrows(ZxvRefusedException::class, static fn () => $log->append(Hexagram::fromTag(0x3B), 0, 0, str_repeat("\0", 32)));
            $this->assertSame("ZXV_E_CAPACITY", $e->getStatusName());
        });
    }

    public function testQuarantineRefusals(): void
    {
        $forb = Hexagram::of(HexBase::Both, HexPolarity::Forbidden, HexPhase::Emerging);
        $zero = str_repeat("\0", 32);
        $this->withContext(function (Context $ctx, Capability $cap) use ($forb, $zero): void {
            $noAttest = $cap->attenuate(Capability::ALL & ~Capability::ATTEST);
            $this->assertThrows(CapabilityDeniedException::class, static fn () => new QuarantineLog($noAttest, 4));
            $this->assertThrows(ZxvUsageException::class, static fn () => new QuarantineLog($cap, 0));
            $this->assertThrows(ZxvUsageException::class, static fn () => new QuarantineLog($cap, QuarantineLog::MAX_RECORDS + 1));
            $log = new QuarantineLog($cap, 4);
            $this->assertThrows(ZxvUsageException::class, static fn () => $log->append(Hexagram::of(HexBase::True, HexPolarity::Neutral, HexPhase::Emerging), 1, 1, $zero));
            $this->assertThrows(ZxvUsageException::class, static fn () => $log->append($forb, 1, 1, str_repeat("\0", 31)));
            $this->assertThrows(ZxvUsageException::class, static fn () => $log->append($forb, 256, 1, $zero));
            $this->assertThrows(ZxvUsageException::class, static fn () => $log->append($forb, 1, -1, $zero));
            $this->assertSame(0, $log->append($forb, 9, 0xFFFFFFFF, $zero));
            $this->assertSame(0xFFFFFFFF, $log->record(0)->submitter);
            $ctx->halt("test");
            $this->assertThrows(ZxvFatalError::class, static fn () => $log->count());
        });
    }
}
