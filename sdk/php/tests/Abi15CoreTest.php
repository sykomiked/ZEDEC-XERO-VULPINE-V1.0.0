<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\AipiNode;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Coin;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Fee;
use Zedec\Zxv\Freight;
use Zedec\Zxv\FreightHeader;
use Zedec\Zxv\FreightJoin;
use Zedec\Zxv\FreightOps;
use Zedec\Zxv\FreightOpsWriter;
use Zedec\Zxv\Native;
use Zedec\Zxv\Paradox;
use Zedec\Zxv\ParadoxBook;
use Zedec\Zxv\ParadoxGraph;
use Zedec\Zxv\ParadoxHold;
use Zedec\Zxv\ParadoxPolicy;
use Zedec\Zxv\Status;
use Zedec\Zxv\TokenWallet;
use Zedec\Zxv\Treaty;
use Zedec\Zxv\TreatyPic;
use Zedec\Zxv\VHub;
use Zedec\Zxv\Web4Agent;
use Zedec\Zxv\Web4ConsentLog;
use Zedec\Zxv\Web4Manifest;
use Zedec\Zxv\Zxv;

/**
 * ABI 1.5 plumbing: status codes, capability bits, feature bits, handle
 * lifetimes, the R4 buffer-growth path, error mapping, capability refusals,
 * a poisoned context, 64-bit values and floats.
 */
final class Abi15CoreTest extends TestCase
{
    private const ERROR_TYPES = [
        "ZXV_E_DENOM" => Exception\ZxvUsageException::class, "ZXV_E_TREATY" => Exception\TreatyException::class,
        "ZXV_E_CID" => Exception\ZxvRefusedException::class, "ZXV_E_CONSENT" => Exception\ConsentRequiredException::class,
        "ZXV_E_ERASURE" => Exception\ZxvRefusedException::class, "ZXV_E_PEG" => Exception\ZxvRefusedException::class,
        "ZXV_E_LIMIT" => Exception\ZxvRefusedException::class, "ZXV_E_PARADOX_STEP" => Exception\ZxvRefusedException::class,
        "ZXV_E_PARADOX_OWNER" => Exception\OwnerApprovalRequiredException::class,
    ];

    public function testStatusAbi15VectorsAndMapping(): void
    {
        foreach (self::vectors()["status_abi15"] as $v) {
            $code = $v["code"];
            $this->assertSame($code, constant(Status::class . "::" . substr($v["name"], 4)));
            $this->assertSame($v["name"], Status::name($code));
            $this->assertSame($v["class"], Status::classOf($code));
            $err = Native::errorFor($code);
            $this->assertSame(self::ERROR_TYPES[$v["name"]], get_class($err));
            $this->assertTrue($v["class"] === Status::CLASS_USAGE ? $err instanceof Exception\ZxvUsageException : $err instanceof Exception\ZxvRefusedException);
            $this->assertSame($code, $err->getStatus());
            $this->assertSame($v["name"], $err->getStatusName());
            $raised = $this->assertThrows(Exception\ZxvException::class, fn () => Native::check($code));
            $this->assertSame($code, $raised->getStatus());
        }
    }

    public function testErrorMappingFromRealCalls(): void
    {
        $this->expectStatus("ZXV_E_DENOM", fn () => Coin::rung(4));
        $err = $this->expectStatus("ZXV_E_TREATY", fn () => Treaty::parse("TREATY"));
        $this->assertSame("syntax", $err->fault);
        $this->assertSame(0, $err->offset);
        $this->expectStatus("ZXV_E_CID", fn () => \Zedec\Zxv\Cid::parse("z"));
        $this->expectStatus("ZXV_E_ERASURE", function () {
            $payload = str_repeat("x", 50);
            $h = FreightHeader::forPayload($payload, 1, 10);
            Freight::decode($h, array_slice(Freight::rows(Freight::encode($h, $payload)), 0, 9));
        });
        $this->withContext15(function (Context $ctx, Capability $cap) {
            $book = new ParadoxBook($cap, 8, 4);
            $err = $this->assertThrows(Exception\OwnerApprovalRequiredException::class, fn () => $book->registryAdd(Paradox::signature("m"), "neutral", flags: "money"));
            $this->assertSame("ZXV_E_PARADOX_OWNER", $err->getStatusName());
            $log = new Web4ConsentLog(4);
            $human = new Web4Agent($cap, "human", 1, str_repeat("\x44", 32));
            $err = $this->assertThrows(Exception\ConsentRequiredException::class,
                fn () => $log->check(null, $human->publicKey(), str_repeat("\x01", 32), str_repeat("\x02", 32), "money", 1, 2));
            $this->assertSame("ZXV_E_CONSENT", $err->getStatusName());
        });
    }

    public function testCapabilityBitsAndFeatures2(): void
    {
        $this->assertSame(1 << 12, Capability::AIPI);
        $this->assertSame(1 << 13, Capability::MINT);
        $this->assertSame(1 << 14, Capability::PARADOX);
        $this->assertSame(0x7FFF, Capability::ALL_1_5);
        $this->assertSame(0x3FF, Capability::ALL); // ALL never silently grows
        $f2 = Zxv::features2();
        foreach (["treaty", "aipi", "coin", "fee", "vhub", "freight", "web4", "paradox"] as $g) {
            $this->assertTrue(in_array($g, $f2, true), "feature $g");
            $this->assertTrue(Zxv::hasFeature($g));
        }
        $this->withContext15(function (Context $ctx, Capability $cap) {
            $this->assertSame(Capability::ALL_1_5, $cap->info()["bits"]);
            $this->assertTrue(in_array("paradox", $cap->info()["names"], true));
            $this->assertFalse(in_array("mint", $ctx->rootCapability()->info()["names"], true));
        });
    }

    public function testCapabilityRefusals(): void
    {
        $this->withContext15(function (Context $ctx, Capability $cap) {
            $plain = $ctx->rootCapability(Capability::ALL_1_4);
            $denied = Exception\CapabilityDeniedException::class;
            $this->assertThrows($denied, fn () => new AipiNode($plain));
            $this->assertThrows($denied, fn () => new ParadoxBook($plain));
            $this->assertThrows($denied, fn () => new ParadoxPolicy($plain, [0], [1], [0]));
            $this->assertThrows($denied, fn () => new ParadoxHold($plain, 2));
            $this->assertThrows($denied, fn () => new ParadoxGraph($plain, 2));
            $err = $this->assertThrows($denied, fn () => new TokenWallet($plain));
            $this->assertSame("ZXV_E_CAP", $err->getStatusName());
            $minted = new TokenWallet($cap->attenuate(Capability::MINT));
            $minted->mint(10);
            $this->assertSame(10, $minted->value());
            // A wallet's changes check the capability it was made with.
            $revocable = $cap->attenuate(Capability::MINT);
            $w = new TokenWallet($revocable);
            $revocable->revoke();
            $this->assertThrows($denied, fn () => $w->mint(1));
            $sub = $cap->attenuate(Capability::PARADOX);
            $sub->revoke();
            $this->assertThrows($denied, fn () => new ParadoxBook($sub));
        });
    }

    public function testPureHandleLifetimes(): void
    {
        $t = Treaty::parse("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nEND\n");
        $this->assertSame("N", $t->name());
        $t->close();
        $this->assertTrue($t->isClosed());
        $t->close(); // idempotent
        $err = $this->assertThrows(Exception\ZxvUsageException::class, fn () => $t->name());
        $this->assertSame(Status::E_STATE, $err->getStatus());
        $j = new FreightJoin(2);
        $j->close();
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $j->status());
        $w = new FreightOpsWriter(1);
        $w->close();
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $w->literal("a"));
        $m = new Web4Manifest(1, 1, 2);
        $m->close();
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $m->info());
    }

    public function testContextHandleLifetimes(): void
    {
        $ctx = new Context();
        $cap = $ctx->rootCapability(Capability::ALL_1_5);
        $w = new TokenWallet($cap);
        $handles = [new ParadoxBook($cap), new VHub($cap), new AipiNode($cap), new Web4Agent($cap, "a", 1, str_repeat("\x11", 32))];
        $w->close();
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => $w->mint(1));
        // Closing the context releases the rest first (R6), then refuses use.
        $ctx->close();
        foreach ($handles as $h) {
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $h->ptr(), get_class($h));
        }
    }

    /** The R4 bytes pattern: a too-small first buffer is regrown to fit. */
    public function testBufferGrowth(): void
    {
        $t = Treaty::parse(self::vectors()["treaty"]["sutra"][1]["sutra"]);
        $full = $t->toRecord();
        $grown = Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_emit_record($t->ptr(), $b, $c, $l), 1);
        $this->assertSame($full, $grown);
        $this->assertSame(Treaty::REC_LEN, strlen($grown));
        // A buffer one byte short gets ZXV_E_BUFFER and the needed length.
        [$len, $lenP] = Native::out("size_t");
        $buf = Native::new("uint8_t[" . (Treaty::REC_LEN - 1) . "]");
        $this->assertSame(Status::E_BUFFER, Native::ffi15()->zxv_treaty_emit_record($t->ptr(), $buf, Treaty::REC_LEN - 1, $lenP));
        $this->assertSame(Treaty::REC_LEN, $len->cdata);
        // Text R4: the canonical Sutra re-emitted through a growing buffer.
        $this->assertSame(self::vectors()["treaty"]["sutra"][1]["reemit"], $t->toSutra());
        $big = str_repeat("x", 5000);
        $bm = Native::bytes($big);
        $this->assertSame(strlen(FreightOps::encode($big, true)),
            strlen(Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_freight_ops_encode($bm, 5000, 1, $b, $c, $l), 1)));
    }

    public function testPoisonedContext(): void
    {
        $ctx = new Context();
        $cap = $ctx->rootCapability(Capability::ALL_1_5);
        $w = new TokenWallet($cap);
        $book = new ParadoxBook($cap);
        $w->mint(5);
        $ctx->halt("abi15 test");
        $this->assertTrue($ctx->isPoisoned());
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => $w->mint(1));
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => $book->registryAdd(Paradox::signature("x"), "destructive"));
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => new VHub($cap));
        // Pure functions need no context and still work.
        $this->assertSame(1, Fee::assure(1125));
        $ctx->close();
    }

    public function testFloatsRefused(): void
    {
        $f = Exception\FloatRejectedException::class;
        $this->assertThrows($f, fn () => Fee::assure(1.5));
        $this->assertThrows($f, fn () => Fee::assure(2.0));
        $this->assertThrows($f, fn () => Coin::split(2.0));
        $this->assertThrows($f, fn () => TreatyPic::add(1.0, 1));
    }

    /** uint64 values above PHP_INT_MAX travel as decimal strings, both ways. */
    public function testU64Conversion(): void
    {
        $max = "18446744073709551615";
        $this->assertSame(-1, Native::u64($max, "v"));
        $this->assertSame(PHP_INT_MIN, Native::u64("9223372036854775808", "v"));
        $this->assertSame(PHP_INT_MAX, Native::u64("9223372036854775807", "v"));
        $this->assertSame(1234, Native::u64("1234", "v"));
        $this->assertSame($max, Native::u64Out(-1));
        $this->assertSame("9223372036854775808", Native::u64Out(PHP_INT_MIN));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Native::u64("18446744073709551616", "v"));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Native::u64("-1", "v"));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Native::u64("01", "v"));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Native::u64(-1, "v"));
        // Through a real call: the largest gross has an exact fee.
        $this->assertSame(16397310807120420, Fee::assure($max));
        $this->assertSame(PHP_INT_SIZE, 8);
    }
}
