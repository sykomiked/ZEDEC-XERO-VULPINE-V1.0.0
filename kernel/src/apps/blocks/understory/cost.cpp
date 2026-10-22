// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// What energy costs at a tariff (understory_rules.h UR1), in C++ compiled
// to WebAssembly (build: tools/apps/build_wasm_blocks.sh): mj millijoules
// at tariff minor units per kWh (3600000000 mJ), floor(mj * tariff /
// 3600000000). A negative energy or tariff, or a product past 64 bits, is
// no cost: the module traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never a wrapped answer. cost.sutra holds 9(12) and
// 9(6); past them the two disagree (held). Arithmetic only: Z-Wasm's
// validator refuses wasm's untyped select, which a conditional would
// compile to.
extern "C" long long run(long long mj, long long tariff)
{
    if (mj < 0) __builtin_trap();
    if (tariff < 0) __builtin_trap();
    if (tariff > 0) {
        if (mj > 9223372036854775807LL / tariff) __builtin_trap();
    }
    return mj * tariff / 3600000000LL;
}
