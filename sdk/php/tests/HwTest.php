<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception\CapabilityDeniedException;
use Zedec\Zxv\Exception\VerificationFailedException;
use Zedec\Zxv\Exception\ZxvException;
use Zedec\Zxv\Exception\ZxvFatalError;
use Zedec\Zxv\Exception\ZxvRefusedException;
use Zedec\Zxv\Exception\ZxvSystemException;
use Zedec\Zxv\Exception\ZxvUsageException;
use Zedec\Zxv\Hexagram;
use Zedec\Zxv\Hw;
use Zedec\Zxv\HwMatrix;
use Zedec\Zxv\HwProfile;
use Zedec\Zxv\HwRegion;
use Zedec\Zxv\Status;
use Zedec\Zxv\Truth;
use Zedec\Zxv\Zxv;

/** ABI 1.4: hardware emulation and hardware updates (mirrors test_zxv_abi14.c). */
final class HwTest extends TestCase
{
    private const SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    /** RFC 4231 test case 2 (key "Jefe"). */
    private const HMAC_JEFE = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";

    private static function macInput(): string
    {
        return pack("v", 4) . "Jefe" . "what do ya want for nothing?";
    }

    /** @param callable(Context, Capability, Capability): void $fn */
    private function withHw(callable $fn): void
    {
        $ctx = new Context();
        try {
            $cap = $ctx->rootCapability(Capability::ALL_1_4);
            $fn($ctx, $cap, $cap->attenuate(Capability::ALL));
        } finally {
            $ctx->close();
        }
    }

    private function status(string $class, callable $fn): int
    {
        $e = $this->assertThrows($class, $fn);
        return $e->getStatus();
    }

    public function testVersionStatusesAndCaps(): void
    {
        [$major, $minor] = Zxv::abiVersion();
        $this->assertSame(1, $major);
        $this->assertTrue($minor >= 4);
        $this->assertTrue(Zxv::hasFeature("hw"));
        $codes = ["E_NOPROVIDER" => -25, "E_DISPUTE" => -26, "E_QUARANTINED" => -27, "E_ROLLBACK" => -28, "E_BLOCKED" => -29, "E_TARGET" => -30, "E_POLICY" => -31];
        foreach ($codes as $name => $v) {
            $this->assertSame($v, constant(Status::class . "::" . $name));
            $this->assertSame("ZXV_" . $name, Status::name($v));
        }
        $this->assertSame(Status::CLASS_REFUSED, Status::classOf(Status::E_DISPUTE));
        $this->assertSame(Status::CLASS_USAGE, Status::classOf(Status::E_TARGET));
        $this->assertSame(Status::CLASS_REFUSED, Status::classOf(Status::E_POLICY));
        $this->assertSame(0x3FF, Capability::ALL);
        $this->assertSame(0xFFF, Capability::ALL_1_4);
        $this->withHw(function (Context $ctx, Capability $cap, Capability $nohw): void {
            $this->assertTrue(in_array("hw_reconf", $cap->info()["names"], true));
            $this->assertFalse(in_array("hw_matrix", $nohw->info()["names"], true));
            $this->assertSame(Capability::ALL, $ctx->rootCapability()->info()["bits"]);
        });
    }

    public function testProfiles(): void
    {
        $host = HwProfile::probeHost();
        $r1 = HwProfile::reference();
        $this->assertSame(16, count($r1));
        $this->assertTrue(count($host) >= 2);
        $this->assertSame(PHP_INT_SIZE * 8, $host->cap(2, Hw::CAP_WORD_BITS));
        $this->assertTrue($host->cap(2, Hw::CAP_COUNT) >= 1);

        [$native, $emulated] = $host->match($r1);
        $this->assertTrue($native >= 1);
        $this->assertSame(14, $native + $emulated);
        $p = $host->pair($r1, 1);
        $this->assertSame([2, Hw::GROUP], [$p["needId"], $p["kind"]]);
        $p = $host->pair($r1, 2);
        $this->assertSame([3, Hw::NATIVE, 2], [$p["needId"], $p["kind"], $p["haveId"]]);
        $this->assertSame(Hw::EMULATE, $host->pair($r1, 7)["kind"]);
        $this->assertSame(Status::E_NOT_FOUND, $this->status(ZxvException::class, fn () => $host->pair($r1, 99)));

        $this->assertSame(Hw::FN_TENSOR, $r1->cap(6, Hw::CAP_CONTRACT_ID));
        $pres = HwProfile::present($host, $r1);
        $this->assertSame(Hw::KIND_HARD, $pres->cap(3, Hw::CAP_KIND));
        $this->assertSame(Hw::KIND_SOFT, $pres->cap(8, Hw::CAP_KIND));
        $this->assertSame(null, $pres->cap(1, Hw::CAP_KIND));
        $pres->close();
        $this->assertTrue($pres->isClosed());

        $bytes = $r1->encode();
        $copy = HwProfile::decode($bytes);
        $this->assertSame($r1->digest(), $copy->digest());
        $this->assertSame($r1->nodes(), $copy->nodes());
        $this->assertSame(Status::E_FORMAT, $this->status(ZxvException::class, fn () => HwProfile::decode(substr($bytes, 0, -1))));

        $mine = HwProfile::create()
            ->addNode(1, Hw::ROOT, Hw::CLASS_MACHINE)
            ->addNode(2, 1, Hw::CLASS_TERNARY_UNIT)
            ->setCap(2, Hw::CAP_RADIX, 3);
        $this->assertSame(Status::E_ARG, $this->status(ZxvUsageException::class, fn () => $mine->addNode(2, 1, Hw::CLASS_CACHE)));
        $this->assertSame(Status::E_NOT_FOUND, $this->status(ZxvException::class, fn () => $mine->setCap(9, Hw::CAP_RADIX, 3)));
        $mine->addNode(3, 1, Hw::CLASS_PRIVATE | 1);
        $p = $mine->pair($r1, 7);
        $this->assertSame([Hw::NATIVE, 2], [$p["kind"], $p["haveId"]]);
        $d1 = $mine->digest();
        $mine->setExt(3, "\xA5\x00\x5A");
        $this->assertFalse($d1 === $mine->digest(), "the extension is part of the identity");
        $this->assertSame("\xA5\x00\x5A", $mine->ext(3));
        $this->assertSame("", $mine->ext(2));
        $this->assertSame(Status::E_RANGE, $this->status(ZxvException::class, fn () => $mine->setExt(3, str_repeat("\0", Hw::EXT_MAX + 1))));
        $this->assertSame(["id" => 3, "parent" => 1, "cls" => Hw::CLASS_PRIVATE | 1, "ncaps" => 0], $mine->node(2));
    }

    public function testStandardEnginesAndDiscovery(): void
    {
        $this->withHw(function (Context $ctx, Capability $cap, Capability $nohw): void {
            $this->assertThrows(CapabilityDeniedException::class, fn () => new HwMatrix($nohw));
            $mx = new HwMatrix($cap);
            $this->assertSame(self::SHA256_ABC, bin2hex($mx->call(Hw::FN_SHA256, "abc")));
            $this->assertSame(self::HMAC_JEFE, bin2hex($mx->call(Hw::FN_MAC, self::macInput())));
            $this->assertSame(pack("P", 6), $mx->call(Hw::FN_GCD, pack("PP", 12, 18)));
            $engines = $mx->providers();
            $this->assertTrue(count($engines) >= 5);
            foreach ($engines as $e) {
                $this->assertSame(Hw::KIND_SOFT, $e->kind);
                $this->assertSame(Truth::true(), $e->proof);
                $this->assertSame(1, $e->contract);
                $this->assertFalse($e->hasFlag(Hw::PF_FAULT));
            }
            $fns = array_map(fn ($e) => $e->fnId, $engines);
            foreach ([Hw::FN_SHA256, Hw::FN_KECCAK, Hw::FN_MAC, Hw::FN_GCD, Hw::FN_LOGIC64] as $f) {
                $this->assertTrue(in_array($f, $fns, true));
            }
            $this->assertSame(Status::E_NOT_FOUND, $this->status(ZxvException::class, fn () => $mx->providerInfo(count($engines))));
            $info = $mx->functionInfo(Hw::FN_MAC);
            $this->assertSame(1, $info["contract"]);
            $this->assertSame(Hw::POLICY_KEY_BEARING, $info["policy"] & Hw::POLICY_KEY_BEARING);
            $this->assertTrue($mx->providerInfo((int) $mx->bound(Hw::FN_SHA256))->isServing());
        });
    }

    public function testMacRefusesRemoteAndPlacementPolicy(): void
    {
        $this->withHw(function (Context $ctx, Capability $cap): void {
            $mx = new HwMatrix($cap);
            $local = $mx->bound(Hw::FN_MAC);
            $this->assertTrue($local !== null);
            $reached = 0;
            $mx->addResource(7);
            $remote = $mx->addProvider(Hw::FN_MAC, 7, Hw::KIND_REMOTE, 1, function (int $op, string $in) use (&$reached): int {
                $reached++;
                return Status::E_STATE;
            });
            $mx->attest($remote);
            $before = $reached;
            $this->assertSame(self::HMAC_JEFE, bin2hex($mx->call(Hw::FN_MAC, self::macInput())));
            $this->assertSame($before, $reached, "key-bearing work never reaches a REMOTE implementation");
            $this->assertSame($local, $mx->bound(Hw::FN_MAC));
            $this->assertTrue($mx->providerInfo($remote)->hasFlag(Hw::PF_POLICY));
            $this->assertSame(Status::E_POLICY, $this->status(ZxvRefusedException::class, fn () => $mx->setPolicy(Hw::FN_MAC)));
            $this->assertSame(Status::E_POLICY, $this->status(ZxvRefusedException::class, fn () => $mx->memo(Hw::FN_MAC, 16)));
            $this->assertSame(Status::E_ARG, $this->status(ZxvUsageException::class, fn () => $mx->setPolicy(Hw::FN_MAC, 0x80)));

            $g = pack("PP", 12, 18);
            $mx->setPolicy(Hw::FN_GCD, 0, 0, 500);
            $this->assertSame(Status::E_NOPROVIDER, $this->status(ZxvRefusedException::class, fn () => $mx->call(Hw::FN_GCD, $g)));
            $this->assertSame(null, $mx->bound(Hw::FN_GCD));
            $gcd = array_values(array_filter($mx->providers(), fn ($e) => $e->fnId === Hw::FN_GCD))[0]->index;
            $mx->setTraits($gcd, 2000, 300, 4);
            $this->assertSame(Status::E_RANGE, $this->status(ZxvException::class, fn () => $mx->setTraits($gcd, 2000, 300, 16)));
            $this->assertSame(pack("P", 6), $mx->call(Hw::FN_GCD, $g));
            $i = $mx->providerInfo($gcd);
            $this->assertSame([4, Hw::POWER_1W], [$i->throughputClass, $i->powerClass]);
            $mx->setPolicy(Hw::FN_GCD, Hw::POLICY_LOCAL_ONLY, 1000);
            $this->assertSame(Status::E_NOPROVIDER, $this->status(ZxvRefusedException::class, fn () => $mx->call(Hw::FN_GCD, $g)));
            $mx->setPolicy(Hw::FN_GCD);
            $this->assertSame(pack("P", 6), $mx->call(Hw::FN_GCD, $g));
        });
    }

    public function testPhpImplementationsProofFailoverShadowMemoContracts(): void
    {
        $this->withHw(function (Context $ctx, Capability $cap): void {
            $mx = (new HwMatrix($cap))->addFunction(0x100, "add8")->addFunction(0x101, "add16");
            $this->assertSame(Status::E_DUPLICATE, $this->status(ZxvException::class, fn () => $mx->addFunction(0x100, "again")));
            $mx->addResource(1)->addResource(2);
            $in = pack("C*", 0, 1, 9, 200);
            $ex8 = pack("C*", 8, 9, 17, 208);
            $ex16 = pack("C*", 16, 17, 25, 216);
            $mx->addVector(0x100, $in, $ex8)->addVector(0x101, $in, $ex16);
            $fx = 0;
            $sw = 0;
            $add8 = static function (int $bad, string $d): string {
                if ($bad === 2) {
                    throw new ZxvSystemException(Status::E_IO, "unit offline");
                }
                return implode("", array_map(fn (int $b) => chr(($b + 8 + ($bad === 1 && $b === 9 ? 1 : 0)) & 0xFF), array_values(unpack("C*", $d))));
            };
            $pfx = $mx->addProvider(0x100, 2, Hw::KIND_HARD, 1, function (int $op, string $d) use (&$fx, $add8) {
                return $add8($fx, $d);
            });
            $psw = $mx->addProvider(0x100, 1, Hw::KIND_SOFT, 10, function (int $op, string $d) use (&$sw, $add8) {
                return $add8($sw, $d);
            });
            $p16 = $mx->addProvider(0x101, 1, Hw::KIND_SOFT, 20, fn (int $op, string $d) => $mx->call(0x100, $mx->call(0x100, $d, $op), $op));
            $this->assertSame(Status::E_ARG, $this->status(ZxvUsageException::class, fn () => $mx->addProvider(0x100, 1, 9, 1, fn () => "")));
            $this->assertSame(Status::E_NOPROVIDER, $this->status(ZxvRefusedException::class, fn () => $mx->call(0x100, $in)));
            $this->assertSame(Truth::true(), $mx->attest($pfx));
            $this->assertSame(Truth::true(), $mx->attest($psw));
            $this->assertSame(Truth::false(), $mx->attest($p16), "an undeclared dependency must not prove");
            $mx->addDependency($p16, 0x100);
            $this->assertSame(Truth::true(), $mx->attest($p16));
            $this->assertSame($ex16, $mx->call(0x101, $in));

            $this->assertSame($pfx, $mx->bound(0x100));
            $mx->setResource(2, Hw::RES_FAULTED);
            $this->assertSame($ex8, $mx->call(0x100, $in));
            $this->assertSame($psw, $mx->bound(0x100));
            $mx->setResource(2, Hw::RES_OK);

            $fx = 2;
            $this->assertSame(Status::E_IO, $this->status(ZxvSystemException::class, fn () => $mx->call(0x100, $in)));
            $fx = 0;

            $this->assertSame([$ex8, true], $mx->shadow(0x100, $in));
            $sw = 1;
            $this->assertSame(Status::E_DISPUTE, $this->status(ZxvRefusedException::class, fn () => $mx->shadow(0x100, $in)));
            $this->assertSame(Status::E_NOPROVIDER, $this->status(ZxvRefusedException::class, fn () => $mx->call(0x100, $in)));
            $this->assertSame(Truth::true(), $mx->attest($pfx));
            $this->assertSame(Truth::false(), $mx->attest($psw));

            $this->assertSame(Status::E_ARG, $this->status(ZxvUsageException::class, fn () => $mx->memo(0x100, 6)));
            $mx->memo(0x100, 16);
            $calls0 = $mx->providerInfo($pfx)->calls;
            $this->assertSame($ex8, $mx->call(0x100, $in));
            $this->assertSame($ex8, $mx->call(0x100, $in));
            $this->assertSame($calls0 + 1, $mx->providerInfo($pfx)->calls);
            $this->assertSame([1, 1], $mx->memo(0x100, 16));

            $this->assertSame(["contract" => 0, "policy" => 0, "vectors" => 1], $mx->functionInfo(0x100));
            $mx->startContract(0x100, 2);
            $this->assertSame(Status::E_ROLLBACK, $this->status(ZxvRefusedException::class, fn () => $mx->startContract(0x100, 2)));
            $mx->addVector(0x100, $in, $ex8);
            $mx->attest($pfx);
            $this->assertSame(2, $mx->providerInfo($pfx)->contract);
            $this->assertTrue($mx->providerInfo($psw)->hasFlag(Hw::PF_UNPROVEN));
            $this->assertThrows(\TypeError::class, fn () => $mx->call(1.5, $in));
        });
    }

    /** Three image builds in total (SLH-DSA signing takes seconds each). */
    public function testSignedImagesRegionsAndContractPackages(): void
    {
        $this->withHw(function (Context $ctx, Capability $cap, Capability $nohw): void {
            $key = Hw::keygen($cap);
            $other = Hw::keygen($nohw); // CRYPTO_SIGN is an ABI 1.1 bit
            try {
                $this->assertSame(Hw::PK_BYTES, strlen($key->publicKey));
                $this->assertSame(Hw::MLDSA_SK_BYTES, $key->mldsaSecret->bytesize());
                $this->assertFalse($key->publicKey === $other->publicKey);

                $oracle = new HwMatrix($cap);
                $mx = (new HwMatrix($cap))->addFunction(0x200, "inc")->addResource(1)->addResource(2);
                $vin = implode("", array_map("chr", range(0, 255)));
                $inc = implode("", array_map(fn ($i) => chr(($i + 1) & 0xFF), range(0, 255)));
                $mx->addVector(0x200, $vin, $inc);

                // A simulated two-slot device; readback asks the oracle's SHA-256 engine.
                $slots = ["", ""];
                $active = -1;
                $load = function (int $slot, string $payload) use (&$slots) {
                    if ($slot > 1 || strlen($payload) > 256) {
                        return Status::E_RANGE;
                    }
                    $slots[$slot] = $payload;
                    return null;
                };
                $readback = function (int $slot) use (&$slots, $oracle): string {
                    return $oracle->call(Hw::FN_SHA256, $slots[$slot]);
                };
                $activate = function (int $slot) use (&$active) {
                    $active = $slot;
                    return null;
                };
                $psoft = $mx->addProvider(0x200, 1, Hw::KIND_SOFT, 50, fn (int $op, string $d) => implode("", array_map(fn ($b) => chr(($b + 1) & 0xFF), array_values(unpack("C*", $d)))));
                $preg = $mx->addProvider(0x200, 2, Hw::KIND_FABRIC, 1, function (int $op, string $d) use (&$slots, &$active) {
                    if ($active < 0) {
                        return Status::E_STATE;
                    }
                    return implode("", array_map(fn ($b) => $slots[$active][$b], array_values(unpack("C*", $d))));
                });
                $this->assertSame(Truth::true(), $mx->attest($psoft));

                $this->assertThrows(CapabilityDeniedException::class, fn () => new HwRegion($nohw, $mx, $preg, 1, 0xD0, $load, $readback, $activate, null, 12));
                $this->assertSame(Status::E_ARG, $this->status(ZxvUsageException::class, fn () => new HwRegion($cap, $mx, $psoft, 1, 0xD0, $load, $readback, $activate, null, 12)));
                $rg = new HwRegion($cap, $mx, $preg, 1, 0xD0, $load, $readback, $activate, null, 12);
                $rg->trustKey($key->publicKey);
                $events = [];
                $rg->onEvent(function (int $ev, int $reason, int $version) use (&$events): void {
                    $events[] = [$ev, $version];
                });

                $forged = Hw::buildImage($cap, Hw::IMG_PARTIAL, 0xD0, 0x200, 1, 1, $inc, $other);
                $good = Hw::buildImage($cap, Hw::IMG_PARTIAL, 0xD0, 0x200, 1, 1, $inc, $key);
                $this->assertSame(Hw::HEADER_BYTES + 256 + Hw::SIG_BYTES, strlen($good));
                $this->assertSame(Status::E_VERIFY, $this->status(VerificationFailedException::class, fn () => $rg->stage($forged)));
                $wrongHw = substr_replace($good, pack("V", 0xD1), 12, 4);
                $this->assertSame(Status::E_TARGET, $this->status(ZxvUsageException::class, fn () => $rg->stage($wrongHw)));

                $rg->stage($good);
                $this->assertSame(Status::E_STATE, $this->status(ZxvException::class, fn () => $rg->commit()));
                $rg->trial(5);
                $this->assertSame($preg, $mx->bound(0x200));
                $rg->commit();
                $this->assertSame(["state" => Hw::REGION_ACTIVE, "version" => 1, "floor" => 1, "quarantined" => 1], $rg->info());
                $this->assertSame([[Hw::EV_REJECTED, 1], [Hw::EV_STAGED, 1], [Hw::EV_TESTED, 1], [Hw::EV_ACTIVATED, 1]], $events);
                $this->assertSame(null, $rg->eventError());
                $this->assertSame(substr($inc, 0, 4), $mx->call(0x200, substr($vin, 0, 4)));

                $this->assertSame(Status::E_ROLLBACK, $this->status(ZxvRefusedException::class, fn () => $rg->stage($good)));
                [$head, $count] = $rg->log();
                $this->assertSame(5, $count);
                $this->assertSame(count($events), $count);
                $last = $rg->logEntry($count - 1);
                $this->assertSame([Hw::EV_REJECTED, 1, $head], [$last->event, $last->id, $last->hash]);
                $act = $rg->logEntry(3);
                $this->assertSame([Hw::EV_ACTIVATED, 1], [$act->event, $act->version]);
                $this->assertFalse((($act->tag >> 2) & 3) === 2);
                $this->assertTrue($act->hexagram() instanceof Hexagram);
                $this->assertSame($count, count($rg->logEntries()));
                $this->assertSame(Status::E_NOT_FOUND, $this->status(ZxvException::class, fn () => $rg->logEntry($count)));

                $payload = Hw::vectorList([[0, pack("C*", 5, 250), pack("C*", 6, 251)]]);
                $this->assertSame(16, strlen($payload));
                $pkg = Hw::buildImage($cap, Hw::IMG_VECTORS, 0, 0x200, 7, 3, $payload, $key);
                $matrixOnly = new HwMatrix($cap->attenuate(Capability::ALL | Capability::HW_MATRIX));
                $this->assertThrows(CapabilityDeniedException::class, fn () => $matrixOnly->applyContract($pkg));
                $this->assertSame(Status::E_VERIFY, $this->status(ZxvException::class, fn () => $mx->applyContract($pkg)));
                $mx->trustKey($key->publicKey);
                $this->assertSame(Status::E_FORMAT, $this->status(ZxvException::class, fn () => $mx->applyContract($good)));
                $this->assertSame(Status::E_FORMAT, $this->status(ZxvException::class, fn () => $rg->stage($pkg)));
                $this->assertSame(2, $mx->applyContract($pkg));
                $info = $mx->functionInfo(0x200);
                $this->assertSame([7, 1], [$info["contract"], $info["vectors"]]);
                $this->assertSame(Status::E_ROLLBACK, $this->status(ZxvException::class, fn () => $mx->applyContract($pkg)));
                $contract = array_values(array_filter($mx->logEntries(), fn ($e) => $e->event === Hw::EV_CONTRACT));
                $this->assertSame([0x200, 7], [$contract[0]->id, $contract[0]->version]);

                // Fill the region's log (12): the next recorded refusal halts
                // the region and poisons the context. Fail closed.
                while ($rg->log()[1] < 12) {
                    $this->assertThrows(VerificationFailedException::class, fn () => $rg->stage($forged));
                }
                $this->assertThrows(ZxvFatalError::class, fn () => $rg->stage($forged));
                $this->assertTrue($ctx->isPoisoned());
            } finally {
                $key->close();
                $other->close();
            }
            $this->assertTrue($key->mldsaSecret->isClosed());
            $this->assertTrue($other->slhdsaSecret->isClosed());
        });
    }
}
