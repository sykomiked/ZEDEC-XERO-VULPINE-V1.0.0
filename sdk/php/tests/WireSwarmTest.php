<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Swarm;
use Zedec\Zxv\Ubh168Frame;

final class WireSwarmTest extends TestCase
{
    public function testUbh168FramesByteForByte(): void
    {
        foreach (self::vectors()["ubh168"] as $v) {
            $tag = Ubh168Frame::tag($v["trunk"], $v["shell"], $v["sync"]);
            $this->assertSame($v["tag"], $tag);
            $f = new Ubh168Frame($tag, $v["s_plus"], $v["s_minus"]);
            $this->assertSame($v["frame"], bin2hex($f->toBytes()), "pure PHP pack");
            $this->assertSame($v["frame"], bin2hex($f->nativeBytes()), "libzxv pack");
            $back = Ubh168Frame::fromBytes((string) hex2bin($v["frame"]));
            $this->assertSame($v["s_plus"], $back->sPlus);
            $this->assertSame($v["s_minus"], $back->sMinus);
            $this->assertSame($v["truth"], $f->resolve());
        }
        foreach (self::vectors()["ubh168_idle"] as $trunk => $hex) {
            $this->assertSame($hex, bin2hex(Ubh168Frame::idle($trunk)->toBytes()));
        }
        $this->assertSame(Ubh168Frame::TRUTH_NEUTRAL, Ubh168Frame::idle(0)->resolve());
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Ubh168Frame::tag(10, 0, 0));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Ubh168Frame::fromBytes(str_repeat("\0", 20)));
    }

    public function testSwarmAllotmentsMatchVectors(): void
    {
        $sv = self::vectors()["swarm"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($sv): void {
            $s = new Swarm($cap, $sv["levels"], $sv["tokens_per_cycle"]);
            foreach ($sv["models"] as [$id, $level]) {
                $s->register($id, $level);
            }
            $s->setMood(...$sv["mood"]);
            foreach ($sv["feelings"] as [$id, $cp, $int]) {
                $s->setFeeling($id, $cp, $int);
            }
            $s->beginCycle();
            $total = 0;
            foreach ($sv["allotments"] as [$id, $allotted, $imaginary]) {
                $a = $s->allotment($id);
                $this->assertSame([$allotted, $imaginary], [$a["allotted"], $a["imaginary"]], "model $id");
                $total += $allotted;
            }
            $this->assertSame($sv["tokens_per_cycle"], $total);
            [$first, $firstAllot] = $sv["allotments"][0];
            $this->assertSame(10, $s->consume($first, 10));
            $e = $this->assertThrows(Exception\BudgetExhaustedException::class, fn () => $s->consume($first, $firstAllot));
            $this->assertSame($firstAllot - 10, $e->getGranted());
            $this->assertTrue($s->endCycle() > 0);
            $this->assertThrows(Exception\ZxvRefusedException::class, fn () => $s->register(999, 0));
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $s->setMood(0x1F600, 1));
        });
        foreach ($sv["level_capacity"] as $d => $c) {
            $this->assertSame($c, Swarm::levelCapacity($d));
        }
        $this->assertTrue(is_int(Swarm::emotionProfile(0x1F914)["temperatureMilli"]));
    }
}
