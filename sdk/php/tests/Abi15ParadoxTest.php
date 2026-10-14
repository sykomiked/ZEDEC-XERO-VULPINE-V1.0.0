<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\HexAction;
use Zedec\Zxv\Paradox;
use Zedec\Zxv\ParadoxBook;
use Zedec\Zxv\ParadoxGraph;
use Zedec\Zxv\ParadoxHold;
use Zedec\Zxv\ParadoxPolicy;
use Zedec\Zxv\ParadoxRepairProof;

/**
 * ABI 1.5 conformance: the paradox section of vectors.json. Every book step
 * runs in order on ONE book (log capacity 64, registry capacity 16), exactly
 * as the generator ran them, so the log heads must match.
 */
final class Abi15ParadoxTest extends TestCase
{
    /** @return array<string, mixed> */
    private static function pv(): array
    {
        return self::vectors()["paradox"];
    }

    private static function sigRef(?string $ref): ?string
    {
        if ($ref === null) {
            return null;
        }
        preg_match('/\d+/', $ref, $m);
        return self::hex(self::pv()["signature"][(int) $m[0]]["sig"]);
    }

    private static function id(string $letter): string
    {
        return str_repeat($letter, 32);
    }

    private static function snake(string $name): string
    {
        return str_replace("-", "_", $name);
    }

    public function testPureTables(): void
    {
        $pv = self::pv();
        foreach ($pv["signature"] as $v) {
            $this->assertSame($v["sig"], self::unhex(Paradox::signature($v["utf8"])));
        }
        foreach ($pv["kind_names"] as $i => $n) {
            $this->assertSame($n, Paradox::kindName($i));
        }
        foreach ($pv["outcome_names"] as $i => $n) {
            $this->assertSame($n, Paradox::outcomeName($i));
            $this->assertSame($n, Paradox::outcomeName(Paradox::OUTCOMES[$i]));
        }
        foreach ($pv["node_state_names"] as $i => $n) {
            $this->assertSame($n, Paradox::nodeStateName($i));
        }
        $this->assertSame(array_map(self::snake(...), $pv["kind_names"]), Paradox::KINDS);
        $this->assertSame(array_map(self::snake(...), $pv["outcome_names"]), Paradox::OUTCOMES);
        $this->assertSame(array_map(self::snake(...), $pv["node_state_names"]), Paradox::NODE_STATES);
        foreach ($pv["kind_of_tag"] as $t => $k) {
            $this->assertSame(Paradox::KINDS[$k], Paradox::kindOfTag($t));
        }
        foreach ($pv["action"] as $k => $row) {
            foreach ($row as $t => $d) {
                if ($d === -1) {
                    $this->expectStatus("ZXV_E_ARG", fn () => Paradox::action($k, $t));
                } else {
                    $this->assertSame(Paradox::DOS[$d], Paradox::action($k, $t));
                }
            }
        }
        foreach ($pv["outcome_action"] as $o => $a) {
            $this->assertSame(HexAction::from($a), Paradox::outcomeAction($o));
        }
    }

    /** @param array<string, array{count:int, head:string}> $expected */
    private function heads(ParadoxBook $book, array $expected): void
    {
        foreach ($expected as $w => $h) {
            $this->assertSame($h["count"], $book->logCount($w), $w);
            $this->assertSame($h["head"], self::unhex($book->logHead($w)), $w);
        }
    }

    public function testBookScenario(): void
    {
        $pv = self::pv();
        $this->withContext15(function (Context $ctx, Capability $cap) use ($pv) {
            $book = new ParadoxBook($cap, 64, 16);
            $this->heads($book, $pv["heads_fresh"]);
            $this->bookClassify($book);
            $this->heads($book, $pv["heads_after_classify"]);
            $this->bookConstructive($cap, $book);
            $this->bookDestructive($book);
            $this->bookNeutral($cap, $book);
            $this->bookSpecial($book);
            $this->bookGraphs($cap, $book);
            $this->heads($book, $pv["heads_final"]);
            foreach ($pv["logs"] as $w => $recs) {
                $this->assertSame($recs, array_map(fn (array $r) => self::unhex($r["bytes"]), $book->logRecords($w)), $w);
            }
            $q = $book->logRecord("quarantine", 0);
            $this->assertSame([0, ["steps", "cycle"], 7], [$q["seq"], $q["reason"], $q["submitter"]]);
            $op = $book->logRecord("ops", 0);
            $this->assertSame([0, "destructive"], [$op["seq"], $op["kind"]]);
            foreach (array_map(null, $pv["log_verify"], array_keys(Paradox::LOGS)) as [$st, $w]) {
                $this->expectStatus($st, fn () => $book->verifyLog($w));
            }
            $book->close();
        });
    }

    private function bookClassify(ParadoxBook $book): void
    {
        $pv = self::pv();
        foreach ($pv["classify"] as $v) {
            if (isset($v["registry_add"])) {
                $r = $v["registry_add"];
                $idx = $this->expectStatus($r["status"], fn () => $book->registryAdd(self::sigRef($r["sig"]), $r["kind"], $r["reason"], $r["flags"], $r["owner_ok"]));
                $this->assertSame($r["index"], $idx);
                continue;
            }
            $c = $this->expectStatus($v["status"], fn () => $book->classify($v["tag"], $v["evidence"], $v["stale"], self::sigRef($v["sig"] ?? null)));
            $this->assertSame(Paradox::KINDS[$v["kind"]], $c->kind);
            $this->assertSame($v["out_tag"], $c->tag->tag);
            $this->assertSame($v["reg_index"] < 0 ? null : $v["reg_index"], $c->registryIndex);
            if (!isset($v["sig"])) {
                // Without a registry hit, the bookless classifier agrees.
                $this->assertSame($c->toArray(), Paradox::classify($v["tag"], $v["evidence"], $v["stale"])->toArray());
            }
        }
        $sigM = Paradox::signature("money:drain");
        foreach ($pv["registry"] as $v) {
            $idx = $this->expectStatus($v["status"], fn () => $book->registryAdd($sigM, $v["kind"], 2, $v["flags"], $v["owner_ok"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["index"], $idx);
            }
        }
        $this->assertSame(1, $book->registryFind($sigM));
        $this->assertSame(null, $book->registryFind(Paradox::signature("unknown")));
        $e = $book->registryEntry(1);
        $this->assertSame([$sigM, "neutral", 2, ["money"]], [$e["signature"], $e["kind"], $e["reason"], $e["flags"]]);
    }

    private function bookConstructive(Capability $cap, ParadoxBook $book): void
    {
        $c = self::pv()["constructive"];
        [$lo, $hi, $init] = array_map(fn (string $k) => array_map(fn ($x) => (int) $x, $c[$k]), ["lo", "hi", "initial"]);
        $pols = ["plain" => new ParadoxPolicy($cap, $lo, $hi, $init), "money" => new ParadoxPolicy($cap, $lo, $hi, $init, "money")];
        $this->assertSame([$lo, $hi], $pols["plain"]->bounds());
        $this->assertSame(["money"], $pols["money"]->flags());
        foreach ($c["steps"] as $i => $s) {
            $pol = $pols[$s["policy"]];
            $synth = isset($s["synth"]) ? array_map(fn ($x) => (int) $x, $s["synth"]) : null;
            $got = $this->expectStatus($s["status"], fn () => $book->constructive($pol, $s["tension"], synth: $synth, kind: $s["kind"], tag: $s["tag"]));
            if ($s["status"] === "ZXV_OK") {
                $this->assertSame(Paradox::OUTCOMES[$s["outcome"]], $got);
            }
            $this->assertSame(array_map(fn ($x) => (int) $x, $s["values"]), $pol->values(), "step $i");
            $this->assertSame(array_map(fn ($x) => (int) $x, $s["prev"]), $pol->previous(), "step $i");
            $this->assertSame($s["armed"], $pol->isArmed(), "step $i");
            $this->assertSame($s["tension_at"], $pol->tensionAt(), "step $i");
            if (isset($s["proposal"])) {
                $this->assertSame($s["proposal"], self::unhex($pol->proposal($synth)));
            }
        }
        foreach ($pols as $p) {
            $p->close();
        }
    }

    private function bookDestructive(ParadoxBook $book): void
    {
        foreach (self::pv()["destructive"] as $d) {
            $r = $this->expectStatus($d["status"], fn () => $book->destructive($d["state_utf8"], $d["checkpoint_utf8"], kind: $d["kind"], tag: $d["tag"],
                steps: $d["steps"], stepLimit: $d["step_limit"], spent: $d["spent"], budget: $d["budget"],
                trace: array_map(fn ($x) => self::u64($x), $d["trace"]), invariantOk: $d["invariant_ok"], pattern: $d["pattern_utf8"], submitter: $d["submitter"]));
            $this->assertSame(Paradox::OUTCOMES[$d["outcome"]], $r["outcome"]);
            $this->assertSame($d["state_after"], self::unhex($r["state"]));
            if (!isset($d["repair_proof"])) {
                $this->assertSame(null, $r["proof"]);
                continue;
            }
            $p = $r["proof"];
            $this->assertSame($d["repair_proof"], self::unhex($p->bytes));
            $this->assertSame($d["repair_verify"] === "ZXV_OK", $p->isValid());
            $this->assertSame(Paradox::signature($d["pattern_utf8"]), $p->signature());
            $this->assertSame([10, 3, 7], [$p->steps(), $p->spent(), $p->submitter()]);
            $flipped = $p->bytes;
            $flipped[0] = chr(ord($flipped[0]) ^ 1);
            $this->assertSame($d["repair_verify_flipped_byte0"] === "ZXV_OK", (new ParadoxRepairProof($flipped))->isValid());
        }
    }

    private function bookNeutral(Capability $cap, ParadoxBook $book): void
    {
        $n = self::pv()["neutral"];
        $hold = new ParadoxHold($cap, $n["capacity"], $n["decisive"]);
        [$b0, $b1] = array_map(fn ($x) => self::hex($x), $n["branches"]);
        $hold->add($b0)->add($b1);
        $hold->evidence($b0, true)->evidence($b1, true);
        $this->assertSame([["id" => $b0, "for" => 1, "against" => 0], ["id" => $b1, "for" => 1, "against" => 0]], $hold->branches());
        $this->assertSame(null, $hold->decisiveBranch()); // 1 for each: neither leads by 2
        foreach (array_slice($n["steps"], 1) as $s) {
            for ($i = 0; $i < $s["add_for_branch0"]; $i++) {
                $hold->evidence($b0, true);
            }
            $pick = $s["owner_pick"] < 0 ? null : $s["owner_pick"];
            [$oc, $settled] = $this->expectStatus($s["status"], fn () => $book->neutral($hold, ownerPick: $pick, kind: $s["kind"], tag: $s["tag"]));
            $this->assertSame(Paradox::OUTCOMES[$s["outcome"]], $oc);
            $this->assertSame($s["settled"] < 0 ? null : $s["settled"], $settled);
        }
        $this->assertSame(0, $hold->settled());
        $hold->close();
    }

    private function bookSpecial(ParadoxBook $book): void
    {
        $s = self::pv()["special"];
        $this->assertSame($s["sig"], self::unhex(Paradox::signature($s["sig_utf8"])));
        [$oc, $rec] = $this->expectStatus($s["status"], fn () => $book->special(self::hex($s["sig"]), self::hex($s["subject"]), kind: $s["kind"], tag: $s["tag"]));
        $this->assertSame([Paradox::OUTCOMES[$s["outcome"]], $s["rec"]], [$oc, $rec]);
        $drafts = [];
        foreach (["draft", "draft_money"] as $k) {
            $d = $s[$k];
            $got = $this->expectStatus($d["status"], fn () => $book->draft($rec, $d["kind"], $d["reason"], $d["flags"]));
            $this->assertSame($d["bytes"], self::unhex($got->bytes));
            $drafts[$k] = $got;
        }
        $this->assertSame(["destructive", 3, [], $rec], [$drafts["draft"]->kind(), $drafts["draft"]->reason(), $drafts["draft"]->flags(), $drafts["draft"]->record()]);
        $this->assertSame(["money"], $drafts["draft_money"]->flags());
        foreach ($s["review"] as $r) {
            $got = $this->expectStatus($r["status"], fn () => $book->review($drafts[$r["draft"]], $r["owner_ok"], $r["consensus_ok"]));
            $this->assertSame(Paradox::OUTCOMES[$r["outcome"]], $got);
        }
        $this->assertSame($s["registry_count_after"], $book->registrySize());
    }

    private function bookGraphs(Capability $cap, ParadoxBook $book): void
    {
        foreach (self::pv()["graphs"] as $gv) {
            $g = new ParadoxGraph($cap, $gv["step_limit"]);
            foreach ($gv["nodes"] as $n) {
                $this->expectStatus($n["status"], fn () => $g->addNode(self::id($n["id"]), $n["mag"], $n["dir"], kind: $n["kind"], tag: $n["tag"]));
            }
            foreach ($gv["deps"] as $d) {
                $this->expectStatus($d["status"], fn () => $g->addDependency(self::id($d["id"]), self::id($d["dep"])));
            }
            foreach ($gv["passes"] as $p) {
                $got = $this->expectStatus($p["status"], fn () => $book->pass($g));
                if ($p["status"] === "ZXV_OK") {
                    $this->assertSame(Paradox::OUTCOMES[$p["outcome"]], $got);
                }
                $info = $g->info();
                $this->assertSame([$p["pass"], $p["total_mag"], $p["total_dir"], $p["escalated"]],
                    [$info["passes"], $info["totalMag"], $info["totalDir"], $info["escalated"]], $gv["label"]);
                $this->assertSame($p["chain"], self::unhex($info["chain"]));
                foreach ($p["states"] as $letter => $st) {
                    $this->assertSame(Paradox::NODE_STATES[$st], $g->state(self::id($letter)), $gv["label"] . " " . $letter);
                }
            }
            $this->assertSame(count($gv["nodes"]), count($g->nodes()));
            $g->close();
        }
    }
}
