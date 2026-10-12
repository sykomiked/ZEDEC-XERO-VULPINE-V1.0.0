<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Hashes;
use Zedec\Zxv\Web4;
use Zedec\Zxv\Web4Agent;
use Zedec\Zxv\Web4ConsentLog;
use Zedec\Zxv\Web4Directory;
use Zedec\Zxv\Web4Envelope;
use Zedec\Zxv\Web4Manifest;

/**
 * ABI 1.5 conformance: the web4 section of vectors.json. Signatures are
 * hedged, so signed encodings are compared by length and by the bytes
 * before the trailing signature.
 */
final class Abi15Web4Test extends TestCase
{
    /** @return array<string, mixed> */
    private static function wv(): array
    {
        return self::vectors()["web4"];
    }

    private static function body(string $bytes): string
    {
        return substr($bytes, 0, strlen($bytes) - Web4::SIG_BYTES);
    }

    /** @return array<string, Web4Agent> */
    private function agents(Capability $cap): array
    {
        $by = [];
        foreach (self::wv()["agents"] as $a) {
            $node = isset($a["node"]) ? $by[$a["node"]] : null;
            $by[$a["name"]] = $this->expectStatus($a["status"], fn () => new Web4Agent($cap, $a["name"], self::u64($a["now_ms"]), str_repeat(self::hex($a["seed_byte"]), 32), $node));
        }
        return $by;
    }

    public function testAgentsAndCards(): void
    {
        $wv = self::wv();
        $this->assertSame($wv["sig_bytes"], Web4::SIG_BYTES);
        $this->withContext15(function (Context $ctx, Capability $cap) use ($wv) {
            $ag = $this->agents($cap);
            foreach ($wv["agents"] as $a) {
                $h = $ag[$a["name"]];
                $this->assertSame($a["agent_id"], self::unhex($h->agentId()));
                $this->assertSame($a["peer_id"], self::unhex($h->peerId()));
                $this->assertSame($a["pk_sha3_256"], self::unhex(Hashes::sha3_256($h->publicKey())));
                $card = $h->card();
                $this->assertSame($a["card_len"], strlen($card));
                $this->assertSame($a["card_signed_body_sha3_256"], self::unhex(Hashes::sha3_256(self::body($card))));
                $info = $this->expectStatus($a["card_verify"], fn () => Web4::verifyCard($card));
                $this->assertSame([$h->agentId(), $h->peerId(), $a["name"], self::u64($a["now_ms"]), !isset($a["node"])],
                    [$info["agentId"], $info["peerId"], $info["name"], $info["boundMs"], $info["selfBound"]]);
                $flipped = $card;
                $flipped[-1] = chr(ord($flipped[-1]) ^ 1);
                $this->expectStatus($a["card_verify_flipped_last_byte"], fn () => Web4::verifyCard($flipped));
            }
            $dir = new Web4Directory(8);
            $cards = array_map(fn (Web4Agent $x) => $x->card(), $ag);
            foreach ($cards as $c) {
                $dir->add($c);
            }
            $this->assertSame(4, $dir->count());
            $this->assertSame($cards["bob"], $dir->find($ag["bob"]->agentId()));
            $this->assertSame(null, $dir->find(str_repeat("\0", 32)));
            $dir->close();
            foreach ($ag as $x) {
                $x->close();
            }
        });
    }

    public function testActionDigests(): void
    {
        $ad = self::wv()["action_digest"];
        foreach ($ad["cases"] as $c) {
            $payload = isset($c["payload_ramp_len"]) ? implode("", array_map(fn (int $i) => chr($i & 255), range(0, $c["payload_ramp_len"] - 1))) : $c["payload_utf8"];
            $got = $this->expectStatus($c["status"], fn () => Web4::actionDigest($c["tool"], self::hex($ad["from"]), self::hex($ad["to"]), self::u64($c["req_id"]), $payload));
            if ($c["status"] === "ZXV_OK") {
                $this->assertSame($c["digest"], self::unhex($got));
            }
        }
    }

    public function testManifest(): void
    {
        $mv = self::wv()["manifest"];
        $this->withContext15(function (Context $ctx, Capability $cap) use ($mv) {
            $ag = $this->agents($cap);
            $m = new Web4Manifest($mv["version"], self::u64($mv["issued_ms"]), self::u64($mv["expires_ms"]));
            foreach ($mv["tools"] as $t) {
                $this->expectStatus($t["status"], fn () => $m->addTool($t["name"], self::u64($t["price_vfv"]), $t["rate_per_min"], $t["burst"], $t["flags"], $t["max_input"]));
            }
            $this->expectStatus($mv["add_duplicate"], fn () => $m->addTool("pay", 1, 1, 1));
            $this->expectStatus($mv["encode_unsigned"], fn () => $m->encode());
            $this->assertFalse($m->isSigned());
            $m->sign($ag[$mv["signer"]]);
            $this->assertTrue($m->isSigned());
            $enc = $m->encode();
            $this->assertSame($mv["encoding_len"], strlen($enc));
            $this->assertSame($mv["encoding_signed_body"], self::unhex(self::body($enc)));
            $this->assertSame($mv["json"], $m->toJson());
            $d = $this->expectStatus($mv["decode"], fn () => Web4Manifest::decode($enc));
            $this->assertSame(3, $d->info()["toolCount"]);
            $this->assertSame(2, $d->find("stream.feed"));
            $this->assertSame(["stream"], $d->tool(2)["flags"]);
            $this->assertSame(150, $d->tool(0)["priceVfv"]);
            $this->assertSame(3, count($d->tools()));
            $this->assertSame(null, $d->find("nope"));
            foreach ($mv["verify"] as $v) {
                $this->expectStatus($v["status"], fn () => $d->verify($ag[$v["key"]]->publicKey(), self::u64($v["now_ms"])));
            }
            $this->assertTrue($d->isValid($ag["alice"]->publicKey(), self::u64($mv["issued_ms"])));
            $this->assertFalse($d->isValid($ag["bob"]->publicKey(), self::u64($mv["issued_ms"])));
            $m->close();
            $d->close();
        });
    }

    public function testConsent(): void
    {
        $cv = self::wv()["consent"];
        $ad = self::wv()["action_digest"];
        $this->withContext15(function (Context $ctx, Capability $cap) use ($cv, $ad) {
            $ag = $this->agents($cap);
            $human = $ag[$cv["human"]];
            $agent = $ag[$cv["agent"]];
            $this->assertSame("action_digest.cases[1].digest", $cv["action"]);
            $action = self::hex($ad["cases"][1]["digest"]);
            $issue = fn (string $nonce) => $human->issueConsent($agent->agentId(), $action, $cv["scope"], self::u64($cv["max_vfv"]),
                self::u64($cv["issued_ms"]), self::u64($cv["expires_ms"]), $nonce);
            $nonce = self::hex($cv["nonce"]);
            $tok = $this->expectStatus($cv["status"], fn () => $issue($nonce));
            $this->assertSame($cv["token_len"], strlen($tok));
            $this->assertSame($cv["token_signed_body"], self::unhex(self::body($tok)));
            $info = Web4::consentInfo($tok);
            $this->assertSame([$human->agentId(), $agent->agentId(), $action, self::u64($cv["max_vfv"]), $nonce],
                [$info["humanId"], $info["agentId"], $info["action"], $info["maxVfv"], $info["nonce"]]);
            $n2 = $nonce;
            $n2[0] = chr(ord($n2[0]) ^ 0xFF);
            $tok2 = $issue($n2);
            $log = new Web4ConsentLog(16);
            foreach ($cv["check"] as $c) {
                $second = str_starts_with($c["what"], "second token");
                if ($second) {
                    $log->revoke($tok2, self::u64($c["now_ms"]));
                }
                $this->expectStatus($c["status"], fn () => $log->check($second ? $tok2 : $tok, $human->publicKey(), $ag[$c["agent"]]->agentId(),
                    $action, $c["need"], self::u64($c["amount"]), self::u64($c["now_ms"])));
            }
            $this->assertTrue(is_int($log->prune(self::u64($cv["expires_ms"]))));
            $log->close();
        });
    }

    public function testEnvelopes(): void
    {
        $ev = self::wv()["envelopes"];
        $this->withContext15(function (Context $ctx, Capability $cap) use ($ev) {
            $ag = $this->agents($cap);
            $s = $ev[0];
            $sender = $ag[$s["sender"]];
            $req = $this->expectStatus($s["status"], fn () => $sender->seal($ag[$s["to"]]->agentId(), $s["kind"], $s["tool"], self::u64($s["req_id"]),
                self::u64($s["now_ms"]), $s["payload_utf8"], $s["chunk"], ($s["flags"] & Web4::MF_FINAL) !== 0));
            $this->assertSame($s["env_len"], strlen($req));
            $this->assertSame($s["env_signed_body"], self::unhex(self::body($req)));
            $env = new Web4Envelope($req);
            $this->expectStatus($s["verify_sender"], fn () => $env->verifyOrThrow($sender->publicKey()));
            $this->expectStatus($s["verify_other"], fn () => $env->verifyOrThrow($ag[$s["to"]]->publicKey()));
            $this->assertSame(["request", "pay", 7, "amount=100", null], [$env->kind, $env->tool, $env->reqId, $env->payload, $env->consent]);
            $r = $ev[1];
            $replier = $ag[$r["sender"]];
            $rsp = $this->expectStatus($r["status"], fn () => $replier->reply($req, $r["kind"], self::u64($r["now_ms"]), $r["payload_utf8"], $r["chunk"],
                ($r["flags"] & Web4::MF_FINAL) !== 0));
            $this->assertSame($r["env_len"], strlen($rsp));
            $this->assertSame($r["env_signed_body"], self::unhex(self::body($rsp)));
            $renv = new Web4Envelope($rsp);
            $this->expectStatus($r["verify_sender"], fn () => $renv->verifyOrThrow($replier->publicKey()));
            $this->assertTrue($renv->isFinal());
            $this->assertSame($ag[$r["to"]]->agentId(), $renv->to);
            // Only the addressee may reply.
            $this->expectStatus("ZXV_E_ARG", fn () => $ag["human"]->reply($req, "response", 1));
            // Key material never shows in a dump.
            $this->assertSame(["closed" => false], $sender->__debugInfo());
        });
    }
}
