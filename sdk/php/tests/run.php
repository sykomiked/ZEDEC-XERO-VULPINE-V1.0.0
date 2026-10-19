<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Plain-PHP test runner (no PHPUnit needed):
//   php tests/run.php [filter]

declare(strict_types=1);

require dirname(__DIR__) . "/autoload.php";
require __DIR__ . "/TestCase.php";
foreach (glob(__DIR__ . "/*Test.php") as $f) {
    require $f;
}

$filter = $argv[1] ?? "";
$tests = $failures = $assertions = 0;
foreach (get_declared_classes() as $class) {
    if (!is_subclass_of($class, Zedec\Zxv\Tests\TestCase::class) || (new ReflectionClass($class))->isAbstract()) {
        continue;
    }
    foreach (get_class_methods($class) as $m) {
        if (!str_starts_with($m, "test") || ($filter !== "" && !str_contains("$class::$m", $filter))) {
            continue;
        }
        $tests++;
        $t = new $class();
        try {
            $t->$m();
            echo ".";
        } catch (Throwable $e) {
            $failures++;
            echo "\nFAIL $class::$m\n  " . get_class($e) . ": " . $e->getMessage() . "\n  at " . $e->getFile() . ":" . $e->getLine() . "\n";
        }
        $assertions += $t->assertions;
    }
}
echo "\n$tests tests, $assertions assertions, $failures failures\n";
exit($failures === 0 ? 0 : 1);
