<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Exception;
use Zedec\Zxv\Iso4217;
use Zedec\Zxv\Native;
use Zedec\Zxv\Rational;
use Zedec\Zxv\Status;
use Zedec\Zxv\Truth;
use Zedec\Zxv\Zxv;

final class CoreTest extends TestCase
{
    public function testAbiAndFeatures(): void
    {
        $v = Zxv::abiVersion();
        $this->assertSame(array_slice(self::vectors()["abi"], 0, 2), array_slice($v, 0, 2));
        $this->assertTrue(Zxv::hasFeature("rational"));
        $this->assertFalse(Zxv::hasFeature("lora"));
    }

    public function testStatusNamesClassesAndExceptionTypes(): void
    {
        $byClass = [1 => Exception\ZxvUsageException::class, 2 => Exception\ZxvRefusedException::class,
            3 => Exception\ZxvFatalError::class, 4 => Exception\ZxvSystemException::class];
        foreach (self::vectors()["status"] as $s) {
            $this->assertSame($s["name"], Status::name($s["code"]));
            $this->assertSame($s["class"], Status::classOf($s["code"]));
            if ($s["code"] === 0) {
                continue;
            }
            $e = Native::errorFor($s["code"]);
            $this->assertTrue($e instanceof $byClass[$s["class"]], $s["name"]);
            $this->assertSame($s["code"], $e->getStatus());
            $this->assertSame($s["name"], $e->getStatusName());
        }
        $this->assertTrue(is_subclass_of(Exception\ZxvFatalError::class, \Error::class));
        $this->assertFalse(is_subclass_of(Exception\ZxvFatalError::class, \Exception::class));
        $this->assertFalse(is_subclass_of(Exception\ZxvFatalError::class, Exception\ZxvException::class));
        $this->assertTrue(is_subclass_of(Exception\ZxvUsageException::class, \InvalidArgumentException::class));
        $this->assertTrue(Native::errorFor(Status::E_CAP) instanceof Exception\CapabilityDeniedException);
        $this->assertTrue(Native::errorFor(Status::E_FLOAT) instanceof Exception\FloatRejectedException);
    }

    public function testRationalOpsMatchVectors(): void
    {
        foreach (self::vectors()["rational_ops"] as $v) {
            $a = Rational::of(...$v["a"]);
            $b = Rational::of(...$v["b"]);
            foreach (["add", "sub", "mul", "div"] as $op) {
                $r = $a->$op($b);
                $this->assertSame($v[$op], [$r->numerator, $r->denominator], "$op");
            }
            $this->assertSame($v["cmp"], $a->compare($b));
        }
    }

    public function testRationalParseMatchesVectors(): void
    {
        foreach (self::vectors()["rational_parse"] as $v) {
            if ($v["status"] === 0) {
                $r = Rational::parse($v["text"]);
                $this->assertSame($v["value"], [$r->numerator, $r->denominator], $v["text"]);
                $this->assertSame($v["format"], (string) $r);
            } else {
                $e = $this->assertThrows(Exception\ZxvUsageException::class, fn () => Rational::parse($v["text"]), $v["text"]);
                $this->assertSame($v["status"], $e->getStatus(), $v["text"]);
            }
        }
    }

    public function testIeee754BoundaryMatchesVectors(): void
    {
        foreach (self::vectors()["ieee754"] as $v) {
            $bits = unpack("J", (string) hex2bin($v["bits"]))[1];
            if ($v["status"] === 0) {
                $r = Rational::fromIeee754Bits($bits);
                $this->assertSame($v["value"], [$r->numerator, $r->denominator], $v["bits"]);
                $f = unpack("E", (string) hex2bin($v["bits"]))[1];
                $this->assertTrue(Rational::fromDoubleExact($f)->equals($r));
            } else {
                $e = $this->assertThrows(Exception\ZxvUsageException::class, fn () => Rational::fromIeee754Bits($bits));
                $this->assertSame($v["status"], $e->getStatus(), $v["bits"]);
                if ($v["status"] === Status::E_FLOAT) {
                    $this->assertTrue($e instanceof Exception\FloatRejectedException);
                }
            }
        }
        $tenth = Rational::fromDoubleExact(0.1);
        $this->assertSame([3602879701896397, 36028797018963968], [$tenth->numerator, $tenth->denominator]);
        $this->assertFalse($tenth->equals(Rational::of(1, 10)));
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => Rational::fromDoubleExact(NAN));
    }

    public function testQ16MatchesVectors(): void
    {
        foreach (self::vectors()["q16"] as $v) {
            $r = Rational::of(...$v["value"]);
            if ($v["status"] === 0) {
                $this->assertSame($v["q16"], $r->toQ16());
            } else {
                $this->assertThrows(Exception\ZxvUsageException::class, fn () => $r->toQ16());
            }
        }
    }

    public function testFloatsAreRejected(): void
    {
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => Rational::of(0.5));
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => Rational::of(1, 2.0));
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => Rational::of(1)->add(0.5));
        $this->assertThrows(Exception\FloatRejectedException::class, fn () => Rational::from(0.1));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Rational::of(PHP_INT_MAX)->mul(2));
        $this->assertThrows(Exception\ZxvUsageException::class, fn () => Rational::of(1)->div(0));
        $this->assertFalse(method_exists(Rational::class, "toFloat"));
    }

    public function testTruthTablesMatchVectors(): void
    {
        $lp = self::vectors()["lpres"];
        $this->assertSame($lp["names"], array_map(fn (Truth $t) => $t->name(), Truth::all()));
        foreach (Truth::all() as $a) {
            $this->assertSame($lp["not"][$a->value], $a->not()->value);
            foreach (Truth::all() as $b) {
                $this->assertSame($lp["and"][$a->value][$b->value], $a->and($b)->value);
                $this->assertSame($lp["or"][$a->value][$b->value], $a->or($b)->value);
            }
        }
        $this->assertTrue(Truth::fromEvidence(2, 1) === Truth::both());
        $this->assertTrue(Truth::of(1) === Truth::true());
        $this->assertSame(0, Truth::true()->mode);
    }

    public function testTruthNeedsAnExplicitPolicy(): void
    {
        $this->assertFalse(Truth::both()->isCertainlyTrue());
        $this->assertTrue(Truth::both()->toBool(onBoth: true, onNeither: false));
        $this->assertFalse(Truth::neither()->toBool(onBoth: true, onNeither: false));
        $this->assertThrows(Exception\TruthConversionException::class, fn () => Truth::false()->requireTrue());
        $this->assertTrue(Truth::true()->requireTrue() === Truth::true());
    }

    public function testIso4217AndRandom(): void
    {
        $ngn = Iso4217::lookup("NGN");
        $this->assertSame(566, $ngn?->numeric);
        $this->assertSame(2, $ngn?->minorUnits);
        $this->assertSame(null, Iso4217::lookup("VFV"));
        $this->assertSame("NGN", Iso4217::byNumeric(566)?->code);
        $this->assertSame(null, Iso4217::byNumeric(777));
        $this->assertSame("Naira", Iso4217::name("NGN"));
        $this->assertTrue(in_array("GHS", Iso4217::codes(), true));
        $this->assertTrue(Iso4217::isAfricanUnionMember("NG"));
        $this->assertSame(32, strlen(Zxv::randomBytes(32)));
        $this->assertFalse(Zxv::randomBytes(32) === Zxv::randomBytes(32));
    }
}
