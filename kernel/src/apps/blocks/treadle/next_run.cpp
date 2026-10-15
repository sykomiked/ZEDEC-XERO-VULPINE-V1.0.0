// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A task's next run (treadle_rules.h TR1), in C++ compiled to WebAssembly
// (build: tools/apps/build_wasm_blocks.sh): the first start + k * period
// (k >= 0) at or after now, in minutes from today's midnight, or
// tomorrow's first run (start + 1440) when that is past the day. start and
// now are 0 .. 1439 and period 1 .. 1440; anything else traps, which the
// runtime turns into a refusal (zxapp_wasm.c W3), where KRON answers.
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long start, long long period, long long now)
{
    if (start < 0) __builtin_trap();
    if (start > 1439) __builtin_trap();
    if (period < 1) __builtin_trap();
    if (period > 1440) __builtin_trap();
    if (now < 0) __builtin_trap();
    if (now > 1439) __builtin_trap();
    if (now <= start) return start;
    long long due = start + (now - start + period - 1) / period * period;
    if (due > 1439) return start + 1440;
    return due;
}
