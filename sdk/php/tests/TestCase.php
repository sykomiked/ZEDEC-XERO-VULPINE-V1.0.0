<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;

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
