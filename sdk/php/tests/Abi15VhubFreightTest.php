<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Cid;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Freight;
use Zedec\Zxv\FreightHeader;
use Zedec\Zxv\FreightJoin;
use Zedec\Zxv\FreightOps;
use Zedec\Zxv\FreightOpsWriter;
use Zedec\Zxv\Hashes;
use Zedec\Zxv\MlDsa87;
use Zedec\Zxv\VHub;

/** ABI 1.5 conformance: the vhub and freight sections of vectors.json. */
final class Abi15VhubFreightTest extends TestCase
{
    private static function ramp(int $n): string
    {
        $s = "";
        for ($i = 0; $i < $n; $i++) {
            $s .= chr($i & 255);
        }
        return $s;
    }

    public function testVhubPegMessages(): void
    {
        foreach (self::vectors()["vhub"]["peg_msg"] as $v) {
            $msg = VHub::pegMessage($v["slot"], self::u64($v["num"]), self::u64($v["den"]), self::u64($v["epoch"]));
            $this->assertSame($v["msg"], self::unhex($msg));
        }
    }

    public function testVhubScenario(): void
    {
        $sc = self::vectors()["vhub"]["scenario"];
        $this->withContext15(function (Context $ctx, Capability $cap) use ($sc) {
            $dsa = new MlDsa87($cap);
            $keys = [];
            foreach ($sc["keys"] as $k) {
                $kp = $dsa->generate(self::hex($k["seed"]));
                $this->assertSame($k["pk_sha3_256"], self::unhex(Hashes::sha3_256($kp->publicKey)));
                $keys[] = $kp;
            }
            $hub = new VHub($cap, self::hex($sc["hub_seed"]));
            foreach ($sc["currencies"] as $c) {
                $key = $keys[$c["issuer_key"] ?? 0]->publicKey;
                $info = $this->expectStatus($c["status"], fn () => $hub->addCurrency($c["slot"], $c["code"], $c["minor_units"] ?? 2, $key,
                    self::u64($c["pool_funding"] ?? "0"), self::u64($c["vbill_backing"] ?? "0")));
                if ($c["status"] !== "ZXV_OK") {
                    continue;
                }
                $this->assertSame([$c["issuer_account"], $c["pool_account"], $c["vfloat_account"], 0],
                    [$info["issuerAccount"], $info["poolAccount"], $info["vfloatAccount"], $info["pegDen"]]);
            }
            foreach ($sc["pegs"] as $p) {
                $msg = VHub::pegMessage($p["slot"], self::u64($p["num"]), self::u64($p["den"]), self::u64($p["epoch"]));
                $sig = $dsa->sign($keys[$p["signer"]]->secretKey, $msg, deterministic: true);
                $this->assertSame($p["sig_sha3_256"], self::unhex(Hashes::sha3_256($sig)));
                $this->expectStatus($p["status"], fn () => $hub->setPeg($p["slot"], self::u64($p["num"]), self::u64($p["den"]), self::u64($p["epoch"]), $sig));
            }
            $c0 = $hub->currency(0);
            $this->assertSame([350, 1, 1], [$c0["pegNum"], $c0["pegDen"], $c0["pegEpoch"]]);
            foreach ($sc["accounts"] as $a) {
                $this->assertSame($a["account"], $hub->openAccount($a["slot"], $a["owner"]));
                if (isset($a["issued"])) {
                    $hub->issue($a["account"], self::u64($a["issued"]));
                }
            }
            foreach ($sc["quotes"] as $q) {
                $got = $this->expectStatus($q["status"], fn () => $hub->quote($q["from"], $q["to"], self::u64($q["want"])));
                if ($q["status"] === "ZXV_OK") {
                    $this->assertSame(["amount" => self::u64($q["amount"]), "vbills" => self::u64($q["vbills"]), "received" => self::u64($q["received"])], $got);
                }
            }
            foreach ($sc["swaps"] as $s) {
                if (isset($s["add_limit"])) {
                    $hub->addLimit($s["add_limit"]["slot"], self::u64($s["add_limit"]["limit"]));
                }
                $got = $this->expectStatus($s["status"], fn () => $hub->swap($s["from"], $s["to"], $s["sender"], $s["receiver"], self::u64($s["amount"]), self::u64($s["tick"])));
                if ($s["status"] === "ZXV_OK") {
                    $this->assertSame(["vbills" => self::u64($s["vbills"]), "received" => self::u64($s["received"])], $got);
                }
            }
            $this->assertSame("ZXV_OK", $sc["check"]);
            $this->assertSame(self::u64($sc["postings"]), $hub->check());
            $bal = $hub->balances();
            $this->assertSame(count($sc["balances"]), count($bal));
            foreach ($sc["balances"] as $b) {
                $want = ["debit" => self::u64($b["debit"]), "credit" => self::u64($b["credit"]), "equity" => (int) $b["equity"]];
                $this->assertSame($want, $bal[$b["account"]]);
                $this->assertSame($want, $hub->balance($b["account"]));
            }
            foreach ($keys as $k) {
                $k->close();
            }
        });
    }

    public function testFreightHeaders(): void
    {
        $fv = self::vectors()["freight"];
        foreach ($fv["header_serialize"] as $v) {
            $h = $this->expectStatus($v["status"], fn () => FreightHeader::build($v["k"], $v["payload_len"], $v["seq"], $v["seq_count"],
                self::u64($v["freight_id"]), self::hex($v["payload_sha256"]), $v["flags"], self::u64($v["expand_limit"])));
            if ($v["status"] !== "ZXV_OK") {
                continue;
            }
            $this->assertSame($v["header"], self::unhex($h->bytes));
            $back = new FreightHeader(self::hex($v["header"]));
            $this->assertSame([$v["k"], $v["flags"], $v["payload_len"], $v["seq"], $v["seq_count"], self::u64($v["freight_id"]), self::u64($v["expand_limit"])],
                [$back->k, $back->flags, $back->payloadLen, $back->seq, $back->seqCount, $back->freightId, $back->expandLimit]);
            $this->assertSame(self::hex($v["payload_sha256"]), $back->payloadSha256);
        }
        foreach ($fv["header_init_reject"] as $v) {
            $this->expectStatus($v["status"], fn () => FreightHeader::forPayload(self::ramp($v["len"]), 0, $v["k"], ($v["flags"] ?? 0) !== 0, self::u64($v["expand_limit"] ?? "0")));
        }
        $bad = self::hex($fv["header_serialize"][0]["header"]);
        $bad[0] = "\0";
        $this->expectStatus("ZXV_E_FORMAT", fn () => new FreightHeader($bad));
        $this->expectStatus("ZXV_E_FORMAT", fn () => FreightHeader::check($bad));
        FreightHeader::check(self::hex($fv["header_serialize"][0]["header"]));
    }

    public function testFreightCodec(): void
    {
        foreach (self::vectors()["freight"]["codec"] as $c) {
            $payload = self::hex($c["payload"]);
            $h = FreightHeader::forPayload($payload, self::u64($c["freight_id"]), $c["k"]);
            $this->assertSame($c["header"], self::unhex($h->bytes));
            $packets = Freight::encode($h, $payload);
            $this->assertSame($c["packets"], self::unhex($packets));
            $rows = Freight::rows($packets);
            $this->assertSame(Freight::ROWS, count($rows));
            foreach ($c["decode"] as $d) {
                $pick = array_map(fn (int $i) => $rows[$i], $d["rows"]);
                if (isset($d["flip"])) {
                    $f = $d["flip"];
                    $row = $pick[$f["index_in_rows"]];
                    $row[$f["byte"]] = chr(ord($row[$f["byte"]]) ^ ord(self::hex($f["xor"])));
                    $pick[$f["index_in_rows"]] = $row;
                }
                $got = $this->expectStatus($d["status"], fn () => Freight::decode($h, $pick));
                if (!empty($d["matches_payload"])) {
                    $this->assertSame($payload, $got);
                    $this->assertSame($payload, Freight::decode($h, implode("", $pick)));
                }
            }
        }
    }

    public function testFreightSetsAndJoin(): void
    {
        $fv = self::vectors()["freight"];
        foreach ($fv["set_count"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Freight::setCount(self::u64($v["len"]), $v["k"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["count"], $got);
            }
        }
        $s = $fv["split"];
        $data = self::ramp($s["len"]);
        foreach ($s["members"] as $m) {
            $got = $this->expectStatus($m["status"], fn () => Freight::splitMember($data, $m["seq"], self::u64($s["freight_id"]), $s["k"]));
            if ($m["status"] === "ZXV_OK") {
                $this->assertSame($m["header"], self::unhex($got[0]->bytes));
                $this->assertSame($m["packets_sha3_256"], self::unhex(Hashes::sha3_256($got[1])));
            }
        }
        $members = Freight::split($data, self::u64($s["freight_id"]), $s["k"]);
        $this->assertSame(3, count($members));
        $join = new FreightJoin(strlen($data));
        $this->expectStatus("ZXV_E_STATE", fn () => $join->result());
        foreach (array_reverse($members) as [$h, $p]) {
            $join->add($h, $p);
        }
        $join->add($members[0][0], $members[0][1]); // a repeat is a no-op
        $this->assertSame(["received" => 3, "count" => 3, "complete" => true, "total" => strlen($data)], $join->status());
        $this->assertSame($data, $join->result());
        $join->close();
    }

    public function testFreightOps(): void
    {
        $fv = self::vectors()["freight"];
        foreach ($fv["ops_encode"] as $v) {
            $ops = $this->expectStatus($v["status"], fn () => FreightOps::encode($v["utf8"], $v["with_sha"]));
            $this->assertSame($v["ops"], self::unhex($ops));
            $this->assertSame(self::u64($v["bound"]), FreightOps::bound(strlen($v["utf8"]), $v["with_sha"]));
            $this->assertSame($v["utf8"], $this->expectStatus($v["expand_status"], fn () => FreightOps::expand($ops, 256)));
            $this->assertSame([strlen($v["utf8"]), $v["with_sha"]], FreightOps::peek($ops));
        }
        [$w1, $w2] = $fv["ops_writer"];
        $w = new FreightOpsWriter(self::u64($w1["declared_total"]), maxBytes: 4096);
        foreach ($w1["ops_list"] as $op) {
            $this->expectStatus($op["status"], fn () => match ($op["op"]) {
                "literal" => $w->literal($op["utf8"]),
                "copy" => $w->copy(self::u64($op["distance"]), self::u64($op["len"])),
                "seed" => $w->seed($op["gen"], self::hex($op["params"]), self::u64($op["len"])),
            });
        }
        $ops = $this->expectStatus($w1["finish_status"], fn () => $w->finish());
        $w->close();
        $this->assertSame($w1["ops"], self::unhex($ops));
        $out = $this->expectStatus($w1["expand_status"], fn () => FreightOps::expand($ops, 1024));
        $this->assertSame($w1["expanded"], self::unhex($out));
        $this->assertSame([self::u64($w1["peek_total"]), $w1["peek_has_sha"]], FreightOps::peek($ops));
        $this->expectStatus($w1["expand_limit_too_small"], fn () => FreightOps::expand($ops, self::u64($w1["declared_total"]) - 1));
        $this->expectStatus($w1["expand_truncated"], fn () => FreightOps::expand(substr($ops, 0, -1), 1024));

        $cid = new Cid(self::hex($w2["ops_list"][1]["cid"]));
        $w = new FreightOpsWriter(self::u64($w2["declared_total"]));
        $w->literal($w2["ops_list"][0]["utf8"])->cidref($cid, self::u64($w2["ops_list"][1]["len"]))->literal($w2["ops_list"][2]["utf8"]);
        $ops = $w->finish();
        $w->close();
        $this->assertSame($w2["ops"], self::unhex($ops));
        $this->expectStatus($w2["expand_without_resolver"], fn () => FreightOps::expand($ops, 64));
        $seen = [];
        $got = FreightOps::expand($ops, 64, function (string $c, int $len) use (&$seen, $cid, $w2) {
            $seen[] = [$c, $len];
            return $c === $cid->bytes ? $w2["ops_list"][1]["resolves_to_utf8"] : null;
        });
        $this->assertSame($w2["expanded_with_resolver_utf8"], $got);
        $this->assertSame([[$cid->bytes, 3]], $seen);
        // A resolver that throws never unwinds through C: the call fails cleanly.
        $this->assertThrows(Exception\ZxvException::class, fn () => FreightOps::expand($ops, 64, function () {
            throw new \RuntimeException("boom");
        }));
        // A resolver that returns the wrong length is refused too.
        $this->assertThrows(Exception\ZxvException::class, fn () => FreightOps::expand($ops, 64, fn () => "toolong"));
        foreach ($fv["gen_run"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => FreightOps::generate($v["gen"], self::hex($v["params"]), $v["len"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["out"], self::unhex($got));
            }
        }
    }
}
