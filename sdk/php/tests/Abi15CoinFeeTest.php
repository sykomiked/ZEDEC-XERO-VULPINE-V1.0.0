<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Coin;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Fee;
use Zedec\Zxv\Status;
use Zedec\Zxv\Token;
use Zedec\Zxv\TokenWallet;

/** ABI 1.5 conformance: the coin and fee sections of vectors.json. */
final class Abi15CoinFeeTest extends TestCase
{
    /** @return array<string, mixed> */
    private static function cv(): array
    {
        return self::vectors()["coin"];
    }

    /**
     * Sparse [[tier, count], ...] to a dense 64-entry holding.
     * @param list<array{0:int,1:string}> $pairs
     * @return list<int|string>
     */
    private static function dense(array $pairs): array
    {
        $h = array_fill(0, Token::TIERS, 0);
        foreach ($pairs as [$t, $n]) {
            $h[$t] = self::u64($n);
        }
        return $h;
    }

    public function testCoinFacesRungsSplits(): void
    {
        $cv = self::cv();
        $this->assertSame($cv["rungs"], Coin::RUNGS);
        $this->assertSame(self::u64($cv["top_face"]), Coin::TOP_FACE);
        foreach ($cv["face"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Coin::face($v["rung"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame(self::u64($v["face"]), $got);
            }
        }
        foreach ($cv["rung_of"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Coin::rung(self::u64($v["face"])));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["rung"], $got);
            }
            $this->assertSame($v["status"] === "ZXV_OK", Coin::isFace(self::u64($v["face"])));
        }
        foreach ($cv["split"] as $v) {
            $amount = self::u64($v["amount"]);
            $n = $this->expectStatus($v["status"], fn () => Coin::splitCount($amount));
            if ($v["status"] !== "ZXV_OK") {
                continue;
            }
            $this->assertSame(self::u64($v["count"]), $n);
            if (isset($v["rungs"])) {
                $this->assertSame($v["rungs"], Coin::split($amount));
            }
        }
        // 1e12 needs 558 coins: built, and they add up.
        $coins = Coin::split(1000000000000);
        $this->assertSame(558, count($coins));
        $this->assertSame(1000000000000, array_sum(array_map(fn (int $r) => Coin::face($r), $coins)));
        // 7e18 needs 3.8e9 coins: refused before allocating, counted instead.
        $err = $this->assertThrows(Exception\ZxvUsageException::class, fn () => Coin::split(7000000000000000000));
        $this->assertSame(Status::E_RANGE, $err->getStatus());
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Coin::split(1000, 1));
    }

    public function testTokenFacesAndValues(): void
    {
        $cv = self::cv();
        foreach ($cv["token_face"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Token::face($v["tier"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame(self::u64($v["face"]), $got);
                $this->assertSame($v["super"], Token::isSuper($v["tier"]));
            }
        }
        foreach ($cv["token_tier"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Token::tier(self::u64($v["face"])));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame($v["tier"], $got);
            }
        }
        foreach ($cv["token_value"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Token::value(self::dense($v["counts"])));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame([self::u64($v["value"]), self::u64($v["pieces"])], $got);
            }
        }
    }

    public function testTokenCanonicalCompressFuseSplit(): void
    {
        $cv = self::cv();
        foreach ($cv["token_canonical"] as $v) {
            $got = $this->expectStatus($v["status"], fn () => Token::canonical(self::u64($v["value"]), $v["octane"]));
            if ($v["status"] !== "ZXV_OK") {
                continue;
            }
            $this->assertSame(self::dense($v["counts"]), $got);
            $this->assertSame([self::u64($v["value"]), self::u64($v["pieces"])], Token::value($got));
            $this->assertTrue(Token::isCanonical($got, $v["octane"]));
        }
        foreach ($cv["token_is_canonical"] as $v) {
            $this->assertSame($v["canonical"], $this->expectStatus($v["status"], fn () => Token::isCanonical(self::dense($v["counts"]), $v["octane"])));
        }
        foreach ($cv["token_compress"] as $v) {
            $this->assertSame(self::dense($v["out"]), $this->expectStatus($v["status"], fn () => Token::compress(self::dense($v["counts"]), $v["octane"])));
        }
        foreach ($cv["token_fuse_split"] as $v) {
            $input = self::dense($v["counts"]);
            $got = $this->expectStatus($v["status"], fn () => $v["op"] === "fuse" ? Token::fuse($input, $v["tier"], $v["octane"]) : Token::split($input, $v["tier"]));
            if ($v["status"] === "ZXV_OK") {
                $this->assertSame(self::dense($v["out"]), $got);
            } else {
                $this->assertSame(self::dense($v["out"]), $input); // unchanged on failure
            }
        }
        $this->assertSame(9, Token::octane("midgrade"));
    }

    public function testTokenCodes(): void
    {
        $cv = self::cv();
        foreach ($cv["token_code"] as $v) {
            $value = self::u64($v["value"]);
            $bits = $this->expectStatus($v["status"], fn () => Token::codeBits($value));
            if ($v["status"] !== "ZXV_OK") {
                continue;
            }
            $this->assertSame($v["bits"], $bits);
            [$bytes, $n] = Token::encodeCodes([$value]);
            $this->assertSame($v["bytes"], self::unhex($bytes));
            $this->assertSame($v["bits"], $n);
            $code = "";
            for ($i = 0; $i < $n; $i++) {
                $code .= (ord($bytes[intdiv($i, 8)]) >> ($i % 8)) & 1;
            }
            $this->assertSame($v["code"], $code);
            $this->assertSame([[$value], $n], Token::decodeCodes($bytes, 1));
        }
        $s = $cv["token_code_stream"];
        $vals = array_map(fn ($x) => self::u64($x), $s["values"]);
        [$bytes, $n] = Token::encodeCodes($vals);
        $this->assertSame($s["bits"], $n);
        $this->assertSame($s["bytes"], self::unhex($bytes));
        $this->assertSame([$vals, $n], Token::decodeCodes($bytes, bits: $n));
        foreach ($cv["token_code_get_reject"] as $v) {
            $this->expectStatus($v["status"], fn () => Token::decodeCodes(self::hex($v["bytes"]), 1, $v["bit"]));
        }
    }

    public function testTokenWallet(): void
    {
        $this->withContext15(function (Context $ctx, Capability $cap) {
            $a = new TokenWallet($cap, "racing", 7);
            $b = new TokenWallet($cap);
            $a->mint(1000000);
            $this->assertSame(1000000, $a->value());
            $this->assertSame(Token::canonical(1000000, "racing"), $a->counts());
            $this->assertSame(17, $a->pieces());
            $this->assertSame(7, $a->info()["modelId"]);
            $a->transfer($b, 1000);
            $this->assertSame(999000, $a->value());
            $this->assertSame(1000, $b->value());
            $this->assertThrows(Exception\InsufficientFundsException::class, fn () => $b->spend(1001));
            $b->spend(1);
            $this->assertSame(999, $b->value());
            $a->setOctane(0);
            $this->assertSame(999000, $a->pieces());
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $a->mint("18446744073709551615"));
            $this->assertSame(999000, $a->value()); // nothing changed
        });
    }

    public function testFee(): void
    {
        $fv = self::vectors()["fee"];
        $this->assertSame($fv["num"], Fee::NUM);
        $this->assertSame($fv["den"], Fee::DEN);
        foreach ($fv["assure"] as $v) {
            $this->assertSame(self::u64($v["fee"]), Fee::assure(self::u64($v["gross"])));
        }
        foreach ($fv["carry"] as $c) {
            $carry = self::u64($c["rem_in"]);
            foreach ($c["steps"] as $s) {
                [$fee, $carry] = $this->expectStatus($s["status"], fn () => Fee::assureWithCarry(self::u64($s["gross"]), $carry));
                $this->assertSame([self::u64($s["fee"]), self::u64($s["rem"])], [$fee, $carry]);
            }
        }
        foreach ($fv["carry_reject"] as $v) {
            $this->expectStatus($v["status"], fn () => Fee::assureWithCarry(self::u64($v["gross"]), self::u64($v["rem_in"])));
        }
        $this->assertSame(count($fv["buckets"]), count(Fee::BUCKETS));
        foreach ($fv["split"] as $v) {
            $parts = Fee::split(self::u64($v["fee"]));
            $this->assertSame(array_map(fn ($x) => self::u64($x), $v["parts"]), array_values($parts));
            $this->assertSame(Fee::BUCKETS, array_keys($parts));
        }
        $p = $fv["policy_default"];
        $pol = Fee::defaultPolicy();
        $this->assertSame($p["pct"], array_values($pol["percent"]));
        $this->assertSame($p["allow_below"] !== 0, $pol["allowBelow"]);
        $this->assertSame(array_map(fn ($r) => array_map(fn ($x) => self::u64($x), $r), $p["rate"]), array_values($pol["creditRates"]));
        $this->assertSame(array_map(fn ($x) => self::u64($x), $p["multiplier"]), $pol["multiplier"]);
        $this->assertSame(array_map(fn ($x) => self::u64($x), $p["commons_cap"]), $pol["commonsCap"]);
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Fee::assure("18446744073709551616"));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Fee::assure(-1));
    }
}
