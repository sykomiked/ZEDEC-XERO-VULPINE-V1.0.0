<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Status;

/** A dependency-free test base (assertions in the PHPUnit style). */
abstract class TestCase
{
    public int $assertions = 0;
    /** @var array<string, mixed> */
    protected static array $vectors = [];

    public static function vectors(): array
    {
        if (self::$vectors === []) {
            self::$vectors = json_decode((string) file_get_contents(self::sdk() . "/conformance/vectors.json"), true, 512, JSON_THROW_ON_ERROR);
        }
        return self::$vectors;
    }

    public static function sdk(): string
    {
        return dirname(__DIR__, 2);
    }

    public static function fixtures(): string
    {
        return self::sdk() . "/conformance/fixtures";
    }

    /** @param callable(Context, Capability): void $fn */
    protected function withContext(callable $fn): void
    {
        $ctx = new Context();
        try {
            $fn($ctx, $ctx->rootCapability());
        } finally {
            $ctx->close();
        }
    }

    /**
     * ABI 1.5: a context with a root capability holding every 1.5 bit.
     * @param callable(Context, Capability): void $fn
     */
    protected function withContext15(callable $fn): void
    {
        $ctx = new Context();
        try {
            $fn($ctx, $ctx->rootCapability(Capability::ALL_1_5));
        } finally {
            $ctx->close();
        }
    }

    /**
     * Run $fn. For "ZXV_OK" return its value; otherwise assert it throws a
     * ZXV error whose status name is $name and return the error.
     */
    protected function expectStatus(string $name, callable $fn): mixed
    {
        if ($name === "ZXV_OK") {
            return $fn();
        }
        $this->assertions++;
        try {
            $fn();
        } catch (\Throwable $e) {
            if (!method_exists($e, "getStatus")) {
                throw new AssertionFailed("expected $name, got " . get_class($e) . ": " . $e->getMessage());
            }
            $this->assertSame($name, Status::name($e->getStatus()), $e->getMessage());
            return $e;
        }
        throw new AssertionFailed("expected $name, nothing thrown");
    }

    /** A 64-bit vector number (a decimal string) as the SDK returns it. */
    protected static function u64(string|int $text): int|string
    {
        $t = (string) $text;
        return (string) (int) $t === $t ? (int) $t : $t;
    }

    protected static function hex(string $s): string
    {
        return (string) hex2bin($s);
    }

    protected static function unhex(string $b): string
    {
        return bin2hex($b);
    }

    protected function assertSame(mixed $expected, mixed $actual, string $msg = ""): void
    {
        $this->assertions++;
        if ($expected !== $actual) {
            throw new AssertionFailed(($msg !== "" ? "$msg: " : "") . "expected " . var_export($expected, true) . ", got " . var_export($actual, true));
        }
    }

    protected function assertTrue(bool $cond, string $msg = ""): void
    {
        $this->assertSame(true, $cond, $msg);
    }

    protected function assertFalse(bool $cond, string $msg = ""): void
    {
        $this->assertSame(false, $cond, $msg);
    }

    /**
     * @template T of \Throwable
     * @param class-string<T> $class
     * @return T
     */
    protected function assertThrows(string $class, callable $fn, string $msg = ""): \Throwable
    {
        $this->assertions++;
        try {
            $fn();
        } catch (\Throwable $e) {
            if ($e instanceof $class) {
                return $e;
            }
            throw new AssertionFailed(($msg !== "" ? "$msg: " : "") . "expected $class, got " . get_class($e) . ": " . $e->getMessage());
        }
        throw new AssertionFailed(($msg !== "" ? "$msg: " : "") . "expected $class, nothing thrown");
    }
}

final class AssertionFailed extends \Exception
{
}
