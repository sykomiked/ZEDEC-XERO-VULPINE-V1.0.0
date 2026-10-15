<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Hashes;
use Zedec\Zxv\MlDsa87;
use Zedec\Zxv\Native;
use Zedec\Zxv\Status;
use Zedec\Zxv\Vino;
use Zedec\Zxv\VinoLane;
use Zedec\Zxv\VinoPaymentRequest;
use Zedec\Zxv\VinoPlay;
use Zedec\Zxv\VinoRail;
use Zedec\Zxv\VinoReceipt;
use Zedec\Zxv\VinoSpendMeter;
use Zedec\Zxv\VinoVoucherState;
use Zedec\Zxv\Zxv;

/**
 * ABI 1.6 (Vino Embed): the status_abi16 and vino sections of vectors.json,
 * plus handle lifetimes, the vouchers buffer path, error mapping,
 * capability refusals and a poisoned context.
 */
final class Abi16VinoTest extends TestCase
{
    private const ERROR_TYPES = [
        "ZXV_E_RAIL" => Exception\WrongRailException::class, "ZXV_E_COVERAGE" => Exception\CoverageException::class,
        "ZXV_E_LANE" => Exception\LaneException::class, "ZXV_E_EXPIRED" => Exception\ExpiredException::class,
        "ZXV_E_CHANCE" => Exception\ChanceRefusedException::class,
    ];

    // The header's numbers (zxv_api.h), written out independently of the SDK
    // enums so a wrong enum value is caught. Vector integers go through these.
    private const RAIL_NAMES = [0 => "Bearer", 1 => "Ghost"];
    private const LANE_NAMES = [1 => "Play", 2 => "Live"];
    private const STATE_NAMES = [1 => "Active", 2 => "Redeemed"];

    /** @return array<string, mixed> */
    private static function vv(): array
    {
        return self::vectors()["vino"];
    }

    private static function rail(int $n): VinoRail|int
    {
        return isset(self::RAIL_NAMES[$n]) ? constant(VinoRail::class . "::" . self::RAIL_NAMES[$n]) : $n;
    }

    /** @param array<string, mixed> $v */
    private static function requestFrom(array $v): VinoPaymentRequest
    {
        $n = (int) $v["lane"];
        $lane = isset(self::LANE_NAMES[$n]) ? constant(VinoLane::class . "::" . self::LANE_NAMES[$n]) : $n;
        return new VinoPaymentRequest($lane, self::hex($v["merchant"]), self::hex($v["nonce"]), self::u64($v["amount"]),
            self::u64($v["created_at"]), self::u64($v["expires_at"]), $v["memo"], $v["flags"]);
    }

    private static function baseBytes(): string
    {
        return self::hex(self::vv()["requests"][0]["bytes"]);
    }

    // --- status codes, bits, features ------------------------------------

    public function testStatusAbi16VectorsAndMapping(): void
    {
        foreach (self::vectors()["status_abi16"] as $v) {
            $code = $v["code"];
            $this->assertSame($code, constant(Status::class . "::" . substr($v["name"], 4)));
            $this->assertSame($v["name"], Status::name($code));
            $this->assertSame($v["class"], Status::classOf($code));
            $err = Native::errorFor($code);
            $this->assertSame(self::ERROR_TYPES[$v["name"]], get_class($err));
            $this->assertTrue($v["class"] === Status::CLASS_USAGE ? $err instanceof Exception\ZxvUsageException : $err instanceof Exception\ZxvRefusedException);
            $this->assertSame($v["name"], $err instanceof Exception\ZxvException ? $err->getStatusName() : "");
        }
    }

    public function testCapabilityBitsAndFeatures2(): void
    {
        $this->assertSame(1 << 15, Capability::VINO);
        $this->assertSame(0xFFFF, Capability::ALL_1_6);
        $this->assertSame(0x7FFF, Capability::ALL_1_5);
        $this->assertSame(0x100, Zxv::FEATURES2["vino"]);
        $this->assertTrue(Zxv::hasFeature("vino"));
        $this->assertTrue(Native::has16());
        $this->assertSame([1, 6], array_slice(Zxv::abiVersion(), 0, 2));
        $this->withContext16(function (Context $ctx, Capability $cap): void {
            $this->assertSame(Capability::ALL_1_6, $cap->info()["bits"]);
            $this->assertSame(["vino"], $ctx->rootCapability(Capability::VINO)->info()["names"]);
            // 1 << 15 is now known; the first unknown bit is 1 << 16.
            $this->expectStatus("ZXV_E_ARG", fn () => $ctx->rootCapability(Capability::ALL_1_6 + 1));
        });
    }

    // --- payment requests --------------------------------------------------

    public function testRequestVectors(): void
    {
        foreach (self::vv()["requests"] as $v) {
            $req = self::requestFrom($v);
            $got = $this->expectStatus($v["status"], fn () => $req->encodeWithId());
            if ($v["status"] !== "ZXV_OK") {
                continue;
            }
            [$bytes, $id] = $got;
            $this->assertSame($v["bytes"], self::unhex($bytes), $v["memo"]);
            $this->assertSame($v["id"], self::unhex($id));
            $this->assertSame($bytes, $req->encode());
            $this->assertSame($id, $req->id());
            $back = VinoPaymentRequest::decode($bytes);
            $this->assertTrue($back->equals($req));
            $this->assertSame($v["memo"], $back->memo);
            $this->assertSame(self::u64($v["amount"]), $back->amount);
            $this->assertSame(self::u64($v["expires_at"]), $back->expiresAt);
            $this->assertSame(($v["flags"] & 1) === 1, $back->isChance());
            $this->assertSame($id, VinoPaymentRequest::idOf($bytes));
        }
    }

    public function testRequestDecodeRefusals(): void
    {
        foreach (self::vv()["decode"] as $v) {
            $bad = self::baseBytes();
            $bad[$v["offset"]] = chr($v["value"]);
            $this->expectStatus($v["status"], fn () => VinoPaymentRequest::decode($bad));
            $this->expectStatus($v["status"], fn () => VinoPaymentRequest::idOf($bad));
        }
    }

    public function testRequestValueType(): void
    {
        $v = self::vv()["requests"][0];
        $req = self::requestFrom($v);
        $this->assertSame(VinoLane::Live, $req->lane);
        $this->assertSame(VinoLane::Play, self::requestFrom(self::vv()["requests"][1])->lane);
        $a = $req->toArray();
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => new VinoPaymentRequest(...array_merge($a, ["amount" => 1.0])));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => new VinoPaymentRequest(...array_merge($a, ["merchant" => "short"])));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => new VinoPaymentRequest(...array_merge($a, ["memo" => "a\0b"])));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => VinoPaymentRequest::decode(str_repeat("x", 159)));
        // Non-ASCII memo text is the library's call: ZXV_E_FORMAT.
        $this->expectStatus("ZXV_E_FORMAT", fn () => (new VinoPaymentRequest(...array_merge($a, ["memo" => "caf\u{e9}"])))->encode());
        // A uint64 past PHP_INT_MAX round-trips as a decimal string.
        $max = self::requestFrom(self::vv()["requests"][14]);
        $this->assertSame("18446744073709551615", $max->expiresAt);
        $this->assertSame("18446744073709551615", VinoPaymentRequest::decode($max->encode())->expiresAt);
    }

    // --- receipts ----------------------------------------------------------

    public function testReceiptVectors(): void
    {
        $rv = self::vv()["receipt"];
        $req0 = self::vv()["requests"][0];
        $reqBytes = self::baseBytes();
        $reqId = self::hex($req0["id"]);
        $approved = (int) $rv["approved_at"];
        $this->withContext16(function (Context $ctx, Capability $cap) use ($rv, $req0, $reqBytes, $reqId, $approved): void {
            $dsa = new MlDsa87($cap);
            $kp = $dsa->generate(self::hex($rv["seed"]));
            $this->assertSame($rv["pk_sha3_256"], self::unhex(Hashes::sha3_256($kp->publicKey)));
            $msg = VinoReceipt::message($reqId, $kp->publicKey, $approved);
            $this->assertSame($rv["msg"], self::unhex($msg));
            // The vector signature is the deterministic ML-DSA-87 signature over msg.
            $det = $dsa->sign($kp->secretKey, $msg, deterministic: true);
            $this->assertSame($rv["sig"], self::unhex($det));
            foreach ($rv["checks"] as $c) {
                $sig = $det;
                if ($c["flip_sig"] >= 0) {
                    $sig[$c["flip_sig"]] = chr(ord($sig[$c["flip_sig"]]) ^ 1);
                }
                $r = new VinoReceipt($reqBytes, $kp->publicKey, self::u64($c["approved_at"]), $sig);
                $this->expectStatus($c["status"], fn () => $r->verify(self::u64($c["now"])));
                $this->assertSame($c["status"] === "ZXV_OK", $r->isValid(self::u64($c["now"])));
            }
            // receipt_sign (hedged) gives a receipt that verifies the same way.
            $signed = VinoReceipt::sign($cap, $reqBytes, $kp->secretKey, $kp->publicKey, $approved);
            $this->assertSame($signed, $signed->verify($approved + 100));
            $this->assertSame($reqId, $signed->requestId());
            $this->assertTrue($signed->paymentRequest()->equals(self::requestFrom($req0)));
            $this->assertFalse($signed->isValid((int) $req0["expires_at"] + 1));
            // A VinoPaymentRequest may stand in for its bytes.
            (new VinoReceipt(self::requestFrom($req0), $kp->publicKey, $approved, $det))->verify($approved);
            // The wallet refuses to sign outside the request's window.
            $this->assertThrows(Exception\ExpiredException::class,
                fn () => VinoReceipt::sign($cap, $reqBytes, $kp->secretKey, $kp->publicKey, (int) $req0["expires_at"] + 1));
            // Signing is the wallet's call: it needs CRYPTO_SIGN.
            $err = $this->assertThrows(Exception\CapabilityDeniedException::class,
                fn () => VinoReceipt::sign($cap->attenuate(Capability::VINO), $reqBytes, $kp->secretKey, $kp->publicKey, $approved));
            $this->assertSame("ZXV_E_CAP", $err->getStatusName());
            // A closed key is refused, never read.
            $closed = $dsa->generate();
            $closed->close();
            $this->assertThrows(Exception\ZxvUsageException::class,
                fn () => VinoReceipt::sign($cap, $reqBytes, $closed->secretKey, $closed->publicKey, $approved));
            // A receipt over corrupted request bytes is refused by decode rules.
            $bad = $reqBytes;
            $bad[0] = "\0";
            $this->expectStatus("ZXV_E_FORMAT", fn () => (new VinoReceipt($bad, $kp->publicKey, $approved, $det))->verify($approved));
            $kp->close();
        });
    }

    // --- play lane -----------------------------------------------------------

    /** @param array<string, mixed> $s */
    private static function playStep(VinoPlay $play, array $s): mixed
    {
        return match ($s["op"]) {
            // 0/1 map to false/true; anything else is not a bool: ZXV_E_ARG.
            "open" => in_array($s["is_issuer"], [0, 1], true)
                ? $play->openAccount($s["owner"], $s["is_issuer"] === 1)
                : throw new Exception\ZxvUsageException(Status::E_ARG, "is_issuer must be 0 or 1"),
            "back" => $play->back($s["issuer"], self::u64($s["amount"])),
            "unback" => $play->unback($s["issuer"], self::u64($s["amount"])),
            "issue" => $play->issue($s["issuer"], $s["holder"], self::u64($s["face"]), self::rail($s["rail"])),
            "transfer" => $play->transfer(self::u64($s["voucher"]), $s["from"], $s["to"], self::rail($s["rail"])),
            "set_rail" => $play->setRail(self::u64($s["voucher"]), $s["holder"], self::rail($s["rail"])),
            "redeem" => $play->redeem(self::u64($s["voucher"]), $s["holder"]),
            default => throw new AssertionFailed("unknown op " . $s["op"]),
        };
    }

    public function testPlayScenario(): void
    {
        $pv = self::vv()["play"];
        $this->withContext16(function (Context $ctx, Capability $cap) use ($pv): void {
            $play = new VinoPlay($cap);
            foreach ($pv["steps"] as $i => $s) {
                $got = $this->expectStatus($s["status"], fn () => self::playStep($play, $s));
                if ($s["status"] !== "ZXV_OK") {
                    continue;
                }
                if (isset($s["account"])) {
                    $this->assertSame($s["account"], $got, "step $i");
                }
                if (isset($s["id"])) {
                    $this->assertSame(self::u64($s["id"]), $got, "step $i");
                }
            }
            $this->assertSame($play, $play->check());
            foreach ($pv["accounts"] as $a) {
                $got = $play->account($a["account"]);
                $this->assertSame([$a["owner"], $a["is_issuer"] === 1, self::u64($a["backing"]), self::u64($a["faces"]), $a["count"]],
                    [$got->owner, $got->issuer, $got->backing, $got->faces, $got->voucherCount], "account " . $a["account"]);
                $this->assertSame(array_map(fn ($x) => self::u64($x), $a["vouchers"]), $play->vouchers($a["account"]));
            }
            foreach ($pv["vouchers"] as $v) {
                $got = $play->voucher(self::u64($v["id"]));
                $this->assertSame([self::u64($v["face"]), $v["issuer"], $v["holder"], self::RAIL_NAMES[$v["rail"]], self::STATE_NAMES[$v["state"]]],
                    [$got->face, $got->issuer, $got->holder, $got->rail instanceof VinoRail ? $got->rail->name : $got->rail,
                        $got->state instanceof VinoVoucherState ? $got->state->name : $got->state]);
            }
            $this->assertTrue($play->voucher(1)->isRedeemed());
            $this->assertTrue($play->voucher(2)->isActive());
            $this->expectStatus("ZXV_E_NOT_FOUND", fn () => $play->account(99));
            $this->expectStatus("ZXV_E_NOT_FOUND", fn () => $play->vouchers(99));
            $this->expectStatus("ZXV_E_NOT_FOUND", fn () => $play->voucher(99));
            $play->close();
        });
    }

    // The two-call vouchers path: count first, then fetch; growth and order.
    public function testVouchersBufferPath(): void
    {
        $this->withContext16(function (Context $ctx, Capability $cap): void {
            $play = new VinoPlay($cap);
            $shop = $play->openIssuer(1);
            $alice = $play->openHolder(2);
            $bob = $play->openHolder(3);
            $this->assertSame([], $play->vouchers($alice));
            $play->back($shop, 10000);
            $ids = [];
            for ($i = 0; $i < 40; $i++) {
                $ids[] = $play->issue($shop, $alice, [1, 2, 3, 5, 8][$i % 5], $i % 2 === 0 ? VinoRail::Bearer : VinoRail::Ghost);
            }
            $this->assertSame($ids, $play->vouchers($alice));
            $this->assertSame($ids, $play->vouchers($shop));
            $play->transfer($ids[3], $alice, $bob, VinoRail::Ghost);
            $play->redeem($ids[0], $alice);
            $this->assertSame(array_values(array_diff($ids, [$ids[0], $ids[3]])), $play->vouchers($alice));
            $this->assertSame([$ids[3]], $play->vouchers($bob));
            $this->assertSame(38, $play->account($alice)->voucherCount);
            // The rail numbers reaching C are the header's (BEARER 0, GHOST 1).
            $f = Native::ffi16();
            [$r, $rP] = Native::out("int32_t");
            foreach ([[$ids[1], 1], [$ids[2], 0]] as [$vid, $want]) {
                $this->assertSame(Status::OK, $f->zxv_vino_voucher_info($cap->ptr(), $play->ptr(), $vid, null, null, null, $rP, null));
                $this->assertSame($want, $r->cdata);
            }
            // Raw: a short buffer gets ZXV_E_BUFFER with the count and is untouched.
            [$n, $nP] = Native::out("size_t");
            $this->assertSame(Status::E_BUFFER, $f->zxv_vino_vouchers($cap->ptr(), $play->ptr(), $bob, null, 0, $nP));
            $this->assertSame(1, $n->cdata);
            $buf = Native::new("uint64_t[2]");
            $buf[0] = 7;
            $buf[1] = 7;
            $this->assertSame(Status::E_BUFFER, $f->zxv_vino_vouchers($cap->ptr(), $play->ptr(), $alice, $buf, 2, $nP));
            $this->assertSame(38, $n->cdata);
            $this->assertSame([7, 7], [$buf[0], $buf[1]]);
            $play->check();
        });
    }

    public function testErrorMappingFromRealCalls(): void
    {
        $this->withContext16(function (Context $ctx, Capability $cap): void {
            $play = new VinoPlay($cap);
            $shop = $play->openIssuer(1);
            $a = $play->openHolder(2);
            $b = $play->openHolder(3);
            $play->back($shop, 18);
            $err = $this->assertThrows(Exception\CoverageException::class, fn () => $play->issue($shop, $a, 13));
            $this->assertSame("ZXV_E_COVERAGE", $err->getStatusName());
            $id = $play->issue($shop, $a, 8, VinoRail::Ghost);
            $err = $this->assertThrows(Exception\WrongRailException::class, fn () => $play->transfer($id, $a, $b, VinoRail::Bearer));
            $this->assertSame("ZXV_E_RAIL", $err->getStatusName());
            $this->assertThrows(Exception\WrongRailException::class, fn () => $play->setRail($id, $a, VinoRail::Ghost));
            $this->assertThrows(Exception\CoverageException::class, fn () => $play->unback($shop, 4));
            $this->expectStatus("ZXV_E_ARG", fn () => $play->issue($shop, $a, 1, 7));
            $this->expectStatus("ZXV_E_DENOM", fn () => $play->issue($shop, $a, 4));
            $this->assertThrows(Exception\FloatRejectedException::class, fn () => $play->back($shop, 1.0));
            // A refusal changed nothing.
            $acct = $play->account($shop);
            $this->assertSame([18, 8, 1], [$acct->backing, $acct->faces, $acct->voucherCount]);
        });
        $a = self::requestFrom(self::vv()["requests"][0])->toArray();
        $err = $this->assertThrows(Exception\LaneException::class, fn () => (new VinoPaymentRequest(...array_merge($a, ["lane" => 3])))->encode());
        $this->assertSame("ZXV_E_LANE", $err->getStatusName());
        $err = $this->assertThrows(Exception\ChanceRefusedException::class,
            fn () => (new VinoPaymentRequest(...array_merge($a, ["flags" => Vino::FLAG_CHANCE])))->encode());
        $this->assertSame("ZXV_E_CHANCE", $err->getStatusName());
        // The same flag on the PLAY lane is allowed.
        $this->assertSame(160, strlen((new VinoPaymentRequest(...array_merge($a, ["lane" => VinoLane::Play, "flags" => Vino::FLAG_CHANCE])))->encode()));
    }

    // --- the daily meter -----------------------------------------------------

    public function testMeterScenario(): void
    {
        $mv = self::vv()["meter"];
        $this->withContext16(function (Context $ctx, Capability $cap) use ($mv): void {
            $meter = new VinoSpendMeter($cap, self::u64($mv["daily_cap"]));
            $this->assertSame(100, $meter->dailyCap);
            foreach ($mv["steps"] as $i => $s) {
                $got = $this->expectStatus($s["status"], fn () => $meter->admit(self::u64($s["day"]), self::u64($s["amount"])));
                if ($s["status"] === "ZXV_OK") {
                    $this->assertSame(self::u64($s["left"]), $got, "step $i");
                } elseif ($s["status"] === "ZXV_E_LIMIT") {
                    $this->assertSame(Exception\DailyCapExceededException::class, get_class($got));
                    $this->assertTrue($got instanceof Exception\ZxvRefusedException);
                    $this->assertSame(self::u64($s["left"]), $got->left, "step $i");
                }
            }
            $this->expectStatus("ZXV_E_RANGE", fn () => new VinoSpendMeter($cap, 0));
            $this->expectStatus("ZXV_E_RANGE", fn () => new VinoSpendMeter($cap, Vino::AMOUNT_MAX + 1));
            $meter->close();
        });
    }

    // --- lifetimes, capabilities, poisoning ----------------------------------

    public function testHandleLifetimes(): void
    {
        $ctx = new Context();
        $cap = $ctx->rootCapability(Capability::ALL_1_6);
        $play = new VinoPlay($cap);
        $meter = new VinoSpendMeter($cap, 10);
        $play->close();
        $this->assertTrue($play->isClosed());
        $play->close(); // idempotent
        $err = $this->assertThrows(Exception\ZxvUsageException::class, fn () => $play->openHolder(1));
        $this->assertSame(Status::E_STATE, $err->getStatus());
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $play->vouchers(0));
        $play2 = new VinoPlay($cap);
        $play2->openHolder(1);
        // Closing the context releases the rest first (R6), then refuses use.
        $ctx->close();
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $play2->openHolder(2));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $meter->admit(1, 1));
    }

    public function testCapabilityRefusals(): void
    {
        $this->withContext16(function (Context $ctx, Capability $cap): void {
            $plain = $ctx->rootCapability(Capability::ALL_1_5); // everything but VINO
            $err = $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => new VinoPlay($plain));
            $this->assertSame("ZXV_E_CAP", $err->getStatusName());
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => new VinoSpendMeter($plain, 5));
            // A sandbox checks the capability it was made with on every call.
            $sub = $cap->attenuate(Capability::VINO);
            $play = new VinoPlay($sub);
            $meter = new VinoSpendMeter($sub, 5);
            $holder = $play->openHolder(1);
            $sub->revoke();
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $play->openHolder(2));
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $play->vouchers($holder));
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $meter->admit(1, 1));
            // Pure calls need no capability at all.
            $this->assertSame(160, strlen(self::requestFrom(self::vv()["requests"][0])->encode()));
        });
    }

    public function testPoisonedContext(): void
    {
        $ctx = new Context();
        $cap = $ctx->rootCapability(Capability::ALL_1_6);
        $play = new VinoPlay($cap);
        $shop = $play->openIssuer(1);
        $meter = new VinoSpendMeter($cap, 10);
        Native::ffi()->zxv_ctx_halt($ctx->raw(), "vino test"); // halted behind the SDK's back
        $err = $this->assertThrows(Exception\ZxvFatalError::class, fn () => $play->back($shop, 5));
        $this->assertSame("ZXV_E_POISONED", Status::name($err->getStatus()));
        $this->assertTrue($ctx->isPoisoned());
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => $play->account($shop));
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => $meter->admit(1, 1));
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => new VinoPlay($cap));
        // Pure calls still work.
        $this->assertSame(32, strlen(self::requestFrom(self::vv()["requests"][0])->id()));
        $ctx->close();
    }
}
