<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Aipi;
use Zedec\Zxv\AipiCallRecord;
use Zedec\Zxv\AipiClaimRecord;
use Zedec\Zxv\AipiFunctionRecord;
use Zedec\Zxv\AipiNode;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Cid;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Treaty;
use Zedec\Zxv\TreatyPic;

/** ABI 1.5 conformance: the treaty, cid and aipi sections of vectors.json. */
final class Abi15TreatyCidTest extends TestCase
{
    /** @return array<string, mixed> */
    private static function tv(): array
    {
        return self::vectors()["treaty"];
    }

    public function testTreatySutra(): void
    {
        $tv = self::tv();
        $this->assertSame($tv["rec_len"], Treaty::REC_LEN);
        $this->assertSame($tv["result_len"], Treaty::RESULT_LEN);
        foreach ($tv["sutra"] as $v) {
            $t = Treaty::parse($v["sutra"]);
            $this->assertSame("ZXV_OK", $v["status"]);
            $this->assertTrue($t->isValid());
            $this->assertSame($v["record"], self::unhex($t->toRecord()));
            $back = Treaty::fromRecord(self::hex($v["record"]));
            $this->assertSame($v["record_parses_equal"], $back->equals($t));
            $this->assertSame($v["reemit"], $t->toSutra());
            $this->assertSame($v["name"], $t->name());
            $this->assertSame($v["asset"], $t->asset());
            $this->assertSame(Treaty::OPS[$v["op"]], $t->op());
            $this->assertSame(strtolower($tv["op_names"][(string) $v["op"]]), $t->op());
            $this->assertSame($v["party_count"], $t->partyCount());
            $this->assertSame($v["owe_count"], $t->obligationCount());
            foreach ($v["parties"] as $i => $p) {
                $got = $t->party($i);
                $this->assertSame([$p["id"], self::u64($p["deposit"]), self::u64($p["claim"]), $p["weight"]],
                    [$got["id"], $got["deposit"], $got["claim"], [$got["weightNum"], $got["weightDen"]]]);
            }
            foreach ($v["owes"] as $i => $o) {
                $got = $t->obligation($i);
                $this->assertSame([$o["from"], $o["to"], self::u64($o["amount"])], [$got["from"], $got["to"], $got["amount"]]);
            }
            $this->assertSame(count($v["parties"]), count($t->parties()));
            $this->expectStatus("ZXV_E_NOT_FOUND", fn () => $t->party($v["party_count"]));
            $t->close();
            $back->close();
        }
    }

    public function testTreatyRejectsWithFaultAndOffset(): void
    {
        $tv = self::tv();
        foreach ($tv["reject"] as $v) {
            $err = $this->expectStatus($v["status"], fn () => Treaty::parse($v["sutra"]));
            $this->assertTrue($err instanceof Exception\TreatyException);
            $this->assertSame(Treaty::FAULTS[$v["fault"]], $err->fault, var_export($v["sutra"], true));
            $this->assertSame($v["offset"], $err->offset, var_export($v["sutra"], true));
        }
        // A failed re-parse leaves the treaty unchanged.
        $t = Treaty::parse($tv["sutra"][0]["sutra"]);
        $this->assertThrows(Exception\TreatyException::class, fn () => $t->parseSutra("TREATY"));
        $this->assertSame("CORRIDOR-7", $t->name());
        $t->clear();
        $this->assertFalse($t->isValid());
        $this->assertThrows(Exception\TreatyException::class, fn () => $t->validate());
    }

    public function testTreatyRecordRejects(): void
    {
        $tv = self::tv();
        foreach ($tv["reject_record"] as $v) {
            $this->assertSame("sutra[0].record", $v["base"]);
            $rec = self::hex($tv["sutra"][0]["record"]);
            $rec[$v["patch_offset"]] = self::hex($v["patch_byte"]);
            $err = $this->expectStatus($v["status"], fn () => Treaty::fromRecord($rec));
            $this->assertSame(Treaty::FAULTS[$v["fault"]], $err->fault);
            $this->assertSame($v["offset"], $err->offset);
        }
    }

    public function testTreatyPic(): void
    {
        foreach (self::tv()["pic"] as $v) {
            $a = (int) $v["a"];
            $b = (int) $v["b"];
            if ($v["op"] === "div") {
                $got = $this->expectStatus($v["status"], fn () => TreatyPic::divide($a, $b));
                if ($v["status"] === "ZXV_OK") {
                    $this->assertSame([(int) $v["out"], (int) $v["rem"]], $got);
                }
            } else {
                $fn = ["add" => "add", "sub" => "sub", "mul" => "mul"][$v["op"]];
                $got = $this->expectStatus($v["status"], fn () => TreatyPic::$fn($a, $b));
                if ($v["status"] === "ZXV_OK") {
                    $this->assertSame((int) $v["out"], $got);
                }
            }
        }
    }

    public function testTreatyRun(): void
    {
        foreach (self::tv()["run"] as $v) {
            $t = Treaty::parse($v["sutra"]);
            $r = $this->expectStatus($v["status"], fn () => $t->run());
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["parties"], $r->partyCount());
                $this->assertSame($v["legs"], $r->legCount());
                $this->assertSame(self::u64($v["fees"]), $r->fees());
                foreach ($v["party"] as $i => $p) {
                    $this->assertSame(["net" => (int) $p["net"], "balance" => (int) $p["balance"], "dividend" => (int) $p["dividend"]], $r->party($i));
                }
                foreach ($v["leg"] as $i => $l) {
                    $this->assertSame(["from" => $l["from"], "to" => $l["to"], "amount" => self::u64($l["amount"])], $r->leg($i));
                }
                $this->assertSame($v["result_record"], self::unhex($r->toRecord()));
                $r->close();
                $this->assertThrows(Exception\ZxvUsageException::class, fn () => $r->fees());
            } else {
                $this->assertTrue($r instanceof Exception\InsufficientFundsException);
            }
            $t->close();
        }
    }

    /** @param array<string, mixed> $v */
    private function checkCid(Cid $cid, array $v): void
    {
        $this->assertSame($v["cid"], self::unhex($cid->bytes));
        $this->assertSame($v["text_default"], (string) $cid);
        if (isset($v["text_58btc"])) {
            if (str_starts_with($v["text_58btc"], "ZXV_")) {
                $this->expectStatus($v["text_58btc"], fn () => $cid->text(Cid::BASE58BTC));
            } else {
                $this->assertSame($v["text_58btc"], $cid->text(Cid::BASE58BTC));
            }
        }
        foreach (["version" => "version", "codec" => "codec", "hash" => "hashCode"] as $k => $prop) {
            if (isset($v[$k])) {
                $this->assertSame($v[$k], $cid->$prop);
            }
        }
        if (isset($v["digest"])) {
            $this->assertSame($v["digest"], self::unhex($cid->digest));
        }
    }

    public function testCidComputeUnixfsText(): void
    {
        $cv = self::vectors()["cid"];
        foreach ($cv["compute"] as $v) {
            $c = $this->expectStatus($v["status"], fn () => Cid::compute($v["utf8"], $v["codec"]));
            $this->checkCid($c, $v);
            $this->assertTrue($c->verify($v["utf8"]));
        }
        foreach ($cv["unixfs"] as $v) {
            $data = !empty($v["ramp"]) ? self::ramp($v["len"]) : $v["utf8"];
            $c = $this->expectStatus($v["status"], fn () => Cid::unixfs($data));
            $this->checkCid($c, $v);
        }
        foreach ($cv["text"] as $v) {
            $c = $this->expectStatus($v["status"], fn () => Cid::parse($v["text"]));
            $this->checkCid($c, $v);
            $this->assertSame($v["v1"], self::unhex($c->toV1()->bytes));
            $this->assertSame($v["v1_text"], (string) $c->toV1());
            $this->assertSame($v["provider_key"], self::unhex($c->providerKey()));
            $this->assertTrue($c->equals(Cid::parse((string) $c)));
        }
        foreach ($cv["text_reject"] as $v) {
            $this->expectStatus($v["status"], fn () => Cid::parse($v["text"]));
        }
    }

    public function testCidVerify(): void
    {
        foreach (self::vectors()["cid"]["verify"] as $v) {
            $this->expectStatus($v["status"], fn () => (new Cid(self::hex($v["cid"])))->verifyOrThrow($v["utf8"]));
        }
        $good = Cid::compute("abc");
        $this->assertTrue($good->verify("abc"));
        $this->assertFalse($good->verify("abd"));
    }

    /** @param array<string, mixed> $v */
    private static function fnRecord(array $v): AipiFunctionRecord
    {
        $av = self::vectors()["aipi"];
        return AipiFunctionRecord::encode(
            intent: $v["intent"], verify: $v["verify"], determinism: $v["determinism"], fuelMax: self::u64($v["fuel_max"]),
            moduleCid: new Cid(self::hex($av["module_cid"])), entry: $v["entry"], privacy: $v["privacy"],
            quorumN: $v["quorum_n"], quorumK: $v["quorum_k"],
            contractCid: !empty($v["contract"]) ? new Cid(self::hex($av["contract_cid"])) : null,
            checker: $v["checker"] ?? null, certifier: $v["certifier"] ?? null,
        );
    }

    public function testAipiRecords(): void
    {
        $av = self::vectors()["aipi"];
        $this->assertSame($av["module_cid"], self::unhex(Cid::compute($av["module_utf8"])->bytes));
        $this->assertSame($av["contract_cid"], self::unhex(Cid::compute($av["contract_utf8"])->bytes));
        $fns = [];
        foreach ($av["fn"] as $v) {
            $rec = $this->expectStatus($v["status"], fn () => self::fnRecord($v));
            if ($v["status"] !== "ZXV_OK") {
                $fns[] = null;
                continue;
            }
            $this->assertSame($v["record"], self::unhex($rec->bytes));
            $this->assertSame($v["fn_cid"], self::unhex($rec->cid()->bytes));
            $this->assertSame($v["fn_cid_text"], $rec->cid()->text(Cid::BASE58BTC));
            $d = new AipiFunctionRecord(self::hex($v["record"]));
            $this->assertTrue($rec->equals($d));
            $this->assertSame(array_search($v["intent"], Aipi::INTENTS, true), $d->intent);
            $this->assertSame(array_search($v["verify"], Aipi::VERIFY_MODES, true), $d->verify);
            $this->assertSame(self::u64($v["fuel_max"]), $d->fuelMax);
            $this->assertSame([$v["quorum_n"], $v["quorum_k"]], [$d->quorumN, $d->quorumK]);
            $this->assertSame($v["entry"], $d->entry);
            $this->assertSame($v["checker"] ?? null, $d->checker);
            $this->assertSame($v["certifier"] ?? null, $d->certifier);
            $this->assertSame($av["module_cid"], self::unhex($d->moduleCid->bytes));
            $this->assertSame(!empty($v["contract"]) ? $av["contract_cid"] : null, $d->contractCid === null ? null : self::unhex($d->contractCid->bytes));
            $fns[] = $rec;
        }
        $calls = [];
        foreach ($av["call"] as $v) {
            $fn = $v["fn"] === "module_cid" ? new Cid(self::hex($av["module_cid"])) : $fns[0]->cid();
            if ($v["fn"] !== "module_cid") {
                $this->assertSame("fn[0].fn_cid", $v["fn"]);
            }
            if (isset($v["status"]) && $v["status"] !== "ZXV_OK") {
                $this->expectStatus($v["status"], fn () => AipiCallRecord::encode($fn, $v["input_utf8"]));
                $this->expectStatus($v["status"], fn () => Aipi::callCid($fn, $v["input_utf8"]));
                $calls[] = null;
                continue;
            }
            $rec = AipiCallRecord::encode($fn, $v["input_utf8"]);
            $this->assertSame($v["record"], self::unhex($rec->bytes));
            $this->assertSame($v["call_cid"], self::unhex($rec->cid()->bytes));
            $this->assertSame($v["call_cid_text"], Aipi::callCid($fn, $v["input_utf8"])->text(Cid::BASE58BTC));
            $d = new AipiCallRecord(self::hex($v["record"]));
            $this->assertTrue($fn->equals($d->fnCid));
            $this->assertSame($v["input_utf8"], $d->input);
            $calls[] = $rec;
        }
        foreach ($av["claim"] as $v) {
            $this->assertSame("call[1].call_cid", $v["call"]);
            $rec = AipiClaimRecord::encode($calls[1]->cid(), self::hex($v["executor"]), $v["output_utf8"], $v["cert_utf8"]);
            $this->assertSame($v["record"], self::unhex($rec->bytes));
            $d = new AipiClaimRecord(self::hex($v["record"]));
            $this->assertTrue($calls[1]->cid()->equals($d->callCid));
            $this->assertSame([self::hex($v["executor"]), $v["output_utf8"], $v["cert_utf8"]], [$d->executor, $d->output, $d->cert]);
        }
    }

    public function testAipiNode(): void
    {
        $av = self::vectors()["aipi"];
        $this->withContext15(function (Context $ctx, Capability $cap) use ($av) {
            $node = new AipiNode($cap, 1 << 16, 16);
            $mod = $node->put($av["module_utf8"]);
            $this->assertSame($av["module_cid"], self::unhex($mod->bytes));
            $this->assertSame($av["module_utf8"], $node->get($mod));
            $this->assertSame(null, $node->get(Cid::compute("not held")));
            $rec = self::fnRecord($av["fn"][0]);
            $fnCid = $node->publish($rec);
            $this->assertSame($av["fn"][0]["fn_cid"], self::unhex($fnCid->bytes));
            $this->assertTrue($rec->equals($node->loadFunction($fnCid)));
            $this->assertSame(null, $node->loadFunction(Cid::compute("nope", Aipi::MC_FN)));
            $this->assertSame("local", $node->plan($fnCid, "x"));
            $this->assertTrue($node->info()["blocks"] >= 2);
            // Private blocks need a node key.
            $this->expectStatus("ZXV_E_STATE", fn () => $node->put("private bytes", true));
            $keyed = new AipiNode($cap, nodeKey: str_repeat("\x07", 32));
            $secret = $keyed->put("private bytes", true);
            $this->assertSame("private bytes", $keyed->get($secret));
            $keyed->close();
            $node->close();
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $node->info());
        });
    }

    private static function ramp(int $n): string
    {
        $s = "";
        for ($i = 0; $i < $n; $i++) {
            $s .= chr($i & 255);
        }
        return $s;
    }
}
