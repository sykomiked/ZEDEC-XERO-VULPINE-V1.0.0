<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// PSR-4 autoloader for use without Composer: require this file.

declare(strict_types=1);

spl_autoload_register(static function (string $class): void {
    $prefix = "Zedec\\Zxv\\";
    if (strncmp($class, $prefix, strlen($prefix)) !== 0) {
        return;
    }
    $file = __DIR__ . "/src/" . str_replace("\\", "/", substr($class, strlen($prefix))) . ".php";
    if (is_file($file)) {
        require $file;
    }
});
